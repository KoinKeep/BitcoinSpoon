#ifndef PBKDF_H
#define PBKDF_H

#include <stdint.h>
#include <stdio.h>

enum {
    kCCPBKDF2 = 2,
};

typedef uint32_t CCPBKDFAlgorithm;

enum {
    kCCPRFHmacAlgSHA256 = 3,
    kCCPRFHmacAlgSHA512 = 5,
};

typedef uint32_t CCPseudoRandomAlgorithm;

int CCKeyDerivationPBKDF( CCPBKDFAlgorithm algorithm, const char *password, size_t passwordLen,
                      const uint8_t *salt, size_t saltLen,
                      CCPseudoRandomAlgorithm prf, unsigned rounds,
                      uint8_t *derivedKey, size_t derivedKeyLen);

#ifndef CC_SHA256_DIGEST_LENGTH
#define CC_SHA256_DIGEST_LENGTH     32
#endif

#endif
