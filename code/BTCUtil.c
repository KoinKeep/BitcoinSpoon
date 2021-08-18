#include "BTCUtil.h"
#include "BTCConstants.h"
#include "../libraries/rmd160/rmd160.h"
#include "../libraries/secp256k1/include/secp256k1.h"
#include "../libraries/secp256k1/include/secp256k1_ecdh.h"
#include "../libraries/aes/aes.h"
#include "../libraries/segwit_addr/segwit_addr.h"
#include "../libraries/mnemonic/mnemonic.h"
#include "../libraries/mnemonic/wordlist.h"
#include "../libraries/murmur3/murmur3.h"
#include "../libraries/sha256/sha256.h"
#include "../libraries/sha512/sha512.h"
#include "../libraries/pbkdf/pbkdf.h"
#include "../libraries/hmacsha512/hmacsha512.h"
#include "base58.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if !defined(__ANDROID__)
#include <uuid/uuid.h>
#endif

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

static secp256k1_context *secpCtx = NULL;

static Data publicKeyFromPrivate(Data privateKey);
static Data publicKeySerializeDefault(Data publicKey);
static Data publicKeySerialize(Data publicKey, int compressed);
static Data keyFromHdWallet(Data hdWallet);

void BTCUtilStartup()
{
    secpCtx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
}

void BTCUtilShutdown()
{
    secp256k1_context_destroy(secpCtx);
    
    secpCtx = NULL;
}

uint64_t readVarInt(const uint8_t **ptr, const uint8_t* end)
{
    uint64_t result = uint8readP(ptr, end);
    
    if(result < 253)
        return result;
    
    if(result < 254)
        return uint16readP(ptr, end);
    
    if(result < 255)
        return uint32readP(ptr, end);
    
    return uint64readP(ptr, end);
}

uint64_t readPushDataWithOp(const uint8_t **ptr, const uint8_t* end, uint8_t *opcode)
{
    uint64_t result = uint8readP(ptr, end);
    
    if(opcode)
        *opcode = result;
    
    if(result < OP_PUSHDATA1)
        return result;
    
    if(result == OP_PUSHDATA1)
        return uint8readP(ptr, end);
    
    if(result == OP_PUSHDATA2)
        return uint16readP(ptr, end);
    
    BTCUTILAssert(result == OP_PUSHDATA4);
    
    return uint32readP(ptr, end);
}

uint64_t readPushData(const uint8_t **ptr, const uint8_t* end)
{
    return readPushDataWithOp(ptr, end, NULL);
}

Data pushData(uint32_t amount)
{
    if(amount & 0xFFFF0000)
        return DataAdd(uint8D(OP_PUSHDATA4), uint32D(amount));

    if(amount & 0xFF00)
        return DataAdd(uint8D(OP_PUSHDATA2), uint16D(amount));

    if(amount >= OP_PUSHDATA1)
        return DataAdd(uint8D(OP_PUSHDATA1), uint8D(amount));

    return uint8D(amount);
}

Data scriptPush(Data data)
{
    DataTrackPush();

    return DTPop(DataAddCopy(pushData((uint32_t)data.length), data));
}

Datas readPushes(Data script)
{
    Datas array = DatasNew();
    
    const uint8_t *ptr = (const uint8_t*)script.bytes;
    const uint8_t *end = ptr + script.length;
    
    while(isPushData(ptr, end)) {
        
        uint64_t length = readPushData(&ptr, end);

        array = DatasAddRef(array, readBytes(length, &ptr, end));
    }
    
    return array;
}

Data writePushes(Datas items)
{
    Data data = DataNew(0);

    for(int i = 0; i < items.count; i++)
        data = DataAdd(data, scriptPush(items.ptr[i]));
    
    return data;
}

int isPushData(const uint8_t *ptr, const uint8_t *end)
{
    if(end - ptr <= 0)
        return 0;
    
    return ptr[0] > 0 && ptr[0] <= OP_PUSHDATA4;
}

uint8_t uint8readP(const uint8_t **ptr, const uint8_t* end)
{
    uint8_t result = 0;
    
    if(end - *ptr >= sizeof(result)) {
        
        result = *(typeof(result)*)*ptr;
        
        *ptr += sizeof(result);
        
        return result;
    }
    
    *ptr = end;
    
    return 0;
}

uint16_t uint16readP(const uint8_t **ptr, const uint8_t* end)
{
    uint16_t result = 0;
    
    if(end - *ptr >= sizeof(result)) {
        
        result = *(typeof(result)*)*ptr;
        
        *ptr += sizeof(result);
        
        return result;
    }
    
    *ptr = end;
    
    return 0;
}

uint32_t uint32readP(const uint8_t **ptr, const uint8_t* end)
{
    uint32_t result = 0;
    
    if(end - *ptr >= sizeof(result)) {
        
        result = *(typeof(result)*)*ptr;
        
        *ptr += sizeof(result);
        
        return result;
    }
    
    *ptr = end;
    
    return 0;
}

uint64_t uint64readP(const uint8_t **ptr, const uint8_t* end)
{
    uint64_t result = 0;
    
    if(end - *ptr >= sizeof(result)) {
        
        result = *(typeof(result)*)*ptr;
        
        *ptr += sizeof(result);
        
        return result;
    }
    
    *ptr = end;
    
    return 0;
}

uint8_t uint8read(Data data)
{
    if(!data.length)
        return 0;

    return *(uint8_t*)data.bytes;
}

uint16_t uint16read(Data data)
{
    if(data.length < 2)
        return 0;

    return *(uint16_t*)data.bytes;
}

uint32_t uint32read(Data data)
{
    if(data.length < 4)
        return 0;

    return *(uint32_t*)data.bytes;
}

uint64_t uint64read(Data data)
{
    if(data.length < 8)
        return 0;

    return *(uint64_t*)data.bytes;
}

Data readBytesUnsafe(uint64_t bytes, const uint8_t **ptr, const uint8_t* end)
{
    if(end - *ptr >= bytes) {

        Data result = DataRef((void*)*ptr, (uint32_t)bytes);

        *ptr += bytes;

        return result;
    }

    *ptr = end;

    return DataNull();
}

Data readBytes(uint64_t bytes, const uint8_t **ptr, const uint8_t* end)
{
    return DataCopyData(readBytesUnsafe(bytes, ptr, end));
}

Data sha256(Data data)
{
    if(!data.bytes)
        return DataNull();

    Data result = DataNew(SHA256_BLOCK_SIZE);

    SHA256_CTX ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, (void*)data.bytes, data.length);
    sha256_final(&ctx, (void*)result.bytes);
    secure_wipe((uint8_t *)&ctx, sizeof(ctx));

    return result;
}

Data sha512(Data data)
{
    if(!data.bytes)
        return DataNull();

    Data result = DataNew(SHA512_DIGEST_LENGTH);

    SHA512_CTX ctx;

    mbedtls_sha512_init(&ctx);
    mbedtls_sha512_starts(&ctx);
    mbedtls_sha512_update(&ctx, (void*)data.bytes, data.length);
    mbedtls_sha512_finish(&ctx, (void*)result.bytes);
    secure_wipe((uint8_t *)&ctx, sizeof(ctx));

    return result;
}

Data ripemd160(Data data)
{
    DataTrackPush();

    if(!data.bytes)
        return DTPopNull();

    const int RMDsize = 160;

    typedef    uint8_t        byte;
    typedef    uint32_t        dword;
    
    dword         MDbuf[RMDsize/32];   /* contains (A, B, C, D(, E))   */
    byte          hashcode[RMDsize/8]; /* for final hash-value         */
    dword         X[16];               /* current 16-word chunk        */
    unsigned int  i;                   /* counter                      */
    dword  length = (dword)data.length;/* length in bytes of message   */
    dword         nbytes;              /* # of bytes not yet processed */
    
    byte *ptr = (byte*)data.bytes;
    
    MDinit(MDbuf);
    
    /* process message in 16-word chunks */
    for (nbytes=(uint32_t)length; nbytes > 63; nbytes-=64) {
        for (i=0; i<16; i++) {
            X[i] = BYTES_TO_DWORD(ptr);
            ptr += 4;
        }
        compress(MDbuf, X);
    }                                    /* length mod 64 bytes left */
    
    /* finish: */
    MDfinish(MDbuf, ptr, length, 0);
    
    for (i=0; i<RMDsize/8; i+=4) {
        hashcode[i]   =  MDbuf[i>>2];         /* implicit cast to byte  */
        hashcode[i+1] = (MDbuf[i>>2] >>  8);  /*  extracts the 8 least  */
        hashcode[i+2] = (MDbuf[i>>2] >> 16);  /*  significant bits.     */
        hashcode[i+3] = (MDbuf[i>>2] >> 24);
    }

    return DTPop(DataCopy((void*)hashcode, RMDsize / 8));
}

Data hash160(Data data)
{
    return DTPop(ripemd160(sha256(DTPush(data))));
}

Data hash256(Data data)
{
    return DTPop(sha256(sha256(DTPush(data))));
}

Data hmacSha256(Data key, Data data)
{
    uint8_t result[CC_SHA256_DIGEST_LENGTH];

    CCHmac(kCCHmacAlgSHA256, key.bytes, key.length, data.bytes, data.length, result);

    return DataCopy((void*)result, sizeof(result));
}

Data hmacSha512(Data key, Data data)
{
    uint8_t result[CC_SHA512_DIGEST_LENGTH];

    CCHmac(kCCHmacAlgSHA512, key.bytes, key.length, data.bytes, data.length, result);

    return DataCopy((void*)result, sizeof(result));
}

uint32_t murmur3(Data key, uint32_t seed)
{
    uint32_t result;

    MurmurHash3_x86_32(key.bytes, (int)key.length, seed, &result);

    return result;
}

Data bloomFilterArray(Datas searchElements, float falsePositiveRate, int flag)
{
    Data bloom = bloomFilter((int)searchElements.count, falsePositiveRate, flag);

   for(int i = 0; i < searchElements.count; i++)
       if(!bloomFilterAddElement(bloom, searchElements.ptr[i]))
           return DataNull();

    return bloom;
}

static uint32_t calculateBloomSize(int elements, float rate)
{
    return (uint32_t)((-1 / pow(log(2), 2) * elements * log(rate)) / 8);
}

static uint32_t bloomHashCount(int elements, int bloomSize)
{
    return (uint32_t)(bloomSize * 8 / elements * log(2));
}

#define BLOOM_SEED 0xFBA4C795

Data bloomFilter(int estimatedElements, float rate, int flag)
{
    if(!(flag & BLOOM_ALLOW_LOW_ESTIMATE))
        if(estimatedElements < 3)
            estimatedElements = 3;

    flag &= ~BLOOM_ALLOW_LOW_ESTIMATE;

    uint32_t bloomSize = calculateBloomSize(estimatedElements, rate);

    bloomSize = MIN(bloomSize, 36000);

    uint32_t hashCount = bloomHashCount(estimatedElements, bloomSize);

    hashCount = MIN(hashCount, 50);

    uint32_t tweak = 0;

    Data data = varIntD(bloomSize);

    data = DataAdd(data, DataZero(bloomSize));

    data = DataAdd(data, uint32D(hashCount));
    data = DataAdd(data, uint32D(tweak));
    data = DataAdd(data, uint8D(BLOOM_UPDATE_NONE));

    return data;
}

int bloomFilterAddElement(Data bloomFilter, Data element)
{
    BTCUTILAssert(bloomFilter.length >= 9);

    if(bloomFilter.length < 9)
        return 0;

    uint8_t *ptr = (uint8_t*)bloomFilter.bytes;
    uint8_t *end = ptr + bloomFilter.length;

    uint64_t filterSize = readVarInt((const uint8_t**)&ptr, end);

    BTCUTILAssert(filterSize + 8 < end - ptr);

    if(filterSize >= end - ptr)
        return 0;

    uint8_t *filter = ptr;

    ptr += filterSize;

    uint32_t hashCount = *(uint32_t*)ptr;

    ptr += 4;

    uint32_t tweak = *(uint32_t*)ptr;

    ptr += 4;

    BTCUTILAssert(hashCount <= 50);

    if(hashCount > 50)
        return 0;

    for(uint32_t i = 0; i < hashCount; i++) {

        uint32_t number = murmur3(element, i * BLOOM_SEED + tweak);

        number %= filterSize * 8;

        filter[number / 8] |= 1 << number % 8;
    }

    return 1;
}

int bloomFilterCheckElement(Data bloomFilter, Data element)
{
    BTCUTILAssert(bloomFilter.length >= 9);

    if(bloomFilter.length < 9)
        return 0;

    const uint8_t *ptr = (uint8_t*)bloomFilter.bytes;
    const uint8_t *end = ptr + bloomFilter.length;

    uint64_t filterSize = readVarInt(&ptr, end);

    BTCUTILAssert(filterSize + 8 < end - ptr);

    if(filterSize >= end - ptr)
        return 0;

    const uint8_t *filter = ptr;

    ptr += filterSize;

    const uint32_t hashCount = *(uint32_t*)ptr;

    ptr += 4;

    const uint32_t tweak = *(uint32_t*)ptr;

    ptr += 4;

    BTCUTILAssert(hashCount <= 50);

    if(hashCount > 50)
        return 0;

    for(uint32_t i = 0; i < hashCount; i++) {

        uint32_t number = murmur3(element, i * BLOOM_SEED + tweak);

        number %= filterSize * 8;

        if (!(filter[number / 8] & 1 << number % 8))
            return 0;
    }

    return 1;
}

Data PBKDF2(const char *sentence, const char *passphrase)
{
    uint8_t result[64];

    DataTrackPush();

    String str = StringNew(passphrase ?: "");

    str = StringPrefix("mnemonic", str);

    if(0 != CCKeyDerivationPBKDF(kCCPBKDF2, sentence, strlen(sentence), (void*)str.bytes, str.length - 1, kCCPRFHmacAlgSHA512, 2048, result, sizeof(result)))
        return DTPop(DataNull());

    return DTPop(DataCopy((void*)result, sizeof(result)));
}

int hdWalletVerify(Data hdWallet)
{
    if(hdWallet.length != 82)
        return 0;

    DataTrackPush();

    Data checksum = hash256(DataCopyDataPart(hdWallet, 0, 78));

    const uint8_t *ptr = (uint8_t*)hdWallet.bytes;

    int result = (0 == memcmp((const uint8_t*)ptr + 4 + 1 + 4 + 4 + 32 + 33, checksum.bytes, 4));

    DataTrackPop();

    return result;
}

#define HD_WALLET_PRIV 0x0488ADE4

Data hdWalletPriv(Data masterKey, Data chainCode)
{
    DataTrackPush();

    BTCUTILAssert(masterKey.length == 32);
    BTCUTILAssert(chainCode.length == 32);

    if(masterKey.length != 32 && chainCode.length != 32)
        return DTPopNull();

    Data data = DataFlipEndianCopy(uint32D(HD_WALLET_PRIV));

    data = DataAdd(data, DataAdd(DataAdd(uint8D(0), uint32D(0)), uint32D(0)));

    data = DataAddCopy(data, chainCode);
    data = DataAdd(data, uint8D(0));
    data = DataAddCopy(data, masterKey);

    data = DataAdd(data, DataCopyDataPart(hash256(data), 0, 4));

    return DTPop(data);
}

#define HD_WALLET_PUB 0x0488B21E

Data hdWalletPub(Data masterKey, Data chainCode)
{
    DataTrackPush();

    masterKey = publicKeyCompress(publicKeyParse(masterKey));

    BTCUTILAssert(chainCode.length == 32);

    if(!masterKey.bytes && chainCode.length != 32)
        return DTPopNull();

    Data data = DataFlipEndianCopy(uint32D(HD_WALLET_PUB));

    data = DataAdd(data, DataAdd(DataAdd(uint8D(0), uint32D(0)), uint32D(0)));

    data = DataAddCopy(data, chainCode);
    data = DataAddCopy(data, masterKey);

    data = DataAdd(data, DataCopyDataPart(hash256(data), 0, 4));

    return DTPop(data);
}

#define HD_WALLET_PRIV_TESTNET 0x04358394

Data hdWalletPrivTestNet(Data masterKey, Data chainCode)
{
    DataTrackPush();

    BTCUTILAssert(masterKey.length == 32);
    BTCUTILAssert(chainCode.length == 32);

    if(masterKey.length != 32 && chainCode.length != 32)
        return DTPopNull();

    Data data = DataFlipEndianCopy(uint32D(HD_WALLET_PRIV_TESTNET));

    data = DataAdd(data, DataAdd(DataAdd(uint8D(0), uint32D(0)), uint32D(0)));

    data = DataAddCopy(data, chainCode);
    data = DataAddCopy(data, uint8D(0));
    data = DataAddCopy(data, masterKey);

    data = DataAdd(data, DataCopyDataPart(hash256(data), 0, 4));

    return DTPop(data);
}

#define HD_WALLET_PUB_TESTNET 0x043587CF

Data hdWalletPubTestNet(Data masterKey, Data chainCode)
{
    DataTrackPush();

    masterKey = publicKeyCompress(publicKeyParse(masterKey));

    BTCUTILAssert(chainCode.length == 32);

    if(!masterKey.bytes && chainCode.length != 32)
        return DTPopNull();

    Data data = DataFlipEndianCopy(uint32D(HD_WALLET_PUB_TESTNET));

    data = DataAdd(data, DataAdd(DataAdd(uint8D(0), uint32D(0)), uint32D(0)));

    data = DataAddCopy(data, chainCode);
    data = DataAddCopy(data, masterKey);

    data = DataAdd(data, DataCopyDataPart(hash256(data), 0, 4));

    return DTPop(data);
}

Data hdWalletSetDepth(Data hdWallet, uint8_t depth)
{
    BTCUTILAssert(hdWallet.length == 82);

    DataTrackPush();

    if(hdWallet.length != 82)
        return DTPopNull();

    Data data = DataCopyData(hdWallet);

    uint8_t *ptr = (uint8_t*)data.bytes;

    (*(uint8_t*)(ptr + 4)) = depth;

    Data checksum = hash256(DataCopyDataPart(data, 0, 78));

    memcpy(ptr + 4 + 1 + 4 + 4 + 32 + 33, checksum.bytes, 4);

    return DTPop(data);
}

Data publicHdWallet(Data privHdWallet)
{
    BTCUTILAssert(privHdWallet.length == 82);

    DataTrackPush();

    if(privHdWallet.length != 82)
        return DTPopNull();

    Data result = DataCopyData(privHdWallet);

    uint32_t version = uint32read(DataFlipEndianCopy(DataCopyDataPart(result, 0, 4)));

    if(version == HD_WALLET_PRIV)
        version = HD_WALLET_PUB;
    else if(version == HD_WALLET_PRIV_TESTNET)
        version = HD_WALLET_PUB_TESTNET;
    else if(version == HD_WALLET_PUB)
        return privHdWallet;
    else if(version == HD_WALLET_PUB_TESTNET)
        return privHdWallet;
    else {

        BTCUTILAssert(NO, "Invalid hdWallet version in publicHdWallet conversion, version: %u", version);
        return DTPopNull();
    }

    *(uint32_t*)result.bytes = *(uint32_t*)DataFlipEndianCopy(DataCopy((void*)&version, sizeof(version))).bytes;

    Data privKey = keyFromHdWallet(privHdWallet);

    BTCUTILAssert(privKey.length == 33);

    if(privKey.length != 33)
        return DTPopNull();

    BTCUTILAssert(*(uint8_t*)privKey.bytes == 0);

    if(*(uint8_t*)privKey.bytes != 0)
        return DTPopNull();

    Data pubKeyResult = pubKey(DataCopyDataPart(privKey, 1, 32));

    BTCUTILAssert(pubKeyResult.length == 33);

    memcpy(result.bytes + 4 + 1 + 4 + 4 + 32, pubKeyResult.bytes, 33);

    Data checksum = hash256(DataCopyDataPart(result, 0, 78));

    memcpy(result.bytes + 4 + 1 + 4 + 4 + 32 + 33, checksum.bytes, 4);

    return DTPop(result);
}

Datas ckdIndicesFromPath(const char *cStr)
{
    DataTrackPush();

    String path = StringNew(cStr);

    path = StringLowercase(path);

    Datas result = DatasNew();

    if(StringEqual(path, "") || StringEqual(path, "m") || StringEqual(path, "/"))
        return DTPopDatas(result);

    if(StringHasPrefix(path, "m"))
        path = DataTrimFront(path, 1);

    if(StringHasPrefix(path, "/"))
        path = DataTrimFront(path, 1);

    Datas components = StringComponents(path, '/');

    for(int i = 0; i < components.count; i++) {

        String component = StringCopy(components.ptr[i]);

        long long index = strtoll(component.bytes, NULL, 10);

        if(strstr(component.bytes, "'"))
            index |= 0x80000000;

        result = DatasAddRef(result, DataCopy((void*)&index, sizeof(index)));
    }

    return DTPopDatas(result);
}

Data hdWallet(Data hdWallet, const char *path)
{
    DataTrackPush();

    hdWallet = DataCopyData(hdWallet);

    Datas indicies = ckdIndicesFromPath(path);

    for(int i = 0; i < indicies.count; i++)
        hdWallet = childKeyDerivation(hdWallet, *(uint32_t*)indicies.ptr[i].bytes);

    return DTPop(hdWallet);
}

Data privKeyToHdWallet(Data privKey, const char *passphrase) {

    if (passphrase == "")
        passphrase = NULL;

    Data seedPhrase = DataCopy("Bitcoin seed", strlen("Bitcoin seed"));

    Data seed = PBKDF2(toMnemonic(privKey).bytes, passphrase);

    Data hash = hmacSha512(seedPhrase, seed);

    return hdWalletPriv(DataCopyDataPart(hash, 0, 32), DataCopyDataPart(hash, 32, 32));
}

Data chainCodeFromHdWallet(Data hdWallet)
{
    BTCUTILAssert(hdWallet.length == 82);

    if(hdWallet.length != 82)
        return DataNull();

    return DataCopyDataPart(hdWallet, 4 + 1 + 4 + 4, 32);
}

Datas pubKeysFromHdWallets(Datas hdWallets)
{
    Datas result = DatasNew();

    for(int i = 0; i < hdWallets.count; i++)
        result = DatasAddRef(result, pubKeyFromHdWallet(hdWallets.ptr[i]));

    return result;
}

static Data keyFromHdWallet(Data hdWallet)
{
    BTCUTILAssert(hdWallet.length == 82);

    if(hdWallet.length != 82)
        return DataNull();

    return DataCopyDataPart(hdWallet, 4 + 1 + 4 + 4 + 32, 33);
}

Data anyKeyFromHdWallet(Data hdWallet)
{
    DataTrackPush();

    Data result = keyFromHdWallet(hdWallet);

    if(uint8read(result))
        return DTPop(result);

    BTCUTILAssert(result.length == 33);

    if(result.length != 33)
        return DTPopNull();

    return DTPop(DataCopyDataPart(result, 1, 32));
}

Datas anyKeysFromHdWallets(Datas hdWallets)
{
    Datas result = DatasNew();

    for(int i = 0; i < hdWallets.count; i++)
        result = DatasAddRef(result, anyKeyFromHdWallet(hdWallets.ptr[i]));

    return result;
}

Data pubKeyFromHdWallet(Data hdWallet)
{
    DataTrackPush();

    Data key = keyFromHdWallet(hdWallet);

    if(!key.bytes)
        return DTPopNull();

    const uint8_t *ptr = (uint8_t*)key.bytes;

    if(ptr[0])
        return DTPop(key);

    return DTPop(pubKey(DataCopyDataPart(key, 1, 32)));
}

int isZero(Data data)
{
    BTCUTILAssert(data);

    for(int i = 0; i < data.length; i++)
        if(((uint8_t*)data.bytes)[i])
            return 0;

    return 1;
}

Data childKeyDerivation(Data hdWallet, uint32_t index)
{
    BTCUTILAssert(hdWallet.length == 82);

    DataTrackPush();

    if(hdWallet.length != 82)
        return DTPopNull();

    Data result = DataCopyData(hdWallet);

    uint8_t *ptr = (uint8_t*)result.bytes;

    // Increment depth
    (*(uint8_t*)(ptr + 4))++;

    // Set (4 bytes of) the hash160
    memcpy(ptr + 4 + 1, hash160(pubKeyFromHdWallet(hdWallet)).bytes, 4);

    uint32_t *childIndex = (uint32_t*)(ptr + 4 + 1 + 4);

    *childIndex = *(uint32_t*)DataFlipEndianCopy(DataCopy((void*)&index, sizeof(index))).bytes;

    Data oldKey = keyFromHdWallet(hdWallet);
    Data oldChainCode = chainCodeFromHdWallet(hdWallet);

    // Private key derivation
    if(uint8read(oldKey) == 0) {

        Data oldKeyTrim = DataCopyDataPart(oldKey, 1, 32);

        Data hash;

        // If hardened key
        if(index >= 0x80000000)
            hash = hmacSha512(oldChainCode, DataAddCopy(oldKey, uint32D(*childIndex)));
        else
            hash = hmacSha512(oldChainCode, DataAddCopy(pubKey(oldKeyTrim), uint32D(*childIndex)));

        Data I_L = DataCopyDataPart(hash, 0, 32);

        Data newKey = addToPrivKey(I_L, oldKeyTrim);

        if(!newKey.bytes)
            return DTPop(childKeyDerivation(hdWallet, index + 1));

        BTCUTILAssert(newKey.length == 32);

        memcpy(ptr + 4 + 1 + 4 + 4, ((uint8_t*)hash.bytes) + 32, 32);
        memset(ptr + 4 + 1 + 4 + 4 + 32, 0, 1);
        memcpy(ptr + 4 + 1 + 4 + 4 + 32 + 1, newKey.bytes, 32);
    }
    else {

        // If hardened key
        if(index >= 0x80000000) {

            BTCUTILAssert(NO, "Can't derive public keys for a hardened index");
            return DTPopNull();
        }

        Data hash = hmacSha512(oldChainCode, DataAddCopy(oldKey, uint32D(*childIndex)));

        Data I_L = DataCopyDataPart(hash, 0, 32);

        Data newKey = publicKeyCompress(addPubKeys(oldKey, pubKey(I_L)));

        if(!newKey.bytes)
            return DTPop(childKeyDerivation(hdWallet, index + 1));

        BTCUTILAssert(newKey.length == 33);

        memcpy(ptr + 4 + 1 + 4 + 4, ((uint8_t*)hash.bytes) + 32, 32);
        memcpy(ptr + 4 + 1 + 4 + 4 + 32, newKey.bytes, 33);
    }

    Data checksum = hash256(DataCopyDataPart(result, 0, 78));

    memcpy(ptr + 4 + 1 + 4 + 4 + 32 + 33, checksum.bytes, 4);

    return DTPop(result);
}

Data ckd(Data hdWallet, uint32_t index)
{
    return childKeyDerivation(hdWallet, index);
}

Data implode(Datas items)
{
    DataTrackPush();

    Data data = DataNew(0);

    for(int i = 0; i < items.count; i++)
        DataAddCopy(data, items.ptr[i]);

    return DTPop(data);
}

String base58Encode(Data data)
{
    return Base58Encode(data);
}

Data base58Dencode(const char *string)
{
    return Base58Decode(string);
}

static Data publicKeyFromPrivate(Data privateKey)
{
    BTCUTILAssert(privateKey.length == 32);
    
    if(privateKey.length != 32)
        return DataNull();
    
    secp256k1_pubkey pubkey;
    
    if(!secp256k1_ec_pubkey_create(secpCtx, &pubkey, (void*)privateKey.bytes))
        return  DataNull();
    
    BTCUTILAssert(sizeof(pubkey.data) == 64);

    return DataCopy((void*)pubkey.data, sizeof(pubkey.data));
}

Data publicKeyParse(Data keyData)
{
    if(!keyData.length)
        return DataNull();
    
    secp256k1_pubkey pubkey;
    
    if(!secp256k1_ec_pubkey_parse(secpCtx, &pubkey, (void*)keyData.bytes, keyData.length))
        return DataNull();
    
    BTCUTILAssert(sizeof(pubkey.data) == 64);

    return DataCopy((void*)pubkey.data, sizeof(pubkey.data));
}

Data pubKeyExpand(Data publicKey)
{
    return publicKeySerializeDefault(publicKeyParse(publicKey));
}

Data publicKeyCompress(Data publicKey)
{
    return publicKeySerialize(publicKey, 1);
}

static Data publicKeySerializeDefault(Data publicKey)
{
    return publicKeySerialize(publicKey, 0);
}

static Data publicKeySerialize(Data publicKey, int compressed)
{
    if(!publicKey.bytes)
        return DataNull();
    
    BTCUTILAssert(publicKey.length == sizeof(secp256k1_pubkey));

    if(publicKey.length != sizeof(secp256k1_pubkey))
        return DataNull();
    
    unsigned char output[65];
    size_t length = sizeof(output);
    unsigned int flags = compressed ? SECP256K1_EC_COMPRESSED : SECP256K1_EC_UNCOMPRESSED;
    
    if(!secp256k1_ec_pubkey_serialize(secpCtx, output, &length, (void*)publicKey.bytes, flags))
        return DataNull();

    return DataCopy((void*)output, (uint32_t)length);
}

static Data signatureCreate(Data message32, Data privateKey)
{
    BTCUTILAssert(message32.length == 32);
    BTCUTILAssert(privateKey.length == 32);
    
    if(message32.length != 32)
        return DataNull();
    
    if(privateKey.length != 32)
        return DataNull();
    
    secp256k1_ecdsa_signature sig;
    
    if(!secp256k1_ecdsa_sign(secpCtx, &sig, (void*)message32.bytes, (void*)privateKey.bytes, NULL, NULL))
        return DataNull();

    return DataCopy((void*)sig.data, sizeof(sig.data));
}

Data signatureParse(Data signatureData)
{
    DataTrackPush();

    BTCUTILAssert(signatureData.length);
    
    if(!signatureData.length)
        return DTPopNull();
    
    secp256k1_ecdsa_signature sig;

    Data trimmed = DataCopy(signatureData.bytes, signatureData.length - 1);
    
    if(!secp256k1_ecdsa_signature_parse_der(secpCtx, &sig, (void*)trimmed.bytes, trimmed.length))
        return DTPopNull();

    return DTPop(DataCopy((void*)sig.data, sizeof(sig.data)));
}

Data signatureDER(Data signature)
{
    BTCUTILAssert(signature.length == sizeof(secp256k1_ecdsa_signature));
    
    if(signature.length != sizeof(secp256k1_ecdsa_signature))
        return DataNull();
    
    uint8_t output[64 * 2];
    size_t length = sizeof(output);
    
    if(!secp256k1_ecdsa_signature_serialize_der(secpCtx, output, &length, (void*)signature.bytes))
        return DataNull();

    return DataCopy((void*)output, (uint32_t)length);
}

Data signatureFromSig64(Data sig64)
{
    DataTrackPush();

    if(sig64.length != 64)
        return DTPopNull();

    secp256k1_ecdsa_signature sig;
    secp256k1_ecdsa_signature sigout;

    if(!secp256k1_ecdsa_signature_parse_compact(secpCtx, &sig, (void*)sig64.bytes))
        return DTPopNull();

    secp256k1_ecdsa_signature_normalize(secpCtx, &sigout, &sig);

    Data data = DataAdd(signatureDER(DataCopy((void*)&sigout.data, sizeof(sigout.data))), uint8D(0x01));

    return DTPop(data);
}

Data compressSignature(Data derSignature)
{
    Data signature = signatureParse(derSignature);

    BTCUTILAssert(signature.length == sizeof(secp256k1_ecdsa_signature));

    if(signature.length != sizeof(secp256k1_ecdsa_signature))
        return DataNull();

    uint8_t output[64];

    if(!secp256k1_ecdsa_signature_serialize_compact(secpCtx, output, (void*)signature.bytes))
        return DataNull();

    return DataCopy((void*)output, sizeof(output));
}

int signatureVerify(Data signature, Data message32, Data publicKey)
{
    if(!publicKey.bytes)
        return 0;
    
    BTCUTILAssert(signature.length == sizeof(secp256k1_ecdsa_signature));
    BTCUTILAssert(message32.length == 32);
    BTCUTILAssert(publicKey.length == sizeof(secp256k1_pubkey));
    
    if(signature.length != sizeof(secp256k1_ecdsa_signature))
        return 0;
    
    if(message32.length != 32)
        return 0;
    
    if(publicKey.length != sizeof(secp256k1_pubkey))
        return 0;
    
    return secp256k1_ecdsa_verify(secpCtx, (void*)signature.bytes, (void*)message32.bytes, (void*)publicKey.bytes);
}

Data pubKey(Data privateKey)
{
    return publicKeyCompress(publicKeyFromPrivate(privateKey));
}

Data pubKeyHash(Data privateKey)
{
    return hash256(pubKey(privateKey));
}

Data pubKeyFull(Data privateKey)
{
    return publicKeySerializeDefault(publicKeyFromPrivate(privateKey));
}

Data pubKeyFromPubKey64(Data pubKey64)
{
    BTCUTILAssert(pubKey64.length == 64);

    if(pubKey64.length != 64)
        return DataNull();

    DataTrackPush();

    return DTPop(publicKeySerialize(publicKeyParse(DataAdd(uint8D(0x04), pubKey64)), 1));
}

Data signAll(Data unhashedMsg, Data privateKey)
{
    return sign(unhashedMsg, privateKey, 0x01);
}

Data sign(Data unhashedMsg, Data privateKey, uint8_t typeFlag)
{
    DataTrackPush();

    Data sig = signatureCreate(hash256(unhashedMsg), privateKey);
    
    return DTPop(DataAdd(signatureDER(sig), uint8D(typeFlag)));
}

int verify(Data derSignature, Data unhashedMsg, Data compressedPublicKey)
{
    // Special case 0 length signature for MULTISIG extra element
    if(derSignature.length == 0)
        return 0;
    
    Data sigParam = derSignature;

    if(sigParam.length != 64)
        sigParam = signatureParse(sigParam);

    DataTrackPush();

    return DTPopi(signatureVerify(sigParam, hash256(unhashedMsg), publicKeyParse(compressedPublicKey)));
}

Data ecdhKey(Data privateKey, Data publicKey)
{
    DataTrackPush();

    Data parsedPubKey = publicKeyParse(publicKey);
    
    BTCUTILAssert(privateKey.length == 32);
    BTCUTILAssert(parsedPubKey.length == sizeof(secp256k1_pubkey));
    
    if(privateKey.length != 32)
        return DTPopNull();
    
    if(parsedPubKey.length != sizeof(secp256k1_pubkey))
        return DTPopNull();
    
    uint8_t result[32];
    
    if(!secp256k1_ecdh(secpCtx, result, (void*)parsedPubKey.bytes, (void*)privateKey.bytes, NULL, NULL))
        return DTPopNull();
    
    return DTPop(DataCopy((void*)result, sizeof(result)));
}

Data ecdhKeyRotate(Data privateKey, Data publicKey, uint32_t amount)
{
    DataTrackPush();

    Data secretZero = ecdhKey(privateKey, publicKey);
    
    if(!amount)
        return DTPop(secretZero);

    Data tweak = sha256(DataAdd(secretZero, uint32D(amount)));
    
    privateKey = addToPrivKey(tweak, privateKey);
    publicKey = addToPubKey(tweak, publicKey);
    
    return DTPop(ecdhKey(privateKey, publicKey));
}

Data multiplyWithPubKey(Data tweak, Data pubKey)
{
    DataTrackPush();

    Data parsedPubKey = publicKeyParse(pubKey);
    
    BTCUTILAssert(tweak.length == 32);
    BTCUTILAssert(parsedPubKey.length == sizeof(secp256k1_pubkey));
    
    if(parsedPubKey.length != sizeof(secp256k1_pubkey))
        return DTPopNull();
    
    if(tweak.length != 32)
        return DTPopNull();
    
    secp256k1_pubkey pubKeyObj;
    
    memcpy(&pubKeyObj, parsedPubKey.bytes, sizeof(pubKeyObj));
    
    if(!secp256k1_ec_pubkey_tweak_mul(secpCtx, &pubKeyObj, (void*)tweak.bytes))
        return DTPopNull();

    return DTPop(publicKeySerializeDefault(DataCopy((void*)&pubKeyObj, sizeof(pubKeyObj))));
}

Data multiplyWithPrivKey(Data tweak, Data privKey)
{
    BTCUTILAssert(tweak.length == 32);
    BTCUTILAssert(privKey.length == 32);
    
    if(tweak.length != 32)
        return DataNull();
    
    if(privKey.length != 32)
        return DataNull();
    
    uint8_t privKeyObj[32];
    
    memcpy(&privKeyObj, privKey.bytes, sizeof(privKeyObj));
    
    if(!secp256k1_ec_privkey_tweak_mul(secpCtx, privKeyObj, (void*)tweak.bytes))
        return DataNull();
    
    return DataCopy((void*)privKeyObj, sizeof(privKeyObj));
}

Data addToPubKey(Data tweak, Data pubKey)
{
    DataTrackPush();

    Data parsedPubKey = publicKeyParse(pubKey);

    BTCUTILAssert(tweak.length == 32);
    BTCUTILAssert(parsedPubKey.length == sizeof(secp256k1_pubkey));

    if(parsedPubKey.length != sizeof(secp256k1_pubkey))
        return DTPopNull();

    if(tweak.length != 32)
        return DTPopNull();

    secp256k1_pubkey pubKeyObj;

    memcpy(&pubKeyObj, parsedPubKey.bytes, sizeof(pubKeyObj));

    if(!secp256k1_ec_pubkey_tweak_add(secpCtx, &pubKeyObj, (void*)tweak.bytes))
        return DTPopNull();

    return DTPop(publicKeySerializeDefault(DataCopy((void*)&pubKeyObj, sizeof(pubKeyObj))));
}

Data addToPrivKey(Data tweak, Data privKey)
{
    BTCUTILAssert(tweak.length == 32);
    BTCUTILAssert(privKey.length == 32);

    if(tweak.length != 32)
        return DataNull();

    if(privKey.length != 32)
        return DataNull();

    uint8_t privKeyObj[32];

    memcpy(&privKeyObj, privKey.bytes, sizeof(privKeyObj));

    if(!secp256k1_ec_privkey_tweak_add(secpCtx, privKeyObj, (void*)tweak.bytes))
        return DataNull();

    return DataCopy((void*)privKeyObj, sizeof(privKeyObj));
}

Data addPubKeys(Data keyA, Data keyB)
{
    DataTrackPush();

    keyA = publicKeyParse(keyA);
    keyB = publicKeyParse(keyB);

    if(keyA.length != sizeof(secp256k1_pubkey))
        return DTPopNull();

    if(keyB.length != sizeof(secp256k1_pubkey))
        return DTPopNull();

    const secp256k1_pubkey * const ins[2] = { (void*)keyA.bytes, (void*)keyB.bytes };

    secp256k1_pubkey result;

    if(!secp256k1_ec_pubkey_combine(secpCtx, &result, ins, 2))
        return DTPopNull();

    return DTPop(DataCopy((void*)result.data, sizeof(result.data)));
}

Data padData16(Data dataIn)
{
    Data data = DataCopyData(dataIn);
    
    uint8_t padNum = 16 - (data.length % 16);
    
    for(int i = 0; i < padNum; i++)
        data = DataAdd(data, uint8D(padNum));
    
    return data;
}

Data unpadData16(Data data)
{
    if(!data.length)
        return DataCopyData(data);
    
    uint8_t padNum = ((uint8_t*)data.bytes)[data.length - 1];
    
    if(padNum > data.length || padNum > 16)
        return DataNull();

    return DataCopyDataPart(data, 0, data.length - padNum);
}

Data encryptAES(Data data, Data privKey, uint64_t nonce)
{
    DataTrackPush();

    Data paddedData = padData16(data);
    
    BTCUTILAssert(!(paddedData.length % 16));
    BTCUTILAssert(privKey.length == 32);
    
    if(paddedData.length % 16)
        return DTPopNull();
    
    if(privKey.length != 32)
        return DTPopNull();

    Data nonceData = uint64D(nonce);
    
    Data iv = sha256(nonceData);
    
    struct AES_ctx ctx;
    
    AES_init_ctx_iv(&ctx, (void*)privKey.bytes, (void*)iv.bytes);

    Data result = DataNew(paddedData.length);

    uint8_t *buffer = (uint8_t*)result.bytes;
    
    memcpy(buffer, paddedData.bytes, paddedData.length);
    
    AES_CBC_encrypt_buffer(&ctx, buffer, (uint32_t)paddedData.length);
    
    return DTPop(result);
}

Data decryptAES(Data data, Data privKey, uint64_t nonce)
{
    DataTrackPush();

    BTCUTILAssert(!(data.length % 16));
    BTCUTILAssert(privKey.length == 32);
    
    if(data.length % 16)
        return DTPopNull();
    
    if(privKey.length != 32)
        return DTPopNull();

    Data iv = sha256(uint64D(nonce));
    
    struct AES_ctx ctx;
    
    AES_init_ctx_iv(&ctx, (void*)privKey.bytes, (void*)iv.bytes);

    Data result = DataNew(data.length);
    
    uint8_t *buffer = (void*)result.bytes;
    
    memcpy(buffer, data.bytes, data.length);
    
    AES_CBC_decrypt_buffer(&ctx, buffer, (uint32_t)data.length);

    return DTPop(unpadData16(result));
}

int validPublicKey(Data keyData)
{
    DataTrackPush();

    return DTPopi(publicKeyParse(keyData).bytes != NULL);
}

int validSignature(Data signature)
{
    DataTrackPush();

    return DTPopi(signatureParse(signature).bytes != NULL);
}

Datas allPubKeys(Data script)
{
    DataTrackPush();

    Datas array = DatasNew();
    
    ScriptTokens tokens = scriptToTokens(script);
    
    for(int i = 0; i < tokens.count; i++) {
        
        if(ScriptTokenI(tokens, i).op == OP_CHECKSIG || ScriptTokenI(tokens, i).op == OP_CHECKSIGVERIFY)
            if(i > 0 && ScriptTokenI(tokens, i - 1).data.bytes)
                array = DatasAddCopyFront(array, ScriptTokenI(tokens, i - 1).data);
        
        if(ScriptTokenI(tokens, i).op == OP_CHECKMULTISIG || ScriptTokenI(tokens, i).op == OP_CHECKMULTISIGVERIFY) {
            
            if(!i || ScriptTokenI(tokens, i - 1).op < OP_1 || ScriptTokenI(tokens, i - 1).op > OP_16)
                continue;
            
            int numPubKeys = 1 + ScriptTokenI(tokens, i - 1).op - OP_1;
            
            for(int j = 0; j < numPubKeys; j++)
                if(i - j - 2 >= 0)
                array = DatasAddCopyFront(array, ScriptTokenI(tokens, i - j - 2).data);
        }
    }
    
    return DTPopDatas(array);
}

ScriptTokens allCheckSigs(Data script)
{
    DataTrackPush();

    ScriptTokens array = DatasNew();

    ScriptTokens tokens = scriptToTokens(script);
    
    for(int i = 0; i < tokens.count; i++) {
        
        if(ScriptTokenI(tokens, i).op == OP_CHECKSIG || ScriptTokenI(tokens, i).op == OP_CHECKSIGVERIFY) {
            
            if(i > 0 && ScriptTokenI(tokens, i - 1).data.bytes) {
                
                ScriptToken token = { 0 };
                
                token.op = ScriptTokenI(tokens, i).op;
                token.neededSigs = 1;
                token.pubKeys = DatasTranscend(DatasAddCopy(DatasNew(), ScriptTokenI(tokens, i - 1).data));

                array = DatasAddCopyFront(array, DataCopy((void*)&token, sizeof(token)));
            }
        }
        
        if(ScriptTokenI(tokens, i).op == OP_CHECKMULTISIG || ScriptTokenI(tokens, i).op == OP_CHECKMULTISIGVERIFY) {
            
            if(!i || ScriptTokenI(tokens, i - 1).op < OP_1 || ScriptTokenI(tokens, i - 1).op > OP_16)
                continue;
            
            int numPubKeys = 1 + ScriptTokenI(tokens, i - 1).op - OP_1;

            Datas pubs = DatasNew();
            
            int j;
            
            for(j = 0; j < numPubKeys; j++)
                if(i - j - 2 >= 0 && ScriptTokenI(tokens, i - j - 2).data.bytes)
                    pubs = DatasAddCopyFront(pubs, ScriptTokenI(tokens, i - j - 2).data);
            
            ScriptToken token = { 0 };
            
            token.op = ScriptTokenI(tokens, i).op;
            token.pubKeys = DatasTranscend(pubs);
            
            if(i - numPubKeys - 2 < 0)
                continue;
            
            token.neededSigs = 1 + ScriptTokenI(tokens, i - numPubKeys - 2).op - OP_1;

            array = DatasAddCopyFront(array, DataCopy((void*)&token, sizeof(token)));
        }
    }
    
    return DTPopDatas(array);
}

Data multisigScript(Datas pubKeys)
{
    Data data = DataNew(0);

    pubKeys = DatasSort(pubKeys, DataCompare);

    long m = pubKeys.count * 2 / 3;
    long n = pubKeys.count;

    if(m < 1)
        m = 1;

    data = DataAppend(data, uint8D((OP_1 - 1) + m));

    FORDATAIN(pubKey, pubKeys)
        data = DataAppend(data, scriptPush(*pubKey));

    data = DataAppend(data, uint8D((OP_1 - 1) + n));
    data = DataAppend(data, uint8D(OP_CHECKMULTISIG));

    return data;
}

Data vaultScript(Data masterPubKey, Datas pubKeys)
{
    DataTrackPush();

    Data data = DataNew(0);

    pubKeys = DatasSort(pubKeys, DataCompare);

    long m = pubKeys.count * 2 / 3;
    long n = pubKeys.count;

    if(m < 1)
        m = 1;

    data = DataAdd(data, scriptPush(masterPubKey));
    data = DataAdd(data, uint8D(OP_CHECKSIGVERIFY));

    data = DataAdd(data, uint8D((OP_1 - 1) + m));

    for(int i = 0; i < pubKeys.count; i++)
        data = DataAdd(data, scriptPush(pubKeys.ptr[i]));

    data = DataAdd(data, uint8D((OP_1 - 1) + n));
    data = DataAdd(data, uint8D(OP_CHECKMULTISIG));

    return DTPop(data);
}

Data encodeScriptNum(int64_t value)
{
    Data result = DataNew(0);

    if(value == 0)
        return result;

    int neg = value < 0;
    uint64_t absvalue = value;

    while(absvalue) {

        result = DataAppend(result, uint8D(absvalue & 0xff));
        absvalue >>= 8;
    }

    //    - If the most significant byte is >= 0x80 and the value is positive, push a
    //    new zero-byte to make the significant byte < 0x80 again.

    //    - If the most significant byte is >= 0x80 and the value is negative, push a
    //    new 0x80 byte that will be popped off when converting to an integral.

    //    - If the most significant byte is < 0x80 and the value is negative, add
    //    0x80 to it, since it will be subtracted and interpreted as a negative when
    //    converting to an integral.

    if(result.bytes[result.length - 1] & 0x80)
        result = DataAppend(result, uint8D(neg ? 0x80 : 0));
    else if(neg)
        result.bytes[result.length - 1] |= 0x80;

    return result;
}

Data jimmyScript(Data jimmyPubKey, Data receiverPubKey, uint32_t currentBlockHeight)
{
    DataTrackPush();

    Data data = DataNew(0);

    const int sixMonths = 25920;

    data = DataAdd(data, scriptPush(encodeScriptNum(currentBlockHeight + sixMonths)));
    data = DataAdd(data, uint8D(OP_CHECKLOCKTIMEVERIFY));
    data = DataAdd(data, uint8D(OP_DROP));

    data = DataAdd(data, scriptPush(receiverPubKey));
    data = DataAdd(data, uint8D(OP_CHECKSIG));

    data = DataAdd(data, uint8D(OP_IF));

    data = DataAdd(data, uint8D(OP_1));

    data = DataAdd(data, uint8D(OP_ELSE));

    data = DataAdd(data, scriptPush(encodeScriptNum(currentBlockHeight + sixMonths * 3)));
    data = DataAdd(data, uint8D(OP_CHECKLOCKTIMEVERIFY));
    data = DataAdd(data, uint8D(OP_DROP));

    data = DataAdd(data, scriptPush(jimmyPubKey));
    data = DataAdd(data, uint8D(OP_CHECKSIG));

    data = DataAdd(data, uint8D(OP_ENDIF));

    return DTPop(data);
}

Data nestedP2wpkhScript(Data pubKey)
{
    DataTrackPush();

    Data data = DataNew(0);
    
    Data pubKeyCompressed = publicKeyCompress(publicKeyParse(pubKey));

    data = DataAdd(data, uint8D(0));
    data = DataAdd(data, scriptPush(hash160(pubKeyCompressed)));
    
    return DTPop(data);
}

Data nestedP2wshScript(Data script)
{
    DataTrackPush();

    Data data = DataNew(0);

    data = DataAdd(data, uint8D(0));
    data = DataAdd(data, scriptPush(sha256(script)));

    return DTPop(data);
}

Data p2wshHashFromPubScript(Data pubScript)
{
    if(pubScript.length != 34)
        return DataNull();

    const uint8_t *ptr = (uint8_t*)pubScript.bytes;
    const uint8_t *end = ptr + pubScript.length;

    if(uint8readP(&ptr, end) != 0)
        return DataNull();

    uint64_t length = readPushData(&ptr, end);

    if(length != 32 || ptr + length != end)
        return DataNull();

    return readBytes(length, &ptr, end);
}

String toSegwit(Data program, const char *prefix)
{
    char output[73 + strlen(prefix) + 1];

    *output = 0;
    
    if(segwit_addr_encode(output, prefix, 0, (void*)program.bytes, program.length) != 1)
        return DataNull();

    if(strlen(output) > 73 + strlen(prefix) + 1)
        abort();

    return StringNew(output);
}

Data fromSegwitVersion(String string, char *prefix, int *version)
{
    uint8_t prog[40];
    size_t length = sizeof(prog);
    
    if(segwit_addr_decode(version, prog, &length, NULL, string.bytes) != 1)
        return DataNull();

    return DataCopy((void*)prog, (uint32_t)length);
}

Data fromSegwit(String string)
{
    return fromSegwitVersion(string, NULL, NULL);
}

String segwitPrefix(String string)
{
    char prefix[84];
    
    if(!fromSegwitVersion(string, prefix, 0).bytes)
        return DataNull();
    
    return StringNew(prefix);
}

int segwitVersion(String string)
{
    int version;
    
    if(!fromSegwitVersion(string, NULL, &version).bytes)
        return 0;
    
    return version;
}

Data uint8D(uint8_t number)
{
    return DataCopy((void*)&number, sizeof(number));
}

Data uint16D(uint16_t number)
{
    return DataCopy((void*)&number, sizeof(number));
}

Data uint32D(uint32_t number)
{
    return DataCopy((void*)&number, sizeof(number));
}

Data uint64D(uint64_t number)
{
    return DataCopy((void*)&number, sizeof(number));
}

Data varIntD(uint64_t number)
{
    Data data = DataNew(0);
    
    if(number & 0xFFFFFFFF00000000) {

        data = DataAdd(data, uint8D(0xff));
        data = DataAdd(data, uint64D(number));
    }
    else if(number & 0xFFFF0000) {

        data = DataAdd(data, uint8D(0xfe));
        data = DataAdd(data, uint32D((uint32_t)number));
    }
    else if(number & 0xFF00) {

        data = DataAdd(data, uint8D(0xfd));
        data = DataAdd(data, uint16D((uint16_t)number));
    }
    else {

        data = DataAdd(data, uint8D(number));
    }
    
    return data;
}

String makeUuid()
{
#ifdef __ANDROID__
    String result = StringNew("");

    char entropy[16];

    arc4random_buf(entropy, 16);

    result = StringAdd(result, toHex(DataCopy(entropy, 4)).bytes);
    result = StringAdd(result, "-");
    result = StringAdd(result, toHex(DataCopy(entropy, 2)).bytes);
    result = StringAdd(result, "-");
    result = StringAdd(result, toHex(DataCopy(entropy, 2)).bytes);
    result = StringAdd(result, "-");
    result = StringAdd(result, toHex(DataCopy(entropy, 2)).bytes);
    result = StringAdd(result, "-");
    result = StringAdd(result, toHex(DataCopy(entropy, 6)).bytes);

    return result;
#else
    uuid_t uuid;

    uuid_generate(uuid);

    String result = DataNew(37);

    uuid_unparse_upper(uuid, result.bytes);

    return result;
#endif
}

String toHex(Data data)
{
    String result = DataNew(data.length * 2 + 1);

    memset(result.bytes, 0, result.length);

    int numWritten = 0;
    
    const uint8_t *ptr = (uint8_t*)data.bytes;
    const uint8_t *end = ptr + data.length;
    
    while(ptr < end)
        numWritten += snprintf((char*)result.bytes + numWritten, result.length - numWritten, "%02x", (uint32_t)*ptr++);

    return result;
}

static uint8_t parseHexChar(uint8_t hexChar)
{
    if(hexChar >= 'a' && hexChar <= 'f')
        return hexChar - 'a' + 10;
    
    if(hexChar >= 'A' && hexChar <= 'F')
        return hexChar - 'A' + 10;
    
    if(hexChar >= '0' && hexChar <= '9')
        return hexChar - '0';
    
    return 0;
}

Data fromHex(const char *str)
{
    Data data = DataNew(0);
    
    for(const char *end = str + (str ? strlen(str) : 0); str < end; str += 2) {
        
        uint8_t byte = (parseHexChar(str[0]) << 4) | parseHexChar(str[1]);

        data = DataAdd(data, DataCopy((void*)&byte, sizeof(byte)));
    }
    
    return data;
}

String formatBitcoinAmount(int64_t amount)
{
    int64_t value1 = amount / 100000000;
    int64_t value2 = amount % 100000000;

    char *sign = "";

    if(amount < 0) {

        sign = "-";

        value2 = llabs(value2);
        value1 = llabs(value1);
    }

    return StringF("%s%lld.%08lld", sign, value1, value2);
}

static struct words *nmemonicWordlist()
{
    static struct words *words = NULL;

    if(!words) {

        const char *wordString = "abandon ability able about above absent absorb abstract absurd abuse access accident account accuse achieve acid acoustic acquire across act action actor actress actual adapt add addict address adjust admit adult advance advice aerobic affair afford afraid again age agent agree ahead aim air airport aisle alarm album alcohol alert alien all alley allow almost alone alpha already also alter always amateur amazing among amount amused analyst anchor ancient anger angle angry animal ankle announce annual another answer antenna antique anxiety any apart apology appear apple approve april arch arctic area arena argue arm armed armor army around arrange arrest arrive arrow art artefact artist artwork ask aspect assault asset assist assume asthma athlete atom attack attend attitude attract auction audit august aunt author auto autumn average avocado avoid awake aware away awesome awful awkward axis baby bachelor bacon badge bag balance balcony ball bamboo banana banner bar barely bargain barrel base basic basket battle beach bean beauty because become beef before begin behave behind believe below belt bench benefit best betray better between beyond bicycle bid bike bind biology bird birth bitter black blade blame blanket blast bleak bless blind blood blossom blouse blue blur blush board boat body boil bomb bone bonus book boost border boring borrow boss bottom bounce box boy bracket brain brand brass brave bread breeze brick bridge brief bright bring brisk broccoli broken bronze broom brother brown brush bubble buddy budget buffalo build bulb bulk bullet bundle bunker burden burger burst bus business busy butter buyer buzz cabbage cabin cable cactus cage cake call calm camera camp can canal cancel candy cannon canoe canvas canyon capable capital captain car carbon card cargo carpet carry cart case cash casino castle casual cat catalog catch category cattle caught cause caution cave ceiling celery cement census century cereal certain chair chalk champion change chaos chapter charge chase chat cheap check cheese chef cherry chest chicken chief child chimney choice choose chronic chuckle chunk churn cigar cinnamon circle citizen city civil claim clap clarify claw clay clean clerk clever click client cliff climb clinic clip clock clog close cloth cloud clown club clump cluster clutch coach coast coconut code coffee coil coin collect color column combine come comfort comic common company concert conduct confirm congress connect consider control convince cook cool copper copy coral core corn correct cost cotton couch country couple course cousin cover coyote crack cradle craft cram crane crash crater crawl crazy cream credit creek crew cricket crime crisp critic crop cross crouch crowd crucial cruel cruise crumble crunch crush cry crystal cube culture cup cupboard curious current curtain curve cushion custom cute cycle dad damage damp dance danger daring dash daughter dawn day deal debate debris decade december decide decline decorate decrease deer defense define defy degree delay deliver demand demise denial dentist deny depart depend deposit depth deputy derive describe desert design desk despair destroy detail detect develop device devote diagram dial diamond diary dice diesel diet differ digital dignity dilemma dinner dinosaur direct dirt disagree discover disease dish dismiss disorder display distance divert divide divorce dizzy doctor document dog doll dolphin domain donate donkey donor door dose double dove draft dragon drama drastic draw dream dress drift drill drink drip drive drop drum dry duck dumb dune during dust dutch duty dwarf dynamic eager eagle early earn earth easily east easy echo ecology economy edge edit educate effort egg eight either elbow elder electric elegant element elephant elevator elite else embark embody embrace emerge emotion employ empower empty enable enact end endless endorse enemy energy enforce engage engine enhance enjoy enlist enough enrich enroll ensure enter entire entry envelope episode equal equip era erase erode erosion error erupt escape essay essence estate eternal ethics evidence evil evoke evolve exact example excess exchange excite exclude excuse execute exercise exhaust exhibit exile exist exit exotic expand expect expire explain expose express extend extra eye eyebrow fabric face faculty fade faint faith fall false fame family famous fan fancy fantasy farm fashion fat fatal father fatigue fault favorite feature february federal fee feed feel female fence festival fetch fever few fiber fiction field figure file film filter final find fine finger finish fire firm first fiscal fish fit fitness fix flag flame flash flat flavor flee flight flip float flock floor flower fluid flush fly foam focus fog foil fold follow food foot force forest forget fork fortune forum forward fossil foster found fox fragile frame frequent fresh friend fringe frog front frost frown frozen fruit fuel fun funny furnace fury future gadget gain galaxy gallery game gap garage garbage garden garlic garment gas gasp gate gather gauge gaze general genius genre gentle genuine gesture ghost giant gift giggle ginger giraffe girl give glad glance glare glass glide glimpse globe gloom glory glove glow glue goat goddess gold good goose gorilla gospel gossip govern gown grab grace grain grant grape grass gravity great green grid grief grit grocery group grow grunt guard guess guide guilt guitar gun gym habit hair half hammer hamster hand happy harbor hard harsh harvest hat have hawk hazard head health heart heavy hedgehog height hello helmet help hen hero hidden high hill hint hip hire history hobby hockey hold hole holiday hollow home honey hood hope horn horror horse hospital host hotel hour hover hub huge human humble humor hundred hungry hunt hurdle hurry hurt husband hybrid ice icon idea identify idle ignore ill illegal illness image imitate immense immune impact impose improve impulse inch include income increase index indicate indoor industry infant inflict inform inhale inherit initial inject injury inmate inner innocent input inquiry insane insect inside inspire install intact interest into invest invite involve iron island isolate issue item ivory jacket jaguar jar jazz jealous jeans jelly jewel job join joke journey joy judge juice jump jungle junior junk just kangaroo keen keep ketchup key kick kid kidney kind kingdom kiss kit kitchen kite kitten kiwi knee knife knock know lab label labor ladder lady lake lamp language laptop large later latin laugh laundry lava law lawn lawsuit layer lazy leader leaf learn leave lecture left leg legal legend leisure lemon lend length lens leopard lesson letter level liar liberty library license life lift light like limb limit link lion liquid list little live lizard load loan lobster local lock logic lonely long loop lottery loud lounge love loyal lucky luggage lumber lunar lunch luxury lyrics machine mad magic magnet maid mail main major make mammal man manage mandate mango mansion manual maple marble march margin marine market marriage mask mass master match material math matrix matter maximum maze meadow mean measure meat mechanic medal media melody melt member memory mention menu mercy merge merit merry mesh message metal method middle midnight milk million mimic mind minimum minor minute miracle mirror misery miss mistake mix mixed mixture mobile model modify mom moment monitor monkey monster month moon moral more morning mosquito mother motion motor mountain mouse move movie much muffin mule multiply muscle museum mushroom music must mutual myself mystery myth naive name napkin narrow nasty nation nature near neck need negative neglect neither nephew nerve nest net network neutral never news next nice night noble noise nominee noodle normal north nose notable note nothing notice novel now nuclear number nurse nut oak obey object oblige obscure observe obtain obvious occur ocean october odor off offer office often oil okay old olive olympic omit once one onion online only open opera opinion oppose option orange orbit orchard order ordinary organ orient original orphan ostrich other outdoor outer output outside oval oven over own owner oxygen oyster ozone pact paddle page pair palace palm panda panel panic panther paper parade parent park parrot party pass patch path patient patrol pattern pause pave payment peace peanut pear peasant pelican pen penalty pencil people pepper perfect permit person pet phone photo phrase physical piano picnic picture piece pig pigeon pill pilot pink pioneer pipe pistol pitch pizza place planet plastic plate play please pledge pluck plug plunge poem poet point polar pole police pond pony pool popular portion position possible post potato pottery poverty powder power practice praise predict prefer prepare present pretty prevent price pride primary print priority prison private prize problem process produce profit program project promote proof property prosper protect proud provide public pudding pull pulp pulse pumpkin punch pupil puppy purchase purity purpose purse push put puzzle pyramid quality quantum quarter question quick quit quiz quote rabbit raccoon race rack radar radio rail rain raise rally ramp ranch random range rapid rare rate rather raven raw razor ready real reason rebel rebuild recall receive recipe record recycle reduce reflect reform refuse region regret regular reject relax release relief rely remain remember remind remove render renew rent reopen repair repeat replace report require rescue resemble resist resource response result retire retreat return reunion reveal review reward rhythm rib ribbon rice rich ride ridge rifle right rigid ring riot ripple risk ritual rival river road roast robot robust rocket romance roof rookie room rose rotate rough round route royal rubber rude rug rule run runway rural sad saddle sadness safe sail salad salmon salon salt salute same sample sand satisfy satoshi sauce sausage save say scale scan scare scatter scene scheme school science scissors scorpion scout scrap screen script scrub sea search season seat second secret section security seed seek segment select sell seminar senior sense sentence series service session settle setup seven shadow shaft shallow share shed shell sheriff shield shift shine ship shiver shock shoe shoot shop short shoulder shove shrimp shrug shuffle shy sibling sick side siege sight sign silent silk silly silver similar simple since sing siren sister situate six size skate sketch ski skill skin skirt skull slab slam sleep slender slice slide slight slim slogan slot slow slush small smart smile smoke smooth snack snake snap sniff snow soap soccer social sock soda soft solar soldier solid solution solve someone song soon sorry sort soul sound soup source south space spare spatial spawn speak special speed spell spend sphere spice spider spike spin spirit split spoil sponsor spoon sport spot spray spread spring spy square squeeze squirrel stable stadium staff stage stairs stamp stand start state stay steak steel stem step stereo stick still sting stock stomach stone stool story stove strategy street strike strong struggle student stuff stumble style subject submit subway success such sudden suffer sugar suggest suit summer sun sunny sunset super supply supreme sure surface surge surprise surround survey suspect sustain swallow swamp swap swarm swear sweet swift swim swing switch sword symbol symptom syrup system table tackle tag tail talent talk tank tape target task taste tattoo taxi teach team tell ten tenant tennis tent term test text thank that theme then theory there they thing this thought three thrive throw thumb thunder ticket tide tiger tilt timber time tiny tip tired tissue title toast tobacco today toddler toe together toilet token tomato tomorrow tone tongue tonight tool tooth top topic topple torch tornado tortoise toss total tourist toward tower town toy track trade traffic tragic train transfer trap trash travel tray treat tree trend trial tribe trick trigger trim trip trophy trouble truck true truly trumpet trust truth try tube tuition tumble tuna tunnel turkey turn turtle twelve twenty twice twin twist two type typical ugly umbrella unable unaware uncle uncover under undo unfair unfold unhappy uniform unique unit universe unknown unlock until unusual unveil update upgrade uphold upon upper upset urban urge usage use used useful useless usual utility vacant vacuum vague valid valley valve van vanish vapor various vast vault vehicle velvet vendor venture venue verb verify version very vessel veteran viable vibrant vicious victory video view village vintage violin virtual virus visa visit visual vital vivid vocal voice void volcano volume vote voyage wage wagon wait walk wall walnut want warfare warm warrior wash wasp waste water wave way wealth weapon wear weasel weather web wedding weekend weird welcome west wet whale what wheat wheel when where whip whisper wide width wife wild will win window wine wing wink winner winter wire wisdom wise wish witness wolf woman wonder wood wool word work world worry worth wrap wreck wrestle wrist write wrong yard year yellow you young youth zebra zero zone zoo";

        words = wordlist_init(wordString);
    }

    return words;
}

static size_t len_to_mask(size_t len)
{
    switch (len) {
        case BIP39_ENTROPY_LEN_128: return 0xf0;
        case BIP39_ENTROPY_LEN_160: return 0xf8;
        case BIP39_ENTROPY_LEN_192: return 0xfc;
        case BIP39_ENTROPY_LEN_224: return 0xfe;
        case BIP39_ENTROPY_LEN_256: return 0xff;
        case BIP39_ENTROPY_LEN_288: return 0x80ff;
        case BIP39_ENTROPY_LEN_320: return 0xC0ff;
    }
    return 0;
}

String toMnemonic(Data data)
{
    DataTrackPush();

    size_t mask = len_to_mask(data.length);

    Data hash = sha256(data);

    size_t checksum = ((char*)hash.bytes)[0] | (((char*)hash.bytes)[0] << 8);

    checksum = checksum & mask;

    data = DataAddCopy(data, uint8D(checksum & 0xff));

    if(mask > 0xff)
        data = DataAddCopy(data, uint8D((checksum >> 8) & 0xff));

    char *str = mnemonic_from_bytes(nmemonicWordlist(), (void*)data.bytes, data.length);

    return DTPop(str ? StringNew(str) : DataNull());
}

Data fromMnemonic(String mnemonic)
{
    DataTrackPush();

    Data result = DataNew(mnemonic.length);
    size_t written = 0;

    if(0 != mnemonic_to_bytes(nmemonicWordlist(), (void*)mnemonic.bytes, (void*)result.bytes, result.length, &written))
        return DTPopNull();

    written--;

    if(written > BIP39_ENTROPY_LEN_256)
        written--;

    Data data = DataCopy((void*)result.bytes, (uint32_t)written);

    BTCUTILAssert(0 == DataCompare(toMnemonic(data), mnemonic));

    if(0 != DataCompare(toMnemonic(data), mnemonic))
        return DTPopNull();

    return DTPop(data);
}

static String base58AddressPayloadHash(Data hash, uint8_t prefix)
{
    DataTrackPush();

    Data data = DataNew(0);

    data = DataAdd(data, uint8D(prefix));
    data = DataAddCopy(data, hash);

    Data checksum = hash256(data);

    if(checksum.length > 4)
        checksum = DataCopyDataPart(checksum, 0, 4);

    data = DataAdd(data, checksum);

    return DTPop(Base58Encode(data));
}

String base58Address(Data payload, uint8_t prefix)
{
    DataTrackPush();

    return DTPop(base58AddressPayloadHash(hash160(payload), prefix));
}

Data addressToPubScript(String address)
{
    DataTrackPush();

    if(address.length < 1)
        return DTPopNull();

    Data data = DataNull();

    if(StringHasPrefix(address, "tb") || StringHasPrefix(address, "bc"))
        data = fromSegwit(address);
    else
        data = base58Dencode((char*)address.bytes);

    // witness pay to public key hash
    if(data.length == 20)
        return DTPop(p2wpkhPubScript(data));// returns 2 byets....

    // witness pay to witness script hash
    if(data.length == 32)
        return DTPop(p2wshPubScript(data));

    // origianl hash method
    if(data.length == 25) {

        Data hash = DataCopyDataPart(data, 1, 20);
        Data checksum = DataCopyDataPart(data, 21, 4);
        Data actualChecksum = hash256(DataCopyDataPart(data, 0, 21));

        if(!DataEqual(checksum, DataCopyDataPart(actualChecksum, 0, 4)))
            return DTPopNull();

        uint8_t version = *(uint8_t*)data.bytes;

        if(version == 0x00 || version == 0x6F)
            return DTPop(p2pkhPubScript(hash));

        if(version == 0x05 || version == 0xc4)
            return DTPop(p2shPubScript(hash));
    }

    return DTPopNull();
}

String pubScriptToAddress(Data pubScript)
{
    DataTrackPush();

    if(pubScript.length < 1)
        return DTPopNull();

    ScriptTokens tokens = scriptToTokens(pubScript);

    if(tokens.count == 2 && ScriptTokenI(tokens, 0).op == 65 && ScriptTokenI(tokens, 0).data.length == 65 && ScriptTokenI(tokens, 1).op == OP_CHECKSIG) {

        // Pay to public key is not supported
        return DTPopNull();
    }

    // Pay to pubkey hash
    if(tokens.count == 5 && ScriptTokenI(tokens, 0).op == OP_DUP && ScriptTokenI(tokens, 1).op == OP_HASH160)
        if(ScriptTokenI(tokens, 2).op == 0x14 && ScriptTokenI(tokens, 3).op == OP_EQUALVERIFY && ScriptTokenI(tokens, 4).op == OP_CHECKSIG)
            return DTPop(base58AddressPayloadHash(ScriptTokenI(tokens, 2).data, 0x00));

    // Pay to script hash
    if(tokens.count == 3 && ScriptTokenI(tokens, 0).op == OP_HASH160 && ScriptTokenI(tokens, 1).op == 20 && ScriptTokenI(tokens, 2).op == OP_EQUAL)
        return DTPop(base58AddressPayloadHash(ScriptTokenI(tokens, 1).data, 0x05));

    // Pay to witness public key hash
    if(tokens.count == 2 && ScriptTokenI(tokens, 0).op == 0 && ScriptTokenI(tokens, 1).op == 20)
        return DTPop(toSegwit(ScriptTokenI(tokens, 1).data, "bc"));

    // Pay to witness public key hash
    if(tokens.count == 2 && ScriptTokenI(tokens, 0).op == 0 && ScriptTokenI(tokens, 1).op == 32)
        return DTPop(toSegwit(ScriptTokenI(tokens, 1).data, "bc"));

    return DTPopNull();
}

String p2pkhAddress(Data publicKey)
{
    DataTrackPush();

    return DTPop(base58Address(publicKeyCompress(publicKeyParse(publicKey)), 0x00));
}

String p2pkhAddressTestNet(Data publicKey)
{
    DataTrackPush();

    return DTPop(base58Address(publicKeyCompress(publicKeyParse(publicKey)), 0x6F));
}

Data p2pkhPubScript(Data hash)
{
    DataTrackPush();

    uint8_t prefix[] = { OP_DUP, OP_HASH160, 0x14 };
    uint8_t suffix[] = { OP_EQUALVERIFY, OP_CHECKSIG };

    Data data = DataNew(0);

    data = DataAdd(data, DataCopy((void*)prefix, sizeof(prefix)));
    data = DataAdd(data, DataCopyData(hash));
    data = DataAdd(data, DataCopy((void*)suffix, sizeof(suffix)));

    return DTPop(data);
}

Data p2pkhPubScriptFromPubKey(Data publicKey)
{
    DataTrackPush();

    Data hash = hash160(publicKeyCompress(publicKeyParse(publicKey)));

    return DTPop(p2pkhPubScript(hash));
}

String p2shAddress(Data scriptData)
{
    return base58Address(scriptData, 0x05);
}

String p2shAddressTestNet(Data scriptData)
{
    return base58Address(scriptData, 0xC4);
}

Data p2shPubScriptWithScript(Data scriptData)
{
    return DTPop(p2shPubScript(hash160(DTPush(scriptData))));
}

Data p2shPubScript(Data scriptHash)
{
    DataTrackPush();

    Data data = DataNew(0);

    data = DataAdd(data, uint8D(OP_HASH160));
    data = DataAdd(data, scriptPush(scriptHash));
    data = DataAdd(data, uint8D(OP_EQUAL));

    return DTPop(data);
}

String p2wpkhAddress(Data publicKey)
{
    DataTrackPush();

    Data compressedPublicKey = publicKeyCompress(publicKeyParse(publicKey));

    return DTPop(toSegwit(hash160(compressedPublicKey), "bc"));
}

String p2wpkhAddressTestNet(Data publicKey)
{
    DataTrackPush();

    Data compressedPublicKey = publicKeyCompress(publicKeyParse(publicKey));

    return DTPop(toSegwit(hash160(compressedPublicKey), "tb"));
}

Data p2wpkhPubScript(Data hash)
{
    if(!hash.bytes)
        return DataNull();

    Data data = DataNew(0);

    data = DataAdd(data, uint8D(0));
    data = DataAdd(data, scriptPush(hash));

     return data;
}

Data p2wpkhPubScriptFromPubKey(Data publicKey)
{
    DataTrackPush();

    //TODO: Does if(!publicKey) compile time fail? If not we need to go through and catch all these! They should be !publicKey.bytes
    if(!publicKey.bytes)
        return DTPopNull();

    Data compressedPublicKey = publicKeyCompress(publicKeyParse(publicKey));

    if(!compressedPublicKey.bytes)
        return DTPopNull();

    return DTPop(p2wpkhPubScript(hash160(compressedPublicKey)));
}

Data p2wpkhImpliedScript(Data publicKey)
{
    DataTrackPush();

    uint8_t prefix[] = { OP_DUP, OP_HASH160, 0x14 };
    uint8_t suffix[] = { OP_EQUALVERIFY, OP_CHECKSIG };

    Data data = DataNew(0);

    Data hash = hash160(publicKeyCompress(publicKeyParse(publicKey)));

    data = DataAdd(data, DataCopy((void*)prefix, sizeof(prefix)));
    data = DataAdd(data, hash);
    data = DataAdd(data, DataCopy((void*)suffix, sizeof(suffix)));

    return DTPop(data);
}

String p2wshAddress(Data scriptData)
{
    return DTPop(toSegwit(sha256(DTPush(scriptData)), "bc"));
}

String p2wshAddressTestNet(Data scriptData)
{
    return DTPop(toSegwit(sha256(DTPush(scriptData)), "tb"));
}

Data p2wshPubScriptWithScript(Data scriptData)
{
    return DTPop(p2wshPubScript(sha256(DTPush(scriptData))));
}

Data p2wshPubScript(Data hash)
{
    Data data = DataNew(0);

    data = DataAdd(data, uint8D(0));
    data = DataAdd(data, scriptPush(hash));

    return data;
}

String scriptToString(Data script)
{
    String result = StringNew("");

    ScriptTokens tokens = scriptToTokens(script);

    for(int i = 0; i < tokens.count; i++) {

        ScriptToken token = ScriptTokenI(tokens, i);
        
        if(result.length)
            result = StringAdd(result, " ");

        result = StringAdd(result, ScriptTokenDescription(token).bytes);
    }
    
    return result;
}

String firstError(Data script)
{
    ScriptTokens tokens = scriptToTokens(script);

    for(int i = 0; i < tokens.count; i++) {

        ScriptToken token = ScriptTokenI(tokens, i);

        if(token.error.bytes)
            return token.error;
    }
    
    return DataNull();
}

ScriptTokens scriptToTokens(Data script)
{
    ScriptTokens result = DatasNew();
    ScriptTokens tmp = scriptToTokensUnsafe(script);

    for(int i = 0; i < tmp.count; i++)
        result = DatasAddCopy(result, ScriptTokenCopy(tmp.ptr[i]));

    DatasFree(tmp);

    return result;
}

ScriptTokens scriptToTokensUnsafe(Data script)
{
    ScriptTokens result = DatasNew();
    
    const uint8_t *ptr = (uint8_t*)script.bytes;
    const uint8_t *end = ptr + script.length;
    
    while(ptr < end) {
        
        ScriptToken token = { 0 };
        
        if(isPushData(ptr, end)) {
            
            uint8_t op;
            
            uint64_t size = readPushDataWithOp(&ptr, end, &op);
            
            token.op = op;
            
            token.data = readBytesUnsafe(size, &ptr, end);
            
            if(token.data.length != size)
                token.error = StringNew("script ended before push data count was reached");
        }
        else {
            
            token.op = uint8readP(&ptr, end);
        }

        result = DatasAddRef(result, DataCopy((void*)&token, sizeof(token)));
    }
    
    return result;
}
