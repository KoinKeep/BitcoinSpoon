//
//  TransactionAnalyzer.m
//  KoinKeep
//
//  Created by Dustin Dettmer on 12/29/18.
//  Copyright © 2018 Dustin. All rights reserved.
//

#include "TransactionAnalyzer.h"
#include "BTCUtil.h"
#include "Database.h"
#include "BasicStorage.h"

#define DUST_LIMIT 10000

TransactionAnalyzer TANew(Datas/*Transaction*/ transactions, Dict keysAndHashes)
{
    TransactionAnalyzer instance = {0};

    Datas array = DatasNew();

    FORIN(Transaction, trans, transactions) {
        Transaction txCopy = TransactionCopy(*trans);
        array = DatasAddCopy(array, DataRaw(txCopy));
    }

    instance.transactions = array;
    instance.keysAndHashes = DictCopy(keysAndHashes);
    instance.eventsCache = DatasNew();

    TAUntrack(&instance);

    return instance;
}

void TATrack(TA *self)
{
    FORIN(Transaction, trans, self->transactions)
        TransactionTrack(trans);

    FORIN(Datas, array, self->eventsCache)
        DatasTrack(*array);

    DatasTrack(self->transactions);
    DictTrack(self->keysAndHashes);
    DatasTrack(self->eventsCache);
}

void TAUntrack(TA *self)
{
    FORIN(Transaction, trans, self->transactions)
        TransactionUntrack(trans);

    FORIN(Datas, array, self->eventsCache)
        DatasUntrack(*array);

    DatasUntrack(self->transactions);
    DictUntrack(self->keysAndHashes);
    DatasUntrack(self->eventsCache);
}

static int scriptDataMatches(TA *self, Data scriptData)
{
    if(!scriptData.length)
        return 0;

    Datas tokens = scriptToTokensUnsafe(scriptData);

    FORIN(ScriptToken, token, tokens)
        if(token->data.length && DictHasKey(self->keysAndHashes, token->data))
            return 1;

    return 0;
}

static Transaction *transactionForTxid(TA *self, Data txid)
{
    FORIN(Transaction, trans, self->transactions)
        if(DataEqual(TransactionTxid(*trans), txid))
            return trans;

    return NULL;
}

static TransactionInput *inputReferencing(TA *self, Transaction *fromTransaction, uint32_t index)
{
    Data txid = TransactionTxid(*fromTransaction);

    FORIN(Transaction, transaction, self->transactions)
        FORIN(TransactionInput, input, transaction->inputs)
            if(DataEqual(input->previousTransactionHash, txid) && input->outputIndex == index)
                return input;

    return NULL;
}

TransactionAnalyzer TAAnalyzerForTransactionsMatching(TA *self, int (*chooser)(Transaction *trans))
{
    Datas transactions = DatasNew();

    FORIN(Transaction, trans, self->transactions)
        if(chooser(trans))
            transactions = DatasAddCopy(transactions, DataRaw(*trans));

    return TANew(transactions, self->keysAndHashes);
}

uint64_t TATotalAmount(Datas/*TAEvent*/ events)
{
    uint64_t amount = 0;

    FORIN(TAEvent, event, events)
        amount += event->amount;

    return amount;
}

uint64_t TATotalBalance(TA *self)
{
    return TATotalAmount(TAEvents(self, TAEventTypeDeposit)) - TATotalAmount(TAEvents(self, TAEventTypeWithdrawl));
}

static Datas allEvents(TA *self)
{
    if(self->eventsCache.count)
        return self->eventsCache;

    Datas/*TAEvent*/ array = DatasNew();

    FORIN(Transaction, trans, self->transactions) {

        int selfFundedInputs = 0;

        FORIN(TransactionInput, input, trans->inputs) {

            Transaction *otherTrans = transactionForTxid(self, input->previousTransactionHash);

            int index = input->outputIndex;

            if(index < otherTrans->outputs.count && scriptDataMatches(self, TransactionOutputAt(otherTrans, index)->script))
                selfFundedInputs++;
            else if(DictionaryDoesIntersect(self->keysAndHashes, TransactionInputGetScriptTokensPushDataSet(input)))
                selfFundedInputs++;
            else if(DictionaryDoesIntersect(self->keysAndHashes, TransactionOutputGetScriptTokensPushDataSet(&input->fundingOutput)))
                selfFundedInputs++;
        }

        Datas/*TAEvent*/ newEvents = DatasNew();

        int changeAndDepositCount = 0;
        uint32_t index = 0;

        FORIN(TransactionOutput, output, trans->outputs) {

            TAEvent event = {0};

            event.transaction = trans;
            event.amount = output->value;
            event.outputIndex = index;
            event.type = 0;

            if(scriptDataMatches(self, output->script)) {

                changeAndDepositCount++;

                // For now we assume any self funded inputs means we sent this tx.
                if(selfFundedInputs)
                    event.type = TAEventTypeChange | TAEventTypeUnspent;
                else
                    event.type = TAEventTypeDeposit | TAEventTypeUnspent;
            }
            else if(selfFundedInputs)
                event.type = TAEventTypeWithdrawl;

            if(inputReferencing(self, trans, index))
                event.type = event.type & ~TAEventTypeUnspent;

            if(event.type)
                newEvents = DatasAddCopy(newEvents, DataRaw(event));

            index++;
        }

        if(changeAndDepositCount == newEvents.count && selfFundedInputs) {

            // If this is soley an internal transfer, we'll remove all events
            // and replace them with a single "transfer" type event using a copy
            // of the first one.

            TAEvent event = *(TAEvent*)DatasFirst(newEvents).bytes;

            event.type |= TAEventTypeTransfer;

            newEvents = DatasOneCopy(DataRaw(event));
        }
        else if(selfFundedInputs) {

            TAEvent event = {0};

            event.transaction = trans;
            event.amount = 0;
            event.outputIndex = 0;
            event.type = TAEventTypeFee;

            newEvents = DatasAddCopy(newEvents, DataRaw(event));
        }

        array = DatasAddCopy(array, DataDatas(newEvents));
    }

    DatasTrack(self->eventsCache);
    self->eventsCache = DatasUntrack(array);

    return self->eventsCache;
}

Datas/*TAEvent*/ TAEvents(TA *self, TAEventType typeMask)
{
    Datas/*TAEvent*/ result = DatasNew();

    Datas events = allEvents(self);

    FORIN(TAEvent, event, events)
        if(event->type & typeMask)
            result = DatasAddCopy(result, DataRaw(*event));

    return result;
}

TAPaymentCandidate TAPaymentCandidateSearch(uint64_t amount, Datas/*TAEvent*/ events)
{
    if(amount == 0 || !events.count)
        return (TAPaymentCandidate){0};

    TAEvent *candidate = NULL;
    uint64_t extra = UINT64_MAX;

    for(int i = 0; i < 2 && !candidate; i++) {

        for(int j = events.count = 1; j >= 0; j--) {

            TAEvent *event = (TAEvent*)events.ptr[j].bytes;

            if(event->amount >= amount && event->amount - amount < extra) {

                if(i || event->amount - amount > DUST_LIMIT || event->amount - amount == 0) {

                    candidate = event;
                    extra = event->amount - amount;
                }
            }
        }
    }

    if(candidate->amount >= amount) {

        TAPaymentCandidate result = {0};

        result.valid = 1;

        result.events = DatasOneCopy(DataRaw(*candidate));

        result.amount = amount;

        return result;
    }

    if(!candidate)
        candidate = (TAEvent*)DatasRandom(events).bytes;

    Datas/*TAEvent*/ subEvents = DatasNew();

    FORIN(TAEvent, event, events)
        if(event != candidate)
            subEvents = DatasAddCopy(subEvents, DataRaw(*event));

    TAPaymentCandidate result = TAPaymentCandidateSearch(amount - candidate->amount, subEvents);

    result.events = DatasAddCopy(result.events, DataRaw(*candidate));

    result.amount = amount;

    return result;
}

uint64_t TAPaymentCandidateRemainder(TAPaymentCandidate *self)
{
    if(!self->events.count)
        return 0;

    return TATotalAmount(self->events) - self->amount;
}
