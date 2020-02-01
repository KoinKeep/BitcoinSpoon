/*
 * sha512.h - interface to mbedTLS SHA512 hash function.
 *
 * Copyright 2017 Google Inc.
 * Author: Joe Richey (joerichey@google.com)
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */

#ifndef SHA512_H
#define SHA512_H

#include <stddef.h>
#include <stdint.h>

#define SHA512_DIGEST_LENGTH 64

#define SHA_LBLOCK 16

#define SHA512_CBLOCK (SHA_LBLOCK*8)
/*
 * SHA-512 context structure
 */
typedef struct {
    uint64_t total[2];         /*!< number of bytes processed  */
    union {
        uint64_t state[8];         /*!< intermediate digest state  */
        uint64_t h[8];
    };
    unsigned char buffer[128]; /*!< data block being processed */
} mbedtls_sha512_context;

typedef mbedtls_sha512_context SHA512_CTX;

void mbedtls_sha512_init(mbedtls_sha512_context *ctx);
void mbedtls_sha512_starts(mbedtls_sha512_context *ctx);

void mbedtls_sha512_init_and_start(mbedtls_sha512_context *ctx);

void mbedtls_sha512_update(mbedtls_sha512_context *ctx,
                           const unsigned char *input, size_t ilen);
void mbedtls_sha512_process(mbedtls_sha512_context *ctx,
                            const unsigned char data[128]);
void mbedtls_sha512_finish(mbedtls_sha512_context *ctx,
                           unsigned char output[64]);

extern void SHA512(const uint8_t* in, size_t n,
                   uint8_t out[SHA512_DIGEST_LENGTH]);

// Zero the memory pointed to by v; this will not be optimized away.
extern void secure_wipe(uint8_t* v, uint32_t n);

#endif /* SHA512_H */

