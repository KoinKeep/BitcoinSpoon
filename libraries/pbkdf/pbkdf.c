//
// Created by Dustin Dettmer on 2020-01-13.
//

#include "pbkdf.h"

#include "../hmac_and_pbkdf/hmac_and_pbkdf.h"

int CCKeyDerivationPBKDF( CCPBKDFAlgorithm algorithm, const char *password, size_t passwordLen,
                          const uint8_t *salt, size_t saltLen,
                          CCPseudoRandomAlgorithm prf, unsigned rounds,
                          uint8_t *derivedKey, size_t derivedKeyLen)
{
    if(algorithm != kCCPBKDF2)
        return -1;

    if(prf == kCCPRFHmacAlgSHA256) {

        PBKDF2(local_sha256)((const uint8_t*)password, passwordLen, salt, saltLen, rounds, derivedKey, derivedKeyLen);
        return 0;
    }

    if(prf == kCCPRFHmacAlgSHA512) {

        PBKDF2(local_sha512)((const uint8_t*)password, passwordLen, salt, saltLen, rounds, derivedKey, derivedKeyLen);
        return 0;
    }

    return -1;
}
