#ifndef NODE_H
#define NODE_H

#include "MerkleBlock.h"
#include <pthread.h>

extern const char *NodeConnectionStatusChanged;

typedef uint32_t InventoryType;

static InventoryType InventoryTypeError = 0;
static InventoryType InventoryTypeTx = 1;
static InventoryType InventoryTypeBlock = 2;
static InventoryType InventoryTypeFilteredBlock = 3;
static InventoryType InventoryTypeCompactBlock = 4;

typedef struct Node {

    struct NodeDelegate {

        void (*newAddress)(struct Node *node, String ip, uint16_t port, uint64_t date, uint64_t services);

        /** IMPORTANT NOTICE **
         All Data objects will become invalid sometime after the delegate call is completed.
         It is vital to copy these data objects to preserve the data they point to if you plan on keeping them!!!
         **/

        void (*merkleBlock)(struct Node *node, Data blockData);
        void (*blockHeaders)(struct Node *node, Datas headers);
        void (*inventory)(struct Node *node, Datas types, Datas hashes);
        void (*tx)(struct Node *node, Data tx);
        int (*message)(struct Node *node, String message, char rejectCode, String reason, Data data); // Return non-zero to have this issue added to rejectCodes.

        void *extraPtr;

    } delegate;

    uint64_t lastHeadersOrMerkleMessage;
    int gotFinalHeadersMessage;

    MerkleBlock lastMerkleBlock;
    int transactionsSinceLastMerkleBlock;
    int32_t lastDlHeight;
    int32_t lastDlSize;

    Datas errors;
    Datas rejectCodes;

    String address;
    uint16_t port; // defaults to 8333 (or 18333 for testnet)
    int testnet;

    int connected;
    int connectionReady;

    Data curBloomFilter;

    int verackSent;

    int pingSendCount;
    int pongRecvCount;

    uint32_t version;
    uint64_t services;
    uint64_t connectTimestamp;
    String userAgent;

    /** internal properties **/

    int connection;

    Data inputBuffer;
    Data outputBuffer;

    Data versionPacket;

    int connectAttemptCount;
    pthread_t connectThread;

} Node;

Node NodeNew(String address);
void NodeFree(Node *node);

void NodeConnect(Node *node);
void NodeClose(Node *node);

// Call this repeatidly to process packets & connection state for node.
void NodeProcessPackets(Node *node);

// This must be the first message
void NodeSendVersion(Node *node, int relayFlag);

void NodeFilterLoad(Node *node, Data bloomFilter);
void NodeFilterClear(Node *node);

void NodeSendTx(Node *node, Data tx);

void NodeSendHeaders(Node *node);
void NodeSendPing(Node *node);

void NodeGetHeaders(Node *node, Datas knownBlockHashes, Data stopHash);
void NodeGetData(Node *node, Datas inventoryVectors);

// For nodes 'address' is compared.
int NodeIsEqual(Node *nodeOne, Node *nodeTwo);

String NodeDescription(Node *node);

#endif
