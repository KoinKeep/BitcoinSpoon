//
//  KeyManager.m
//  KoinKeep
//
//  Created by Dustin Dettmer on 10/18/18.
//  Copyright © 2018 Dustin. All rights reserved.
//

#include "KeyManager.h"
#include "BTCUtil.h"
#include "BasicStorage.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <pthread.h>

const char *keyManagerKeyDirectory = "/tmp";
void (*KeyManagerCustomEntropy)(char *buf, int length) = NULL;
Data (*KeyManagerUniqueData)() = NULL;

// Override here returning an existing directory to put keyfiles in.
// It must not end in a slash.
static String getKeyDirectory()
{
    return StringNew(keyManagerKeyDirectory);
}

// Override here with your best entropy for the platform you're on
static Data getEntropy(int length)
{
    Data data = DataNew(length);

    if(KeyManagerCustomEntropy)
        KeyManagerCustomEntropy(data.bytes, length);
    else
        arc4random_buf(data.bytes, length);

    return data;
}

// Override here with the best unique data for this device you can get
static Data getUniqueData()
{
    if(KeyManagerUniqueData)
        return KeyManagerUniqueData();

    Data data = DataNew(0);

    time_t t;

    time(&t);

    data = DataAppend(data, DataRaw(t));

    return data;
}

static int KMSwapPrivKey(KeyManager *self, int indexA, int indexB);

typedef enum {
	KeyTypeEnc = 1,//
	KeyTypeComm,//
} KeyType;

KeyManager km;

void KMInit()
{
    KeyManager *self = &km;

	const char *noncePrefixKey = "noncePrefix";

    Data entropy = bsLoad(noncePrefixKey);

	if(!entropy.length) {

        entropy = getEntropy(sizeof(uint32_t));

        bsSave(noncePrefixKey, entropy);
	}

    self->noncePrefix = *(uint32_t*)entropy.bytes;

    self->privKeyHashCache = DatasUntrack(DatasNew());
    self->vaultMasterHdWalletCache = DictUntrack(DictNew());
    self->mainSeed = DataUntrack(DataNew(0));
}

int KMHasMasterPrivKey(KeyManager *self)
{
    return KMPrivKeyAtIndex(self, 0).length ? 1 : 0;
}

Data KMMasterPrivKey(KeyManager *self)
{
    return KMPrivKeyAtIndex(self, 0);
}

void KMImportMasterPrivKey(KeyManager *self, Data key)
{
    KMSwapPrivKey(self, 0, KMAddPrivKey(self, key));
}

void KMGenerateMasterPrivKey(KeyManager *self)
{
    Data data = getEntropy(32);

    data = DataAppend(data, getUniqueData());

	KMImportMasterPrivKey(self, hash256(data));
}

Data KMMasterPubKey(KeyManager *self)
{
    Data key = KMMasterPrivKey(self);

	if(!key.length)
		return DataNull();

	return pubKeyFull(DataCopyDataPart(key, 1, 64));
}

Data KMMasterPrivKeyHash(KeyManager *self)
{
    return hash256(KMMasterPrivKey(self));
}

Data KMEncKey(KeyManager *self, Data privKey)
{
	return hmacSha256(privKey, uint8D(KeyTypeEnc));
}

Data KMCommPrivKey(KeyManager *self, Data privKey)
{
	return hmacSha256(privKey, uint8D(KeyTypeComm));
}

uint64_t KMNextNonce(KeyManager *self)
{
	const char *nonceCounterKey = "nonceCounter";

    Data data = bsLoad(nonceCounterKey);

	uint64_t result = 0;

	uint32_t noncePrefix = self->noncePrefix;
    uint32_t nonce = data.length == sizeof(uint32_t) ? *(uint32_t*)data.bytes : 0;

	memcpy(&result, &noncePrefix, 4);
	memcpy(((uint8_t*)&result) + 4, &nonce, 4);

	nonce++;

    bsSave(nonceCounterKey, DataRaw(nonce));

	return result;
}

static const char *KeyManagerAllKeysPrefix = "KeyManager.allKeys.";
static const char *KeyManagerAllKeysPrefixTestNet = "KeyManager.testNet.allKeys.";

String KMPrivKeyKey(KeyManager *self, int index)
{
    if(self->testnet)
        return StringF("%s%d", KeyManagerAllKeysPrefixTestNet, index);

    return StringF("%s%d", KeyManagerAllKeysPrefix, index);
}

static pthread_mutex_t privKeyHashCacheMutex = PTHREAD_MUTEX_INITIALIZER;

Datas KMAllPrivKeyHashes(KeyManager *self)
{
    pthread_mutex_lock(&privKeyHashCacheMutex);

    Datas result = DatasCopy(self->privKeyHashCache);

    if(!self->privKeyHashCache.count) {

        for(int i = 0;; i++) {

            Data data = KMPrivKeyAtIndex(self, i);

            if(!data.length)
                break;

            result = DatasAddCopy(result, sha256(data));
        }

        self->privKeyHashCache = DatasUntrack(result);
    }

    pthread_mutex_unlock(&privKeyHashCacheMutex);

    return result;
}

Datas KMAllPrivKeys(KeyManager *self)
{
    Datas result = DatasNew();

    for(int i = 0;; i++) {

        Data data = KMPrivKeyAtIndex(self, i);

        if(!data.length)
            break;

        result = DatasAddCopy(result, data);
    }

    return result;
}

Data KMPrivKeyForHash(KeyManager *self, Data hash)
{
    FORDATAIN(privKey, KMAllPrivKeys(self))
        if(DataEqual(sha256(*privKey), hash))
            return *privKey;
    
    return DataNull();
}

String KMDocumentsPath(KeyManager *self)
{
    return getKeyDirectory();
}

String KMFilenameForIndex(KeyManager *self, int index)
{
    return StringF("%s/%s", KMDocumentsPath(self).bytes, KMPrivKeyKey(self, index).bytes);
}

Data KMPrivKeyAtIndex(KeyManager *self, int index)
{
    Data result = DataNull();

    FILE *file = fopen(KMFilenameForIndex(self, index).bytes, "r");

    if(file) {

        char buf[32];

        if(fread(buf, 32, 1, file) == 32)
            result = DataCopy(buf, 32);

        fclose(file);
    }

    return result;
}

static pthread_mutex_t vaultMasterHdWalletCacheMutex = PTHREAD_MUTEX_INITIALIZER;

Data KMVaultMasterHdWalletAtIndex(KeyManager *self, int index)
{
    Data cacheKey = sha256(KMPrivKeyAtIndex(self, index));

    pthread_mutex_lock(&vaultMasterHdWalletCacheMutex);

    Data result = DictGet(self->vaultMasterHdWalletCache, cacheKey);

    if(!result.length) {

        Data seedPhrase = DataCopy("Bitcoin seed", strlen("Bitcoin seed"));

        Data seed = PBKDF2(toMnemonic(KMPrivKeyAtIndex(self, index)).bytes, NULL);

        Data hash = hmacSha512(seedPhrase, seed);

        Data hdWalletData = DataNull();

        if(self->testnet) {

            hdWalletData = hdWalletPrivTestNet(DataCopyDataPart(hash, 0, 32), DataCopyDataPart(hash, 32, 32));
        }
        else {

            hdWalletData = hdWalletPriv(DataCopyDataPart(hash, 0, 32), DataCopyDataPart(hash, 32, 32));
        }

        result = hdWallet(hdWalletData, "m/44'/0'/1'");

        DictAdd(&self->vaultMasterHdWalletCache, cacheKey, result);
    }

    pthread_mutex_unlock(&vaultMasterHdWalletCacheMutex);

    return result;
}

void KMSetPrivKey(KeyManager *self, Data data, int index)
{
    FILE *file = fopen(KMFilenameForIndex(self, index).bytes, "w");

    if(file) {

        fwrite(data.bytes, data.length, 1, file);

        fclose(file);
    }
}

static int KMSwapPrivKey(KeyManager *self, int indexA, int indexB)
{
    Data keyA = KMPrivKeyAtIndex(self, indexA);
    Data keyB = KMPrivKeyAtIndex(self, indexB);

    if(!keyA.length || !keyB.length)
        return 0;

    if(indexA == indexB)
        return 1;

    KMSetPrivKey(self, keyA, indexB);
    KMSetPrivKey(self, keyB, indexA);

    return 1;
}

int KMAddPrivKey(KeyManager *self, Data privKey)
{
    int index = KMAllPrivKeyHashes(self).count;

    pthread_mutex_lock(&privKeyHashCacheMutex);

    /* Reset the priv key hash cache */
    DatasTrack(self->privKeyHashCache);
    self->privKeyHashCache = DatasUntrack(DatasNew());

    KMSetPrivKey(self, privKey, index);

    pthread_mutex_unlock(&privKeyHashCacheMutex);

    return index;
}

static const char *KeyManagerAllHiddenKeysPrefix = "KeyManager.allHiddenKeys.";
static const char *KeyManagerAllHiddenKeysPrefixTestNet = "KeyManager.testNet.allHiddenKeys.";

String KMHiddenPrivKeyKey(KeyManager *self, int index)
{
    if(self->testnet)
        return StringF("%s%d", KeyManagerAllHiddenKeysPrefixTestNet, index);

    return StringF("%s%d", KeyManagerAllHiddenKeysPrefix, index);
}

//Datas allHiddenPrivKeyHashes
//{
//    Datas result = [NSMutableArray new);
//
//    for(int i = 0;; i++) {
//
//        Dict query =
//        @{
//          (id)kSecClass: (id)kSecClassKey,//
//          (id)kSecReturnData: (id)kCFBooleanTrue,//
//          (id)kSecAttrApplicationTag: [self hiddenPrivKeyKey:i],//
//          };
//
//        CFTypeRef resultRef = NULL;
//
//        if(SecItemCopyMatching((CFDictionaryRef)query, &resultRef) != errSecSuccess)
//            break;
//
//        [result addObject:sha256((__bridge NSData*)resultRef]);
//    }
//
//    return result;
//}
//
//int hidePrivKey(KeyManager *self, int index)
//{
//    int i = index;
//
//    while([self swapPrivKey:i withIndex:i + 1])
//        i++;
//
//    Data data = [self privKeyAtIndex:i);
//    NSDictionary *query;
//
//    int hiddenIndex = (int)self->allHiddenPrivKeyHashes.count;
//
//    query =
//    @{
//      (id)kSecClass: (id)kSecClassKey,//
//      (id)kSecAttrApplicationTag: [self hiddenPrivKeyKey:hiddenIndex],//
//      (id)kSecValueData: data,//
//      };
//
//    SecItemAdd((CFDictionaryRef)query, NULL);
//
//    query =
//    @{
//      (id)kSecClass: (id)kSecClassKey,//
//      (id)kSecAttrApplicationTag: [self privKeyKey:i],//
//      };
//
//    SecItemDelete((CFDictionaryRef)query);
//
//    return hiddenIndex;
//}
//
//int unhidePrivKey(KeyManager *self, int)hiddenIndex
//{
//    Dict query =
//    @{
//      (id)kSecClass: (id)kSecClassKey,//
//      (id)kSecReturnData: (id)kCFBooleanTrue,//
//      (id)kSecAttrApplicationTag: [self hiddenPrivKeyKey:hiddenIndex],//
//      };
//
//    CFTypeRef resultRef = NULL;
//
//    if(SecItemCopyMatching((CFDictionaryRef)query, &resultRef) != errSecSuccess)
//        return -1;
//
//    Data data = (__bridge NSData*)resultRef;
//
//    int index = [self addPrivKey:data);
//
//    [self deleteHiddenKey:hiddenIndex);
//
//    return index;
//}
//
//void deleteHiddenKey(KeyManager *self, int)hiddenIndex
//{
//    NSParameterAssert(NO);
//    // Update allPrivKeyHashes if this method is used
//
//    Dict query =
//    @{
//      (id)kSecClass: (id)kSecClassKey,//
//      (id)kSecAttrApplicationTag: [self hiddenPrivKeyKey:hiddenIndex],//
//      };
//
//    SecItemDelete((CFDictionaryRef)query);
//}

static pthread_mutex_t mainSeedMutex = PTHREAD_MUTEX_INITIALIZER;

void KMSetTestnet(KeyManager *self, int testnet)
{
    self->testnet = testnet;

    pthread_mutex_lock(&mainSeedMutex);

    DataTrack(self->mainSeed);
    self->mainSeed = DataUntrack(DataNew(0));

    pthread_mutex_unlock(&mainSeedMutex);

    pthread_mutex_lock(&privKeyHashCacheMutex);

    DatasTrack(self->privKeyHashCache);
    self->privKeyHashCache = DatasUntrack(DatasNew());

    pthread_mutex_unlock(&privKeyHashCacheMutex);
}

Data KMHdWallet(KeyManager *self, uint32_t index)
{
    if(!KMKeyName(self, index).length)
        return DataNull();

    Data seedPhrase = DataCopy("Bitcoin seed", strlen("Bitcoin seed"));

    Data hash = DataNull();

    pthread_mutex_lock(&mainSeedMutex);

    if(!self->mainSeed.length)
        self->mainSeed = DataUntrack(PBKDF2(toMnemonic(KMPrivKeyAtIndex(self, 0)).bytes, NULL));

    hash = hmacSha512(seedPhrase, self->mainSeed);

    pthread_mutex_unlock(&privKeyHashCacheMutex);

    Data hdWalletData = DataNull();

    if(self->testnet) {
        
        hdWalletData = hdWalletPrivTestNet(DataCopyDataPart(hash, 0, 32), DataCopyDataPart(hash, 32, 32));
    }
    else {

        hdWalletData = hdWalletPriv(DataCopyDataPart(hash, 0, 32), DataCopyDataPart(hash, 32, 32));
    }

	return hdWallet(hdWalletData, StringF("m/44'/%d'", index).bytes);
}

static Data KMHdWalletForManualWallets(KeyManager *self, uint32_t index)
{
    Data result = DataNull();
    Dict wallets = KMKnownHDWallets(self);

    FORINDICT(item, wallets) {

        Dict details = DictDeserialize(item->value);

        KeyManagerHdWalletType type = DataGetInt(DictGetS(details, knownHDWalletTypeKeepKey));

        if(type != KeyManagerHdWalletTypeManual)
            continue;

        if(index--)
            continue;

        return DictGetS(details, knownHDWalletDataKey);
    }

    return result;
}

Datas KMAllHdWalletPubRoots(KeyManager *self)
{
    Datas array = DatasNew();

    Data hdWalletData;

    for(uint32_t i = 0; (hdWalletData = KMHdWallet(self, i)).length; i++) {

        array = DatasAddCopy(array, publicHdWallet(hdWallet(hdWalletData, "0'/0")));
        array = DatasAddCopy(array, publicHdWallet(hdWallet(hdWalletData, "0'/1")));
        array = DatasAddCopy(array, publicHdWallet(hdWallet(hdWalletData, "1'/0")));
        array = DatasAddCopy(array, publicHdWallet(hdWallet(hdWalletData, "1'/1")));
    }

    for(uint32_t i = 0; (hdWalletData = KMHdWalletForManualWallets(self, i)).length; i++) {

        array = DatasAddCopy(array, hdWallet(hdWalletData, "0"));
        array = DatasAddCopy(array, hdWallet(hdWalletData, "1"));
    }

    return array;
}

static const char *KeyNameDictKey = "KeyNameDict";
static const char *KeyNameDictKeyTestNet = "KeyNameDictTestNet";

String KMKeyName(KeyManager *self, uint32_t index)
{
    Dict dict = DictDeserialize(bsLoad(self->testnet ? KeyNameDictKeyTestNet : KeyNameDictKey));

    return DictGet(dict, StringF("%u", index));
}

void KMSetKeyName(KeyManager *self, String name, uint32_t index)
{
    Dict dict = DictDeserialize(bsLoad(self->testnet ? KeyNameDictKeyTestNet : KeyNameDictKey));

	if(name.length)
        DictAdd(&dict, StringF("%u", index), name);
	else
        DictRemove(&dict, StringF("%u", index));

    bsSave(self->testnet ? KeyNameDictKeyTestNet : KeyNameDictKey, DictSerialize(dict));
}

uint32_t KMNamedKeyCount(KeyManager *self)
{
	uint32_t result = 0;

	while(KMKeyName(self, result).length)
		result++;

	return result;
}

static const char *knownHDWalletsKey = "knownHDWalletsKeyMainNet";
static const char *knownHDWalletsKeyTestNet = "knownHDWalletsKey";
const char *knownHDWalletDataKey = "dataKey";
const char *knownHDWalletTypeKeepKey = "type";

Dictionary KMKnownHDWallets(KeyManager *self)
{
	return DictDeserialize(bsLoad(self->testnet ? knownHDWalletsKeyTestNet : knownHDWalletsKey));
}

void KMSetHDWalletForUUID(KeyManager *self, Data hdWalletData, String uuid, KeyManagerHdWalletType type)
{
    Dict dict = DictDeserialize(bsLoad(self->testnet ? knownHDWalletsKeyTestNet : knownHDWalletsKey));

    if(hdWalletData.length) {

        Dict item = DictTwo(StringNew(knownHDWalletDataKey), hdWalletData, StringNew(knownHDWalletTypeKeepKey), DataInt(type));

        DictAdd(&dict, uuid, DictSerialize(item));
    }
    else
        DictRemove(&dict, uuid);

    bsSave(self->testnet ? knownHDWalletsKeyTestNet : knownHDWalletsKey, DictSerialize(dict));
}

Data KMHdWalletFrom(KeyManager *self, String uuid)
{
    Dict dict = DictDeserialize(bsLoad(self->testnet ? knownHDWalletsKeyTestNet : knownHDWalletsKey));

    Dict item = DictDeserialize(DictGet(dict, uuid));
    
    String string = DictGetS(item, knownHDWalletDataKey);

    return string;
}

String KMUuidFromHDWallet(KeyManager *self, Data hdWalletData)
{
    Dict dict = KMKnownHDWallets(self);

    FORINDICT(item, dict)
        if(DataEqual(item->value, hdWalletData))
			return item->key;

	return DataNull();
}

KeyManagerHdWalletType KMHdWalletType(KeyManager *self, String uuid)
{
    Dict dict = DictDeserialize(bsLoad(self->testnet ? knownHDWalletsKeyTestNet : knownHDWalletsKey));

    Dict item = DictDeserialize(DictGet(dict, uuid));

    return DataGetInt(DictGetS(item, knownHDWalletTypeKeepKey));
}

Data KMEncKeyUuid(KeyManager *self, String uuid)
{
    Data privKey = KMPrivKeyForHash(self, KMPrivKeyHashForDevice(self, uuid));
    
    return KMEncKey(self, privKey);
}

Data KMCommKeyUuid(KeyManager *self, String uuid)
{
    Data privKey = KMPrivKeyForHash(self, KMPrivKeyHashForDevice(self, uuid));
    
    return KMCommPrivKey(self, privKey);
}

static const char *privKeyHashForDeviceKey = "privKeyHashForDeviceKey";

Data KMPrivKeyHashForDevice(KeyManager *self, String uuid)
{
    Dict dict = DictDeserialize(bsLoad(privKeyHashForDeviceKey));

    String string = DictGet(dict, uuid);

    return string;
}

void KMSetPrivKeyHashForDevice(KeyManager *self, Data hash, String uuid)
{
    Dict dict = DictDeserialize(bsLoad(privKeyHashForDeviceKey));

    DictSet(&dict, uuid, hash);

    bsSave(privKeyHashForDeviceKey, DictSerialize(dict));
}

int64_t KMIndexForKeyNamed(KeyManager *self, String name)
{
	String keyName = DataNull();

	for(uint32_t i = 0; (keyName = KMKeyName(self, i)).length; i++) {

		if(DataEqual(keyName, name))
			return i;
	}

	return -1;
}

static const char *vaultObjectsKey = "vaultObjectsKey";
static const char *vaultObjectsKeyTestNet = "vaultObjectsKeyTestNet";
static const char *vaultObjectNameKey = "vaultObjectNameKey";
static const char *vaultObjectWalletsKey = "vaultObjectWalletsKey";
static const char *vaultObjectMasterKey = "vaultObjectMasterKey";

void KMAddVaultObserver(KeyManager *self, Data masterHdWallet, Datas hdWallets, String name)
{
    hdWallets = DatasCopy(hdWallets);

    Datas array = DatasDeserialize(bsLoad(self->testnet ? vaultObjectsKeyTestNet : vaultObjectsKey));

    Dict info = DictNew();

    DictAddS(&info, vaultObjectNameKey, name);
    DictAddS(&info, vaultObjectWalletsKey, DatasSerialize(hdWallets));
    DictAddS(&info, vaultObjectMasterKey, masterHdWallet);

    array = DatasAddCopy(array, DictSerialize(info));

    bsSave(self->testnet ? vaultObjectsKeyTestNet : vaultObjectsKey, DatasSerialize(array));
}

void KMAddVault(KeyManager *self, Datas hdWallets, String name)
{
    hdWallets = DatasCopy(hdWallets);

    Datas array = DatasDeserialize(bsLoad(self->testnet ? vaultObjectsKeyTestNet : vaultObjectsKey));

    Dict info = DictNew();

    DictAddS(&info, vaultObjectNameKey, name);
    DictAddS(&info, vaultObjectWalletsKey, DatasSerialize(hdWallets));

    array = DatasAddCopy(array, DictSerialize(info));

    bsSave(self->testnet ? vaultObjectsKeyTestNet : vaultObjectsKey, DatasSerialize(array));
}

void KMRemoveVault(KeyManager *self, uint32_t index)
{
    Datas array = DatasDeserialize(bsLoad(self->testnet ? vaultObjectsKeyTestNet : vaultObjectsKey));

    if(index < array.count)
        array = DatasRemoveIndex(array, index);

    bsSave(self->testnet ? vaultObjectsKeyTestNet : vaultObjectsKey, DatasSerialize(array));
}

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

Datas KMVaultMasterHdWallets(KeyManager *self)
{
    Datas result = DatasNew();

    Datas array = DatasDeserialize(bsLoad(self->testnet ? vaultObjectsKeyTestNet : vaultObjectsKey));
    
    Datas allPrivKeyHashes = KMAllPrivKeyHashes(self);

    FORDATAIN(info, array) {

        uint32_t smallestIndex = UINT32_MAX;

        Datas hdWallets = DatasDeserialize(DictGetS(DictDeserialize(*info), vaultObjectWalletsKey));

        FORDATAIN(hdWalletData, hdWallets) {

            String uuid = KMUuidFromHDWallet(self, *hdWalletData);

            uint32_t index = DatasMatchingDataIndex(allPrivKeyHashes, KMPrivKeyHashForDevice(self, uuid));

            smallestIndex = MIN(smallestIndex, index);
        }
        
        Data data = DataNull();

        if(smallestIndex != UINT32_MAX)
            data = KMVaultMasterHdWalletAtIndex(self, smallestIndex);

        if(data.length)
            result = DatasAddCopy(result, data);
    }

    // Add manual master "observer" keys

    Datas vaultObjects = DatasDeserialize(bsLoad(self->testnet ? vaultObjectsKeyTestNet : vaultObjectsKey));

    FORDATAIN(data, vaultObjects) {

        Dict info = DictDeserialize(*data);

        Data masterHdWallet = DictGetS(info, vaultObjectMasterKey);

        if(masterHdWallet.length)
            result = DatasAddCopy(result, masterHdWallet);
    }

    return result;
}

Datas/*Datas*/ KMVaultHdWallets(KeyManager *self)
{
    Datas array = DatasDeserialize(bsLoad(self->testnet ? vaultObjectsKeyTestNet : vaultObjectsKey));
    Datas/*Datas*/ result = DatasNew();

    FORDATAIN(info, array) {

        Datas hdWallets = DatasDeserialize(DictGetS(DictDeserialize(*info), vaultObjectWalletsKey));

        result = DatasAddCopy(result, DatasSerialize(hdWallets));
    }

    return result;
}

Datas/*Datas*/ KMVaultAllHdWallets(KeyManager *self)
{
    Datas masterHdWallets = KMVaultMasterHdWallets(self);
    Datas/*Datas*/ hdWallets = KMVaultHdWallets(self);

    for(int i = 0; i < hdWallets.count; i++) {

        Datas datas = DatasDeserialize(DatasAt(hdWallets, i));

        datas = DatasAddCopyIndex(datas, DatasAt(masterHdWallets, i), 0);

        hdWallets = DatasReplaceIndexCopy(hdWallets, i, DatasSerialize(datas));
    }

    return hdWallets;
}

Datas/*String*/ KMVaultNames(KeyManager *self)
{
    Datas array = DatasDeserialize(bsLoad(self->testnet ? vaultObjectsKeyTestNet : vaultObjectsKey));
    Datas result = DatasNew();

    FORDATAIN(info, array)
        result = DatasAddCopy(result, DictGetS(DictDeserialize(*info), vaultObjectNameKey));

    return result;
}

Data KMVaultScriptDerivation(KeyManager *self, uint32_t index, String path)
{
    Data masterHdWallet = DatasAt(KMVaultMasterHdWallets(self), index);
    Datas hdWallets = DatasDeserialize(DatasAt(KMVaultHdWallets(self), index));

    masterHdWallet = hdWallet(masterHdWallet, path.bytes);

    for(int i = 0; i < hdWallets.count; i++)
        hdWallets = DatasReplaceIndexCopy(hdWallets, i, hdWallet(DatasAt(hdWallets, i), path.bytes));

    return vaultScript(pubKeyFromHdWallet(masterHdWallet), pubKeysFromHdWallets(hdWallets));
}

static const char *sortHashesKey = "sortHashesKey";
static const char *sortHashesKeyTestNet = "sortHashesKeyTestNet";

Datas KMSortHashes(KeyManager *self)
{
    return DatasDeserialize(bsLoad(self->testnet ? sortHashesKeyTestNet : sortHashesKey));
}

void KMSetSortHashes(KeyManager *self, Datas sortHashes)
{
    sortHashes = DatasCopy(sortHashes);

    bsSave(self->testnet ? sortHashesKeyTestNet : sortHashesKey, DatasSerialize(sortHashes));
}

static const char *hiddenHashesKey = "hiddenHashesKey";
static const char *hiddenHashesKeyTestNet = "hiddenHashesKeyTestNet";

Datas KMHiddenHashes(KeyManager *self)
{
    return DatasDeserialize(bsLoad(self->testnet ? hiddenHashesKeyTestNet : hiddenHashesKey));
}

void KMSetHiddenHashes(KeyManager *self, Datas hiddenHashes)
{
    hiddenHashes = DatasCopy(hiddenHashes);

    bsSave(self->testnet ? sortHashesKeyTestNet : sortHashesKey, DatasSerialize(hiddenHashes));
}
