#include "Node.h"
#include "BTCUtil.h"
#include "NodeConstants.h"
#include "NodeManager.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <time.h>
#include "Notifications.h"

#ifdef __APPLE__
#include <sys/_select.h>
#endif

#ifdef __ANDROID__
#include <android/log.h>

#define pthread_cancel(thread_id) pthread_kill(thread_id, SIGUSR1);
#define pthread_setcancelstate(...)

#define printf(...) __android_log_print(ANDROID_LOG_INFO, "Node", __VA_ARGS__)

#endif

const char *NodeConnectionStatusChanged = "NodeConnectionStatusChanged";

// By default we turn this off as it's a privacy leak.
#define RESPOND_TO_GETTX_REQUESTS 0

#define NodeLog(fmt, ...) printf(("Node[%s%s] " fmt "\n"), self->address.bytes, self->userAgent.bytes ?: "", ##__VA_ARGS__); \
    self->errors = DatasUntrack(DatasAddCopy(self->errors, StringF(fmt, ##__VA_ARGS__)))

#define CmdLog(fmt, ...) printf(("Node[%s%s] %s " fmt "\n"), self->address.bytes, self->userAgent.bytes ?: "", command.bytes, ##__VA_ARGS__); \
    self->errors = DatasUntrack(DatasAddCopy(self->errors, StringF(fmt, ##__VA_ARGS__)))

#define CmdRequire(condition, fmt, ...) do { if(!(condition)) { \
        printf(("Node[%s%s] %s " fmt), self->address.bytes, self->userAgent.bytes ?: "", command.bytes, ##__VA_ARGS__); \
        self->errors = DatasUntrack(DatasAddCopy(self->errors, StringF(fmt, ##__VA_ARGS__))); \
        return; \
    } } while(0)

#define CmdLogReturn(fmt, ...) do { printf(("Node[%s%s] %s " fmt "\n"), self->address.bytes, self->userAgent.bytes ?: "", command.bytes, ##__VA_ARGS__); \
    self->errors = DatasUntrack(DatasAddCopy(self->errors, StringF(fmt, ##__VA_ARGS__))); \
    return; } while(0)

#define CmdPtrIncrement(size) \
    (void*)ptr; if((size) > end - ptr) CmdLogReturn("packet too small for increment of %d", (int)(size)); \
    ptr += (size);

static void NodeReset(Node *node);
static void processInputBuffer(Node *node);
static int processVersionAndOutputBuffer(Node *node);
static void received(Node *node, String command, Data payload);

static const uint8_t mainMagic[] = MAIN_MAGIC;
static const uint8_t testMagic[] = TEST_MAGIC;
static const uint8_t test3Magic[] = TEST3_MAGIC;
static const uint8_t nameMagic[] =  NAME_MAGIC;

Node NodeNew(String address)
{
    Node node = { 0 };

    node.address = DataUntrackCopy(address);

    node.connection = -1;

    return node;
}

void NodeFree(Node *self)
{
    NodeReset(self);

    self->address = DataFree(self->address);
    self->userAgent = DataFree(self->userAgent);
    self->curBloomFilter = DataFree(self->curBloomFilter);
    MerkleBlockFree(&self->lastMerkleBlock);
}

static int openConnection(const char *hostname, int port, int *socketRef, struct addrinfo **addrs)
{
    int sd = -1, err;
    struct addrinfo hints = {0};
    char port_str[16] = {0};

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    sprintf(port_str, "%d", port);

    err = getaddrinfo(hostname, port_str, &hints, addrs);

    if (err != 0) {

        //printf("%s: %s\n", hostname, gai_strerror(err));
        return -1;
    }

    for(struct addrinfo *addr = *addrs; addr != NULL; addr = addr->ai_next) {

        sd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        *socketRef = sd;
        if (sd == -1)
        {
            err = errno;
            continue;
        }

        if (connect(sd, addr->ai_addr, addr->ai_addrlen) == 0)
            break;

        err = errno;

        *socketRef = 0;
        close(sd);
        sd = -1;
    }

    if (sd == -1) {

        printf("%s: %s\n", hostname, strerror(err));
    }

    if(sd != -1 && fcntl(sd, F_SETFL, O_NONBLOCK) < 0) {

        printf("Unable to make socket non-blocking");
    }

    return sd;
}

typedef struct {

    Node *node;
    int socketRef;
    struct addrinfo *addrs;
    int attemptNumber;
    char *address;
    uint16_t port;

} ConnectionThreadParameters;

static void connectionThreadCleanup(void *ptr)
{
    ConnectionThreadParameters *parms = (ConnectionThreadParameters *)ptr;

    if(parms->addrs)
        freeaddrinfo(parms->addrs);

    if(!parms->node->connected && parms->socketRef)
        close(parms->socketRef);

    free(parms->address);
    free(ptr);

    DataTrackPop();
}

static void *connectionThread(void *ptr)
{
    DataTrackPush();

    ConnectionThreadParameters *parms = (ConnectionThreadParameters *)ptr;

    pthread_cleanup_push(connectionThreadCleanup, ptr);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    int result = openConnection(parms->address, parms->port, &parms->socketRef, &parms->addrs);

    if(parms->attemptNumber == parms->node->connectAttemptCount) {

        parms->node->connection = result;
        parms->node->connected = 1;

        NotificationsFire(NodeConnectionStatusChanged, DictNew());
    }

    pthread_cleanup_pop(1);

    return NULL;
}

static void NodeReset(Node *self)
{
    self->inputBuffer = DataFree(self->inputBuffer);
    self->outputBuffer = DataFree(self->outputBuffer);
    self->versionPacket = DataFree(self->versionPacket);
    self->errors = DatasFree(self->errors);
    self->rejectCodes = DatasFree(self->rejectCodes);
}

void NodeConnect(Node *self)
{
    if(self->connectThread) {

        pthread_cancel(self->connectThread);
        pthread_join(self->connectThread, NULL);
        self->connectThread = 0;
    }

    NodeReset(self);

    self->inputBuffer = DataUntrack(DataNew(0));
    self->outputBuffer = DataUntrack(DataNew(0));
    self->versionPacket = DataUntrack(DataNew(0));
    self->errors = DatasUntrack(DatasNew());
    self->rejectCodes = DatasUntrack(DatasNew());

    char *address = malloc(self->address.length);

    strncpy(address, self->address.bytes, self->address.length);

    uint16_t port = self->port;
    // TODO: Enable parsing of :port
//    if(!port && address) {
//
//        char *ptr = strstr(address, ":");
//
//        if(ptr && *ptr == ':' && NULL == strstr(ptr + 1, ':')) {
//
//            port = atol(ptr + 1);
//            *ptr = 0;
//        }
//    }

    if(!port)
        port = self->testnet ? 18333 : 8333;

    self->connectAttemptCount = self->connectAttemptCount + 1;

    ConnectionThreadParameters *parms = malloc(sizeof(ConnectionThreadParameters));

    memset(parms, 0, sizeof(ConnectionThreadParameters));

    parms->node = self;
    parms->socketRef = 0;
    parms->addrs = NULL;
    parms->attemptNumber = self->connectAttemptCount;
    parms->address = address;
    parms->port = port;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16384 * 2);

    if(pthread_create(&self->connectThread, &attr, connectionThread, parms))
        abort();
}

void NodeClose(Node *self)
{
    close(self->connection);

    self->connection = -1;

    self->connected = 0;
    self->connectionReady = 0;

    self->connectAttemptCount = self->connectAttemptCount + 1;

    if(self->connectThread) {

        pthread_cancel(self->connectThread);
        pthread_join(self->connectThread, NULL);
        self->connectThread = 0;
    }

    self->connectThread = 0;

    NodeReset(self);

    NotificationsFire(NodeConnectionStatusChanged, DictNew());
}

static void NodeProcessWrite(Node *self)
{

}

static void NodeProcessRead(Node *self)
{
    DataTrackPush();

    uint8_t buffer[SINGLE_READ_BUFFER];

    ssize_t result = recv(self->connection, buffer, sizeof(buffer), 0);

    if(result > 0) {

        if(self->inputBuffer.length + result > MAX_ALLOWED_BUFFER_BACKLOG) {

            NodeClose(self);
        }
        else {

            self->inputBuffer = DataUntrack(DataAppend(self->inputBuffer, DataRef(buffer, (uint32_t)result)));

            processInputBuffer(self);
        }
    }

    if(result == 0) {

        NodeLog("Other side closed the connection");

        NodeClose(self);

        NotificationsFire(NodeConnectionStatusChanged, DictNew());
    }

    if(result == -1) {

        NodeClose(self);

        NotificationsFire(NodeConnectionStatusChanged, DictNew());
    }

    DataTrackPop();
}

void NodeProcessPackets(Node *self)
{
    int sd = self->connection;

    if(sd < 1)
        return;

    if(self->connectThread) {

        pthread_join(self->connectThread, NULL);
        self->connectThread = 0;
    }

    DataTrackPush();

    if(self->inputBuffer.length)
        processInputBuffer(self);

    fd_set read, write, except;

    FD_ZERO(&read);
    FD_ZERO(&write);
    FD_ZERO(&except);

    FD_SET(sd, &read);
    FD_SET(sd, &write);
    FD_SET(sd, &except);

    struct timeval waitTime = { 0 };

    int activity = select(sd + 1, &read, &write, &except, &waitTime);

    switch(activity) {
        case 0:
            perror("select() shouldn't return 0");

        case -1:
            perror("select()");
            NodeClose(self);
            abort();
            break;

        default:
            if(self->connection > 0 && FD_ISSET(self->connection, &read)) {

                NodeProcessRead(self);
            }
            if(self->connection > 0 && FD_ISSET(self->connection, &write)) {

                NodeProcessWrite(self);
            }
            if(self->connection > 0 && FD_ISSET(self->connection, &except)) {

                NodeLog("socket select exception, dropping node connetion");
                NodeClose(self);
            }
    }

    processVersionAndOutputBuffer(self);

    DataTrackPop();
}

static void processInputBuffer(Node *self)
{
    DataTrackPush();

    (void)testMagic;
    (void)nameMagic;

    if(sizeof(PacketHeader) != 4 + 12 + 4 + 4)
        abort();

    uint8_t *ptr = (void*)self->inputBuffer.bytes;
    uint8_t *end = ptr + self->inputBuffer.length;

    int count = 0;

    while(ptr + sizeof(PacketHeader) <= end) {

        DataTrackPush();

        if(++count > PACKETS_PER_PROCESS) {

            break;
        }

        PacketHeader *header = (PacketHeader*)ptr;

        if(!self->testnet && 0 != memcmp(header->magic, mainMagic, 4)) {

            ptr++;
            continue;
        }

        if(self->testnet && 0 != memcmp(header->magic, test3Magic, 4)) {

            ptr++;
            continue;
        }

        if(sizeof(PacketHeader) + header->length > (end - ptr))
            break;

        uint8_t *payload = ptr + sizeof(PacketHeader);

        ptr += sizeof(PacketHeader) + header->length;

        Data payloadObj = DataRef(payload, header->length);

        char buf[sizeof(header->command) + 1];
        memset(buf, 0, sizeof(header->command) + 1);
        memcpy(buf, header->command, sizeof(header->command));

        String command = StringNew(buf);

        Data hash = hash256(DataRef(payload, header->length));

        if(hash.length < 4 || 0 != memcmp(header->checksum, hash.bytes, 4)) {

            NodeLog("dropping %s packet with invalid checksum", command.bytes);
            continue;
        }

        received(self, command, payloadObj);

        DataTrackPop();
    }

    unsigned long usedBytes = ptr - (uint8_t*)self->inputBuffer.bytes;

    self->inputBuffer = DataUntrack(DataDelete(self->inputBuffer, 0, (uint32_t)usedBytes));

    DataTrackPop();
}

static int processVersionAndOutputBuffer(Node *self)
{
    // Non blocking send here. less bytes may be sent that we want.

    if(self->connected < 1)
        return 0;

    while(self->versionPacket.length) {

        errno = 0;

        ssize_t result = send(self->connection, self->versionPacket.bytes, self->versionPacket.length, 0);

        if(result < 0) {

            if(errno == EAGAIN) {

                break;
            }
            else {

                //NodeLog("send error, errno: %d %s", errno, strerror(errno));
                NodeClose(self);
                return 0;
            }
        }

        if(result == 0) {

            NodeLog("connection closed in send (of version)");
            NodeClose(self);
            return 0;
        }

        self->versionPacket = DataUntrack(DataDelete(self->versionPacket, 0, (uint32_t)result));
    }

    // Wait for verack before sending queued data
    if(self->versionPacket.length || !self->verackSent)
        return 0;

    while(self->outputBuffer.length) {

        errno = 0;

        ssize_t result = send(self->connection, self->outputBuffer.bytes, self->outputBuffer.length, 0);

        if(result < 0) {

            if(errno == EAGAIN) {

                break;
            }

            NodeLog("send error, errno: %d strerror: %s", errno, strerror(errno));
            NodeClose(self);
            return 0;
        }

        if(result == 0) {

            NodeLog("connection closed in send errno: %d strerror: %s", errno, strerror(errno));
            NodeClose(self);
            return 0;
        }

        self->outputBuffer = DataUntrack(DataDelete(self->outputBuffer, 0, (uint32_t)result));
    }

    return 1;
}

static int nodeSend(Node *self, const char *str, Data payload)
{
    DataTrackPush();

    if(!payload.bytes)
        payload = DataNew(0);

    PacketHeader header;

    memset(&header, 0, sizeof(header));

    Data command = DataRef((void*)str, (uint32_t)strlen(str) + 1);

    BTCUTILAssert(str && strlen(str) <= sizeof(header.command));

    if(strlen(str) > sizeof(header.command))
        return DTPopi(0);

    Data hash = hash256(payload);

    BTCUTILAssert(hash.length == 32);

    if(hash.length != 32)
        return DTPopi(0);

    memcpy(&header.magic, self->testnet ? test3Magic : mainMagic, 4);
    strncpy(header.command, str, 12);
    header.length = (uint32_t)payload.length;
    memcpy(header.checksum, hash.bytes, 4);

    // verack jumps to the front of the line
    if(DataEqual(command, StringNew("verack"))) {

        self->outputBuffer = DataUntrack(DataInsert(self->outputBuffer, 0, payload));
        self->outputBuffer = DataUntrack(DataInsert(self->outputBuffer, 0, DataRaw(header)));

        self->verackSent = 1;
    }
    else if(DataEqual(command, StringNew("version"))) {

        self->versionPacket = DataUntrack(DataAppend(self->versionPacket, DataRaw(header)));
        self->versionPacket = DataUntrack(DataAppend(self->versionPacket, payload));
    }
    else {

        self->outputBuffer = DataUntrack(DataAppend(self->outputBuffer, DataRaw(header)));
        self->outputBuffer = DataUntrack(DataAppend(self->outputBuffer, payload));
    }

    return DTPopi(processVersionAndOutputBuffer(self));
}

String readString(const uint8_t **ptr, const uint8_t *end)
{
    uint64_t length = readVarInt(ptr, end);

    if(length > end - *ptr)
        return DataNull();

    Data result = DataCopy(*ptr, (uint32_t)length);
    *ptr += length;

    result = DataAppend(result, DataInt(0));

    return result;
}

static String parseIp(uint8_t ip[16])
{
    uint8_t ipv4Prefix[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };

    char str[INET6_ADDRSTRLEN + 1] = {0};

    if(0 == memcmp(ipv4Prefix, ip, sizeof(ipv4Prefix))) {

        if(!inet_ntop(AF_INET, ip + sizeof(ipv4Prefix), str, INET6_ADDRSTRLEN))
            return DataNull();
    }
    else {

        if(!inet_ntop(AF_INET6, ip, str, INET6_ADDRSTRLEN))
            return DataNull();
    }

    return StringNew(str);
}

static void received(Node *self, String command, Data payload)
{
    const uint8_t *ptr = (const uint8_t*)payload.bytes;
    const uint8_t *end = ptr + payload.length;

    if(DataEqual(command, StringNew("version")) && payload.length > sizeof(VersionHeader)) {

        VersionHeader *header = CmdPtrIncrement(sizeof(*header));
        DataFree(self->userAgent);
        self->userAgent = DataUntrack(readString(&ptr, end));
        VersionPayload *payload = CmdPtrIncrement(sizeof(*payload));

        self->version = header->version;
        self->services = header->services;
        self->connectTimestamp = header->timestamp;

        nodeSend(self, "verack", DataNull());
    }
    else if(DataEqual(command, StringNew("verack"))) {

        self->connectionReady = 1;
    }
    else if(DataEqual(command, StringNew("ping"))) {

        uint64_t *nonce = CmdPtrIncrement(sizeof(*nonce));

        nodeSend(self, "pong", uint64D(*nonce));
    }
    else if(DataEqual(command, StringNew("pong"))) {

        self->pongRecvCount = self->pongRecvCount + 1;
    }
    else if(DataEqual(command, StringNew("alert"))) {

        // ignore alerts
    }
    else if(DataEqual(command, StringNew("addr"))) {

        uint64_t count = readVarInt(&ptr, end);

        CmdRequire(count <= 1000, "addr count higher than spec");

        for(int i = 0; i < count; i++) {

            uint32_t *timestamp = CmdPtrIncrement(sizeof(*timestamp));
            NetworkAddress *address = CmdPtrIncrement(sizeof(*address));

            if(self->delegate.newAddress)
                self->delegate.newAddress(self, parseIp(address->ip), ntohs(address->port), *timestamp, address->services);
        }
    }
    else if(DataEqual(command, StringNew("inv"))) {

        uint64_t count = readVarInt(&ptr, end);

        CmdRequire(count <= 50000, "inv count higher than spec");

        Datas types = DatasNew();
        Datas hashes = DatasNew();

        for(int i = 0; i < count; i++) {

            uint32_t *type = CmdPtrIncrement(sizeof(*type));
            uint8_t *hash = CmdPtrIncrement(32);

            types = DatasAddCopy(types, DataRaw(*type));
            hashes = DatasAddCopy(hashes, DataRef(hash, 32));
        }

        if(self->delegate.inventory)
            self->delegate.inventory(self, types, hashes);
    }
    else if(DataEqual(command, StringNew("headers"))) {

        uint64_t count = readVarInt(&ptr, end);

        CmdRequire(count <= 2000, "received more than 2000 headers at once");

        Datas result = DatasNew();

        for(int i = 0; i < count; i++) {

            uint8_t *header = CmdPtrIncrement(80);
            readVarInt(&ptr, end);

            CmdRequire(ptr - header > 80, "Transaction count missing from header");

            self->lastHeadersOrMerkleMessage = time(0);

            result = DatasAddCopy(result, DataRef(header, (uint32_t)(ptr - header)));
        }

        if(self->delegate.blockHeaders)
            self->delegate.blockHeaders(self, result);
    }
    else if(DataEqual(command, StringNew("merkleblock"))) {

        self->lastHeadersOrMerkleMessage = time(0);

        if(self->delegate.merkleBlock)
            self->delegate.merkleBlock(self, payload);
    }
    else if(DataEqual(command, StringNew("tx"))) {

        if(self->delegate.tx)
            self->delegate.tx(self, payload);
    }
    else if(DataEqual(command, StringNew("reject"))) {

        String message = readString(&ptr, end);
        char *ccode = CmdPtrIncrement(1);
        String reason = readString(&ptr, end);
        Data data = DataCopy(ptr, (uint32_t)(end - ptr));

        int addRejectCode = 1;

        if(self->delegate.message)
            addRejectCode = self->delegate.message(self, message, *ccode, reason, data);
        else
            CmdRequire(0, "[%s request failed] ccode %x reason: %s %s", message.bytes, *ccode, reason.bytes, toHex(data).bytes);

        if(addRejectCode)
            self->rejectCodes = DatasUntrack(DatasAddCopy(self->rejectCodes, DataRef(ccode, sizeof(*ccode))));
    }
    else if(DataEqual(command, StringNew("getdata"))) {

        uint64_t count = readVarInt(&ptr, end);

        for(int i = 0; i < count; i++) {

            uint32_t type = uint32readP(&ptr, end);

            Data hash = readBytes(32, &ptr, end);

            const char *typeStr = type == InventoryTypeTx ? "tx" : type == InventoryTypeBlock ? "block" : "";

            NodeLog("%s getdata %s", typeStr, toHex(DataFlipEndianCopy(hash)).bytes);

            if(type == InventoryTypeTx) {

#if RESPOND_TO_GETTX_REQUESTS
                Data tx = DatabaseTransactionForTxid(hash);

                if(tx.bytes) {

                    NodeLog("Responding to tx getdata with transaction.");

                    sendTx(self, tx);
                }
#endif
            }
        }
    }
    else
        CmdLog("unrecognized command: %s", command.bytes);
}

void NodeSendVersion(Node *self, int relayFlag)
{
    DataTrackPush();

    uint8_t buffer[sizeof(VersionHeader) + 1 + sizeof(VersionPayload)];

    VersionHeader *header = (VersionHeader*)buffer;
    uint8_t *userAgent = (uint8_t*)header + sizeof(*header);
    VersionPayload *payload = (VersionPayload*)(userAgent + sizeof(*userAgent));

    header->version = NODE_VERSION;
    header->services = 0;
    header->timestamp = time(0);

    memset(&header->addressOfReceiver, 0, sizeof(header->addressOfReceiver));
    memset(&header->addressOfSender, 0, sizeof(header->addressOfSender));
    header->nonce = 0xffffffffffffffff;

    *userAgent = 0;

    payload->startHeight = 0;
    payload->relay = relayFlag ? 1 : 0;

    nodeSend(self, "version", DataRaw(buffer));

    DataTrackPop();
}

void NodeFilterLoad(Node *self, Data bloomFilter)
{
    nodeSend(self, "filterload", bloomFilter);

    self->curBloomFilter = DataFree(self->curBloomFilter);
    self->curBloomFilter = DataUntrackCopy(bloomFilter);
}

void NodeFilterClear(Node *self)
{
    nodeSend(self, "filterclear", DataNull());

    self->curBloomFilter = DataFree(self->curBloomFilter);
}

void NodeSendTx(Node *self, Data tx)
{
    nodeSend(self, "tx", tx);
}

void NodeSendHeaders(Node *self)
{
    nodeSend(self, "sendheaders", DataNull());
}

void NodeSendPing(Node *self)
{
    int count = self->pingSendCount + 1;

    nodeSend(self, "ping", uint64D(count));

    self->pingSendCount = count;

}

void NodeGetHeaders(Node *self, Datas knownHashes, Data stopHash)
{
    NodeLog("Requesting blocks past %s", toHex(DataFlipEndianCopy(DatasFirst(knownHashes))).bytes);

    if(!stopHash.bytes)
        stopHash = DataZero(32);

    if(!knownHashes.count)
        knownHashes = DatasOneCopy(DataFlipEndianCopy(fromHex("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f")));

    Data payload = DataNew(0);

    payload = DataAppend(payload, uint32D(NODE_VERSION));
    payload = DataAppend(payload, varIntD(knownHashes.count));

    for(int i = 0; i < knownHashes.count; i++)
        payload = DataAppend(payload, knownHashes.ptr[i]);

    payload = DataAppend(payload, stopHash);

    nodeSend(self, "getheaders", payload);
}

void NodeGetData(Node *self, Datas inventoryVectors)
{
    Data payload = DataNew(0);

    payload = DataAppend(payload, varIntD(inventoryVectors.count));

    for(int i = 0; i < inventoryVectors.count; i++)
        payload = DataAppend(payload, inventoryVectors.ptr[i]);

    nodeSend(self, "getdata", payload);
}

int NodeIsEqual(Node *nodeOne, Node *nodeTwo)
{
    return DataEqual(nodeOne->address, nodeTwo->address);
}

String NodeDescription(Node *self)
{
    String result = StringF("%s%s:%d%s", self->address.bytes, self->userAgent.bytes ?: "", self->port, self->testnet ? "TN" : "");

    return result;
}
