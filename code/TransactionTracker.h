//
//  TransactionTracker.h
//  KoinKeep
//
//  Created by Dustin Dettmer on 12/27/18.
//  Copyright © 2018 Dustin. All rights reserved.
//

#ifndef TRANSACTIONTRACKER_H
#define TRANSACTIONTRACKER_H

#include "Data.h"
#include "Transaction.h"
#import "TransactionAnalyzer.h"

extern const char *TransactionTrackerTransactionAdded;
extern int TransactionTrackerBloomaheadCount; // If setting this, set before TransactionTracker creation.

// Uses KMAllHdWalletPubRoots to generate n (probably 100) lookahead
typedef struct TransactionTracker {

    Data bloomFiler;

    // 'updateBloomFilter' resets this to 0. Increment it whenever a block is downloaded that is
    // 1 below this value.
    int32_t bloomFilterDlHeight;

    /** Private **/

    // Optimization potential: sort these arrays by the same criteria or make an index
    Datas/*Transaction*/ allTransactions;
    Datas/*Data*/ allTransactionHashes;
    Datas/*Data*/ allTransactionTxids;

    Dict/*Data:DataNull*/ keysAndKeyHashes; // Stored in Dict keys for search speed

    Dict scriptAndHashCache;
    Dict masterHdWalletCache;

    int matchCount;
    int mismatchCount;

    int testnet;

} TransactionTracker;

extern TransactionTracker tracker;

// Database must be reset *before* this when reseting testnet
// Data is all untracked
TransactionTracker TTNew(int testnet);

// Takes a TransactionTracker and tracks it
void TTTrack(TransactionTracker *self);

// TODO
//void TTResync(TransactionTracker *self);

Data TTBloomFilter(TransactionTracker *self);

int TTHasTransctionHash(TransactionTracker *self, Data hash);

int32_t TTBloomFilterDlHeight(TransactionTracker *self);
void TTSetBloomFilterDlHeight(TransactionTracker *self, int32_t param);

Transaction *TTTransactionForHash(TransactionTracker *self, Data hash);
Transaction *TTTransactionForTxid(TransactionTracker *self, Data txid);

TransactionAnalyzer TTAnalyzerFor(TransactionTracker *self, Data hdWalletRoot);
TransactionAnalyzer TTAnalyzerForVault(TransactionTracker *self, int vaultIndex);

Datas/*Transaction*/ TTAllTransactionsFor(TransactionTracker *self, Data hdWalletRoot, Dict *usedKeysAndHashes);
Datas/*Transaction*/ TTAllTransactionsForVault(TransactionTracker *self, int vaultIndex, Dict *usedKeysAndHashes);

// if n >= HDWALLET_SCANAHEAD_COUNT it wraps around back to 0.
Data TTUnusedWallet(TransactionTracker *self, Data hdWallet, unsigned int nthUnusedDerivation);
Data TTUnusedVaultScript(TransactionTracker *self, int vaultIndex, unsigned int nthUnusedDerivation, int change);

String TTVaultUuid(TransactionTracker *self, int vaultIndex, Data forPubKey);

Data TTVaultScriptForScriptHash(TransactionTracker *self, int vaultIndex, Data scriptHash);
String TTVaultPathForScriptHash(TransactionTracker *self, int vaultIndex, Data scriptHash); // Contains no preceding slash

Data TTVaultScriptForDerivationPath(TransactionTracker *self, int vaultIndex, String derivationPath);

Datas/*Data*/ TTAllActiveDerivations(TransactionTracker *self, Data hdWallet);
Datas/*Data*/ TTAllVaultActiveDerivations(TransactionTracker *self, int vaultIndex);
Dict/*Data:DataNull*/ TTKeysAndHashesFromHdWallets(TransactionTracker *self, Datas/*Data*/ hdWallets);

int TTInputContainsOneOf(TransactionTracker *self, TransactionInput *input, Dict/*Data:DataNull*/ keysAndHashes);
int TTOutputContainsOneOf(TransactionTracker *self, TransactionOutput *output, Dict/*Data:DataNull*/ keysAndHashes);

// Returns -1 if transaction is already known, returns -2 if it is not "interesting" according to hdwallet look ahead logic.
// Returns 1 if the transction was saved.
// Success and failure updates internal failure rate, potentially triggering -bloomFilterNeedsUpdate.
// NOTE: if interested, a copy of transaction is stored.
int TTAddTransaction(TransactionTracker *self, Data transaction);
int TTRemoveTransactionByHash(TransactionTracker *self, Data hash);
int TTRemoveTransactionByTxid(TransactionTracker *self, Data txid);

Datas/*Data*/ TTMissingFundingTransactions(TransactionTracker *self);

// This method is fundamentally insecure since this interface is thread safe without making copies of Transaction objects.
// Unimplemented for now.
// Datas/*DataPtr(Transaction)*/ TTInterestingTransactions(TransactionTracker *self);

Datas/*Data*/ TTInterestingTransactionHashes(TransactionTracker *self);

int TTInterestingTransaction(TransactionTracker *self, Transaction *transaciton);

// Does not update failure rate.
int TTInterestingInput(TransactionTracker *self, TransactionInput *input);
int TTInterestingOutput(TransactionTracker *self, TransactionOutput *output);

Datas TTInterestingOutputMatches(TransactionTracker *self, TransactionOutput *output);

uint64_t TTCalculateTransactionFee(TransactionTracker *self, Data transaction);

float TTFailureRate(TransactionTracker *self);
int TTFailureRateTooHigh(TransactionTracker *self);
void TTResetFailureRate(TransactionTracker *self);

int TTMatchCount(TransactionTracker *self);
int TTMismatchCount(TransactionTracker *self);

int TTBloomFilterNeedsUpdate(TransactionTracker *self);
void TTUpdateBloomFilter(TransactionTracker *self);
void TTTempBloomFilterAdd(TransactionTracker *self, Data element);

Data TTPubKeyOrScriptForKnownHash(TransactionTracker *self, Data hash);

Dict TTKeysAndKeyHashes(TransactionTracker *self);

#endif
