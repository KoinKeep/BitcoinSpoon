//
//  TransactionTracker.m
//  KoinKeep
//
//  Created by Dustin Dettmer on 12/27/18.
//  Copyright © 2018 Dustin. All rights reserved.
//

#include "TransactionTracker.h"
#include "BTCUtil.h"
#include "Database.h"
#include "KeyManager.h"
#include "Transaction.h"
#include "BasicStorage.h"
#include <pthread.h>
#include "Notifications.h"

const char *TransactionTrackerTransactionAdded = "TransactionTrackerTransactionAdded";

#define HDWALLET_SCANAHEAD_COUNT 10
#define HDWALLET_BLOOMAHEAD_COUNT_DEFAULT 100
#define BLOOM_FAILRATE 0.0000001
#define BLOOM_MAX_FAILRATE 0.95
#define BLOOM_MIN_COUNT 3000

int TransactionTrackerBloomaheadCount = HDWALLET_BLOOMAHEAD_COUNT_DEFAULT;

TransactionTracker tracker = {0};

static Dict TTKeysAndKeyHashesForWallet(TransactionTracker *self, Data hdwallet);
static int TTAnyTransactionContainsOneOf(TransactionTracker *self, Dict keysAndHashes);

static pthread_mutex_t allTransactionsMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t scriptAndHashCacheMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t masterHdWalletCacheMutex = PTHREAD_MUTEX_INITIALIZER;

TransactionTracker TTNew(int testnet)
{
    DataTrackPush();

    TransactionTracker instance = {0};
    TransactionTracker *self = &instance;

    self->testnet = testnet;

    Datas array = DatabaseAllTransactions(&database);

    self->allTransactions = DatasUntrack(DatasNew());
    self->allTransactionHashes = DatasUntrack(DatasNew());
    self->allTransactionTxids = DatasUntrack(DatasNew());

    self->scriptAndHashCache = DictUntrack(DictNew());
    self->masterHdWalletCache = DictUntrack(DictNew());

    FORDATAIN(data, array) {

        Transaction transaction = TransactionNew(*data);

        self->allTransactions = DatasAddCopy(self->allTransactions, DataRaw(transaction));
        self->allTransactionHashes = DatasAddCopy(self->allTransactionHashes, hash256(*data));
        self->allTransactionTxids = DatasAddCopy(self->allTransactionTxids, TransactionTxid(transaction));
    }

    DatasUntrack(self->allTransactions);
    DatasUntrack(self->allTransactionHashes);
    DatasUntrack(self->allTransactionTxids);

    self->keysAndKeyHashes = DictUntrack(DictNew());

    FORIN(Transaction, transaction, self->allTransactions) {
        FORIN(TransactionInput, input, transaction->inputs) {

            Transaction *tx = TTTransactionForTxid(self, input->previousTransactionHash);

            TransactionOutput *output = TransactionOutputOrNilAt(tx, input->outputIndex);

            if(output)
                input->fundingOutput = *output;
        }
    }

    FORIN(Transaction, transaction, self->allTransactions)
        TransactionUntrack(transaction);

    return *self;
}

void TTTrack(TransactionTracker *self)
{
    DatasTrack(self->allTransactions);
    DatasTrack(self->allTransactionHashes);
    DatasTrack(self->allTransactionTxids);

    DictTrack(self->keysAndKeyHashes);

    DictTrack(self->scriptAndHashCache);
    DictTrack(self->masterHdWalletCache);

    FORIN(Transaction, transaction, self->allTransactions)
        TransactionTrack(transaction);
}

static const char *bloomFilterKey = "bloomFilterKey";
static const char *lookAheadCountKey = "lookAheadCountKey";
static const char *vaultLookAheadCountKey = "vaultLookAheadCountKey";
static const char *allHdWalletsKey = "allHdWalletsKey";
static const char *vaultCountKey = "vaultCountKey";
static const char *bloomFilterDlHeightKey = "bloomFilterDlHeightKey";
static const char *keysAndKeyHashesKey = "keysAndKeyHashesKey";

static const char *testNetStr(TransactionTracker *self)
{
    return self->testnet ? "testnet" : "";
}

static Data TTLoad(TransactionTracker *self, const char *key)
{
    return basicStorageLoad(StringAddRaw(key, testNetStr(self)));
}

static void TTSave(TransactionTracker *self, const char *key, Data data)
{
    basicStorageSave(StringAddRaw(key, testNetStr(self)), data);
}

Data TTBloomFilter(TransactionTracker *self)
{
    return TTLoad(self, bloomFilterKey);
}

void TTSetBloomFilter(TransactionTracker *self, Data bloomFilter)
{
    TTSave(self, bloomFilterKey, bloomFilter);
}

Datas/*int32_t*/ TTLookAheadCount(TransactionTracker *self)
{
    return DatasDeserialize(TTLoad(self, lookAheadCountKey));
}

void TTSetLookAheadCount(TransactionTracker *self, Datas/*int32_t*/ lookAheadCount)
{
    TTSave(self, lookAheadCountKey, DatasSerialize(lookAheadCount));
}

Datas/*int32_t*/ TTVaultLookAheadCount(TransactionTracker *self)
{
    return DatasDeserialize(TTLoad(self, vaultLookAheadCountKey));
}

void TTSetVaultLookAheadCount(TransactionTracker *self, Datas/*int32_t*/ vaultLookAheadCount)
{
    TTSave(self, vaultLookAheadCountKey, DatasSerialize(vaultLookAheadCount));
}

Datas TTAllHdWallets(TransactionTracker *self)
{
    return DatasDeserialize(TTLoad(self, allHdWalletsKey));
}

void TTSetAllHdWallets(TransactionTracker *self, Datas allHdWallets)
{
    TTSave(self, allHdWalletsKey, DatasSerialize(allHdWallets));
}

int TTVaultCount(TransactionTracker *self)
{
    return DataGetInt(TTLoad(self, vaultCountKey));
}

void TTSetVaultCount(TransactionTracker *self, int vaultCount)
{
    TTSave(self, vaultCountKey, DataInt(vaultCount));
}

int32_t TTBloomFilterDlHeight(TransactionTracker *self)
{
    if(self->bloomFilterDlHeight)
        return self->bloomFilterDlHeight;

    self->bloomFilterDlHeight = DataGetInt(TTLoad(self, bloomFilterDlHeightKey));

    return self->bloomFilterDlHeight;
}

void TTSetBloomFilterDlHeight(TransactionTracker *self, int32_t param)
{
    self->bloomFilterDlHeight = param;

    TTSave(self, bloomFilterDlHeightKey, DataInt(param));
}

Dict TTKeysAndKeyHashes(TransactionTracker *self)
{
    if(!DictCount(self->keysAndKeyHashes)) {

        DictTrack(self->keysAndKeyHashes);
        self->keysAndKeyHashes = DictUntrack(DictDeserialize(TTLoad(self, keysAndKeyHashesKey)));
    }
    
    return self->keysAndKeyHashes;
}

void TTSetKeysAndKeyHashes(TransactionTracker *self, Dict param)
{
    DictTrack(self->keysAndKeyHashes);
    self->keysAndKeyHashes = DictUntrack(param);

    TTSave(self, keysAndKeyHashesKey, DictSerialize(self->keysAndKeyHashes));
}

int TTLastUsedAddress(TransactionTracker *self, Data hdWalletParam)
{
    int index = DatasMatchingDataIndex(TTAllHdWallets(self), hdWalletParam);

    if(index == -1)
        return -1;

    int lastUsedAddress = 0;

    for(int i = 0; i < lastUsedAddress + HDWALLET_SCANAHEAD_COUNT; i++) {

        Dict items = TTKeysAndKeyHashesForWallet(self, hdWallet(hdWalletParam, StringF("%d", i).bytes));

        if(TTAnyTransactionContainsOneOf(self, items))
            lastUsedAddress = i;
    }

    return lastUsedAddress;
}

int TTLastUsedVaultAddress(TransactionTracker *self, long vaultIndex)
{
    int lastUsedAddress = 0;

    for(int i = 0; i < lastUsedAddress + HDWALLET_SCANAHEAD_COUNT; i++) {

        Data depositScript = KMVaultScriptDerivation(&km, (uint32_t)vaultIndex, StringF("0/%d", i));
        Data changeScript = KMVaultScriptDerivation(&km, (uint32_t)vaultIndex, StringF("1/%d", i));

        if(TTAnyTransactionContainsOneOf(self, DictTwoKeys(sha256(depositScript), sha256(changeScript))))
            lastUsedAddress = i;
    }

    return lastUsedAddress;
}

int TTUnusedAddresses(TransactionTracker *self, Data hdWalletParam)
{
    int index = DatasMatchingDataIndex(TTAllHdWallets(self), hdWalletParam);

    if(index == -1 || index >= TTLookAheadCount(self).count)
        return -1;

    int lookAheadCount = DataGetInt(TTLookAheadCount(self).ptr[index]);
    int lastUsedAddress = 0;

    for(int i = lookAheadCount - TransactionTrackerBloomaheadCount; i < lastUsedAddress + HDWALLET_SCANAHEAD_COUNT; i++) {

        if(i < 0)
            abort();

        Dict items = TTKeysAndKeyHashesForWallet(self, hdWallet(hdWalletParam, StringF("%d", i).bytes));

        if(TTAnyTransactionContainsOneOf(self, items))
            lastUsedAddress = i;
    }

    return lookAheadCount - lastUsedAddress;
}

static Dict TTKeysAndKeyHashesForWallet(TransactionTracker *self, Data hdWalletParam)
{
    Data compressedPubKey = pubKeyFromHdWallet(hdWalletParam);
    Data fullPubKey = pubKeyExpand(compressedPubKey);

    Data compressedHash = hash160(compressedPubKey);
    Data fullHash = hash160(fullPubKey);

//    NSLog(@"pubkey: %@, hash: %@", toHex:pubkey], toHex:hash160:pubkey]]);

    Dict result = DictNew();

    DictAdd(&result, compressedPubKey, DataNull());
    DictAdd(&result, fullPubKey, DataNull());
    DictAdd(&result, fullHash, fullPubKey);
    DictAdd(&result, compressedHash, compressedPubKey);

    return result;
}

Dict TTScriptAndHashFlexible(TransactionTracker *self, int vaultIndex, int derivation, int isChange, Data masterHdWallet, Datas hdWallets)
{
    Data key = DatasSerialize(DatasThreeCopy(DataInt(vaultIndex), DataInt(derivation), DataInt(isChange)));
    Dict result = { 0 };

    pthread_mutex_lock(&scriptAndHashCacheMutex);

    result = DataGetDict(DictGet(self->scriptAndHashCache, key));

    if(DictCount(result)) {

        pthread_mutex_unlock(&scriptAndHashCacheMutex);
        return DictionaryCopy(result);
    }
    
    if(!masterHdWallet.length) {

        pthread_mutex_unlock(&scriptAndHashCacheMutex);
        return DictNull();
    }

    masterHdWallet = hdWallet(masterHdWallet, StringF("%d/%d", isChange ? 1 : 0, derivation).bytes);

    hdWallets = DatasCopy(hdWallets);

    for(int i = 0; i < hdWallets.count; i++)
        hdWallets = DatasReplaceIndexRef(hdWallets, i, hdWallet(hdWallets.ptr[i], StringF("%d/%d", isChange ? 1 : 0, derivation).bytes));

    Data script = vaultScript(pubKeyFromHdWallet(masterHdWallet), pubKeysFromHdWallets(hdWallets));

    result = DictNew();

    DictAdd(&result, script, DataNull());
    DictAdd(&result, sha256(script), script);

    DictAdd(&self->scriptAndHashCache, key, DataDict(result));

    DictUntrack(self->scriptAndHashCache);

    pthread_mutex_unlock(&scriptAndHashCacheMutex);

    return result;
}

Dict TTScriptAndHash(TransactionTracker *self, int vaultIndex, int derivation, int isChange)
{
    Data masterHdWallet = DatasAt(KMVaultMasterHdWallets(&km), vaultIndex);
    Datas hdWallets = DatasDeserialize(DatasAt(KMVaultHdWallets(&km), vaultIndex));

    return TTScriptAndHashFlexible(self, vaultIndex, derivation, isChange, masterHdWallet, hdWallets);
}

int TTInputContainsOneOf(TransactionTracker *self, TransactionInput *input, Dict keysAndHashes)
{
    return DictDoesIntersect(TransactionInputGetScriptTokensPushDataSet(input), keysAndHashes);
}

int TTTransactionInputsContainsOneOf(TransactionTracker *self, Transaction *tx, Dict keysAndHashes)
{
    FORIN(TransactionInput, input, tx->inputs) {

        if(DictDoesIntersect(TransactionInputGetScriptTokensPushDataSet(input), keysAndHashes))
            return 1;

        if(DictDoesIntersect(TransactionOutputGetScriptTokensPushDataSet(&input->fundingOutput), keysAndHashes))
            return 1;
    }

    return 0;
}

int TTOutputContainsOneOf(TransactionTracker *self, TransactionOutput *output, Dict keysAndHashes)
{
    return DictDoesIntersect(TransactionOutputGetScriptTokensPushDataSet(output), keysAndHashes);
}

int TTTransactionOutputsContainsOneOf(TransactionTracker *self, Transaction *tx, Dict keysAndHashes)
{
    FORIN(TransactionOutput, output, tx->outputs)
        if(DictDoesIntersect(TransactionOutputGetScriptTokensPushDataSet(output), keysAndHashes))
            return 1;

    return 0;
}

int TTTransactionContainsOneOf(TransactionTracker *self, Transaction *tx, Dict keysAndHashes)
{
    if(TTTransactionInputsContainsOneOf(self, tx, keysAndHashes))
        return 1;

    return TTTransactionOutputsContainsOneOf(self, tx, keysAndHashes);
}

static int TTAnyTransactionContainsOneOf(TransactionTracker *self, Dict keysAndHashes)
{
    pthread_mutex_lock(&allTransactionsMutex);

    int result = 0;

    FORIN(Transaction, tx, self->allTransactions) {
        if(TTTransactionContainsOneOf(self, tx, keysAndHashes)) {

            result = 1;
            break;
        }
    }

    pthread_mutex_unlock(&allTransactionsMutex);

    return result;
}

// Checks if this transaction is interesting according to hd wallet look ahead logic.
// Success and failure updates internal failure rate, potentially triggering -bloomFilterNeedsUpdate.
int TTInterestingTransaction(TransactionTracker *self, Transaction *transaction)
{
    return TTTransactionContainsOneOf(self, transaction, TTKeysAndKeyHashes(self));
}

int TTInterestingInput(TransactionTracker *self, TransactionInput *input)
{
    return DictDoesIntersect(TransactionInputGetScriptTokensPushDataSet(input), TTKeysAndKeyHashes(self));
}

int TTInterestingOutput(TransactionTracker *self, TransactionOutput *output)
{
    return DictDoesIntersect(TransactionOutputGetScriptTokensPushDataSet(output), TTKeysAndKeyHashes(self));
}

Datas TTInterestingOutputMatches(TransactionTracker *self, TransactionOutput *output)
{
    return DictAllKeysRef(DictIntersect(TransactionOutputGetScriptTokensPushDataSet(output), TTKeysAndKeyHashes(self)));
}

void resync(TransactionTracker *self)
{
    TTSetBloomFilterDlHeight(self, 0);
    TTSetVaultCount(self, 0);
}

int TTHasTransctionHash(TransactionTracker *self, Data hash)
{
    pthread_mutex_lock(&allTransactionsMutex);

    int result = DatasHasMatchingData(self->allTransactionHashes, hash);

    pthread_mutex_unlock(&allTransactionsMutex);

    return result;
}

static int txCompare(Data left, Data right)
{
    Transaction *lTx = (Transaction *)left.bytes;
    Transaction *rTx = (Transaction *)right.bytes;

    Data lHash = hash256(TransactionData(*lTx));
    Data rHash = hash256(TransactionData(*rTx));

    int32_t result = DatabaseTransactionHeight(&database, lHash) - DatabaseTransactionHeight(&database, rHash);

    if(!result)
        result = DatabaseTransactionTime(&database, lHash) - DatabaseTransactionTime(&database, rHash);

    return result;
}

static void sortTransactions(Datas/*Transaction*/ *transactions)
{
    *transactions = DatasSort(*transactions, txCompare);
}

TransactionAnalyzer TTAnalyzerFor(TransactionTracker *self, Data hdWalletRoot)
{
    Dict items = DictNew();

    Datas array = TTAllTransactionsFor(self, hdWallet(hdWalletRoot, "0"), &items);

    sortTransactions(&array);

    Datas change = TTAllTransactionsFor(self, hdWallet(hdWalletRoot, "1"), &items);

    FORIN(Transaction, transaction, change) {
        if(!DatasSearchCheck(array, DataRaw(*transaction), txCompare)) {

            array = DatasAddRef(array, DataRaw(*transaction));
            sortTransactions(&array);
        }
    }

    return TANew(array, items);
}

TransactionAnalyzer TTAnalyzerForVault(TransactionTracker *self, int vaultIndex)
{
    Dict items = DictNew();

    Datas array = TTAllTransactionsForVault(self, vaultIndex, &items);

    sortTransactions(&array);

    return TANew(array, items);
}

Datas/*Transaction*/ TTAllTransactionsFor(TransactionTracker *self, Data hdWalletRoot, Dict *usedKeysAndHashes)
{
    Datas array = DatasNew();

    int lastUsedAddress = 0;

    for(int i = 0; i < lastUsedAddress + HDWALLET_SCANAHEAD_COUNT; i++) {

        Data hdWalletTmp = hdWallet(hdWalletRoot, StringF("%d", i).bytes);

        Dict items = TTKeysAndKeyHashesForWallet(self, hdWalletTmp);

        if(usedKeysAndHashes)
            DictAddDict(usedKeysAndHashes, items);

        pthread_mutex_lock(&allTransactionsMutex);

        FORIN(Transaction, tx, self->allTransactions) {

            int inputMatch = TTTransactionInputsContainsOneOf(self, tx, items);
            int outputMatch = TTTransactionOutputsContainsOneOf(self, tx, items);

            if((outputMatch || inputMatch) && !DatasHasMatchingData(array, DataRaw(*tx)))
                array = DatasAddRef(array, DataRaw(*tx));

            if(inputMatch || outputMatch)
                lastUsedAddress = i;
        }

        pthread_mutex_unlock(&allTransactionsMutex);
    }

    return array;
}

Datas/*Transaction*/ TTAllTransactionsForVault(TransactionTracker *self, int vaultIndex, Dict *usedKeysAndHashes)
{
    Datas array = DatasNew();

    int lastUsedAddress = 0;

    for(int i = 0; i < lastUsedAddress + HDWALLET_SCANAHEAD_COUNT; i++) {

        Dict items = TTScriptAndHash(self, vaultIndex, i, 0);
        
        if(DictIsNull(items))
            return DatasNull();

        // Add change transactions
        DictAddDict(&items, TTScriptAndHash(self, vaultIndex, i, 1));

        if(usedKeysAndHashes)
            DictAddDict(usedKeysAndHashes, items);

        pthread_mutex_lock(&allTransactionsMutex);

        FORIN(Transaction, tx, self->allTransactions) {

            int inputMatch = TTTransactionInputsContainsOneOf(self, tx, items);
            int outputMatch = TTTransactionOutputsContainsOneOf(self, tx, items);

            if((outputMatch || inputMatch) && !DatasHasMatchingData(array, DataRaw(*tx)))
                array = DatasAddRef(array, DataRaw(*tx));

            if(inputMatch || outputMatch)
                lastUsedAddress = i;
        }

        pthread_mutex_unlock(&allTransactionsMutex);
    }

    return array;
}

Data TTUnusedWallet(TransactionTracker *self, Data hdWalletParam, unsigned int n)
{
    n %= HDWALLET_SCANAHEAD_COUNT;

    int lastUsedAddress = 0;
    int i;

    for(i = 0; i < lastUsedAddress + HDWALLET_SCANAHEAD_COUNT; i++) {

        Data hdWalletTmp = hdWallet(hdWalletParam, StringF("%d", i).bytes);

        Dict items = TTKeysAndKeyHashesForWallet(self, hdWalletTmp);

        int outputMatch = 0;

        pthread_mutex_lock(&allTransactionsMutex);

        FORIN(Transaction, tx, self->allTransactions)
            if((outputMatch = TTTransactionOutputsContainsOneOf(self, tx, items)))
                break;

        pthread_mutex_unlock(&allTransactionsMutex);

        if(outputMatch)
            lastUsedAddress = i + 1;
        else if(i - lastUsedAddress == n)
            return hdWalletTmp;
    }

    return DataNull();
}

String TTVaultPathDerivation(TransactionTracker *self, unsigned int n, int change)
{
    return StringF("%d/%d", change ? 1 : 0, n);
}

Data TTVaultScriptForDerivationPath(TransactionTracker *self, int vaultIndex, String path)
{
    Data masterHdWallet = publicHdWallet(DatasAt(KMVaultMasterHdWallets(&km), vaultIndex));
    Datas hdWallets = DatasDeserialize(DatasAt(KMVaultHdWallets(&km), vaultIndex));

    masterHdWallet = hdWallet(masterHdWallet, path.bytes);

    for(int i = 0; i < hdWallets.count; i++)
        hdWallets = DatasReplaceIndexCopy(hdWallets, i, hdWallet(hdWallets.ptr[i], path.bytes));

    return vaultScript(pubKeyFromHdWallet(masterHdWallet), pubKeysFromHdWallets(hdWallets));
}

static Data TTVaultScript(TransactionTracker *self, int vaultIndex, unsigned int nthUnusedDerivation, int change)
{
    return TTVaultScriptForDerivationPath(self, vaultIndex, TTVaultPathDerivation(self, nthUnusedDerivation, change));
}

Data TTUnusedVaultScript(TransactionTracker *self, int vaultIndex, unsigned int n, int change)
{
    n %= HDWALLET_SCANAHEAD_COUNT;

    int lastUsedAddress = 0;
    int i;

    for(i = 0; i < lastUsedAddress + HDWALLET_SCANAHEAD_COUNT; i++) {

        Dict items = TTScriptAndHash(self, vaultIndex, i, change);

        int outputMatch = 0;

        pthread_mutex_lock(&allTransactionsMutex);

        FORIN(Transaction, tx, self->allTransactions)
            if((outputMatch = TTTransactionOutputsContainsOneOf(self, tx, items)))
                break;

        pthread_mutex_unlock(&allTransactionsMutex);

        if(outputMatch)
            lastUsedAddress = i + 1;
        else if(i - lastUsedAddress == n)
            return TTVaultScript(self, vaultIndex, i, change);
    }

    return DataNull();
}

String TTVaultUuid(TransactionTracker *self, int vaultIndex, Data forPubKey)
{
    int lastUsedAddress = 0;
    
    for(int i = 0; i < lastUsedAddress + HDWALLET_SCANAHEAD_COUNT; i++) {
        
        for(int change = 0; change < 2; change++) {

            Dict items = TTScriptAndHash(self, vaultIndex, i, change);

            pthread_mutex_lock(&allTransactionsMutex);
                
            FORIN(Transaction, tx, self->allTransactions) {

                int outputMatch = TTTransactionOutputsContainsOneOf(self, tx, items);

                if(outputMatch)
                    lastUsedAddress = i;
            }

            pthread_mutex_unlock(&allTransactionsMutex);
            
            String path = TTVaultPathDerivation(self, i, change);

            Datas vaultHdWallet = DatasDeserialize(DatasAt(KMVaultHdWallets(&km), vaultIndex));

            FORDATAIN(hdWalletData, vaultHdWallet)
                if(DataEqual(pubKeyFromHdWallet(hdWallet(*hdWalletData, path.bytes)), forPubKey))
                    return KMUuidFromHDWallet(&km, *hdWalletData);
        }
    }
    
    return DataNull();
}

String TTVaultPathForScriptHash(TransactionTracker *self, int vaultIndex, Data scriptHash)
{
    int lastUsedAddress = 0;
    
    for(int i = 0; i < lastUsedAddress + HDWALLET_SCANAHEAD_COUNT; i++) {
        
        for(int change = 0; change < 2; change++) {

            Dict items = TTScriptAndHash(self, vaultIndex, i, change);

            if(DictHasKey(items, scriptHash))
                return TTVaultPathDerivation(self, i, change);

            pthread_mutex_lock(&allTransactionsMutex);
                
            FORIN(Transaction, tx, self->allTransactions) {

                int outputMatch = TTTransactionOutputsContainsOneOf(self, tx, items);

                if(outputMatch)
                    lastUsedAddress = i;
            }

            pthread_mutex_unlock(&allTransactionsMutex);
        }
    }
    
    return DataNull();
}

Data TTVaultScriptForScriptHash(TransactionTracker *self, int vaultIndex, Data scriptHash)
{
    String path = TTVaultPathForScriptHash(self, vaultIndex, scriptHash);
    
    if(!path.bytes)
        return DataNull();
    
    return TTVaultScriptForDerivationPath(self, vaultIndex, path);
}

Datas/*Data*/ TTAllActiveDerivations(TransactionTracker *self, Data hdWalletData)
{
    Datas result = DatasNew();

    int lastUsedAddress = 0;

    for(int i = 0; i < lastUsedAddress + HDWALLET_SCANAHEAD_COUNT; i++) {

        Data hdWalletTmp = hdWallet(hdWalletData, StringF("%d", i).bytes);

        Dict items = TTKeysAndKeyHashesForWallet(self, hdWalletTmp);

        pthread_mutex_lock(&allTransactionsMutex);

        FORIN(Transaction, tx, self->allTransactions) {

            int outputMatch = TTTransactionOutputsContainsOneOf(self, tx, items);

            if(outputMatch) {

                result = DatasAddCopy(result, hdWalletTmp);
                lastUsedAddress = i;
            }
        }

        pthread_mutex_unlock(&allTransactionsMutex);
    }

    return result;
}

Datas/*Data*/ TTAllVaultActiveDerivations(TransactionTracker *self, int vaultIndex)
{
    Datas result = DatasNew();

    int lastUsedAddress = 0;

    for(int i = 0; i < lastUsedAddress + HDWALLET_SCANAHEAD_COUNT; i++) {

        for(int change = 0; change < 2; change++) {

            Dict items = TTScriptAndHash(self, vaultIndex, i, change);

            pthread_mutex_lock(&allTransactionsMutex);

            FORIN(Transaction, tx, self->allTransactions) {

                int outputMatch = TTTransactionOutputsContainsOneOf(self, tx, items);

                if(outputMatch) {

                    Data key = DatasSerialize(DatasThreeCopy(DataInt(vaultIndex), DataInt(i), DataInt(change)));

                    pthread_mutex_lock(&masterHdWalletCacheMutex);

                    Data item = DictGet(self->masterHdWalletCache, key);

                    pthread_mutex_unlock(&masterHdWalletCacheMutex);

                    if(!item.bytes) {

                        Data masterHdWallet = DatasAt(KMVaultMasterHdWallets(&km), vaultIndex);

                        item = hdWallet(masterHdWallet, StringF("%d/%d", change, i).bytes);

                        pthread_mutex_lock(&masterHdWalletCacheMutex);

                        DictAdd(&self->masterHdWalletCache, key, item);

                        pthread_mutex_unlock(&masterHdWalletCacheMutex);
                    }

                    result = DatasAddCopy(result, item);
                    lastUsedAddress = i;
                }
            }

            pthread_mutex_unlock(&allTransactionsMutex);
        }
    }

    return result;
}

Dict/*Data:DataNull*/ TTKeysAndHashesFromHdWallets(TransactionTracker *self, Datas/*Data*/ hdWallets)
{
    Dict set = DictNew();

    FORDATAIN(hdWalletData, hdWallets)
        DictAddDict(&set, TTKeysAndKeyHashesForWallet(self, *hdWalletData));

    return set;
}

Transaction *TTTransactionForHash(TransactionTracker *self, Data hash)
{
    pthread_mutex_lock(&allTransactionsMutex);

    int index = DatasMatchingDataIndex(self->allTransactionHashes, hash);

    Transaction *result = NULL;

    if(index >= 0)
        result = (Transaction *)self->allTransactions.ptr[index].bytes;

    pthread_mutex_unlock(&allTransactionsMutex);

    return result;
}

Transaction *TTTransactionForTxid(TransactionTracker *self, Data txid)
{
    pthread_mutex_lock(&allTransactionsMutex);

    int index = DatasMatchingDataIndex(self->allTransactionTxids, txid);

    Transaction *result = NULL;

    if(index >= 0)
        result = (Transaction *)self->allTransactions.ptr[index].bytes;

    pthread_mutex_unlock(&allTransactionsMutex);

    return result;
}

int TTAddTransaction(TransactionTracker *self, Data data)
{
    Transaction transaction = TransactionNew(data);

    FORIN(TransactionInput, input, transaction.inputs) {

        TransactionOutput *output = TransactionOutputOrNilAt(TTTransactionForTxid(self, input->previousTransactionHash), input->outputIndex);

        if(output)
            input->fundingOutput = *output;
    }

    int interesting = TTInterestingTransaction(self, &transaction);

    Data hash = hash256(data);

    if(DatasHasMatchingData(TTMissingFundingTransactions(self), TransactionTxid(transaction)))
        interesting = 1;

    if(!interesting) {

        self->mismatchCount = self->mismatchCount + 1;
        return -2;
    }

    pthread_mutex_lock(&allTransactionsMutex);

    if(DatasHasMatchingData(self->allTransactionHashes, hash) || DatasHasMatchingData(self->allTransactionTxids, hash)) {

        pthread_mutex_unlock(&allTransactionsMutex);
        return -1;
    }

    self->matchCount = self->matchCount + 1;

    TransactionUntrack(&transaction);

    self->allTransactions = DatasUntrack(DatasAddCopy(self->allTransactions, DataRaw(transaction)));
    self->allTransactionHashes = DatasUntrack(DatasAddCopy(self->allTransactionHashes, hash));
    self->allTransactionTxids = DatasUntrack(DatasAddCopy(self->allTransactionTxids, TransactionTxid(transaction)));

    pthread_mutex_unlock(&allTransactionsMutex);

    NotificationsFire(TransactionTrackerTransactionAdded, DictNew());

    return 1;
}

int TTRemoveTransactionByHash(TransactionTracker *self, Data hash)
{
    if(!hash.length)
        return 0;
    
    int result = 0;
    
    pthread_mutex_lock(&allTransactionsMutex);

    for(long i = 0; i < self->allTransactionHashes.count; i++) {

        if(DataEqual(self->allTransactionHashes.ptr[i], hash)) {

            TransactionTrack((Transaction*)self->allTransactions.ptr[i].bytes);

            self->allTransactionTxids = DatasRemoveIndex(self->allTransactionTxids, (int32_t)i);
            self->allTransactionHashes = DatasRemoveIndex(self->allTransactionHashes, (int32_t)i);
            self->allTransactions = DatasRemoveIndex(self->allTransactions, (int32_t)i);

            i--;
            result++;
        }
    }

    pthread_mutex_unlock(&allTransactionsMutex);
    
    return result;
}

int TTRemoveTransactionByTxid(TransactionTracker *self, Data txid)
{
    if(!txid.length)
        return 0;
    
    int result = 0;

    pthread_mutex_lock(&allTransactionsMutex);

    for(long i = 0; i < self->allTransactionTxids.count; i++) {

        if(DataEqual(self->allTransactionTxids.ptr[i], txid)) {

            TransactionTrack((Transaction*)self->allTransactions.ptr[i].bytes);

            self->allTransactionTxids = DatasRemoveIndex(self->allTransactionTxids, (int32_t)i);
            self->allTransactionHashes = DatasRemoveIndex(self->allTransactionHashes, (int32_t)i);
            self->allTransactions = DatasRemoveIndex(self->allTransactions, (int32_t)i);

            i--;
            result++;
        }
    }

    pthread_mutex_unlock(&allTransactionsMutex);

    return result;
}

Datas/*Data*/ TTMissingFundingTransactions(TransactionTracker *self)
{
    Datas result = DatasNew();

    pthread_mutex_lock(&allTransactionsMutex);

    FORIN(Transaction, transaction, self->allTransactions) {

        if(!TTInterestingTransaction(self, transaction))
            continue;

        FORIN(TransactionInput, input, transaction->inputs) {

            if(!DatasHasMatchingData(self->allTransactionTxids, input->previousTransactionHash))
                result = DatasAddCopy(result, input->previousTransactionHash);
        }
    }

    pthread_mutex_unlock(&allTransactionsMutex);

    return result;
}

Datas/*Data*/ TTInterestingTransactionHashes(TransactionTracker *self)
{
    Datas result = DatasNew();

    pthread_mutex_lock(&allTransactionsMutex);

    FORIN(Transaction, tx, self->allTransactions)
        if(TTInterestingTransaction(self, tx))
            result = DatasAddCopy(result, hash256(TransactionData(*tx)));

    pthread_mutex_unlock(&allTransactionsMutex);

    return result;
}

uint64_t TTCalculateTransactionFee(TransactionTracker *self, Data data)
{
    Transaction transaction = TransactionNew(data);

    FORIN(TransactionInput, input, transaction.inputs) {

        Transaction *funding = TTTransactionForTxid(self, input->previousTransactionHash);

        if(!funding)
            return 0;

        if(input->outputIndex >= funding->outputs.count)
            return 0;

        input->fundingOutput = *(TransactionOutput*)funding->outputs.ptr[input->outputIndex].bytes;
    }

    return TransactionUsableValue(transaction) - TransactionUsedValue(transaction);
}

float TTFailureRate(TransactionTracker *self)
{
    int count = self->matchCount + self->mismatchCount;

    if(!count)
        return 0;

    return (float)self->mismatchCount / count;
}

int TTFailureRateTooHigh(TransactionTracker *self)
{
    int count = self->matchCount + self->mismatchCount;

    if(count < BLOOM_MIN_COUNT)
        return 0;

    return (float)self->mismatchCount / count > BLOOM_MAX_FAILRATE;
}

void TTResetFailureRate(TransactionTracker *self)
{
    self->matchCount = 0;
    self->mismatchCount = 0;
}

int TTMatchCount(TransactionTracker *self)
{
    return self->matchCount;
}

int TTMismatchCount(TransactionTracker *self)
{
    return self->mismatchCount;
}

int TTBloomFilterNeedsUpdate(TransactionTracker *self)
{
    if(!TTBloomFilter(self).length)
        return 1;

    if(KMVaultNames(&km).count != TTVaultCount(self))
        return 1;
    
    // TODO Check actual vaults instead of the vault count

    Datas allHdWallets = TTAllHdWallets(self);

    if(!DatasEqual(allHdWallets, KMAllHdWalletPubRoots(&km)))
        return 1;

    FORDATAIN(hdWalletData, allHdWallets)
        if(TTUnusedAddresses(self, *hdWalletData) < HDWALLET_SCANAHEAD_COUNT)
            return 1;

#if 0
    FORDATAIN(data, TTMissingFundingTransactions(self))
        if(!bloomFilterCheckElement(TTBloomFilter(self), *data))
            return 1;
#endif

    return 0;
}

static Dict buildKeysAndKeyHashes(TransactionTracker *self)
{
    Dict set = DictNew();

    Datas allHdWallets = TTAllHdWallets(self);
    Datas/*Int*/ lookAheadCount = TTLookAheadCount(self);
    Datas/*Int*/ vaultLookAheadCount = TTVaultLookAheadCount(self);

    Datas/*Data*/ allVaultMasterHdWallets = KMVaultMasterHdWallets(&km);
    Datas/*Datas*/ vaultHdWallets = KMVaultHdWallets(&km);

    for(int i = 0; i < allHdWallets.count; i++)
        for(int j = 0; j < DataGetInt(lookAheadCount.ptr[i]); j++)
            DictAddDict(&set, TTKeysAndKeyHashesForWallet(self, hdWallet(allHdWallets.ptr[i], StringF("%d", j).bytes)));

    for(int i = 0; i < TTVaultCount(self); i++) {

        Data masterHdWallet = DatasAt(allVaultMasterHdWallets, i);
        Datas hdWallets = DatasDeserialize(DatasAt(vaultHdWallets, i));

        for(int j = 0; j < DataGetInt(vaultLookAheadCount.ptr[i]); j++) {

            FORDATAIN(hdWalletData, DatasDeserialize(DatasAt(hdWallets, i))) {

                DictAddDict(&set, TTKeysAndKeyHashesForWallet(self, hdWallet(*hdWalletData, StringF("0/%d", j).bytes)));
                DictAddDict(&set, TTKeysAndKeyHashesForWallet(self, hdWallet(*hdWalletData, StringF("1/%d", j).bytes)));
            }

            DictAddDict(&set, TTScriptAndHashFlexible(self, i, j, 0, masterHdWallet, hdWallets));
            DictAddDict(&set, TTScriptAndHashFlexible(self, i, j, 1, masterHdWallet, hdWallets));
        }
    }

    Datas missing = TTMissingFundingTransactions(self);

    FORDATAIN(data, missing)
        DictAdd(&set, *data, DataNull());

    // Okay put on your seatbelt, we're going for an intellectual ride.
    //
    // The bloom filter technique can only properly detect transactions
    // where an output is going to us. If we send a transaction that has
    // no change coming back to us -- we are kinda screwed.
    //
    // So, to handle this case, all transactions with just one output we
    // assume are this kind. That assumption is mostly correct.
    //
    // When that happens, we add the txid of the transaction itself to the
    // bloom filter.
    //
    // I don't have the link handy right now, but there is a mailing list
    // post from long ago discussing this issue. I think it was written
    // by Greg Maxwell or maybe Luke Jr. If you find it while working on this
    // code -- please add the link.

    pthread_mutex_lock(&allTransactionsMutex);

    FORIN(Transaction, trans, self->allTransactions)
        if(trans->outputs.count == 1)
            DictAdd(&set, TransactionTxid(*trans), DataNull());

    pthread_mutex_unlock(&allTransactionsMutex);

    return set;
}

void TTUpdateBloomFilter(TransactionTracker *self)
{
    TTSetAllHdWallets(self, KMAllHdWalletPubRoots(&km));
    TTSetVaultCount(self, KMVaultNames(&km).count);

    pthread_mutex_lock(&scriptAndHashCacheMutex);

    DictTrack(self->scriptAndHashCache);
    self->scriptAndHashCache = DictUntrack(DictNew());

    pthread_mutex_unlock(&scriptAndHashCacheMutex);

    pthread_mutex_lock(&masterHdWalletCacheMutex);

    DictTrack(self->masterHdWalletCache);
    self->masterHdWalletCache = DictUntrack(DictNew());

    pthread_mutex_unlock(&masterHdWalletCacheMutex);

    Datas lookAheadArray = DatasNew();

    Datas allHdWallets = TTAllHdWallets(self);
    
    FORDATAIN(hdWalletData, allHdWallets)
        lookAheadArray = DatasAddCopy(lookAheadArray, DataInt(TTLastUsedAddress(self, *hdWalletData) + TransactionTrackerBloomaheadCount));

    TTSetLookAheadCount(self, lookAheadArray);

    Datas vaultLookAheadArray = DatasNew();

    for(int i = 0; i < TTVaultCount(self); i++)
        vaultLookAheadArray = DatasAddCopy(vaultLookAheadArray, DataInt(TTLastUsedVaultAddress(self, i) + TransactionTrackerBloomaheadCount));

    TTSetVaultLookAheadCount(self, vaultLookAheadArray);
    
    TTSetKeysAndKeyHashes(self, buildKeysAndKeyHashes(self));

    TTSetBloomFilter(self, bloomFilterArray(DictAllKeysRef(TTKeysAndKeyHashes(self)), BLOOM_FAILRATE, BLOOM_UPDATE_ALL));
}

void TTTempBloomFilterAdd(TransactionTracker *self, Data element)
{
    Data bloomFilter = TTBloomFilter(self);

    bloomFilterAddElement(bloomFilter, element);

    TTSetBloomFilter(self, bloomFilter);
}

Data TTPubKeyOrScriptForKnownHash(TransactionTracker *self, Data hash)
{
    Dict dict = TTKeysAndKeyHashes(self);

    return DictGet(dict, hash);
}
