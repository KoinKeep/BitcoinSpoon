//
//  TransactionAnalyzer.h
//  KoinKeep
//
//  Created by Dustin Dettmer on 12/29/18.
//  Copyright © 2018 Dustin. All rights reserved.
//

#ifndef TRANSACTIONANALYZER_H
#define TRANSACTIONANALYZER_H

#include "Data.h"
#include "Transaction.h"

typedef enum {
    TAEventTypeDeposit = 1,
    TAEventTypeWithdrawl = 2,
    TAEventTypeChange = 4,
    TAEventTypeUnspent = 8,
    TAEventTypeTransfer = 16,
    TAEventTypeFee = 32,
    TAEventBalanceMask = TAEventTypeDeposit | TAEventTypeWithdrawl,
    TAEventChangeMask = TAEventTypeChange,
    TAEventAllMask = TAEventBalanceMask | TAEventChangeMask | TAEventTypeUnspent,
} TAEventType;

typedef struct TAEvent {

    TAEventType type;
    uint64_t amount;

    Transaction *transaction;
    uint32_t outputIndex;

} TAEvent;

typedef struct TAPaymentCandidate {

    int valid;

    Datas/*TAEvent*/ events;
    uint64_t amount;

} TAPaymentCandidate;

typedef struct TransactionAnalyzer {

    Datas/*Transaction*/ transactions;
    Dict keysAndHashes;

    Datas/*TAEvent*/ eventsCache;

} TransactionAnalyzer;

typedef TransactionAnalyzer TA;

TransactionAnalyzer TANew(Datas/*Transaction*/ transactions, Dict keysAndHashes);

void TATrack(TA *ta);
void TAUntrack(TA *ta);

uint64_t TATotalBalance(TA *self);

Datas/*TAEvent*/ TAEvents(TA *self, TAEventType typeMask);

TransactionAnalyzer TAAnalyzerForTransactionsMatching(TA *self, int (*chooser)(Transaction *trans));

uint64_t TATotalAmount(Datas/*TAEvent*/ events);

// Searches through 'events' in reverse order
TAPaymentCandidate TAPaymentCandidateSearch(uint64_t amount, Datas/*TAEvent*/ eventSet);

uint64_t TAPaymentCandidateRemainder(TAPaymentCandidate *event);

#endif
