#ifndef NODEMANAGER_H
#define NODEMANAGER_H

#include "Node.h"
#include "Transaction.h"
#include "WorkQueue.h"

#define NODE_CHECKUP_INTERVAL 15

extern const char *NodeManagerBlockchainSyncChange;

typedef enum {
    NodeManagerErrorNone = 0,
    NodeManagerErrorRejectedInvalid = 0x10,
    NodeManagerErrorRejectedDoubleSpend = 0x12,
    NodeManagerErrorRejectedNonStandard = 0x40,
    NodeManagerErrorRejectedTooMuchDust = 0x41,
    NodeManagerErrorRejectedFeeTooSmall = 0x42,
    NodeManagerErrorTimeout = 0x100,
} NodeManagerErrorType;

typedef struct NodeManager {

    int testnet;
    int blockchainSynced;

    uint32_t lowestSyncedHeight;
    uint32_t lastSyncedHeight;

    // Private

    uint64_t walletCreationDate;

    Data bloomFilter;
    int bloomFilterNeedsUpdate;

    Datas nodes;
    Node *activeNode;
    pthread_mutex_t nodesMutex;

    int activeConnectionsSinceCheckup;
    int rejectCount;

    int automaticallyPublishWaitingTransactions;

    Datas sendTxOnResults;

    WorkQueue workQueue;

} NodeManager;

extern NodeManager nodeManager;

NodeManager NodeManagerNew(uint64_t walletCreationDate);

// Call this repeatedly
void NodeManagerProcessNodes(NodeManager *manager);

int NodeManagerConnections(NodeManager *manager);

typedef void (*SendTxResult)(NodeManagerErrorType result, void *ptr);

// This all must be called on the same thread as NodeManagerProcessNodes
Datas *NodeManagerAllNodes(NodeManager *manager);
int NodeManagerIsActiveNode(NodeManager *manager, Node *node);
void NodeManagerSendTx(NodeManager *manager, Transaction tx, SendTxResult onResult, void *ptr);
void NodeManagerConnectNodes(NodeManager *manager);
void NodeManagerDisconnectAll(NodeManager *manager);
void NodeManagerDidBecomeActive(NodeManager *manager);
void NodeManagerUpdateTransactionFees(NodeManager *manager);

void NodeManagerAppDidBecomeActive(NodeManager *manager);

// The Database work queue must be empty before you can free a NodeManager
// This work queue must also be empty: WorkQueueThreadNamed("Bloom Filter Checkup")
void NodeManagerFree(NodeManager *manager); // Waits on the completion and destruction of the "TxProcessing" worker queue thread

#endif
