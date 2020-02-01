#ifndef HMACSHA512_H
#define HMACSHA512_H

#include <stdint.h>
#include <stdio.h>

enum {
    kCCHmacAlgSHA256,
    kCCHmacAlgSHA512,
};

typedef uint32_t CCHmacAlgorithm;

void CCHmac(CCHmacAlgorithm algorithm, const void *key, size_t keyLength, const void *data, size_t dataLength, void *macOut);

#ifndef CC_SHA512_DIGEST_LENGTH
#define CC_SHA512_DIGEST_LENGTH     64
#endif
#ifndef CC_SHA256_DIGEST_LENGTH
#define CC_SHA256_DIGEST_LENGTH     32
#endif

#endif
