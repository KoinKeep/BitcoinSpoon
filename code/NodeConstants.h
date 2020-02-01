#ifndef NODECONSTANTS_H
#define NODECONSTANTS_H

#include "Node.h"

// Drop node connection once this is reached in either incoming or outgoing buffers
#define MAX_ALLOWED_BUFFER_BACKLOG 10 * 1024 * 1024

// The max number of bytes processed by a single call to 'NodeProcessPackets'
// Too small and you'll never catch up with a fast connection, too large and you'll exhause the stack
#define SINGLE_READ_BUFFER 1024 * 1024

#define PACKETS_PER_PROCESS 1000

#define NODE_VERSION 70001

#define MAIN_MAGIC { 0xf9, 0xbe, 0xb4, 0xd9 }
#define TEST_MAGIC { 0xfa, 0xbf, 0xb5, 0xda }
#define TEST3_MAGIC { 0x0b, 0x11, 0x09, 0x07 }
#define NAME_MAGIC { 0xf9, 0xbe, 0xb4, 0xfe }

#define PACKED __attribute__((packed))

typedef struct PACKED {

    uint8_t magic[4];
    char command[12];
    uint32_t length;
    uint8_t checksum[4];

} PacketHeader;

typedef struct PACKED {

    uint64_t services;
    uint8_t ip[16]; // ipv6 or ipv4 mapped in ipv6
    uint16_t port; // network byte order

} NetworkAddress;

typedef struct PACKED {

    int32_t version;
    uint64_t services;
    int64_t timestamp;
    NetworkAddress addressOfReceiver;
    NetworkAddress addressOfSender;
    uint64_t nonce;
    // var_str userAgent
    // int32_t startHeight
    // uint8_t relay

} VersionHeader;

typedef struct PACKED {

    int32_t startHeight;
    uint8_t relay;

} VersionPayload;

typedef struct PACKET {

    InventoryType type;
    uint8_t hash[32];

} InventoryVector;

#endif
