//
//  NodeManager.m
//  KoinKeep
//
//  Created by Dustin Dettmer on 12/21/18.
//  Copyright © 2018 Dustin. All rights reserved.
//

#import "NodeManager.h"
#import "BTCUtil.h"
#import "Database.h"
#import "MerkleBlock.h"
#include <stdlib.h>
#include <stdio.h>
#import "TransactionTracker.h"
#include "Notifications.h"

#ifdef __ANDROID__
#include <android/log.h>
#define printf(...) __android_log_print(ANDROID_LOG_INFO, "NodeManager", __VA_ARGS__)
#endif

const char *NodeManagerBlockchainSyncChange = "NodeManagerBlockchainSyncChange";

NodeManager nodeManager = {0};

#ifndef BTCUtilAssert
#define BTCUtilAssert(...)
#endif

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#define SERVICE_NODE_NETWORK 1
#define SERVICE_NODE_GETUTXO 2
#define SERVICE_NODE_BLOOM 4
#define SERVICE_NODE_WITNESS 8
#define SERVICE_NODE_LIMITED 1024

// Max value is 50000
#define GET_DATA_BLOCK_COUNT 5000

#define CONSECUTIVE_BADBLOCKS_RESET_LIMIT (2000 * 5 + 1)

// TX timelime expirations are calucated every NODE_CHECKUP_INTERVAL, so the actual expiration
// will be this time or later
#define SEND_TX_TIMELIMIT 20

// The number of seconds to delay an error message for sendTx:
// If the tx succeeds in that time, the error will be ignoerd.
#define SEND_TX_ERRORDELAY 16

#define NODE_INITIAL_DL_DELAY 2
#define BLOOMTILER_CHECKUP_INTERVAL (NODE_CHECKUP_INTERVAL - 5)
#define NODE_STORAGE_COUNT 2000
#define ACTIVE_NODE_COUNT 8
#define REQUIRED_SERVICES (SERVICE_NODE_BLOOM | SERVICE_NODE_WITNESS)

static void requestTransactions(NodeManager *self);
static void requestHeaders(NodeManager *self, Node *node);

const char *TxProcessing = "TxProcessing";

NodeManager NodeManagerNew(uint64_t walletCreationDate)
{
    NodeManager self = { 0 };

    pthread_mutexattr_t recursiveAttr;

    pthread_mutexattr_init(&recursiveAttr);
    pthread_mutexattr_settype(&recursiveAttr, PTHREAD_MUTEX_RECURSIVE);

    self.nodes = DatasNew();
    
    if(pthread_mutex_init(&self.nodesMutex, &recursiveAttr) != 0)
        abort();
    
    if(pthread_mutexattr_destroy(&recursiveAttr) != 0)
        abort();
    
    self.sendTxOnResults = DatasNew();
    self.blockchainSynced = 0;
    self.walletCreationDate = walletCreationDate;
    self.workQueue = WorkQueueNew();
    self.automaticallyPublishWaitingTransactions = 1;

    return self;
}

void NodeManagerFree(NodeManager *self)
{
    WorkQueueThreadWaitAndDestroy(TxProcessing);

    pthread_mutex_lock(&self->nodesMutex);

    FORIN(Node, node, self->nodes) {

        NodeClose(node);
        node->delegate = (struct NodeDelegate) { 0 };
        NodeFree(node);
    }

    DatasFree(self->nodes);

    pthread_mutex_unlock(&self->nodesMutex);
    pthread_mutex_destroy(&self->nodesMutex);

    DataFree(self->bloomFilter);

    FORIN(Dict, dict, self->sendTxOnResults)
        DictionaryFree(*dict);

    DatasFree(self->sendTxOnResults);

    WorkQueueFree(self->workQueue);
}

static Datas nodeList(NodeManager *self)
{
    Datas array = DatabaseNodeList(&database, self->testnet);

    if(array.count > 10)
        return array;

    #include "PeerList.h"

    Datas defArray = StringComponents(StringNew(self->testnet ? peerListTest : peerListLive), '|');

    for(int i = 0; i < defArray.count; i++) {

        Dictionary dict = DictionaryNew(NULL);

        dict = DictionaryAddCopy(dict, StringNew(nodeListIpKey), StringIndex(defArray, i));
        dict = DictionaryAddCopy(dict, StringNew(nodeListTestNetKey), DataInt(self->testnet));

        DatabaseAddNode(&database, dict);
    }

    return DatabaseNodeList(&database, self->testnet);
}

static Datas trimNodeListIfNeeded(NodeManager *self)
{
    Datas array = nodeList(self);

    if(array.count < NODE_STORAGE_COUNT)
        return array;

    Datas tmpList = DatasCopy(nodeList(self));

    Datas timeIntervals = DatasNew();

    timeIntervals = DatasAddCopy(timeIntervals, DataInt(30 * 24 * 3600));
    timeIntervals = DatasAddCopy(timeIntervals, DataInt(7 * 24 * 3600));
    timeIntervals = DatasAddCopy(timeIntervals, DataInt(24 * 3600));
    timeIntervals = DatasAddCopy(timeIntervals, DataInt(4 * 3600));

    for(int i = 0; i < timeIntervals.count; i++) {

        int delta = DataGetInt(timeIntervals.ptr[i]);

        for(int j = 0; j < tmpList.count; j++) {

            Dictionary dict = DictIndex(tmpList, j);

            if(-DataGetInt(DictGetS(dict, nodeListDateKey)) >= delta)
                if(!DataGetInt(DictGetS(dict, nodeListMasterNodeKey)))
                    DatabaseRemoveNode(&database, DataGetInt(DictGetS(dict, nodeListIndexKey)));
        }

        if(DatabaseNodeListCount(&database, self->testnet) < NODE_STORAGE_COUNT)
            return nodeList(self);
    }

    // Reaching this point means we still have too many nodes. Remove them at random.

    while(DatabaseNodeListCount(&database, self->testnet) > NODE_STORAGE_COUNT / 2) {

        Dictionary dict = DictIndex(tmpList, arc4random_uniform((int)tmpList.count));

        DatabaseRemoveNode(&database, DataGetInt(DictGetS(dict, nodeListIndexKey)));
    }

    return nodeList(self);
}

static Datas masterNodeList(NodeManager *self)
{
    Datas array = nodeList(self);
    Datas result = DatasNew();

    for(int i = 0; i < array.count; i++)
        if(DataGetInt(DictGetS(DictIndex(array, i), nodeListMasterNodeKey)))
            result = DatasAddCopy(result, DatasIndex(array, i));

    return result;
}

static void removeWork(Database *db, Dict dict)
{
    DatabaseRemoveNodeByIp(&database, DictGetS(dict, nodeListIpKey));
}

static void removeAddress(String ip)
{
    if(!ip.bytes)
        return;

    DatabaseExecute(&database, removeWork, DictOneS(nodeListIpKey, ip));
}

static void addressWork(Database *db, Dict dict)
{
    DatabaseRemoveNodeByIp(db, DictGetS(dict, nodeListIpKey));
    DatabaseAddNode(db, dict);
}

static void newAddress(struct Node *node, String ip, uint16_t port, uint64_t date, uint64_t services)
{
    Dict entry = DictNew();

    DictAddS(&entry, nodeListIpKey, ip);
    DictAddS(&entry, nodeListPortKey, DataInt(port));
    DictAddS(&entry, nodeListServicesKey, DataLong(services));
    DictAddS(&entry, nodeListDateKey, DataInt((uint32_t)date));
    DictAddS(&entry, nodeListTestNetKey, DataInt(node->testnet));

    if((services & REQUIRED_SERVICES) != REQUIRED_SERVICES)
        return;

    DatabaseExecute(&database, addressWork, entry);
}

int NodeManagerConnections(NodeManager *self)
{
    int result = 0;

    pthread_mutex_lock(&self->nodesMutex);

    FORIN(Node, node, self->nodes)
        if(node->connected)
            result++;

    pthread_mutex_unlock(&self->nodesMutex);

    return result;
}

void NodeManagerProcessNodes(NodeManager *self)
{
    pthread_mutex_lock(&self->nodesMutex);

    FORIN(Node, node, self->nodes)
        NodeProcessPackets(node);

    pthread_mutex_unlock(&self->nodesMutex);

    WorkQueueExecuteAll(&self->workQueue);
}

Datas *NodeManagerAllNodes(NodeManager *self)
{
    return &self->nodes;
}

int NodeManagerIsActiveNode(NodeManager *self, Node *node)
{
    return node && node == self->activeNode;
}

void setBloomFilter(NodeManager *self, Data bloomFilter)
{
    DataTrack(self->bloomFilter);
    self->bloomFilter = DataUntrack(DataCopyData(bloomFilter));

    pthread_mutex_lock(&self->nodesMutex);

    FORIN(Node, node, self->nodes)
        if(node->curBloomFilter.bytes && !DataEqual(node->curBloomFilter, bloomFilter))
            NodeFilterClear(node);

    FORIN(Node, node, self->nodes)
        if(!node->curBloomFilter.bytes)
            NodeFilterLoad(node, self->bloomFilter);

    pthread_mutex_unlock(&self->nodesMutex);
}

Datas blockIndicatorHashes(NodeManager *self)
{
    Datas array = DatasNew();

    uint32_t step = 1;

    int32_t start = DatabaseHighestHeightOrLowestMissingAfter(&database, (uint32_t)self->walletCreationDate);

    for(int64_t index = start; index > 0; index -= step) {

        if(array.count > 10)
            step *= 2;

        array = DatasAddCopy(array, DatabaseHashOf(&database, DatabaseNearestHeight(&database, (int32_t)index)));
    }

    array = DatasAddCopy(array, DatabaseFirstBlockHash(&database));

    return array;
}

static Datas connectedNodes(NodeManager *self)
{
    Datas result = DatasNew();

    pthread_mutex_lock(&self->nodesMutex);

    for(int i = 0; i < self->nodes.count; i++) {

        Data data = DatasIndex(self->nodes, i);

        if(((Node*)data.bytes)->connected)
            result = DatasAddRef(result, data);
    }

    pthread_mutex_unlock(&self->nodesMutex);

    return result;
}

static void changeActiveNode(NodeManager *self)
{
    pthread_mutex_lock(&self->nodesMutex);

    BTCUtilAssert(self->nodes.count);

    if(self->nodes.count == 1) {

        printf("changeActiveNode: Only 1 node left! Can't cycle active node\n");

        pthread_mutex_unlock(&self->nodesMutex);

        return;
    }

    printf("disconnecting from active node %s [%p]\n", self->activeNode ? self->activeNode->address.bytes : "", self->activeNode);

    Node *oldNode = self->activeNode;

    if(oldNode) {

        NodeClose(oldNode);
        oldNode->delegate = (struct NodeDelegate) {0};

        removeAddress(oldNode->address);
        // TODO: Go through workQueue dicts and remove all items that reference "oldnode"
        NodeFree(oldNode);
        self->nodes = DatasRemove(self->nodes, DataRaw(*oldNode));
    }

    self->activeNode = (Node*)DatasRandom(connectedNodes(self)).bytes;

    if(!self->activeNode)
        self->activeNode = (Node*)DatasRandom(self->nodes).bytes;

    printf("Switched %s to be the active node->%p\n", self->activeNode ? self->activeNode->address.bytes : "", self->activeNode);

    pthread_mutex_unlock(&self->nodesMutex);
}

static int nodeStillValid(NodeManager *self, Node *node)
{
    int matchCount = 0;

    pthread_mutex_lock(&self->nodesMutex);

    FORIN(Node, nodeItr, self->nodes)
        if(nodeItr == node)
            matchCount++;

    pthread_mutex_unlock(&self->nodesMutex);

    // Node was abandoned, we're done here.
    if(!matchCount)
        return 0;

    if(matchCount > 1)
        abort();

    return 1;
}

static void blockHeadersWorkMain(NodeManager *self, Dict dict)
{
    Node *node = DataGetPtr(DictGetS(dict, "node"));
    int headerCount = DataGetInt(DictGetS(dict, "headerCount"));
    Data hash = DictGetS(dict, "hash");
    int addCount = DataGetInt(DictGetS(dict, "addCount"));
    int rejectCount = DataGetInt(DictGetS(dict, "rejectCount"));

    pthread_mutex_lock(&self->nodesMutex);

    if(!nodeStillValid(self, node)) {

        pthread_mutex_unlock(&self->nodesMutex);
        return;
    }
    
    node->lastHeadersOrMerkleMessage = time(0);

    // This was a block annoncement -- no need to modify active node->
    if(headerCount == 1) {

        printf("%s got block %s\n", node->address.bytes, toHex(DataFlipEndianCopy(hash)).bytes);
        self->rejectCount = 0;

        pthread_mutex_unlock(&self->nodesMutex);

        return;
    }

    if(headerCount != 2000 && addCount == headerCount) {

        printf("Blockchain headers are up to date!\n");
        self->rejectCount = 0;
        node->gotFinalHeadersMessage = 1;

        requestTransactions(self);

        pthread_mutex_unlock(&self->nodesMutex);

        return;
    }

    int wasActive = node == self->activeNode;

    if(wasActive && !addCount) {

        self->rejectCount = self->rejectCount + rejectCount;

        if(self->rejectCount > CONSECUTIVE_BADBLOCKS_RESET_LIMIT) {

            printf("Rejected block count %d is too high. Reseting &database for complete resync.\n", self->rejectCount);

            DatabaseResetAllBlocks(&database);
            TTSetBloomFilterDlHeight(&tracker, 0);
        }
        else
            printf("%s Active node gave us no useful block headers\n", NodeDescription(node).bytes);

        changeActiveNode(self);
    }

    if(wasActive)
        requestHeaders(self, self->activeNode);

    pthread_mutex_unlock(&self->nodesMutex);
}

static void executeFunc(Dict dict)
{
    void (*func)(NodeManager *self, Dict dict) = DataGetPtr(DictGetS(dict, "func"));
    NodeManager *self = DataGetPtr(DictGetS(dict, "self"));
    Dict subDict = DataGetDict(DictGetS(dict, "dict"));

    func(self, subDict);
}

static void NodeManagerExecute(NodeManager *self, void(*callback)(NodeManager *self, Dict dict), Dict dict)
{
    Dict parmDict = DictOneS("self", DataPtr(self));

    DictAddS(&parmDict, "func", DataPtr(callback));
    DictAddS(&parmDict, "dict", DataDict(dict));

    WorkQueueAdd(&self->workQueue, executeFunc, parmDict);
}

static void blockHeadersWork(Database *db, Dict dict)
{
    NodeManager *self = DataGetPtr(DictGetS(dict, "self"));

    Datas headers = DataGetDatas(DictGetS(dict, "headers"));

    DictRemoveS(&dict, "headers");

    MerkleBlock block = MerkleBlockNew(DatasLast(headers));

    Data hash = blockHash(&block);

    DictAddS(&dict, "hash", hash);

    int addCount = 0;
    int rejectCount = 0;

    FORDATAIN(data, headers) {

        DataTrackPush();

        BTCUtilAssert(data->length >= 80);

        if(data->length < 80)
            return;

        MerkleBlock block = MerkleBlockNew(DataCopyData(*data));

        if(DatabaseAddBlock(&database, &block))
            addCount++;
        else
            rejectCount++;

        MerkleBlockFree(&block);

        DataTrackPop();
    }

    if(addCount)
        printf("Added %d blocks, new height: %d\n", addCount, DatabaseHighestHeight(&database));

    DictAddS(&dict, "addCount", DataInt(addCount));
    DictAddS(&dict, "rejectCount", DataInt(rejectCount));

    NodeManagerExecute(self, blockHeadersWorkMain, dict);
}

void blockHeaders(Node *node, Datas headers)
{
    NodeManager *self = (NodeManager*)node->delegate.extraPtr;

    if(headers.count > 1 && !self->blockchainSynced) {
        
        self->blockchainSynced = 0;

        NotificationsFire(NodeManagerBlockchainSyncChange, DictNew());
    }

    Dict dict = DictOneS("headers", DataDatas(headers));

    DictAddS(&dict, "node", DataPtr(node));
    DictAddS(&dict, "self", DataPtr(self));
    DictAddS(&dict, "headerCount", DataInt(headers.count));

    node->lastHeadersOrMerkleMessage = time(0);

    DatabaseExecute(&database, blockHeadersWork, dict);
}

static void requestTransactionsWorkerMain(NodeManager *self, Dict dict)
{
    Node *node = DataGetPtr(DictGetS(dict, "node"));

    Datas inventoryRequests = DataGetDatas(DictGetS(dict, "inventoryRequests"));

    pthread_mutex_lock(&self->nodesMutex);

    if(!nodeStillValid(self, node)) {

        pthread_mutex_unlock(&self->nodesMutex);
        return;
    }

    // This used to be on a background thread -- we moved it here to the main thread.
    node->lastDlSize = (int32_t)DataGetInt(DictGetS(dict, "itemCount"));

    node->lastDlHeight = DatabaseHeightOf(&database, DictGetS(dict, "lastItem"));

    NodeGetData(node, inventoryRequests);

    pthread_mutex_unlock(&self->nodesMutex);
}

static void requestTransactionsWorker(Database *database, Dict dict)
{
    NodeManager *self = DataGetPtr(DictGetS(dict, "self"));
    Node *node = DataGetPtr(DictGetS(dict, "node"));
    uint32_t walletCreationDate = (uint32_t)DataGetLong(DictGetS(dict, "walletCreationDate"));

    pthread_mutex_lock(&self->nodesMutex);

    if(!nodeStillValid(self, node)) {

        pthread_mutex_unlock(&self->nodesMutex);
        return;
    }

    int32_t start = DatabaseHighestBlockBeforeTime(database, walletCreationDate);

    start -= 144;
    
    self->lowestSyncedHeight = start;

    if(TTBloomFilterDlHeight(&tracker) < start)
        TTSetBloomFilterDlHeight(&tracker, start);

    start = MAX(start, TTBloomFilterDlHeight(&tracker));

    Datas items = DatabaseBlockHashesAbove(database, start, GET_DATA_BLOCK_COUNT);

    DictAddS(&dict, "itemCount", DataInt(items.count));
    DictAddS(&dict, "lastItem", DatasLast(items));

    printf("Node[%s%s] Building bloom reqest from height %d of size %d [failures: %d/%d]\n", node->address.bytes, node->userAgent.bytes ?: "", DatabaseHeightOf(database, DatasFirst(items)), (int)items.count, TTMismatchCount(&tracker), TTMatchCount(&tracker));

    // Why isn't this on the main thread?
    TTResetFailureRate(&tracker);

    Datas inventoryRequests = DatasNew();

    FORDATAIN(data, items)
        inventoryRequests = DatasAddCopy(inventoryRequests, DataAppend(uint32D(InventoryTypeFilteredBlock), *data));

    DictAddS(&dict, "inventoryRequests", DataDatas(inventoryRequests));

    NodeManagerExecute(self, requestTransactionsWorkerMain, dict);

    pthread_mutex_unlock(&self->nodesMutex);
}

static void requestTransactions(NodeManager *self)
{
    Node *node = self->activeNode;

     if(TTBloomFilterDlHeight(&tracker) >= DatabaseHighestHeight(&database)) {

         pthread_mutex_lock(&self->nodesMutex);

         FORIN(Node, node, self->nodes) {

             node->lastDlHeight = DatabaseHighestHeight(&database);
             node->lastDlSize = 1;
         }

         pthread_mutex_unlock(&self->nodesMutex);

         printf("All transactions are up to date!\n");
        
         if(!self->blockchainSynced) {
            
             self->blockchainSynced = 1;

             NotificationsFire(NodeManagerBlockchainSyncChange, DictNew());
         }
        
         return;
     }

    Dict dict = DictNew();

    DictAddS(&dict, "node", DataPtr(node));
    DictAddS(&dict, "self", DataPtr(self));
    DictAddS(&dict, "walletCreationDate", DataLong(self->walletCreationDate));

    node->lastHeadersOrMerkleMessage = time(0);

    DatabaseExecute(&database, requestTransactionsWorker, dict);
}

static void requestHeadersWorkerMain(NodeManager *self, Dict dict)
{
    Node *node = DataGetPtr(DictGetS(dict, "node"));

    Datas hashes = DatasTrack(*(Datas*)DictGetS(dict, "hashes").bytes);

    DictRemoveS(&dict, "hashes");

    pthread_mutex_lock(&self->nodesMutex);

    if(!nodeStillValid(self, node)) {

        pthread_mutex_unlock(&self->nodesMutex);
        return;
    }

    NodeGetHeaders(node, hashes, DataNull());

    pthread_mutex_unlock(&self->nodesMutex);
}

static void requestHeadersWorker(Database *database, Dict dict)
{
    NodeManager *self = DataGetPtr(DictGetS(dict, "self"));

    Datas hashes = blockIndicatorHashes(self);

    DictAddS(&dict, "hashes", DataDatas(hashes));

    NodeManagerExecute(self, requestHeadersWorkerMain, dict);
}

static void requestHeaders(NodeManager *self, Node *node)
{
    Dict dict = DictOneS("self", DataPtr(self));

    DictAddS(&dict, "node", DataPtr(node));

    DatabaseExecute(&database, requestHeadersWorker, dict);
}

static void merkleBlock(struct Node *node, Data blockData)
{
    NodeManager *self = node->delegate.extraPtr;

    MerkleBlockFree(&node->lastMerkleBlock);
    node->lastMerkleBlock = MerkleBlockNew(DataCopyData(blockData));

    MerkleBlockUntrack(&node->lastMerkleBlock);

    DatabaseAddBlock(&database, &node->lastMerkleBlock);

    node->transactionsSinceLastMerkleBlock = 0;

//    printf("%s %d/%d\n", node->address.bytes, (int)TransactionTracker.shared.bloomFilterDlHeight, (int)node->lastDlHeight);

    int32_t height = DatabaseHeightOf(&database, blockHash(&node->lastMerkleBlock));
    
    self->lastSyncedHeight = height;

    if(node->lastDlHeight == height) {

        TTSetBloomFilterDlHeight(&tracker, height);

        if(matchingTxIdsIfValidRoot(&node->lastMerkleBlock).count == 0) {

            requestTransactions(self);
        }
    }
    else if(height % 100 == 0) {

        if(matchingTxIdsIfValidRoot(&node->lastMerkleBlock).count == 0) {

            TTSetBloomFilterDlHeight(&tracker, height);
        }
    }
}

static void updateTransactionFees(Dict unused)
{
    Datas hashes = TTInterestingTransactionHashes(&tracker);
    Datas datas = DatabaseTransactionsWithNoFee(&database, hashes);

    FORDATAIN(data, datas) {

        uint64_t fee = TTCalculateTransactionFee(&tracker, *data);

        if(fee)
            DatabaseRecordTransactionFee(&database, hash256(*data), fee);
    }
}

void NodeManagerUpdateTransactionFees(NodeManager *self)
{
    updateTransactionFees(DictNew());
}

static void txWorkerAddTransaction(Database *db, Dict dict)
{
    Data txData = DictGetS(dict, "txData");
    Data blockData = DictGetS(dict, "blockData");

    MerkleBlock block = MerkleBlockNew(blockData);

    DatabaseAddTransaction(db, txData, blockData.bytes ? &block : NULL);

    MerkleBlockFree(&block);

    updateTransactionFees(DictNew());
}

static void txWorkerMain(Dict dict)
{
    NodeManager *self = DataGetPtr(DictGetS(dict, "self"));
    Node *node = DataGetPtr(DictGetS(dict, "node"));
    int32_t height = (int32_t)DataGetLong(DictGetS(dict, "height"));

    pthread_mutex_lock(&self->nodesMutex);

    if(!nodeStillValid(self, node)) {

        pthread_mutex_unlock(&self->nodesMutex);
        return;
    }

    node->transactionsSinceLastMerkleBlock = node->transactionsSinceLastMerkleBlock + 1;

    if(node->lastDlHeight == height) {

        if(node->transactionsSinceLastMerkleBlock == matchingTxIdsIfValidRoot(&node->lastMerkleBlock).count) {

            if(node->lastDlHeight == TTBloomFilterDlHeight(&tracker) + node->lastDlSize)
                TTSetBloomFilterDlHeight(&tracker, node->lastDlHeight);

            if(!TTFailureRateTooHigh(&tracker))
                requestTransactions(self);
        }
    }

    pthread_mutex_unlock(&self->nodesMutex);
}

static void txWorker(Dict dict)
{
    NodeManager *self = DataGetPtr(DictGetS(dict, "self"));
    Node *node = DataGetPtr(DictGetS(dict, "node"));
    Data txData = DictGetS(dict, "txData");
    Data blockData = DictGetS(dict, "blockData");

    pthread_mutex_lock(&self->nodesMutex);

    if(!nodeStillValid(self, node)) {

        pthread_mutex_unlock(&self->nodesMutex);
        return;
    }

    Transaction trans = TransactionNew(txData);
    MerkleBlock block = MerkleBlockNew(blockData);

    //        Data hash = [BTCUtil hash256:tx];

    if(!DatasHasMatchingData(matchingTxIdsIfValidRoot(&block), TransactionTxid(trans)))
        blockData = DataNull();

    int result = TTAddTransaction(&tracker, txData);

    //        Data hash = [BTCUtil hash256:txCopy];

    //        printf("%@ new tx %@ fr %.05f\n", node, [BTCUtil toHex:hash.flipEndian], TransactionTracker.shared.failureRate);

    if(result == 1) {

//            printf("%@ new tx %@ fr %.05f\n", node, [BTCUtil toHex:hash.flipEndian], TransactionTracker.shared.failureRate);

//            Datas missingFundingTrans = [TransactionTracker.shared missingFundingTransactions];
//
//            printf("Need these txes: %@\n", missingFundingTrans);

//                printf("add1 tx[%@] height: %d\n", [BTCUtil toHex:[BTCUtil hash256:txCopy].flipEndian], (int)[Database.shared heightOf:block.blockHash]);

        Dict txDict = DictNew();

        DictAddS(&txDict, "txData", txData);
        DictAddS(&txDict, "blockData", blockData);

        DatabaseExecute(&database, txWorkerAddTransaction, txDict);
    }

    if(result == -1) {

        Transaction *existingTrans = TTTransactionForTxid(&tracker, TransactionTxid(trans));
        Data txDataCopy = DataNull();

        if(existingTrans)
            txDataCopy = DataCopyData(TransactionData(*existingTrans));

        Dict txDict = DictNew();

        DictAddS(&txDict, "txData", txDataCopy);
        DictAddS(&txDict, "blockData", blockData);

        DatabaseExecute(&database, txWorkerAddTransaction, txDict);
    }

    if(result == -2) {

//            printf("Rejected transaction %@, failure rate: %.05f\n", [BTCUtil toHex:hash.flipEndian], TransactionTracker.shared.failureRate);
    }

//        if(node->transactionsSinceLastMerkleBlock == node->lastMerkleBlock.matchingTxIdsIfValidRoot.count) {
//
//            TransactionTracker.shared.bloomFilterDlHeight = [Database.shared heightOf:node->lastMerkleBlock.blockHash];
//
//            printf("%d/%d\n", (int)TransactionTracker.shared.bloomFilterDlHeight, (int)node->lastDlHeight);
//        }

    int32_t height = DatabaseHeightOf(&database, blockHash(&node->lastMerkleBlock));

    DictAddS(&dict, "height", DataLong(height));

    if(node->lastDlHeight == height) {

        WorkQueueAdd(&self->workQueue, txWorkerMain, dict);
    }
    else if(height % 100 == 0) {

        if(height > TTBloomFilterDlHeight(&tracker))
            TTSetBloomFilterDlHeight(&tracker, height);
    }

    MerkleBlockFree(&block);

    pthread_mutex_unlock(&self->nodesMutex);
}

static void tx(struct Node *node, Data txUnsafe)
{
    NodeManager *self = node->delegate.extraPtr;

    Transaction trans = TransactionNew(txUnsafe);

    for(int i = 0; i < self->sendTxOnResults.count; i++) {

        Dict dict = DataGetDict(self->sendTxOnResults.ptr[i]);

        if(DataEqual(DictGetS(dict, "txid"), TransactionTxid(trans)) || DataEqual(DictGetS(dict, "wtxid"), TransactionWtxid(trans))) {

            SendTxResult onResult = DataGetPtr(DictGetS(dict, "onResult"));

            if(onResult)
                onResult(NodeManagerErrorNone, DataGetPtr(DictGetS(dict, "ptr")));

            self->sendTxOnResults = DatasRemoveIndex(self->sendTxOnResults, i);
            i--;
        }
    }

    if(node != self->activeNode)
        return;

    printf("process tx[%s]\n", toHex(DataFlipEndianCopy(hash256(txUnsafe))).bytes);

    Dict dict = DictNew();

    DictAddS(&dict, "self", DataPtr(self));
    DictAddS(&dict, "node", DataPtr(node));
    DictAddS(&dict, "txData", DataCopyData(txUnsafe));
    DictAddS(&dict, "blockData", node->lastMerkleBlock.data);

    WorkQueueAdd(WorkQueueThreadNamed(TxProcessing), txWorker, dict);
}

static void inventory(Node *node, Datas types, Datas hashes)
{
    NodeManager *self = node->delegate.extraPtr;

    for(int i = 0; i < types.count; i++) {

        uint32_t type = *(uint32_t*)types.ptr[i].bytes;

        if(type == InventoryTypeBlock || type == InventoryTypeFilteredBlock) {

            if(node == self->activeNode) {

                NodeGetData(node, DatasOneCopy(DataAppend(uint32D(InventoryTypeFilteredBlock), hashes.ptr[i])));
            }
            else if(DatabaseHeightOf(&database, hashes.ptr[i]) == -1) {

                NodeGetData(node, DatasOneCopy(DataAppend(uint32D(InventoryTypeFilteredBlock), hashes.ptr[i])));
            }
        }
        else if(type == InventoryTypeTx) {

            if(TTHasTransctionHash(&tracker, DatasAt(hashes, i))) {

                int itemsRemoved = 0;

                for(int i = 0; i < self->sendTxOnResults.count; i++) {

                    Dict dict = DictUntrack(DataGetDict(self->sendTxOnResults.ptr[i]));

                    for(int j = 0; j < hashes.count; j++) {

                        if(DataEqual(DictGetS(dict, "txid"), hashes.ptr[j]) || DataEqual(DictGetS(dict, "wtxid"), hashes.ptr[j])) {

                            void (*onResult)(NodeManagerErrorType) = DataGetPtr(DictGetS(dict, "onResult"));

                            if(onResult)
                                onResult(NodeManagerErrorNone);

                            DatasFree(*(Datas*)DataGetPtr(DictGetS(dict, "nodePtrs")));
                            DictTrack(dict);

                            self->sendTxOnResults = DatasRemoveIndex(self->sendTxOnResults, i);

                            i--;
                            itemsRemoved++;

                            break;
                        }
                    }
                }

                if(!itemsRemoved)
                    printf("Saw a transaction we already have! Skip it.\n");
            }
            else {

                NodeGetData(node, DatasOneCopy(DataAppend(uint32D(InventoryTypeTx), hashes.ptr[i])));
            }
        }
        else {

            printf("Unrecognized type %d\n", type);
        }
    }
}

static void publishedWaitingTransactions(NodeManager *self)
{
    Datas datas = DatabaseTransactionsToPublish(&database);

    for (int i = 0; i < datas.count; i++) {

        Transaction tx = TransactionNew(DatasAt(datas, i));

        if(TTInterestingTransaction(&tracker, &tx))
            NodeManagerSendTx(self, tx, NULL, NULL);
    }
}

static void setActiveNodeIfNone(Dict parm)
{
    NodeManager *self = DataGetPtr(DictGetS(parm, "self"));

    if(self->activeNode)
        return;

    pthread_mutex_lock(&self->nodesMutex);

    self->activeNode = (Node*)DatasRandom(connectedNodes(self)).bytes;

    if(!self->activeNode)
        self->activeNode = (Node*)DatasRandom(self->nodes).bytes;

    requestHeaders(self, self->activeNode);

    if(self->automaticallyPublishWaitingTransactions)
        publishedWaitingTransactions(self);

    pthread_mutex_unlock(&self->nodesMutex);
}

void NodeManagerSendTx(NodeManager *self, Transaction tx, SendTxResult onResult, void *ptr)
{
    printf("sendTx[%s]\n", toHex(DataFlipEndianCopy(TransactionTxid(tx))).bytes);

    TTAddTransaction(&tracker, TransactionData(tx));
    DatabaseAddTransaction(&database, TransactionData(tx), NULL);
    updateTransactionFees(DictNew());

    pthread_mutex_lock(&self->nodesMutex);

    // Special case "send all" transactions -- add them to the bloom filter
    if(tx.outputs.count == 1) {

        TTTempBloomFilterAdd(&tracker, TransactionTxid(tx));

        FORIN(Node, node, self->nodes) {

            NodeFilterClear(node);
            NodeFilterLoad(node, TTBloomFilter(&tracker));
        }
    }

    Datas nodePtrs = DatasNew();

    FORIN(Node, node, self->nodes)
        nodePtrs = DatasAddRef(nodePtrs, DataPtr(node));

    nodePtrs = DatasUntrack(DatasRandomSubarray(nodePtrs, nodePtrs.count / 2));

    FORDATAIN(data, nodePtrs)

    if(onResult) {

        Dict dict = DictNew();

        DictAddS(&dict, "txid", TransactionTxid(tx));
        DictAddS(&dict, "wtxid", TransactionWtxid(tx));
        DictAddS(&dict, "onResult", DataPtr(onResult));
        DictAddS(&dict, "time", DataLong(time(0)));
        DictAddS(&dict, "nodePtrs", DataRaw(nodePtrs));

        self->sendTxOnResults = DatasUntrack(DatasAddRef(self->sendTxOnResults, DataDict(dict)));
    }

    pthread_mutex_unlock(&self->nodesMutex);
}

static void messageWorkerDelayed(Dict parm)
{
    NodeManager *self = DataGetPtr(DictGetS(parm, "self"));
    Node *node = DataGetPtr(DictGetS(parm, "node"));
    NodeManagerErrorType rejectCode = DataGetInt(DictGetS(parm, "rejectCode"));

    pthread_mutex_lock(&self->nodesMutex);

    if(!nodeStillValid(self, node)) {

        pthread_mutex_unlock(&self->nodesMutex);
        return;
    }

    for(int i = 0; i < self->sendTxOnResults.count; i++) {

        Dict dict = *(Dict*)self->sendTxOnResults.ptr[i].bytes;

        DatasFree(*(Datas*)DataGetPtr(DictGetS(dict, "nodePtrs")));
        DictFree(dict);

        Datas *nodePtrs = DataGetPtr(DictGetS(dict, "nodePtrs"));

        int index = DatasMatchingDataIndex(*nodePtrs, DataPtr(node));

        *nodePtrs = DatasRemoveIndex(*nodePtrs, index);

        SendTxResult onResult = DataGetPtr(DictGetS(dict, "onResult"));
        void *ptr = DataGetPtr(DictGetS(dict, "ptr"));

        if(!nodePtrs->count) {

            DatasFree(*(Datas*)DataGetPtr(DictGetS(dict, "nodePtrs")));
            DictFree(dict);

            self->sendTxOnResults = DatasRemoveIndex(self->sendTxOnResults, i);
            i--;
        }

        if(onResult)
            onResult(rejectCode, ptr);
    }

    pthread_mutex_unlock(&self->nodesMutex);
}

static int message(struct Node *node, String message, char rejectCode, String reason, Data data)
{
//    if(data.length == 32)
//        [Database.shared setTransaction:data rejectCode:code];

    NodeManager *self = node->delegate.extraPtr;

    printf("Rejection [%s] because %s (code: %d)\n", toHex(DataFlipEndianCopy(data)).bytes, reason.bytes, (int)rejectCode);

    for(int i = 0; i < self->sendTxOnResults.count; i++) {

        Dict dict = *(Dict*)self->sendTxOnResults.ptr[i].bytes;

        Datas *nodePtrs = DataGetPtr(DictGetS(dict, "nodePtrs"));

        if(!DatasHasMatchingData(*nodePtrs, DataRaw(node)))
            continue;

        Dict parms = DictNew();

        DictAddS(&parms, "self", DataPtr(self));
        DictAddS(&parms, "node", DataPtr(node));
        DictAddS(&parms, "rejectCode", DataInt(rejectCode));

        WorkQueueAddDelayed(&self->workQueue, messageWorkerDelayed, parms, SEND_TX_ERRORDELAY * 1000);

        return 0;
    }

    return 1;
}

static void bloomFilterCheckup(Dict dict);
static void nodeCheckup(Dict dict);

void NodeManagerConnectNodes(NodeManager *self)
{
    if(!self->bloomFilter.bytes) {

        if(TTBloomFilterNeedsUpdate(&tracker))
            TTUpdateBloomFilter(&tracker);

        DataTrack(self->bloomFilter);
        self->bloomFilter = DataUntrack(TTBloomFilter(&tracker));
    }

    pthread_mutex_lock(&self->nodesMutex);

    Datas nodeList = masterNodeList(self);

    if(!nodeList.count)
        nodeList = trimNodeListIfNeeded(self);

    int tryCount = 0;

    while(tryCount++ < 22 && self->nodes.count < ACTIVE_NODE_COUNT) {

        Dict dict = DataGetDictUntracked(DatasRandom(nodeList));

        if(DataGetLong(DictGetS(dict, nodeListServicesKey)))
            if((DataGetLong(DictGetS(dict, nodeListServicesKey)) & REQUIRED_SERVICES) != REQUIRED_SERVICES)
                continue;

        int hasNode = 0;

        FORIN(Node, node, self->nodes)
            if(DataEqual(node->address, DictGetS(dict, nodeListIpKey)))
                hasNode = 1;

        if(hasNode)
            continue;

        Node node = NodeNew(DictGetS(dict, nodeListIpKey));

        node.services = SERVICE_NODE_WITNESS;

        node.testnet = self->testnet;

        if(DataGetInt(DictGetS(dict, nodeListPortKey)))
            node.port = DataGetInt(DictGetS(dict, nodeListPortKey));

        node.delegate.merkleBlock = merkleBlock;
        node.delegate.blockHeaders = blockHeaders;
        node.delegate.inventory = inventory;
        node.delegate.tx = tx;
        node.delegate.message = message;
        node.delegate.newAddress = newAddress;

        node.delegate.extraPtr = self;

        Data nodeData = DataCopyData(DataRaw(node));

        self->nodes = DatasUntrack(DatasAddRef(self->nodes, nodeData));

        Node *nodePtr = (Node*)nodeData.bytes;

        NodeConnect(nodePtr);
        NodeSendVersion(nodePtr, 0);

        if(self->bloomFilter.bytes)
            NodeFilterLoad(nodePtr, self->bloomFilter);
    }

    pthread_mutex_unlock(&self->nodesMutex);

    WorkQueueRemoveByFunction(&self->workQueue, setActiveNodeIfNone);
    WorkQueueAddDelayed(&self->workQueue, setActiveNodeIfNone, DictOneS("self", DataPtr(self)), NODE_INITIAL_DL_DELAY * 1000);

    WorkQueueRemoveByFunction(&self->workQueue, nodeCheckup);
    WorkQueueAddDelayed(&self->workQueue, nodeCheckup, DictOneS("self", DataPtr(self)), NODE_CHECKUP_INTERVAL * 1000);

    WorkQueueRemoveByFunction(&self->workQueue, bloomFilterCheckup);
    WorkQueueAddDelayed(&self->workQueue, bloomFilterCheckup, DictOneS("self", DataPtr(self)), BLOOMTILER_CHECKUP_INTERVAL * 1000);
}

void NodeManagerAppDidBecomeActive(NodeManager *self)
{
    static int activateCount = 0;

    if(activateCount++) {

        requestHeaders(self, self->activeNode);

        if(self->automaticallyPublishWaitingTransactions)
            publishedWaitingTransactions(self);
    }
}

static void bloomFilterWorkerMain(Dict dict)
{
    NodeManager *self = DataGetPtr(DictGetS(dict, "self"));

    self->bloomFilterNeedsUpdate = DataGetInt(DictGetS(dict, "result"));
}

static void bloomFilterWorker(Dict dict)
{
    NodeManager *self = DataGetPtr(DictGetS(dict, "self"));

    int result = TTBloomFilterNeedsUpdate(&tracker);

    DictAddS(&dict, "result", DataInt(result));

    WorkQueueAdd(&self->workQueue, bloomFilterWorkerMain, dict);
}

static void bloomFilterCheckup(Dict dict)
{
    WorkQueueAdd(WorkQueueThreadNamed("Bloom Filter Checkup"), bloomFilterWorker, dict);
}

static void nodeCheckup(Dict dict)
{
    NodeManager *self = DataGetPtr(DictGetS(dict, "self"));

    pthread_mutex_lock(&self->nodesMutex);

    int originalCount = (int)self->nodes.count;

    WorkQueueAdd(&self->workQueue, updateTransactionFees, DictNew());

//    printf("failure rate: %f (%d vs %d)\n", TransactionTracker.shared.failureRate, TransactionTracker.shared.mismatchCount, TransactionTracker.shared.matchCount);

    if(self->bloomFilterNeedsUpdate)
        TTSetBloomFilterDlHeight(&tracker, 0);

    if(self->bloomFilterNeedsUpdate || TTFailureRateTooHigh(&tracker)) {

        TTResetFailureRate(&tracker);

        printf("Update bloom filter\n");

        NodeClose(self->activeNode);

        TTUpdateBloomFilter(&tracker);

        DataTrack(self->bloomFilter);
        self->bloomFilter = DataUntrack(TTBloomFilter(&tracker));

        self->bloomFilterNeedsUpdate = 0;
    }

    for(int i = 0; i < self->sendTxOnResults.count; i++) {

        Dict dict = DataGetDict(self->sendTxOnResults.ptr[i]);

        if(time(0) - DataGetLong(DictGetS(dict, "time")) > SEND_TX_TIMELIMIT) {

            SendTxResult onResult = DataGetPtr(DictGetS(dict, "onResult"));

            if(onResult)
                onResult(NodeManagerErrorTimeout, DataGetPtr(DictGetS(dict, "ptr")));

            DatasFree(*(Datas*)DataGetPtr(DictGetS(dict, "nodePtrs")));
            DictFree(dict);

            self->sendTxOnResults = DatasRemoveIndex(self->sendTxOnResults, i);
            i--;
        }
    }

    Datas masterList = masterNodeList(self);

    for(int i = 0; i < self->nodes.count; i++) {

        Node *node = (Node*)self->nodes.ptr[i].bytes;

        int remove = 0;

        if(masterList.count) {

            int isInMasterList = 0;

            FORIN(Dict, info, masterList)
                if(DataEqual(DictGetS(*info, nodeListIpKey), node->address))
                    isInMasterList = 1;

            if(!isInMasterList)
                remove = 1;
        }

        if(!node->connected) {

            remove = 1;
        }

        if(node->pingSendCount != node->pongRecvCount && (!node->lastHeadersOrMerkleMessage || time(0) - node->lastHeadersOrMerkleMessage > NODE_CHECKUP_INTERVAL / 2)) {

            remove = 1;
        }
        else if(node->rejectCodes.count) {

            printf("Dropping node %s:%d because rejection codes: %d\n", node->address.bytes, (int)node->port, node->rejectCodes.count);
            remove = 1;
        }

        if(remove) {

            if(NodeManagerConnections(self)) {

                Dict *info = NULL;

                Datas array = nodeList(self);

                FORIN(Dict, itr, array)
                    if(DataEqual(DictGetS(*itr, nodeListIpKey), node->address))
                        info = itr;

                if(info) {

                    if (!DataGetInt(DictGetS(*info, nodeListMasterNodeKey)))
                        if (!DataGetInt(DictGetS(*info, nodeListManualNodeKey)))
                            removeAddress(node->address);
                }
                else {

                    printf("Failed to delete node address %s\n", node->address.bytes ?: "");
                }
            }

            if(self->activeNode == node)
                self->activeNode = NULL;

            // TODO: Go through workQueue dicts and remove all items that reference "oldnode"
            NodeClose(node);
            NodeFree(node);
            self->nodes = DatasRemoveIndex(self->nodes, i);
            i--;
        }
    }

    if(!self->activeNode || (!self->activeNode->gotFinalHeadersMessage && time(0) - self->activeNode->lastHeadersOrMerkleMessage > NODE_CHECKUP_INTERVAL / 2)) {

        changeActiveNode(self);
        requestHeaders(self, self->activeNode);
    }

    // If we lose the active node, search for a new one
    if(!self->activeNode) {

        Datas nodePtrs = DatasNew();

        FORIN(Node, node, self->nodes)
            nodePtrs = DatasAddRef(nodePtrs, DataPtr(node));

        nodePtrs = DatasRandomSubarray(nodePtrs, nodePtrs.count);

        FORDATAIN(data, nodePtrs) {

            pthread_mutex_lock(&self->nodesMutex);

            Node *node = DataGetPtr(*data);

            if(nodeStillValid(self, node) && node->connected) {

                self->activeNode = node;
                requestHeaders(self, node);

                pthread_mutex_unlock(&self->nodesMutex);

                break;
            }

            pthread_mutex_unlock(&self->nodesMutex);
        }
    }

    if(originalCount)
        NodeManagerConnectNodes(self);

    if(NodeManagerConnections(self) != ACTIVE_NODE_COUNT || self->activeConnectionsSinceCheckup != NodeManagerConnections(self))
        printf("Node checkup finished with %d active connections\n", NodeManagerConnections(self));

    self->activeConnectionsSinceCheckup = NodeManagerConnections(self);

    FORIN(Node, node, self->nodes)
        NodeSendPing(node);

    WorkQueueRemoveByFunction(&self->workQueue, nodeCheckup);
    WorkQueueAddDelayed(&self->workQueue, nodeCheckup, DictOneS("self", DataPtr(self)), NODE_CHECKUP_INTERVAL * 1000);

    WorkQueueRemoveByFunction(&self->workQueue, bloomFilterCheckup);
    WorkQueueAddDelayed(&self->workQueue, bloomFilterCheckup, DictOneS("self", DataPtr(self)), BLOOMTILER_CHECKUP_INTERVAL * 1000);

    pthread_mutex_unlock(&self->nodesMutex);
}

void NodeManagerDisconnectAll(NodeManager *self)
{
    pthread_mutex_lock(&self->nodesMutex);

    FORIN(Node, node, self->nodes) {

        NodeClose(node);
        node->delegate = (struct NodeDelegate) { 0 };
        NodeFree(node);
    }

    // TODO: Go through workQueue dicts and remove all items that reference "oldnode"
    self->nodes = DatasRemoveAll(self->nodes);

    WorkQueueRemoveByFunction(&self->workQueue, setActiveNodeIfNone);
    WorkQueueRemoveByFunction(&self->workQueue, nodeCheckup);
    WorkQueueRemoveByFunction(&self->workQueue, bloomFilterCheckup);

    self->activeConnectionsSinceCheckup = 0;

    pthread_mutex_unlock(&self->nodesMutex);
}
