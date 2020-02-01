//
// Created by Dustin Dettmer on 2020-01-13.
//

#include "hmacsha512.h"

#include "../hmac_and_pbkdf/hmac_and_pbkdf.h"

void CCHmac(CCHmacAlgorithm algorithm, const void *key, size_t keyLength, const void *data, size_t dataLength, void *macOut)
{
    if(algorithm == kCCHmacAlgSHA256) {

        HMAC_CTX(local_sha256) ctx;

        HMAC_INIT(local_sha256)(&ctx, key, keyLength);
        HMAC_UPDATE(local_sha256)(&ctx, data, dataLength);

        HMAC_FINAL(local_sha256)(&ctx, macOut);
    }

    if(algorithm == kCCHmacAlgSHA512) {

        HMAC_CTX(local_sha512) ctx;

        HMAC_INIT(local_sha512)(&ctx, key, keyLength);
        HMAC_UPDATE(local_sha512)(&ctx, data, dataLength);

        HMAC_FINAL(local_sha512)(&ctx, macOut);
    }
}
