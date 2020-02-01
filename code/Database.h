#ifndef DATABASE_H
#define DATABASE_H

#include "Node.h"
#include "MerkleBlock.h"
#include "Data.h"
#include "WorkQueue.h"
#include <pthread.h>

extern const char *nodeListIndexKey;
extern const char *nodeListIpKey;
extern const char *nodeListPortKey;
extern const char *nodeListServicesKey;
extern const char *nodeListDateKey;
extern const char *nodeListTestNetKey;
extern const char *nodeListManualNodeKey;
extern const char *nodeListMasterNodeKey;

extern const char *DatabaseNewTxNotification;
extern const char *DatabaseNewBlockNotification;
extern const char *DatabaseNodeListChangedNotification;

typedef struct Database {

    pthread_t workThread;

    WorkQueue workQueue;

    void *database;
    Dictionary blockHeightCache;
    Datas nodeListCache;
} Database;

typedef struct TransactionTimeDelta {

    uint32_t deltaSeconds;
    uint64_t fee;
    int bytes;
    int confirmed;

} TransactionTimeDelta;

extern Database database;
extern const char *databaseRootPath;

Database DatabaseNew();

// Performs 'onWorkThread' on the work thread.
void DatabaseExecute(Database *db, void (*onWorkThread)(Database *db, Dict dict), Dict dict);

int DatabaseResetAllBlocks(Database *db);
//void DatabaseResetToOriginal(Database *db);

int DatabaseAnalyze(Database *db);

// If this is a duplicate, the smaller hash replaces the larger
int DatabaseAddBlock(Database *db, MerkleBlock *block);

int DatabaseNumBlocks(Database *db);
Data DatabaseNthBlockHash(Database *db, int n, int *height);
Datas DatabaseTransactionsAtHeight(Database *db, int height); // Array of dictionaries. Keys are: data, fee, rejectCode, time

int DatabaseHasBlock(Database *db, Data hash);

int32_t DatabaseHeightOf(Database *db, Data hash); // Returns -1 if not found.
Data DatabaseHashOf(Database *db, int32_t height);

int32_t DatabaseHighestHeight(Database *db);
int32_t DatabaseHighestHeightOrLowestMissingAfter(Database *db, uint32_t timestamp);

uint32_t DatabaseTimeOfHeight(Database *db, int32_t height);

int32_t DatabaseNearestHeight(Database *db, int32_t height);
int32_t DatabaseHighestBlockBeforeTime(Database *db, uint32_t timestamp);
int32_t DatabaseNearestHeightToTime(Database *db, uint32_t timestamp);

Datas DatabaseBlockHashesAbove(Database *db, int32_t height, int32_t limit);

Datas DatabaseNodeList(Database *db, int testnet); // Array of dictionaries
int32_t DatabaseNodeListCount(Database *db, int testnet);

Dictionary DatabaseNodeForIp(Database *db, String ip, int testnet);

int DatabaseAddNode(Database *db, Dictionary node);
int DatabaseRemoveNode(Database *db, int32_t index);
int DatabaseRemoveNodeByIp(Database *db, String ip);

int32_t DatabaseConfirmations(Database *db, Data data);

// Array of dictionaries.
// dictionaries has form: { @"delta": @((uint32_t)numSeconds), @"fee": @((uint64_t)fee), @"bytes": @((int)bytes), @"confirmed": @((BOOL)confirmed) }
// Returns the newest items first
// By default we include rejects
Datas DatabaseTransactionTimeDeltas(Database *db, int limit);
Datas DatabaseTransactionTimeDeltasFlexible(Database *db, int limit, int includeRejects);

double DatabaseEstimateFeePerByte(Database *db);

Datas DatabaseTransactionsWithNoFee(Database *db, Datas hashes);

int DatabaseSetTransaction(Database *db, Data hash, int rejectCode);
int DatabaseRecordTransactionFee(Database *db, Data transactionHash, uint64_t fee);
int DatabaseAddTransaction(Database *db, Data data, MerkleBlock *block); // block can be NULL
int DatabaseDeleteTransaction(Database *db, Data hash);

// Note this the whole hash not just txid!!
Data DatabaseTransactionForHash(Database *db, Data hash);
int32_t DatabaseTransactionHeight(Database *db, Data hash);
uint32_t DatabaseTransactionTime(Database *db, Data hash);

Datas DatabaseAllTransactions(Database *db);
Datas DatabaseTransactionsToPublish(Database *db);

Data DatabaseFirstBlockHash(Database *db);

#endif
