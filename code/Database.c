//
//  Database.m
//  KoinKeep
//
//  Created by Dustin Dettmer on 12/22/18.
//  Copyright © 2018 Dustin. All rights reserved.
//

#include "Database.h"
#include "BTCUtil.h"
#include "BasicStorage.h"
#include "SqlStructure.h"
#include "blocks.h"
#include "testnet_blocks.h"
#include "Notifications.h"
#include <string.h>
#include <sqlite3.h>
#include <time.h>

// Override here returning an existing directory to put keyfiles in.
// It must not end in a slash.
static String getDatabaseDirectory()
{
    return StringNew(databaseRootPath);
}

Database database = {0};
const char *databaseRootPath = "/tmp";

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

// Time before uncofirmed tx's get weighed
#define MIN_DELTA_WEIGHT (60 * 60)

// Time past this gets ignored
#define MAX_DELTA_WEIGHT (24 * 60 * 60)

#define HASH_CACHE_TRIMSIZE 5000
#define HASH_CACHE_SIZE 15000

//#define HASH_CACHE_TRIMSIZE 5
//#define HASH_CACHE_SIZE 15

const char *nodeListIndexKey = "nodeListIndexKey";
const char *nodeListIpKey = "nodeListIpKey";
const char *nodeListPortKey = "nodeListPortKey";
const char *nodeListServicesKey = "nodeListServicesKey";
const char *nodeListDateKey = "nodeListDateKey";
const char *nodeListTestNetKey = "nodeListTestNetKey";
const char *nodeListManualNodeKey = "nodeListManualNodeKey";
const char *nodeListMasterNodeKey = "nodeListMasterNodeKey";
const char *DatabaseNewTxNotification = "DatabaseNewTxNotification";
const char *DatabaseNewBlockNotification = "DatabaseNewBlockNotification";
const char *DatabaseNodeListChangedNotification = "DatabaseNodeListChangedNotification";

static void insertStartingBlocks(Database *self);

static String dbPath()
{
    String docs = getDatabaseDirectory();

    String path = StringF("%s/blocks.db", docs.bytes);

    if(DataGetInt(bsLoad("testnet")))
        path = StringF("%s/testnet_blocks.db", docs.bytes);

    return path;
}

static void DatabaseExecuteHelper(Dict dict)
{
    Database *db = DataGetPtr(DictGetS(dict, "db"));
    void (*onWorkThread)(Database *db, Dict dict) = DataGetPtr(DictGetS(dict, "node"));
    Dict subDict = DataGetDict(DictGetS(dict, "dict"));

    onWorkThread(db, subDict);
}

void DatabaseExecute(Database *db, void (*onWorkThread)(Database *db, Dict dict), Dict subDict)
{
    Dict dict = DictNew();

    DictAddS(&dict, "db", DataPtr(db));
    DictAddS(&dict, "node", DataPtr(onWorkThread));
    DictAddS(&dict, "dict", DataDict(DictCopy(subDict)));

    WorkQueueAdd(WorkQueueThreadNamedStackSize("Database Thread", 262144), DatabaseExecuteHelper, dict);
}

int DatabaseResetAllBlocks(Database *self)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "delete from `blocks`", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    result = sqlite3_step(stmt);

    if(result != SQLITE_DONE) {

        sqlite3_finalize(stmt);
        return 0;
    }

    stmt = NULL;

    result = sqlite3_prepare_v2(self->database, "delete from `transactions`", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    result = sqlite3_step(stmt);

    if(result != SQLITE_DONE) {

        sqlite3_finalize(stmt);
        return 0;
    }

    insertStartingBlocks(self);

    return 1;
}

void resetToOriginal(Database *self)
{
    // TODO

    WorkQueueWaitUntilEmpty(&self->workQueue);

    FILE *file = fopen(dbPath().bytes, "w");

    if(file)
        fclose(file);
}

static void insertStartingBlocks(Database *self)
{
    char *error = NULL;

    const char *blocksSql = initialBlocksSql;

    if(DataGetInt(bsLoad("testnet")))
        blocksSql = initialTestnetBlocksSql;

    sqlite3_exec(self->database, blocksSql, NULL, NULL, &error);

    if(error) {

        printf("insert blocks error: %s\n", error);
        sqlite3_free(error);
    }
}

Database DatabaseNew()
{
    Database result = {0};
    Database *self = &result;

    self->workQueue = WorkQueueNew();
    self->blockHeightCache = DictUntrack(DictNew());

    String path = dbPath();

    int firstTime = 1;

    FILE *testFile = fopen(path.bytes, "r");

    if(testFile) {

        fseek(testFile, 0, SEEK_END);

        if(ftell(testFile))
            firstTime = 0;

        fclose(testFile);
    }

    volatile int sqlite_result = sqlite3_config(SQLITE_CONFIG_SERIALIZED);

    (void)sqlite_result;

    sqlite_result = sqlite3_open_v2(path.bytes, (struct sqlite3 **)&self->database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);

    if(firstTime) {

        char *error = NULL;

        sqlite3_exec(self->database, initialSqlStructure, NULL, NULL, &error);

        if(error)
            printf("Create table error: %s\n", error);

        insertStartingBlocks(self);
    }

//    prefillBlockHeightCache(self);

    return result;
}

int analyze(Database *self)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "analyze", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    result = sqlite3_step(stmt);

    if(result != SQLITE_DONE) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_finalize(stmt);
    return 1;
}

void trimBlockHeightCacheIfNeeded(Database *self)
{
    while(DictCount(self->blockHeightCache) >= HASH_CACHE_SIZE) {

        DictRemoveIndex(&self->blockHeightCache, DictCount(self->blockHeightCache) - 1);
    }
}

static pthread_mutex_t blockHeightCacheMutex = PTHREAD_MUTEX_INITIALIZER;

int DatabaseAddBlock(Database *self, MerkleBlock *block)
{
    if(!MerkleBlockValid(block))
        return 0;

    int32_t prevHeight = DatabaseHeightOf(self, blockPrevHash(block));

    if(prevHeight < 0) {

        printf("Can't find prevHash of merkBlock to discover height.\n");
        prevHeight = DatabaseHeightOf(self, blockPrevHash(block));
        return 0;
    }

    int32_t height = prevHeight + 1;
    Data hash = blockHash(block);
    uint32_t hashPrefix = *(uint32_t*)hash.bytes;

    sqlite3_stmt *stmt = NULL;

    for(int i = 0; i < 2; i++) {

        String query = StringF("%s into `blocks` (`height`, `hash`, `hashPrefix`, `merkleRoot`, `time`) values (?, ?, ?, ?, ?)", i ? "replace" : "insert");

        int result = sqlite3_prepare_v2(self->database, query.bytes, -1, &stmt, NULL);

        if(result != SQLITE_OK)
            break;

        sqlite3_bind_int(stmt, 1, height);
        sqlite3_bind_blob(stmt, 2, hash.bytes, (int)hash.length, NULL);
        sqlite3_bind_int(stmt, 3, hashPrefix);
        sqlite3_bind_blob(stmt, 4, merkleRoot(block).bytes, (int) merkleRoot(block).length, NULL);
        sqlite3_bind_int64(stmt, 5, blockTimestamp(block));

        result = sqlite3_step(stmt);

        if(result == SQLITE_DONE) {

            pthread_mutex_lock(&blockHeightCacheMutex);

            Data num = DictGet(self->blockHeightCache, DataInt(hashPrefix));

            trimBlockHeightCacheIfNeeded(self);

            // The code area below as revealed as CPU intensive during block downloading,
            // therefore it has been optimized and may take more effort to read.

            if(num.length) {

                Data key = DataNewUntracked(sizeof(hashPrefix));
                Data value = DataNewUntracked(sizeof(height));

                *(int32_t*)key.bytes = hashPrefix;
                *(int32_t*)value.bytes = -1;

                self->blockHeightCache = DictionaryAddRefUntracked(self->blockHeightCache, key, value);
            }
            else {

                Data key = DataNewUntracked(sizeof(hashPrefix));
                Data value = DataNewUntracked(sizeof(height));

                *(int32_t*)key.bytes = hashPrefix;
                *(int32_t*)value.bytes = height;

                self->blockHeightCache = DictionaryAddRefUntracked(self->blockHeightCache, key, value);
            }

            pthread_mutex_unlock(&blockHeightCacheMutex);

            NotificationsFire(DatabaseNewBlockNotification, DictNew());

            sqlite3_finalize(stmt);
            return 1;
        }

        if(i || result != SQLITE_CONSTRAINT)
            break;

        sqlite3_finalize(stmt);
        stmt = NULL;

        result = sqlite3_prepare_v2(self->database, "select `hash` from `blocks` where `height`=?", -1, &stmt, NULL);

        if(result != SQLITE_OK)
            break;

        sqlite3_bind_int(stmt, 1, height);

        result = sqlite3_step(stmt);

        if(result != SQLITE_ROW)
            break;

        Data existingHash = DataCopy(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));

        // if existing hash is smaller than we keep existing hash
        int cmpResult = DataCompare(DataFlipEndianCopy(hash), DataFlipEndianCopy(existingHash));

        if(cmpResult == 0) {

            // Getting here means there was no change.

            sqlite3_finalize(stmt);
            return 0;
        }

        if(cmpResult > 0) {

            sqlite3_finalize(stmt);
            return 0;
        }

        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    sqlite3_finalize(stmt);

    const char *err = sqlite3_errmsg(self->database);

    printf("add merkle error: %s\n", err);

    return 0;
}

int numBlocks(Database *self)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select count(*) from `blocks`", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    result = sqlite3_step(stmt);

    if(result != SQLITE_ROW) {

        sqlite3_finalize(stmt);
        return 0;
    }

    int value = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return value;
}

Data nthBlockHash(Database *self, int n, int *height)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select `hash`, `height` from `blocks` order by `height` desc limit ?,1", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return DataNull();
    }

    sqlite3_bind_int(stmt, 1, n);

    result = sqlite3_step(stmt);

    if(result != SQLITE_ROW) {

        sqlite3_finalize(stmt);
        return DataNull();
    }

    if(height)
        *height = sqlite3_column_int(stmt, 1);

    Data ret = DataCopy(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));

    sqlite3_finalize(stmt);

    return ret;
}

Datas transactionsAtHeight(Database *self, int height)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select `transaction`, `fee`, `rejectCode`, `time` from `transactions` where `height`=?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return DatasNew();
    }

    sqlite3_bind_int(stmt, 1, height);

    Datas array = DatasNew();

    while(SQLITE_ROW == (result = sqlite3_step(stmt))) {

        Dict dict = DictNew();

        DictAddS(&dict, "data", DataCopy(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0)));
        DictAddS(&dict, "fee", DataLong(sqlite3_column_int(stmt, 1)));
        DictAddS(&dict, "rejectCode", DataInt(sqlite3_column_int(stmt, 2)));
        DictAddS(&dict, "time", DataLong(sqlite3_column_int(stmt, 3)));
    }

    sqlite3_finalize(stmt);

    if(result != SQLITE_DONE)
        return DatasNew();

    return array;
}

int hasBlock(Database *self, Data hash)
{
    if(hash.length != 32)
        return 0;

    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select `hash` from `blocks` where `hashPrefix`=?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, *(int32_t*)hash.bytes);

    while(SQLITE_ROW == sqlite3_step(stmt)) {

        Data data = DataCopy(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));

        if(DataEqual(data, hash)) {

            sqlite3_finalize(stmt);
            return 1;
        }
    }

    sqlite3_finalize(stmt);
    return 0;
}

int32_t DatabaseHeightOf(Database *self, Data hash)
{
    if(hash.length != 32)
        return -1;

    int32_t hashPrefix = *(int32_t*)hash.bytes;

    pthread_mutex_lock(&blockHeightCacheMutex);

    Data value = DataCopyData(DictGet(self->blockHeightCache, DataInt(hashPrefix)));

    pthread_mutex_unlock(&blockHeightCacheMutex);

    if(DataGetInt(value) > 0)
        return DataGetInt(value);

    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select `hash`, `height` from `blocks` where `hashPrefix`=?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_bind_int(stmt, 1, hashPrefix);

    while(SQLITE_ROW == sqlite3_step(stmt)) {

        Data data = DataCopy(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));

        if(DataEqual(data, hash)) {

            int height = sqlite3_column_int(stmt, 1);

            pthread_mutex_lock(&blockHeightCacheMutex);

            Data num = DictGet(self->blockHeightCache, DataInt(hashPrefix));

            trimBlockHeightCacheIfNeeded(self);

            // The code area below as revealed as CPU intensive during block downloading,
            // therefore it has been optimized and may take more effort to read.

            if(num.length) {

                Data key = DataNewUntracked(sizeof(hashPrefix));
                Data value = DataNewUntracked(sizeof(height));

                *(int32_t*)key.bytes = hashPrefix;
                *(int32_t*)value.bytes = -1;

                self->blockHeightCache = DictionaryAddRefUntracked(self->blockHeightCache, key, value);
            }
            else {

                Data key = DataNewUntracked(sizeof(hashPrefix));
                Data value = DataNewUntracked(sizeof(height));

                *(int32_t*)key.bytes = hashPrefix;
                *(int32_t*)value.bytes = height;

                self->blockHeightCache = DictionaryAddRefUntracked(self->blockHeightCache, key, value);
            }

            pthread_mutex_unlock(&blockHeightCacheMutex);

            sqlite3_finalize(stmt);
            return height;
        }
    }

    if(DataEqual(hash, DatabaseFirstBlockHash(self))) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_finalize(stmt);
    return -1;
}

int32_t DatabaseHighestHeight(Database *self)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select max(`height`) from `blocks`", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    result = sqlite3_step(stmt);

    if(result != SQLITE_ROW) {

        sqlite3_finalize(stmt);
        return 0;
    }

    int height = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return height;
}

int32_t DatabaseHighestHeightOrLowestMissingAfter(Database *self, uint32_t timestamp)
{
    sqlite3_stmt *stmt = NULL;

    int32_t height = DatabaseHighestBlockBeforeTime(self, timestamp);

    int result = sqlite3_prepare_v2(self->database, "select `height` from `blocks` where `height`>? order by `height`", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, height);

    for(int32_t i = height + 1; (result = sqlite3_step(stmt)) == SQLITE_ROW; i++) {

        if(i != sqlite3_column_int(stmt, 0)) {

            sqlite3_finalize(stmt);
            return i - 1;
        }
    }

    sqlite3_finalize(stmt);
    return DatabaseHighestHeight(self);
}

uint32_t timeOfHeight(Database *self, int32_t height)
{
    if(height < 1)
        return 0;

    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select `time` from `blocks` where `height`=?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, height);

    result = sqlite3_step(stmt);

    uint32_t value = (uint32_t)sqlite3_column_int64(stmt, 0);

    sqlite3_finalize(stmt);
    return value;
}

Data DatabaseHashOf(Database *self, int32_t height)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select `hash` from `blocks` where `height`=?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return DataNull();
    }

    sqlite3_bind_int(stmt, 1, height);

    result = sqlite3_step(stmt);

    if(result != SQLITE_ROW) {

        sqlite3_finalize(stmt);
        return DataNull();
    }

    Data data = DataCopy(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));

    sqlite3_finalize(stmt);
    return data;
}

static pthread_mutex_t nodeListCacheMutex = PTHREAD_MUTEX_INITIALIZER;

Datas/*Dict*/ DatabaseNodeList(Database *self, int testnet)
{
    pthread_mutex_lock(&nodeListCacheMutex);

    Datas cacheResult = DatasNew();

    if(self->nodeListCache.count) {

        FORIN(Dict, itr, self->nodeListCache)
            cacheResult = DatasAddCopy(cacheResult, DataDictStayTracked(DictCopy(*itr)));
    }

    pthread_mutex_unlock(&nodeListCacheMutex);

    if(cacheResult.count)
        return cacheResult;

    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select `id`, `ip`, `port`, `services`, `date`, `testnet`, `manual`, `master` from `nodes` where testnet=? order by `manual` desc, `date` desc", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return DatasNew();
    }

    sqlite3_bind_int(stmt, 1, testnet ? 1 : 0);

    Datas array = DatasNew();

    while((result = sqlite3_step(stmt)) == SQLITE_ROW) {

        Data data = DataCopy(sqlite3_column_blob(stmt, 1), sqlite3_column_bytes(stmt, 1));

        if(!data.length || data.bytes[data.length - 1])
            data = DataAdd(data, StringNew(""));

        Dict dict = DictNew();

        DictSetS(&dict, nodeListIndexKey, DataInt(sqlite3_column_int(stmt, 0)));
        DictSetS(&dict, nodeListIpKey, data);
        DictSetS(&dict, nodeListPortKey, DataInt(sqlite3_column_int(stmt, 2)));
        DictSetS(&dict, nodeListServicesKey, DataLong(sqlite3_column_int64(stmt, 3)));
        DictSetS(&dict, nodeListDateKey, DataInt(sqlite3_column_int(stmt, 4)));
        DictSetS(&dict, nodeListTestNetKey, DataInt(sqlite3_column_int(stmt, 5) ? 1 : 0));
        DictSetS(&dict, nodeListManualNodeKey, DataInt(sqlite3_column_int(stmt, 6) ? 1 : 0));
        DictSetS(&dict, nodeListMasterNodeKey, DataInt(sqlite3_column_int(stmt, 7) ? 1 : 0));

        array = DatasAddCopy(array, DataDict(dict));
    }

    pthread_mutex_lock(&nodeListCacheMutex);

    FORIN(Dict, dict, self->nodeListCache)
        DictTrack(*dict);

    DatasTrack(self->nodeListCache);

    self->nodeListCache = DatasUntrack(array);

    pthread_mutex_unlock(&nodeListCacheMutex);

    sqlite3_finalize(stmt);

    if(result != SQLITE_DONE)
        return DatasNull();

    return array;
}

int32_t DatabaseNodeListCount(Database *self, int testnet)
{
    pthread_mutex_lock(&nodeListCacheMutex);

    int32_t count = self->nodeListCache.count;

    pthread_mutex_unlock(&nodeListCacheMutex);

    if(count)
        return count;

    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select count(*) from `nodes` where testnet=?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, testnet ? 1 : 0);

    result = sqlite3_step(stmt);

    if(result != SQLITE_ROW) {

        sqlite3_finalize(stmt);
        return 0;
    }

    int value = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return value;
}

Dict nodeForIp(Database *self, String ip, int testnet)
{
    Dict result = DictNew();

    pthread_mutex_lock(&nodeListCacheMutex);

    Datas array = DatabaseNodeList(self, DataGetInt(bsLoad("testnet")));

    FORIN(Dict, itr, array) {

        if(DataEqual(ip, DictGetS(*itr, nodeListIpKey))) {

            result = *itr;
            break;
        }
    }

    pthread_mutex_unlock(&nodeListCacheMutex);

    return result;
}

int DatabaseAddNode(Database *self, Dict node)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "insert into `nodes` ( `ip`, `port`, `services`, `date`, `testnet`, `manual`, `master` ) values (?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    String ipData = DictGetS(node, nodeListIpKey);

    sqlite3_bind_blob(stmt, 1, ipData.bytes, (int)strlen(ipData.bytes), NULL);
    sqlite3_bind_int(stmt, 2, DataGetInt(DictGetS(node, nodeListPortKey)));
    sqlite3_bind_int64(stmt, 3, DataGetLong(DictGetS(node, nodeListServicesKey)));
    sqlite3_bind_int(stmt, 4, DataGetInt(DictGetS(node, nodeListDateKey)));
    sqlite3_bind_int(stmt, 5, DataGetInt(DictGetS(node, nodeListTestNetKey)));
    sqlite3_bind_int(stmt, 6, DataGetInt(DictGetS(node, nodeListManualNodeKey)));
    sqlite3_bind_int(stmt, 7, DataGetInt(DictGetS(node, nodeListMasterNodeKey)));

    pthread_mutex_lock(&nodeListCacheMutex);

    result = sqlite3_step(stmt);

    int64_t insertId = sqlite3_last_insert_rowid(self->database);

    sqlite3_finalize(stmt);

    if(result == SQLITE_DONE) {

        if(!self->nodeListCache.count) {

            DatasTrack(self->nodeListCache);
            self->nodeListCache = DatasUntrack(DatasNew());
        }

        Dict dict = DictCopy(node);

        DictSetS(&dict, nodeListIndexKey, DataInt((int32_t)insertId));

        if(DataGetInt(DictGetS(node, nodeListManualNodeKey)))
            self->nodeListCache = DatasUntrack(DatasAddCopyIndex(self->nodeListCache, DataDict(dict), 0));
        else
            self->nodeListCache = DatasUntrack(DatasAddCopy(self->nodeListCache, DataDict(dict)));
    }

    pthread_mutex_unlock(&nodeListCacheMutex);

    NotificationsFire(DatabaseNodeListChangedNotification, DictNew());

    return result == SQLITE_DONE;
}

int DatabaseRemoveNode(Database *self, int32_t index)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "delete from `nodes` where `id`=? limit 1", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, index);

    result = sqlite3_step(stmt);

    pthread_mutex_lock(&nodeListCacheMutex);

    for(int i = 0; i < self->nodeListCache.count; i++) {

        Dict *dict = (Dict*)self->nodeListCache.ptr[i].bytes;

        if(DataGetInt(DictGetS(*dict, nodeListIndexKey)) == index) {

            DictUntrack(*dict);

            self->nodeListCache = DatasUntrack(DatasRemoveIndex(self->nodeListCache, i));
            i--;
        }
    }

    pthread_mutex_unlock(&nodeListCacheMutex);

    NotificationsFire(DatabaseNodeListChangedNotification, DictNew());

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

int DatabaseRemoveNodeByIp(Database *self, String ip)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "delete from `nodes` where `ip`=?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_blob(stmt, 1, ip.bytes, (uint32_t)strlen(ip.bytes), NULL);

    result = sqlite3_step(stmt);

    pthread_mutex_lock(&nodeListCacheMutex);

    for(int i = 0; i < self->nodeListCache.count; i++) {

        Dict *dict = (Dict*)self->nodeListCache.ptr[i].bytes;

        if(DataEqual(DictGetS(*dict, nodeListIpKey), ip)) {

            DictFree(*dict);

            self->nodeListCache = DatasUntrack(DatasRemoveIndex(self->nodeListCache, i));
            i--;
        }
    }

    pthread_mutex_unlock(&nodeListCacheMutex);

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

int32_t DatabaseNearestHeight(Database *self, int32_t height)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select max(`height`) from `blocks` where `height`<?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, height);

    result = sqlite3_step(stmt);

    if(result != SQLITE_ROW) {

        sqlite3_finalize(stmt);
        return 0;
    }

    int lowerValue = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    stmt = NULL;

    result = sqlite3_prepare_v2(self->database, "select min(`height`) from `blocks` where `height`>=?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, height);

    result = sqlite3_step(stmt);

    if(result != SQLITE_ROW) {

        sqlite3_finalize(stmt);
        return 0;
    }

    int upperValue = sqlite3_column_int(stmt, 0);

    if(!upperValue)
        return lowerValue;

    sqlite3_finalize(stmt);
    stmt = NULL;

    if(height - lowerValue < upperValue - height)
        return lowerValue;

    return upperValue;
}

int32_t DatabaseHighestBlockBeforeTime(Database *self, uint32_t timestamp)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select max(`height`) from `blocks` where `time`<?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, timestamp);

    result = sqlite3_step(stmt);

    if(result != SQLITE_ROW) {

        sqlite3_finalize(stmt);
        return 0;
    }

    int height = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return height;
}

int32_t DatabaseNearestHeightToTime(Database *self, uint32_t timestamp)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select max(`height`) from `blocks` where `time`<?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, timestamp);

    result = sqlite3_step(stmt);

    if(result != SQLITE_ROW) {

        sqlite3_finalize(stmt);
        return 0;
    }

    int lowerValue = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    stmt = NULL;

    result = sqlite3_prepare_v2(self->database, "select min(`height`) from `blocks` where `time`>=?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, timestamp);

    result = sqlite3_step(stmt);

    if(result != SQLITE_ROW) {

        sqlite3_finalize(stmt);
        return 0;
    }

    int upperValue = sqlite3_column_int(stmt, 0);

    if(!upperValue)
        return lowerValue;

    sqlite3_finalize(stmt);
    stmt = NULL;

    result = sqlite3_prepare_v2(self->database, "select `height` from `blocks` where `height` in (?, ?) order by abs(? - `time`) limit 1", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, lowerValue);
    sqlite3_bind_int(stmt, 2, upperValue);
    sqlite3_bind_int(stmt, 3, timestamp);

    result = sqlite3_step(stmt);

    if(result != SQLITE_ROW) {

        sqlite3_finalize(stmt);
        return 0;
    }

    int ret = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return ret;
}

Datas DatabaseBlockHashesAbove(Database *self, int32_t height, int32_t limit)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select `hash` from `blocks` where `height`>? order by `height` asc limit ?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return DatasNew();
    }

    sqlite3_bind_int(stmt, 1, height);
    sqlite3_bind_int(stmt, 2, limit);

    Datas array = DatasNew();

    while((result = sqlite3_step(stmt)) == SQLITE_ROW)
        array = DatasAddCopy(array, DataCopy(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0)));

    sqlite3_finalize(stmt);

    if(result != SQLITE_DONE)
        return DatasNew();
    
    return array;
}

int32_t DatabaseConfirmations(Database *self, Data data)
{
    int32_t height = DatabaseTransactionHeight(self, data);

    if(height < 0)
        return -1;

    return DatabaseHighestHeight(self) + 1 - height;
}

Datas/*Dict*/ DatabaseTransactionTimeDeltas(Database *self, int limit)
{
    return DatabaseTransactionTimeDeltasFlexible(self, limit, 1);
}

Datas/*Dict*/ DatabaseTransactionTimeDeltasFlexible(Database *self, int limit, int includeRejects)
{
    sqlite3_stmt *stmt = NULL;

    String query = StringNew("select `time`, (select `time` from `blocks` where `blocks`.`height`=`transactions`.`height` limit 1), `fee`, length(`transaction`) from `transactions` where `fee`!=0 ");

    if(!includeRejects)
        query = StringAdd(query, includeRejects ? " " :  " and `rejectCode`=0 ");

    query = StringAdd(query, " order by `time` desc limit ?");

    int result = sqlite3_prepare_v2(self->database, query.bytes, -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return DatasNew();
    }

    sqlite3_bind_int(stmt, 1, limit);

    Datas array = DatasNew();

    while((result = sqlite3_step(stmt)) == SQLITE_ROW) {

        int64_t t = sqlite3_column_int64(stmt, 0);
        int64_t blockTime = sqlite3_column_int64(stmt, 1);
        uint64_t fee = sqlite3_column_int64(stmt, 2);
        int bytes = sqlite3_column_int(stmt, 3);

        int confirmed = blockTime ? 1 : 0;

        blockTime = blockTime ?: time(0);

        Dict dict = DictNew();

        DictAddS(&dict, "delta", DataLong(blockTime > t ? blockTime - t : 0));
        DictAddS(&dict, "fee", DataLong(fee));
        DictAddS(&dict, "bytes", DataInt(bytes));
        DictAddS(&dict, "confirmed", DataInt(confirmed));

        array = DatasAddCopy(array, DataDict(dict));
        DictTrack(dict);
    }

    if(result != SQLITE_DONE) {

        sqlite3_finalize(stmt);
        return DatasNew();
    }

    sqlite3_finalize(stmt);
    return array;
}

double DatabaseEstimateFeePerByte(Database *self)
{
    double minFeePerByte = 0;
    uint64_t totalFees = 0;
    int totalBytes = 0;

    for(int i = 1; !totalBytes && i < 20; i++) {

        Datas array = DatabaseTransactionTimeDeltasFlexible(self, i * 20, 0);

        FORIN(Dict, dict, array) {

            int64_t delta = DataGetLong(DictGetS(*dict, "delta"));
            uint64_t fee = DataGetLong(DictGetS(*dict, "fee"));
            int bytes = DataGetInt(DictGetS(*dict, "bytes"));

            if(DataGetInt(DictGetS(*dict, "confirmed"))) {

                totalFees += fee;
                totalBytes += bytes;
            }
            else if(delta > MIN_DELTA_WEIGHT) {

                double weight = 1 + MIN(delta, MAX_DELTA_WEIGHT) / MAX_DELTA_WEIGHT;

                minFeePerByte = MAX(minFeePerByte, weight * fee / bytes);
            }
        }
    }

    if(!totalBytes)
        return 0;

    double result = (double)totalFees / (double)totalBytes;

    if(result < minFeePerByte)
        result = minFeePerByte;

    return result;
}

Datas DatabaseTransactionsWithNoFee(Database *self, Datas hashes)
{
    sqlite3_stmt *stmt = NULL;

    String query = StringNew("select `hash`, `transaction` from `transactions` where `fee`=0 and `hash` in (");

    for(int i = 0; i < hashes.count; i++)
        query = StringAddF(query, "%s?", i ? ", " : "");

    query = StringAdd(query, ")");

    int result = sqlite3_prepare_v2(self->database, query.bytes, -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return DatasNew();
    }

    for(int i = 0; i < hashes.count; i++)
        sqlite3_bind_blob(stmt, i + 1, hashes.ptr[i].bytes, (int)hashes.ptr[i].length, NULL);

    Datas array = DatasNew();

    while((result = sqlite3_step(stmt)) == SQLITE_ROW) {

        Data hash = DataCopy(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));
        Data data = DataCopy(sqlite3_column_blob(stmt, 1), sqlite3_column_bytes(stmt, 1));

        if(DatasHasMatchingData(hashes, hash))
            array = DatasAddCopy(array, data);
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? array : DatasNew();
}

int DatabaseSetTransaction(Database *self, Data hash, int code)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "update `transactions` set `rejectCode`=? where `hashPrefix`=? and `hash`=? limit 1", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int64(stmt, 1, code);
    sqlite3_bind_int(stmt, 2, *(int32_t*)hash.bytes);
    sqlite3_bind_blob(stmt, 3, hash.bytes, (int)hash.length, NULL);

    result = sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    return result == SQLITE_DONE;
}

int DatabaseRecordTransactionFee(Database *self, Data hash, uint64_t fee)
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "update `transactions` set `fee`=? where `hashPrefix`=? and `hash`=? limit 1", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int64(stmt, 1, fee);
    sqlite3_bind_int(stmt, 2, *(int32_t*)hash.bytes);
    sqlite3_bind_blob(stmt, 3, hash.bytes, (int)hash.length, NULL);

    result = sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    return result == SQLITE_DONE;
}

int DatabaseAddTransaction(Database *self, Data data, MerkleBlock *block)
{
    int32_t height = DatabaseHeightOf(self, blockHash(block));

    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "insert into `transactions` (`height`, `hash`, `hashPrefix`, `transaction`) values (?, ?, ?, ?)", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    Data hash = hash256(data);

    sqlite3_bind_int(stmt, 1, height);
    sqlite3_bind_blob(stmt, 2, hash.bytes, (int)hash.length, NULL);
    sqlite3_bind_int(stmt, 3, *(int32_t*)hash.bytes);
    sqlite3_bind_blob(stmt, 4, data.bytes, (int)data.length, NULL);

    result = sqlite3_step(stmt);

    if(result == SQLITE_CONSTRAINT && height > 0) {

        sqlite3_finalize(stmt);
        stmt = NULL;

        result = sqlite3_prepare_v2(self->database, "update `transactions` set `height`=?, `time` = MIN(`transactions`.`time`, (select `time` from `blocks` where `height`=? limit 1)) where `hashPrefix`=? and `hash`=? limit 1", -1, &stmt, NULL);

        if(result != SQLITE_OK) {

            sqlite3_finalize(stmt);
            return 0;
        }

        sqlite3_bind_int(stmt, 1, height);
        sqlite3_bind_int(stmt, 2, height);
        sqlite3_bind_int(stmt, 3, *(int32_t*)hash.bytes);
        sqlite3_bind_blob(stmt, 4, hash.bytes, (int)hash.length, NULL);

        result = sqlite3_step(stmt);
    }

    if(result != SQLITE_DONE) {

        sqlite3_finalize(stmt);
        return 0;
    }

    NotificationsFire(DatabaseNewTxNotification, DictNew());

    sqlite3_finalize(stmt);
    return 1;
}

int DatabaseDeleteTransaction(Database *self, Data hash)
{
    if(hash.length < 32)
        return 0;

    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "delete from `transactions` where `hashPrefix`=? and `hash`=? limit 1", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, *(int32_t*)hash.bytes);
    sqlite3_bind_blob(stmt, 2, hash.bytes, (int)hash.length, NULL);

    result = sqlite3_step(stmt);

    NotificationsFire(DatabaseNewTxNotification, DictNew());

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE ? 1 : 0;
}

Data DatabaseTransactionForHash(Database *self, Data hash)
{
    if(hash.length < 32)
        return DataNull();

    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select `transaction` from `transactions` where `hashPrefix`=? and `hash`=? limit 1", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return DataNull();
    }

    sqlite3_bind_int(stmt, 1, *(int32_t*)hash.bytes);
    sqlite3_bind_blob(stmt, 2, hash.bytes, (int)hash.length, NULL);

    result = sqlite3_step(stmt);

    if(result != SQLITE_ROW) {

        sqlite3_finalize(stmt);
        return DataNull();
    }

    Data ret = DataCopy(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));

    sqlite3_finalize(stmt);

    return ret;
}

int32_t DatabaseTransactionHeight(Database *self, Data hash)
{
    if(!self)
        return -1;

    if(hash.length < 32)
        return -1;

    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select `hash`, `height` from `transactions` where `hashPrefix`=?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_bind_int(stmt, 1, *(int32_t*)hash.bytes);

    while((result = sqlite3_step(stmt)) == SQLITE_ROW) {

        Data candidateHash = DataCopy(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));
        int32_t height = sqlite3_column_int(stmt, 1);

        if(DataEqual(hash, candidateHash)) {

            sqlite3_finalize(stmt);
            return height;
        }
    }

    sqlite3_finalize(stmt);
    return -1;
}

uint32_t DatabaseTransactionTime(Database *self, Data hash)
{
    if(hash.length < 32)
        return 0;

    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select `time` from `transactions` where `hashPrefix`=? and `hash`=?", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, *(int32_t*)hash.bytes);
    sqlite3_bind_blob(stmt, 2, hash.bytes, (int)hash.length, NULL);

    result = sqlite3_step(stmt);

    if(result != SQLITE_ROW) {

        sqlite3_finalize(stmt);
        return 0;
    }

    uint32_t ret = (uint32_t)sqlite3_column_int64(stmt, 0);

    sqlite3_finalize(stmt);

    return ret;
}

Datas DatabaseAllTransactions(Database *self)
{
    if(!self)
        return DatasNew();

    Datas array = DatasNew();

    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select `transaction` from `transactions`", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return DatasNew();
    }

    while((result = sqlite3_step(stmt)) == SQLITE_ROW)
        array = DatasAddCopy(array, DataCopy(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0)));

    sqlite3_finalize(stmt);

    if(result != SQLITE_DONE)
        return DatasNew();

    return array;
}

Datas DatabaseTransactionsToPublish(Database *self)
{
    Datas array = DatasNew();

    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(self->database, "select `transaction` from `transactions` where height<1 and time>strftime('%s', 'now')-60*60*24*3", -1, &stmt, NULL);

    if(result != SQLITE_OK) {

        sqlite3_finalize(stmt);
        return DatasNew();
    }

    while((result = sqlite3_step(stmt)) == SQLITE_ROW)
        array = DatasAddCopy(array, DataCopy(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0)));

    sqlite3_finalize(stmt);

    if(result != SQLITE_DONE)
        return DatasNew();

    return array;
}

static Data blockZero;
static Data blockZeroTestnet;

static void firstBlockHash()
{
    blockZeroTestnet = DataFlipEndianCopy(fromHex("000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943"));
    blockZero = DataFlipEndianCopy(fromHex("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"));

    DataUntrack(blockZeroTestnet);
    DataUntrack(blockZero);
}

Data DatabaseFirstBlockHash(Database *self)
{
    static pthread_once_t onceToken = PTHREAD_ONCE_INIT;

    pthread_once(&onceToken, firstBlockHash);
    
    if(DataGetInt(bsLoad("testnet"))) {

        return blockZeroTestnet;
    }

    return blockZero;
}
