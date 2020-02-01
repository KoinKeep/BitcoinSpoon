//
//  Transaction.m
//  KoinKeep
//
//  Created by Dustin Dettmer on 9/20/18.
//  Copyright © 2018 Dustin. All rights reserved.
//

#include "Transaction.h"
#include "BTCUtil.h"
#include <stdlib.h>

#ifndef BTCUtilAssert
#define BTCUtilAssert(...)
#endif

#pragma mark -- Internal Methods

static int sortSignatures(Transaction *transaction);
static Datas TransactionCorrectMultisigSignaturesInStack(Datas stack);
static Transaction TransactionCorrectMultisigSignatures(Transaction transaction);
static int TransactionHasWitnessData(Transaction transaction);

#pragma mark -- Transaction

Transaction TransactionEmpty()
{
    Transaction inst = {0};

    inst.version = 2;
    inst.locktime = 0;

    if(inst.inputs.ptr || inst.outputs.ptr)
        abort();

    return inst;
}

void TransactionTrack(Transaction *self)
{
    FORIN(TransactionInput, input, self->inputs) {

        DataTrack(input->fundingOutput.script);
        DictTrack(input->fundingOutput.scriptTokensPushDataSet);

        DataTrack(input->previousTransactionHash);
        DataTrack(input->scriptData);
        DatasTrack(input->witnessStack);

        DictTrack(input->scriptTokensPushDataSet);
    }

    FORIN(TransactionOutput, output, self->outputs) {

        DataTrack(output->script);
        DictTrack(output->scriptTokensPushDataSet);
    }

    DatasTrack(self->inputs);
    DatasTrack(self->outputs);
}

void TransactionUntrack(Transaction *self)
{
    FORIN(TransactionInput, input, self->inputs) {

        DataUntrack(input->fundingOutput.script);
        DictUntrack(input->fundingOutput.scriptTokensPushDataSet);

        DataUntrack(input->previousTransactionHash);
        DataUntrack(input->scriptData);
        DatasUntrack(input->witnessStack);

        DictUntrack(input->scriptTokensPushDataSet);
    }

    FORIN(TransactionOutput, output, self->outputs) {

        DataUntrack(output->script);
        DictUntrack(output->scriptTokensPushDataSet);
    }

    DatasUntrack(self->inputs);
    DatasUntrack(self->outputs);
}

Transaction TransactionNew(Data data)
{
    Transaction inst = TransactionEmpty();

    const uint8_t *ptr = (void*)data.bytes;
    const uint8_t *end = ptr + data.length;

    inst.version = uint32readP(&ptr, end);

    int hasWitness = (end - ptr > 1 && !ptr[0] && ptr[1]);

    if(hasWitness)
        ptr += 2;

    Datas inputs = DatasNew();

    for(uint64_t i = 0, count = readVarInt(&ptr, end); i < count; i++) {

        TransactionInput input = { 0 };

        input.previousTransactionHash = readBytes(32, &ptr, end);

        input.outputIndex = uint32readP(&ptr, end);

        input.scriptData = readBytes(readVarInt(&ptr, end), &ptr, end);

        input.sequence = uint32readP(&ptr, end);

        inputs = DatasAddCopy(inputs, DataRef((void*)&input, sizeof(input)));
    }

    inst.inputs = inputs;

    Datas outputs = DatasNew();

    for(uint64_t i = 0, count = readVarInt(&ptr, end); i < count; i++) {

        TransactionOutput output = { 0 };

        output.value = uint64readP(&ptr, end);

        output.script = readBytes(readVarInt(&ptr, end), &ptr, end);

        outputs = DatasAddCopy(outputs, DataRef((void*)&output, sizeof(output)));
    }

    inst.outputs = outputs;

    if(hasWitness) {

        for(int i = 0; i < inst.inputs.count; i++) {

            TransactionInput *input = (TransactionInput*)inst.inputs.ptr[i].bytes;

            Datas witnessStack = DatasNew();

            for(uint64_t i = 0, count = readVarInt(&ptr, end); i < count; i++) {

                Data item = readBytes(readVarInt(&ptr, end), &ptr, end);

                witnessStack = DatasAddCopy(witnessStack, item);
            }

            input->witnessStack = witnessStack;

            // Calculate witness flag as per
            // https://github.com/bitcoin/bips/blob/master/bip-0141.mediawiki#witness-program

            if(input->scriptData.length > 3) {

                const uint8_t *ptr = (uint8_t*)input->scriptData.bytes;

                // A scriptPubKey (or redeemScript as defined in BIP16/P2SH) that consists of a 1-byte push opcode (for 0 to 16) followed by a data push between 2 and 40 bytes gets a new special meaning. The value of the first push is called the "version byte". The following byte vector pushed is called the "witness program".

                uint8_t length = ptr[0];
                uint8_t witnessVersion = ptr[1];
                uint8_t pushSize = ptr[2];

                if(length > 2 && length < 40 && !witnessVersion) {

                    if(pushSize == 0x20)
                        input->witnessFlag = OP_P2WSH;
                    else if(pushSize == 0x14)
                        input->witnessFlag = OP_P2WPKH;
                    else
                        input->witnessFlag = OP_ERROR;
                }
            }

            if(!input->scriptData.length) {

                // Triggered by a scriptPubKey that is exactly a push of a version byte, plus a push of a witness program. The scriptSig must be exactly empty or validation fails. ("native witness program")

                if(witnessStack.count == 2)
                    input->witnessFlag = OP_P2WPKH;
                else
                    input->witnessFlag = OP_P2WSH;

            }
        }
    }

    inst.locktime = uint32readP(&ptr, end);

    if(ptr != end)
        abort();

    return inst;
}

Transaction TransactionCopy(Transaction transaction)
{
    Transaction result = TransactionNew(TransactionData(transaction));

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *inputNew = (TransactionInput*)result.inputs.ptr[i].bytes;
        TransactionInput *inputOld = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        inputNew->fundingOutput = inputOld->fundingOutput;

        inputNew->fundingOutput.script = DataCopyData(inputNew->fundingOutput.script);
        inputNew->scriptTokensPushDataSet = DictNew();
    }

    return result;
}

String TransactionDescription(Transaction tx)
{
    String str = StringNew("");

    str = StringAddF(str, "Ver: %u, locktime: %u, value: %llu", tx.version, tx.locktime, TransactionUsedValue(tx));

    if(TransactionUsableValue(tx))
        str = StringAddF(str, ", fee: %llu", TransactionUsableValue(tx) - TransactionUsedValue(tx));

    str = StringAddF(str, "\n");

    int counter = 0;

    FORIN(TransactionInput, input, tx.inputs) {

        str = StringAddF(str, "[Input %d]\n", ++counter);
        str = StringAddF(str, "\tPrevious Transaction Hash: [%s]\n", toHex(DataFlipEndianCopy(input->previousTransactionHash)).bytes);
        str = StringAddF(str, "\tOutput Index: %u\n", input->outputIndex);

        if(input->witnessStack.count > 2) {

            str = StringAddF(str, "\tWitness Stack: ");

            for(int i = 0; i < input->witnessStack.count - 1; i++)
                str = StringAddF(str, "%s[%s]", i ? ", " : "", toHex(input->witnessStack.ptr[i]).bytes);

            str = StringAddF(str, "\n");

            str = StringAddF(str, "\tWitness Script: %s\n", scriptToString(DatasLast(input->witnessStack)).bytes);
        }
        else {

            if(TransactionInputPublicKey(input).bytes)
                str = StringAddF(str, "\tPublic Key: [%s]\n", toHex(TransactionInputPublicKey(input)).bytes);

            if(TransactionInputValidationScript(input).bytes)
                str = StringAddF(str, "\tValidation Script: %s\n", scriptToString(TransactionInputValidationScript(input)).bytes);

            Datas sigs = TransactionInputSignatures(input);

            FORDATAIN(signature, sigs)
                str = StringAddF(str, "\tSignature: [%s]\n", toHex(*signature).bytes);

            if(input->witnessStack.count)
                str = StringAddF(str, "\tWitness stack: %d element(s)\n", (int)input->witnessStack.count);
        }

        str = StringAddF(str, "\tSequence: %s\n", toHex(uint32D(input->sequence)).bytes);
    }

    counter = 0;

    FORIN(TransactionOutput, output, tx.outputs) {

        str = StringAddF(str, "[Output %d]\n", ++counter);
        str = StringAddF(str, "\tValue: %s\n", formatBitcoinAmount(output->value).bytes);
        str = StringAddF(str, "\tScript: %s\n", scriptToString(output->script).bytes);
    }

    return str;
}

Data TransactionData(Transaction transaction)
{
    Data data = DataNew(0);

    data = DataAddCopy(data, uint32D(transaction.version));

    if(TransactionHasWitnessData(transaction))
        data = DataAddCopy(data, uint16D(0x0100));

    data = DataAddCopy(data, varIntD(transaction.inputs.count));

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        data = DataAddCopy(data, TransactionInputData(input));
    }

    data = DataAddCopy(data, varIntD(transaction.outputs.count));

    for(int i = 0; i < transaction.outputs.count; i++) {

        TransactionOutput *output = (TransactionOutput*)transaction.outputs.ptr[i].bytes;

        data = DataAddCopy(data, TransactionOutputData(output));
    }

    if(TransactionHasWitnessData(transaction)) {
        
        for(int i = 0; i < transaction.inputs.count; i++) {

            TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

            data = DataAddCopy(data, TransactionInputWitnessData(input));
        }
    }

    data = DataAddCopy(data, uint32D(transaction.locktime));

    return data;
}

uint64_t TransactionUsedValue(Transaction transaction)
{
    uint64_t value = 0;

    for(int i = 0; i < transaction.outputs.count; i++) {
        
        TransactionOutput *output = (TransactionOutput*)transaction.outputs.ptr[i].bytes;
        value += output->value;
    }

    return value;
}

uint64_t TransactionUsableValue(Transaction transaction)
{
    uint64_t value = 0;

    for(int i = 0; i < transaction.inputs.count; i++) {
        
        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;
        value += input->fundingOutput.value;
    }

    return value;
}

int TransactionEqual(Transaction t1, Transaction t2)
{
    DataTrackPush();

    return DTPopi(DataEqual(TransactionData(t1), TransactionData(t2)));
}

TransactionInput *TransactionAddInput(Transaction *transaction, Data txHash, uint32_t index, Data pubScript, uint64_t value)
{
    transaction->inputs = DatasAddRef(transaction->inputs, DataZero(sizeof(TransactionInput)));

    TransactionInput *input = (TransactionInput*)DatasLast(transaction->inputs).bytes;

    input->previousTransactionHash = DataCopyData(txHash);
    input->outputIndex = index;
    input->sequence = 0xffffffff;
    input->fundingOutput = (TransactionOutput){ 0 };

    input->fundingOutput.script = DataCopyData(pubScript);
    input->fundingOutput.value = value;

    return input;
}

Transaction TransactionAddOutput(Transaction transaction, Data pubScript, uint64_t value)
{
    transaction.outputs = DatasAddRef(transaction.outputs, DataZero(sizeof(TransactionOutput)));

    TransactionOutput *output = (TransactionOutput*)DatasLast(transaction.outputs).bytes;

    output->script = DataCopyData(pubScript);
    output->value = value;

    return transaction;
}

static int inputSorter(Data dataA, Data dataB)
{
    TransactionInput *in1 = (TransactionInput*)dataA.bytes;
    TransactionInput *in2 = (TransactionInput*)dataB.bytes;

    int result = DataCompare(DataFlipEndianCopy(in1->previousTransactionHash), DataFlipEndianCopy(in2->previousTransactionHash));

    if(!result)
        result = in1->outputIndex - in2->outputIndex;

    return result;
}

static int outputSorter(Data dataA, Data dataB)
{
    TransactionOutput *out1 = (TransactionOutput*)dataA.bytes;
    TransactionOutput *out2 = (TransactionOutput*)dataB.bytes;

    int result = 0;
    
    if(out1->value > out2->value)
        result = 1;
    
    if(out1->value < out2->value)
        result = -1;

    if(!result)
        result = DataCompare(out1->script, out2->script);

    return result;
}

Transaction TransactionSort(Transaction transaction)
{
    transaction.inputs = DatasSort(transaction.inputs, inputSorter);
    transaction.outputs = DatasSort(transaction.outputs, outputSorter);

    return transaction;
}

static Datas trimDuplicateInstances(Datas array)
{
    Datas trimmedResult = DatasNew();

    for(int i = 0; i < array.count; i++) {

        Data item = array.ptr[i];

        if(!DatasHasMatchingData(trimmedResult, item))
            trimmedResult = DatasAddCopy(trimmedResult, item);
    }

    return trimmedResult;
}

Transaction TransactionSign(Transaction transaction, Datas privKeysAndScripts, Datas *effectedInputs)
{
    Datas result = DatasNew();

    Dictionary scriptSigs = DictionaryNew(0);

    Datas inputTypes = DatasNew();

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        enum TransactionInputType type = TransactionInputType(input);

        inputTypes = DatasAddCopy(inputTypes, DataRef((void*)&type, sizeof(type)));

        if(type & TransactionInputTypeLegacyMask) {

            if(input->scriptData.bytes)
                scriptSigs = DictionaryAddCopy(scriptSigs, DataInt(i), input->scriptData);

            input->scriptData = DataNull();
        }
    }

    for(int privKeyAndScriptsIndex = 0; privKeyAndScriptsIndex < privKeysAndScripts.count; privKeyAndScriptsIndex++) {

        Data item = privKeysAndScripts.ptr[privKeyAndScriptsIndex];

        for(int i = 0; i < transaction.inputs.count; i++) {

            TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

            enum TransactionInputType type = TransactionInputType(input);

            if(type == TransactionInputTypePayToPubkey) {

                input->scriptData = input->fundingOutput.script;

                if(item.length != 32)
                    continue;

                Data pub = pubKey(item);
                Data pubFull = pubKeyFull(item);

                Datas fundingOutputPubKeys = allPubKeys(input->fundingOutput.script);

                for(int j = 0; j < fundingOutputPubKeys.count; j++) {

                    Data scriptPubKey = fundingOutputPubKeys.ptr[j];

                    if(DataEqual(scriptPubKey, pub) || DataEqual(scriptPubKey, pubFull)) {

                        Datas array = readPushes(DictionaryGetValue(scriptSigs, DataInt(i)));

                        array = DatasAddCopy(array, signAll(TransactionDigest(transaction), item));

                        scriptSigs = DictionaryAddCopy(scriptSigs, DataInt(i), writePushes(array));

                        result = DatasAddCopy(result, DataRaw(input));
                    }
                }

                input->scriptData = DataNull();
            }

            if(type == TransactionInputTypePayToPubkeyHash) {

                input->scriptData = input->fundingOutput.script;

                if(item.length != 32)
                    continue;

                Data pubFull = pubKeyFull(item);
                Data pub = pubKey(item);

                if(pub.bytes && DataEqual(p2pkhPubScript(hash160(pubFull)), input->fundingOutput.script)) {

                    // Uncompressed pub key

                    scriptSigs = DictionaryAddCopy(scriptSigs, DataInt(i), DataAddCopy(scriptPush(signAll(TransactionDigest(transaction), item)), scriptPush(pubFull)));

                    result = DatasAddCopy(result, DataRaw(input));
                }
                else if(pub.bytes && DataEqual(p2pkhPubScript(hash160(pub)), input->fundingOutput.script)) {

                    // Compressed pub key

                    scriptSigs = DictionaryAddCopy(scriptSigs, DataInt(i), DataAddCopy(scriptPush(signAll(TransactionDigest(transaction), item)), scriptPush(pub)));

                    result = DatasAddCopy(result, DataRaw(input));
                }

                input->scriptData = DataNull();
            }

            if(type == TransactionInputTypePayToPubkeyWitness) {

                if(item.length != 32)
                    continue;

                Data pub = pubKey(item);

                if(pub.bytes && DataEqual(p2wpkhPubScriptFromPubKey(pub), input->fundingOutput.script)) {

                    Data oldScriptValue = input->scriptData;

                    input->scriptData = p2wpkhImpliedScript(pub);

                    Data signature = signAll(TransactionWitnessDigest(transaction, i), item);

                    input->witnessStack = DatasAddCopy(DatasAddCopy(DatasNew(), signature), pub);

                    input->scriptData = oldScriptValue;

                    result = DatasAddCopy(result, DataRaw(input));
                }
            }

            if(type == TransactionInputTypePayToScriptHash) {

                if(DataEqual(p2shPubScriptWithScript(item), input->fundingOutput.script)) {

                    scriptSigs = DictionaryAddCopy(scriptSigs, DataCopy((void*)&i, sizeof(i)), scriptPush(item));

                    result = DatasAddCopy(result, DataRaw(input));
                }
            }

            if(type == TransactionInputTypePayToScriptWitness) {

                if(DataEqual(p2wshPubScriptWithScript(item), input->fundingOutput.script)) {

                    input->witnessStack = DatasAddCopy(DatasNew(), item);

                    result = DatasAddCopy(result, DataRaw(input));
                }
            }
        }
    }

    for(int privKeyAndScriptsIndex = 0; privKeyAndScriptsIndex < privKeysAndScripts.count; privKeyAndScriptsIndex++) {

        Data item = privKeysAndScripts.ptr[privKeyAndScriptsIndex];

        for(int i = 0; i < transaction.inputs.count; i++) {

            TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

            enum TransactionInputType type = TransactionInputType(input);

            if(type == TransactionInputTypePayToScriptHash && DictionaryGetValue(scriptSigs, DataInt(i)).length) {

                Datas scriptStack = readPushes(DictionaryGetValue(scriptSigs, DataInt(i)));

                if(DataEqual(DatasLast(scriptStack), nestedP2wshScript(item))) {

                    input->witnessStack = DatasAddCopy(DatasNew(), item);

                    result = DatasAddCopy(result, DataRaw(input));
                }
            }
        }
    }

    for(int privKeyAndScriptsIndex = 0; privKeyAndScriptsIndex < privKeysAndScripts.count; privKeyAndScriptsIndex++) {

        Data item = privKeysAndScripts.ptr[privKeyAndScriptsIndex];

        for(int i = 0; i < transaction.inputs.count; i++) {

            TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

            enum TransactionInputType type = TransactionInputType(input);

            if(type == TransactionInputTypePayToScriptHash) {

                if(item.length != 32)
                    continue;

                Data pub = pubKey(item);

                if(!pub.bytes)
                    continue;

                Data script = DatasLast(readPushes(DictionaryGetValue(scriptSigs, DataInt(i))));

                if(DataEqual(script, nestedP2wpkhScript(pub))) {

                    Data oldScriptValue = input->scriptData;

                    input->scriptData = p2wpkhImpliedScript(pub);

                    input->witnessStack = DatasAddCopy(DatasAddCopy(DatasNew(), signAll(TransactionWitnessDigest(transaction, i), item)), pub);

                    input->scriptData = oldScriptValue;

                    result = DatasAddCopy(result, DataRaw(input));

                    continue;
                }
                else for(int j = 0; j < allPubKeys(script).count; j++) {

                    Data scriptPubKey = allPubKeys(script).ptr[j];

                    if(DataEqual(scriptPubKey, pub)) {

                        Datas array = readPushes(DictionaryGetValue(scriptSigs, DataInt(i)));

                        Data sig = signAll(TransactionDigest(transaction), item);

                        array = DatasAddCopyIndex(array, sig, array.count - 1);

                        scriptSigs = DictionaryAddCopy(scriptSigs, DataInt(i), writePushes(array));

                        result = DatasAddCopy(result, DataRaw(input));

                        break;
                    }
                }
            }

            if(type == TransactionInputTypePayToScriptWitness || input->witnessStack.count) {

                if(item.length != 32)
                    continue;

                Data pub = pubKey(item);

                if(!pub.bytes)
                    continue;

                Data script = DatasLast(input->witnessStack);

                Datas pubKeys = allPubKeys(script);

                for(int j = 0; j < pubKeys.count; j++) {

                    Data scriptPubKey = pubKeys.ptr[j];

                    if(DataEqual(scriptPubKey, pub)) {

                        Datas array = DatasCopy(input->witnessStack);

                        Data oldScriptValue = input->scriptData;

                        input->scriptData = DatasLast(input->witnessStack);

                        Data sig = signAll(TransactionWitnessDigest(transaction, i), item);

                        input->scriptData = oldScriptValue;

                        if(!DatasHasMatchingData(array, sig))
                            array = DatasAddCopyIndex(array, sig, array.count - 1);

                        input->witnessStack = array;

                        result = DatasAddCopy(result, DataRaw(input));

                        break;
                    }
                }
            }
        }
    }

    for(int i = 0; i < DictionaryCount(scriptSigs); i++) {

        DictionaryElement element = DictionaryGetElement(scriptSigs, i);

        ((TransactionInput*)transaction.inputs.ptr[DataGetInt(element.key)].bytes)->scriptData = DataCopyData(element.value);
    }

    if(!TransactionFinalize(&transaction))
        abort();

    if(effectedInputs)
        *effectedInputs = trimDuplicateInstances(result);

    return transaction;
}

int TransactionSignaturesNeeded(Transaction transaction)
{
    return 0;
    // TODO

//    int result = 0;
//
//    for(int i = 0; i < transaction.inputs.count; i++) {
//
//        // Counting pubkeys does not accound for multisig
//        // New implementation needs to actually parse the sig / multisig scripts,
//        // at least for the types of scripts we support (and perhaps that's all thats really possible).
//        //
//        // The Miniscript project makes more possible here.
//    
//        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;
//        result += TransactionInputPublicKeys(input).count - TransactionInputSignatures(input).count;
//    }
//
//    return result;
}

static int sortSignatures(Transaction *transaction)
{
    int result = 1;

    Dictionary scriptSigs = DictionaryNew(0);

    for(int i = 0; i < transaction->inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction->inputs.ptr[i].bytes;

        enum TransactionInputType type = TransactionInputType(input);

        if(type & TransactionInputTypeLegacyMask) {

            if(input->scriptData.bytes)
                scriptSigs = DictionaryAddCopy(scriptSigs, DataInt(i), input->scriptData);

            input->scriptData = DataNull();
        }
    }

    for(int i = 0; i < transaction->inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction->inputs.ptr[i].bytes;

        if(input->witnessStack.count > 2) {

            Datas signatures = DatasCopy(input->witnessStack);

            Data script = DatasTakeLast(&signatures);

            int emptySignatures = 0;

            for(int j = 0; j < signatures.count; j++)
                if(!signatures.ptr[j].length)
                    emptySignatures++;

            Datas newStack = DatasNew();

            Data oldScriptValue = input->scriptData;

            input->scriptData = script;

            for(int j = 0; j < allPubKeys(script).count; j++) {

                Data pub = allPubKeys(script).ptr[j];

                for(int k = 0; k < signatures.count; k++) {

                    if(verify(signatures.ptr[k], TransactionWitnessDigest(*transaction, i), pub)) {

                        newStack = DatasAddCopy(newStack, signatures.ptr[k]);

                        signatures = DatasRemoveIndex(signatures, k);
                    }
                }
            }

            input->scriptData = oldScriptValue;

            newStack = DatasAddCopy(newStack, script);

            BTCUtilAssert(newStack.count + emptySignatures == input->witnessStack.count);

            if(newStack.count == input->witnessStack.count)
                input->witnessStack = newStack;
            else
                result = 0;
        }
        else if(TransactionInputScriptStack(input).count > 2) {

            // TODO
            abort();

//            Datas signatures = DatasNew(); // DatasCopy(input->scriptData);
//
//            Data script = DatasTakeLast(&signatures);
//
//            int emptySignatures = 0;
//
//            for(int j = 0; j < signatures.count; j++)
//                if(!signatures.ptr[j].length)
//                    emptySignatures++;
//
//            Datas newStack = DatasNew();
//
//            Data oldScriptValue = DataNull();
//
//            for(int j = 0; j < allPubKeys(script).count; j++) {
//
//                Data pubKey = allPubKeys(script).ptr[j];
//
//                for(int k = 0; k < signatures.count; k++) {
//
//                    if(verify(signatures.ptr[k], TransactionDigest(*transaction), pubKey)) {
//
//                        newStack = DatasAddCopy(newStack, signatures.ptr[k]);
//
//                        signatures = DatasRemoveLast(signatures);
//                    }
//                }
//            }
//
//            input->scriptData = oldScriptValue;
//
//            newStack = DatasAddCopy(newStack, script);
//
//            BTCUtilAssert(newStack.count + emptySignatures == TransactionInputScriptStack(input).count);
//
//            if(newStack.count == TransactionInputScriptStack(input).count)
//                input->scriptData = writePushes(newStack);
//            else
//                result = 0;
        }
    }

    for(int i = 0; i < DictionaryCount(scriptSigs); i++) {

        DictionaryElement element = DictionaryGetElement(scriptSigs, i);

        ((TransactionInput*)transaction->inputs.ptr[DataGetInt(element.key)].bytes)->scriptData = DataCopyData(element.value);
    }

    return result;
}

static Datas TransactionCorrectMultisigSignaturesInStack(Datas stack)
{
    ScriptTokens tokens = allCheckSigs(DatasLast(stack));

    int neededSigs = 0;

    for(int i = 0; i < tokens.count; i++) {
    
        ScriptToken token = ScriptTokenI(tokens, i);
        neededSigs += token.neededSigs;
    }

    if(stack.count - 1 != neededSigs)
        return stack;

    int multisigs = 0;

    // The goal here is to add an empty element to the begining of the multisig stack

    Datas newStack = DatasNew();

    int index = 0;

    for(int i = 0; i < tokens.count; i++) {

        ScriptToken token = ScriptTokenI(tokens, i);

        if(token.op == OP_CHECKMULTISIG || token.op == OP_CHECKMULTISIGVERIFY) {

            multisigs++;

            newStack = DatasAddCopy(newStack, DataNew(0));

            for(int j = 0; j < token.neededSigs; j++)
                newStack = DatasAddCopy(newStack, stack.ptr[index++]);
        }
        else
            newStack = DatasAddCopy(newStack, stack.ptr[index++]);
    }

    newStack = DatasAddCopy(newStack, DatasLast(stack));

    return newStack;
}

static Transaction TransactionCorrectMultisigSignatures(Transaction transaction)
{
    Dictionary scriptSigs = DictionaryNew(0);

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        enum TransactionInputType type = TransactionInputType(input);

        if(type & TransactionInputTypeLegacyMask) {

            if(input->scriptData.bytes)
                scriptSigs = DictionaryAddCopy(scriptSigs, DataInt(i), input->scriptData);

            input->scriptData = DataNull();
        }
    }

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        if(input->witnessStack.count > 1) {

            input->witnessStack = TransactionCorrectMultisigSignaturesInStack(input->witnessStack);
        }

        if(TransactionInputScriptStack(input).count > 1) {

            Datas stack = TransactionCorrectMultisigSignaturesInStack(TransactionInputScriptStack(input));

            input->scriptData = writePushes(stack);
        }
    }

    for(int i = 0; i < DictionaryCount(scriptSigs); i++) {

        DictionaryElement element = DictionaryGetElement(scriptSigs, i);

        ((TransactionInput*)transaction.inputs.ptr[DataGetInt(element.key)].bytes)->scriptData = DataCopyData(element.value);
    }

    return transaction;
}

Transaction TransactionAddScript(Transaction transaction, Data script, Datas *effectedInputs)
{
    Datas result = DatasNew();

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        enum TransactionInputType type = TransactionInputType(input);

        if(type == TransactionInputTypePayToScriptHash) {

            if(DataEqual(input->fundingOutput.script, p2shPubScriptWithScript(script))) {

                input->scriptData = scriptPush(script);
                result = DatasAddCopy(result, DataRaw(input));
            }

            if(DataEqual(DatasLast(TransactionInputScriptStack(input)), nestedP2wshScript(script))) {

                input->witnessStack = DatasAddCopy(DatasNew(), script );
                result = DatasAddCopy(result, DataRaw(input));
            }
        }

        if(type == TransactionInputTypePayToScriptWitness) {

            if(DataEqual(input->fundingOutput.script, p2wshPubScriptWithScript(script))) {

                input->witnessStack = DatasAddCopy(DatasNew(), script );
                result = DatasAddCopy(result, DataRaw(input));
            }
        }
    }

    if(effectedInputs)
        *effectedInputs = trimDuplicateInstances(result);

    return transaction;
}

Transaction TransactionAddScripts(Transaction transaction, Datas scripts, Datas *effectedInputs)
{
    Datas result = DatasNew();

    Datas array = DatasCopy(scripts);

    unsigned int scriptsLeft = scripts.count;
    unsigned int scriptsProccessed = 0;

    do {

        scriptsProccessed = 0;

        for(unsigned int i = 0; i < array.count; i++) {

            Datas inputs = DatasNew();

            transaction = TransactionAddScript(transaction, array.ptr[i], &inputs);

            if(inputs.count) {

                result = DatasAddDatasCopy(result, inputs);

                scriptsProccessed++;
                scriptsLeft--;

                array = DatasRemoveIndex(array, i--);
            }
        }

    } while(scripts.count && scriptsProccessed);

    if(effectedInputs)
        *effectedInputs = trimDuplicateInstances(result);

    return transaction;
}

Transaction TransactionAddPubKey(Transaction transaction, Data thePubKey, Datas *effectedInputs)
{
    Datas result = DatasNew();

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        enum TransactionInputType type = TransactionInputType(input);

        if(type == TransactionInputTypePayToScriptHash) {

            if(DataEqual(DatasLast(TransactionInputScriptStack(input)), nestedP2wpkhScript(thePubKey))) {

                input->witnessStack = DatasAddCopy(DatasNew(), thePubKey);
                result = DatasAddCopy(result, DataRaw(input));
            }
        }

        if(type == TransactionInputTypePayToPubkeyHash) {

            if(DataEqual(input->fundingOutput.script, p2pkhPubScriptFromPubKey(thePubKey))) {

                input->scriptData = scriptPush(thePubKey);
                result = DatasAddCopy(result, DataRaw(input));
            }
        }

        if(type == TransactionInputTypePayToPubkeyWitness) {

            if(DataEqual(input->fundingOutput.script, p2wpkhPubScriptFromPubKey(thePubKey))) {

                input->witnessStack = DatasAddCopy(DatasNew(), thePubKey);
                result = DatasAddCopy(result, DataRaw(input));
            }
        }
    }

    if(effectedInputs)
        *effectedInputs = trimDuplicateInstances(result);

    return transaction;
}


Transaction TransactionAddPubKeys(Transaction transaction, Datas pubKeys, Datas *effectedInputs)
{
    Datas result = DatasNew();

    for(int i = 0; i < pubKeys.count; i++) {

        Data pub = pubKeys.ptr[i];

        Datas inputs = DatasNew();

        transaction = TransactionAddPubKey(transaction, pub, &inputs);

        result = DatasAddDatasCopy(result, inputs);
    }

    if(effectedInputs)
        *effectedInputs = trimDuplicateInstances(result);

    return transaction;
}

Datas TransactionDigestsFor(Transaction transaction, Data thePubKey)
{
    return TransactionDigestsForFlexible(transaction, thePubKey, 0);
}

Datas TransactionDigestsForFlexible(Transaction transaction, Data thePubKey, int exceptSigned)
{
    Datas result = DatasNew();

    Dictionary scriptSigs = DictionaryNew(0);

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        enum TransactionInputType type = TransactionInputType(input);

        if(type & TransactionInputTypeLegacyMask) {

            if(input->scriptData.bytes)
                scriptSigs = DictionaryAddCopy(scriptSigs, DataInt(i), input->scriptData);

            input->scriptData = DataNull();
        }
    }

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        Datas mainPubKeys = DatasNew();

        Data curScriptSig = DictionaryGetValue(scriptSigs, DataInt(i));

        mainPubKeys = DatasAddDatasCopy(mainPubKeys, allPubKeys(DatasLast(readPushes(curScriptSig))));

        mainPubKeys = DatasAddDatasCopy(mainPubKeys, allPubKeys(input->fundingOutput.script));

        mainPubKeys = DatasAddCopy(mainPubKeys, DatasLast(readPushes(curScriptSig)));

        for(int i = 0; i < mainPubKeys.count; i++) {

            Data mainPubKey = mainPubKeys.ptr[i];

            if(DataEqual(mainPubKey, thePubKey)) {

                input->scriptData = input->fundingOutput.script;

                result = DatasAddCopy(result, TransactionDigest(transaction));

                input->scriptData = DataNull();

                break;
            }
        }

        // Witness parsing -- just use witness stack & ignore input->type

        // Witness public key hash section
        if(DataEqual(DatasLast(input->witnessStack), thePubKey)) {

            Data oldValue = input->scriptData;

            input->scriptData = p2wpkhImpliedScript(thePubKey);

            result = DatasAddCopy(result, TransactionWitnessDigest(transaction, i));

            input->scriptData = oldValue;
        }

        // Witness script section
        {
            Data oldValue = input->scriptData;

            input->scriptData = DatasLast(input->witnessStack);

            Datas signedPubKeys = DatasNew();

            if(exceptSigned)
                for(int j = 0; j < input->witnessStack.count - 1; j++)
                    if(verify(input->witnessStack.ptr[j], TransactionWitnessDigest(transaction, i), thePubKey))
                        signedPubKeys = DatasAddCopy(signedPubKeys, thePubKey);

            Datas scriptPubKeys = allPubKeys(DatasLast(input->witnessStack));

            for(int j = 0; j < scriptPubKeys.count; j++)
                if(DataEqual(scriptPubKeys.ptr[j], thePubKey))
                    if(!DatasHasMatchingData(signedPubKeys, scriptPubKeys.ptr[j]))
                        result = DatasAddCopy(result, TransactionWitnessDigest(transaction, i));

            input->scriptData = oldValue;
        }
    }

    for(int i = 0; i < DictionaryCount(scriptSigs); i++) {

        DictionaryElement element = DictionaryGetElement(scriptSigs, i);

        ((TransactionInput*)transaction.inputs.ptr[DataGetInt(element.key)].bytes)->scriptData = DataCopyData(element.value);
    }

    Datas array = DatasNew();

    for(int i = 0; i < result.count; i++)
        if(!DatasHasMatchingData(array, result.ptr[i]))
            array = DatasAddCopy(array, result.ptr[i]);

    return array;
}

Datas TransactionWitnessPubKeys(Transaction transaction)
{
    Datas result = DatasNew();

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        if(DatasLast(input->witnessStack).length == 33)
            result = DatasAddCopy(result, DatasLast(input->witnessStack));

        result = DatasAddDatasCopy(result, allPubKeys(DatasLast(input->witnessStack)));
    }

    return result;
}

int TransactionValidateSignatures(Transaction transaction)
{
    for(int i = 0; i < transaction.inputs.count; i++)
        if(!TransactionValidateSignature(transaction, i))
            return 0;

    return 1;
}

int TransactionValidateSignature(Transaction transaction, int i)
{
    TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

    // Witness script section
    {
        Data oldValue = input->scriptData;

        if(input->witnessStack.count == 2) {

            input->scriptData = p2wpkhImpliedScript(DatasLast(input->witnessStack));

            Data digest = TransactionWitnessDigest(transaction, i);

            if(verify(DatasFirst(input->witnessStack), digest, DatasLast(input->witnessStack))) {

                input->scriptData = oldValue;

                return 1;
            }
        }

        input->scriptData = DatasLast(input->witnessStack);

        Data digest = TransactionWitnessDigest(transaction, i);

        ScriptTokens tokens = scriptToTokens(input->scriptData);

        input->scriptData = oldValue;

        for(int j = 0; j < tokens.count; j++) {

            // CHECKSIG prefixed by <sig>, <pubkey>
            if(ScriptTokenI(tokens, j).op == OP_CHECKSIG || ScriptTokenI(tokens, j).op == OP_CHECKSIGVERIFY) {

                Data pubKey = j > 0 ? ScriptTokenI(tokens, j - 1).data : DataNull();

                int validSignature = 0;

                for(int k = 0; k < input->witnessStack.count - 1; k++)
                    if(verify(input->witnessStack.ptr[k], digest, pubKey))
                        validSignature = 1;

                if(!validSignature)
                    return 0;
            }

            // CHECKMULTISIG prefixed by <ignored>, <sig1>, <sign>, <sigcount>, <pubkey1>, <pubkeyn>, <pubcount>
            if(ScriptTokenI(tokens, j).op == OP_CHECKMULTISIG || ScriptTokenI(tokens, j).op == OP_CHECKMULTISIGVERIFY) {

                int tokenIndex = j - 1;

                if(tokenIndex < 0)
                    return 0;

                int numPubKeys = 1 + ScriptTokenI(tokens, tokenIndex--).op - OP_1;

                if(numPubKeys < 1 || numPubKeys > 16 || tokenIndex - numPubKeys - 1 < 0)
                    return 0;

                Datas pubKeys = DatasNew();

                while(numPubKeys-- > 0)
                    pubKeys = DatasAddCopy(pubKeys, ScriptTokenI(tokens, tokenIndex--).data);

                int numSigs = 1 + ScriptTokenI(tokens, tokenIndex--).op - OP_1;

                if(numSigs < 1 || numSigs > 16)
                    return 0;
                

                int validSignatures = 0;

                for(int pubKeyIndex = 0; pubKeyIndex < pubKeys.count; pubKeyIndex++) {

                    Data thePubKey = pubKeys.ptr[pubKeyIndex];

                    for(int k = 0; k < input->witnessStack.count - 1; k++)
                        if(verify(input->witnessStack.ptr[k], digest, thePubKey))
                            validSignatures++;
                }

                if(validSignatures != numSigs)
                    return 0;
            }
        }
    }

    return 1;
}

Transaction TransactionAddSignature(Transaction transaction, Data signature, Datas *effectedInputs)
{
    Datas result = DatasNew();

    Dictionary scriptSigs = DictionaryNew(0);

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        enum TransactionInputType type = TransactionInputType(input);

        if(type & TransactionInputTypeLegacyMask) {

            if(input->scriptData.bytes)
                scriptSigs = DictionaryAddCopy(scriptSigs, DataInt(i), input->scriptData);

            input->scriptData = DataNull();
        }
    }

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        Datas mainPubKeys = DatasNew();

        Data scriptSig = DictionaryGetValue(scriptSigs, DataInt(i));

        mainPubKeys = DatasAddDatasCopy(mainPubKeys, allPubKeys(DatasLast(readPushes(scriptSig))));

        mainPubKeys = DatasAddDatasCopy(mainPubKeys, allPubKeys(input->fundingOutput.script));

        mainPubKeys = DatasAddCopy(mainPubKeys, DatasLast(readPushes(scriptSig)));

        for(int i = mainPubKeys.count - 1; i >= 0; i--) {

            Data pubKey = mainPubKeys.ptr[i];

            Datas array = readPushes(DictionaryGetValue(scriptSigs, DataInt(i)));

            input->scriptData = input->fundingOutput.script;

            if(verify(signature, TransactionDigest(transaction), pubKey)) {

                array = DatasAddCopy(array, signature);

                result = DatasAddCopy(result, DataRaw(input));

                scriptSigs = DictionaryAddCopy(scriptSigs, DataInt(i), writePushes(array));
            }

            input->scriptData = DataNull();
        }

        // Witness parsing

        Data oldValue = input->scriptData;

        input->scriptData = p2wpkhImpliedScript(DatasLast(input->witnessStack));

        if(verify(signature, TransactionWitnessDigest(transaction, i), DatasLast(input->witnessStack))) {

            Datas array = DatasCopy(input->witnessStack);

            array = DatasAddCopyFront(array, signature);

            input->witnessStack = array;

            result = DatasAddCopy(result, DataRaw(input));
        }

        input->scriptData = DatasLast(input->witnessStack);

        Datas items = allPubKeys(DatasLast(input->witnessStack));

        for(int j = 0; j < items.count; j++) {

            Data witPubKey = items.ptr[j];

            if(verify(signature, TransactionWitnessDigest(transaction, i), witPubKey)) {

                Datas array = DatasCopy(input->witnessStack);

                array = DatasAddCopyFront(array, signature);

                input->witnessStack = array;

                result = DatasAddCopy(result, DataRaw(input));
            }
        }

        input->scriptData = oldValue;
    }

    for(int i = 0; i < DictionaryCount(scriptSigs); i++) {

        DictionaryElement element = DictionaryGetElement(scriptSigs, i);

        ((TransactionInput*)transaction.inputs.ptr[DataGetInt(element.key)].bytes)->scriptData = DataCopyData(element.value);
    }

    if(effectedInputs)
        *effectedInputs = trimDuplicateInstances(result);

    return transaction;
}

int TransactionFinalize(Transaction *transaction)
{
    int result = sortSignatures(transaction);

    *transaction = TransactionCorrectMultisigSignatures(*transaction);

    return result;
}

static int TransactionHasWitnessData(Transaction transaction)
{
    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        if(input->witnessStack.count)
            return 1;
    }

    return 0;
}

Data TransactionDigest(Transaction transaction)
{
    return DataAddCopy(TransactionTx(transaction), uint32D(0x00000001));
}

Data TransactionWitnessDigest(Transaction transaction, int index)
{
    if(index >= transaction.inputs.count)
        return DataNull();

    TransactionOutput fundingOutput = ((TransactionInput*)transaction.inputs.ptr[index].bytes)->fundingOutput;

    if(!fundingOutput.script.bytes)
        return DataNull();

    return TransactionWitnessDigestFlexible(transaction, index, fundingOutput.value);
}

Data TransactionWitnessDigestFlexible(Transaction transaction, int index, uint64_t value)
{
    if(index >= transaction.inputs.count)
        return DataNull();

    TransactionInput *activeInput = (TransactionInput*)transaction.inputs.ptr[index].bytes;

    Data data = DataNew(0);

    Data prevOuts = DataNew(0);
    Data sequences = DataNew(0);
    Data outputs = DataNew(0);

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;

        prevOuts = DataAddCopy(prevOuts, input->previousTransactionHash);
        prevOuts = DataAddCopy(prevOuts, uint32D(input->outputIndex));
    }

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;
        sequences = DataAddCopy(sequences, uint32D(input->sequence));
    }

    for(int i = 0; i < transaction.outputs.count; i++) {

        TransactionOutput *output = (TransactionOutput*)transaction.outputs.ptr[i].bytes;
        outputs = DataAddCopy(outputs, TransactionOutputData(output));
    }

    data = DataAddCopy(data, uint32D(transaction.version));

    data = DataAddCopy(data, hash256(prevOuts));
    data = DataAddCopy(data, hash256(sequences));

    data = DataAddCopy(data, activeInput->previousTransactionHash);
    data = DataAddCopy(data, uint32D(activeInput->outputIndex));

    if(!activeInput->scriptData.length)
        return DataNull();

    data = DataAddCopy(data, varIntD(activeInput->scriptData.length));
    data = DataAddCopy(data, activeInput->scriptData);
    data = DataAddCopy(data, uint64D(value));
    data = DataAddCopy(data, uint32D(activeInput->sequence));

    data = DataAddCopy(data, hash256(outputs));

    data = DataAddCopy(data, uint32D(transaction.locktime));

    data = DataAddCopy(data, uint32D(0x00000001));

    return data;
}

Data TransactionTx(Transaction transaction)
{
    Data data = DataNew(0);

    data = DataAddCopy(data, uint32D(transaction.version));

    data = DataAddCopy(data, varIntD(transaction.inputs.count));

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;
        data = DataAddCopy(data, TransactionInputData(input));
    }

    data = DataAddCopy(data, varIntD(transaction.outputs.count));

    for(int i = 0; i < transaction.outputs.count; i++) {

        TransactionOutput *output = (TransactionOutput*)transaction.outputs.ptr[i].bytes;
        data = DataAddCopy(data, TransactionOutputData(output));
    }

    data = DataAddCopy(data, uint32D(transaction.locktime));

    return data;
}

Data TransactionWtx(Transaction transaction)
{
    Data data = DataNew(0);

    data = DataAddCopy(data, uint32D(transaction.version));

    if(TransactionHasWitnessData(transaction)) {

        data = DataAddCopy(data, uint8D(0x00));
        data = DataAddCopy(data, uint8D(0x01));
    }

    data = DataAddCopy(data, varIntD(transaction.inputs.count));

    for(int i = 0; i < transaction.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;
        data = DataAddCopy(data, TransactionInputData(input));
    }

    data = DataAddCopy(data, varIntD(transaction.outputs.count));

    for(int i = 0; i < transaction.outputs.count; i++) {

        TransactionOutput *output = (TransactionOutput*)transaction.outputs.ptr[i].bytes;
        data = DataAddCopy(data, TransactionOutputData(output));
    }

    if(TransactionHasWitnessData(transaction)) {

        for(int i = 0; i < transaction.inputs.count; i++) {

            TransactionInput *input = (TransactionInput*)transaction.inputs.ptr[i].bytes;
            data = DataAddCopy(data, TransactionInputWitnessData(input));
        }
    }

    data = DataAddCopy(data, uint32D(transaction.locktime));

    return data;
}

Data TransactionTxid(Transaction transaction)
{
    return hash256(TransactionTx(transaction));
}

Data TransactionWtxid(Transaction transaction)
{
    return hash256(TransactionWtx(transaction));
}

TransactionInput *TransactionInputAt(Transaction *transaction, int index)
{
    TransactionInput inputZero = { 0 };

    while(index >= transaction->inputs.count)
        transaction->inputs = DatasAddCopy(transaction->inputs, DataRaw(inputZero));

    return (TransactionInput*)transaction->inputs.ptr[index].bytes;
}

TransactionOutput *TransactionOutputAt(Transaction *transaction, int index)
{
    TransactionOutput outputZero = { 0 };

    while(index >= transaction->outputs.count)
        transaction->outputs = DatasAddCopy(transaction->outputs, DataRaw(outputZero));

    return (TransactionOutput*)transaction->outputs.ptr[index].bytes;
}

TransactionInput *TransactionInputOrNilAt(Transaction *transaction, int index)
{
    if(!transaction || index < 0 || index >= transaction->inputs.count)
        return NULL;

    return (TransactionInput*)transaction->inputs.ptr[index].bytes;
}

TransactionOutput *TransactionOutputOrNilAt(Transaction *transaction, int index)
{
    if(!transaction || index < 0 || index >= transaction->outputs.count)
        return NULL;

    return (TransactionOutput*)transaction->outputs.ptr[index].bytes;
}


#pragma mark -- TransactionInput


TransactionInput *TransactionInputSetFundingOutput(TransactionInput *input, uint64_t value, Data script)
{
    TransactionOutput output = { 0 };

    output.value = value;
    output.script = script;

    input->fundingOutput = output;

    return input;
}

enum TransactionInputType TransactionInputType(const TransactionInput *input)
{
    if(!input->fundingOutput.script.length)
        return TransactionInputTypeUnknown;

    uint8_t *script = (uint8_t*)input->fundingOutput.script.bytes;
    size_t length = input->fundingOutput.script.length;

    if(length == 23 && script[0] == OP_HASH160 && script[1] == 20 && script[22] == OP_EQUAL)
        return TransactionInputTypePayToScriptHash;

    if(script[0] == 0 && length == 22 && script[1] == 20)
        return TransactionInputTypePayToPubkeyWitness;

    if(script[0] == 0 && length == 34 && script[1] == 32)
        return TransactionInputTypePayToScriptWitness;

    if(script[0] == OP_DUP && length == 25 && script[1] == OP_HASH160 && script[2] == 20 && script[23] == OP_EQUALVERIFY && script[24] == OP_CHECKSIG)
        return TransactionInputTypePayToPubkeyHash;

    return TransactionInputTypePayToPubkey;
}

Datas TransactionInputScriptStack(const TransactionInput *input)
{
    const uint8_t *ptr = (uint8_t*)input->scriptData.bytes;
    const uint8_t *end = ptr + input->scriptData.length;

    Datas array = DatasNew();

    while(ptr < end)
        array = DatasAddRef(array, readBytes(readPushData(&ptr, end), &ptr, end));

    return array;
}

Data TransactionInputData(const TransactionInput *input)
{
    Data data = DataNew(0);

    data = DataAddCopy(data, input->previousTransactionHash);
    data = DataAddCopy(data, uint32D(input->outputIndex));
    data = DataAddCopy(data, varIntD(input->scriptData.length));
    data = DataAddCopy(data, input->scriptData);
    data = DataAddCopy(data, uint32D(input->sequence));

    return data;
}

Data TransactionInputWitnessData(const TransactionInput *input)
{
    Data data = DataNew(0);

    data = DataAddCopy(data, varIntD(input->witnessStack.count));

    for(int i = 0; i < input->witnessStack.count; i++) {

        Data item = input->witnessStack.ptr[i];

        data = DataAddCopy(data, varIntD(item.length));
        data = DataAddCopy(data, item);
    }

    return data;
}

Datas TransactionInputPublicKeys(const TransactionInput *input)
{
    // If there is a valid script, we'll assume there is no public key
    // This is not guarenteed by the standard but it is likely.

    if(TransactionInputValidationScript(input).bytes)
        return DatasNew();

    if(input->witnessFlag == OP_P2WPKH && input->witnessStack.count == 2)
        return DatasAddCopy(DatasNew(), input->witnessStack.ptr[1]);

    Datas scriptStack = TransactionInputScriptStack(input);

    if(scriptStack.count == 2)
        return DatasAddCopy(DatasNew(), scriptStack.ptr[1]);

    return DatasNew();
}

Data TransactionInputPublicKey(const TransactionInput *input)
{
    return DatasFirst(TransactionInputPublicKeys(input));
}

Data TransactionInputValidationScript(const TransactionInput *input)
{
    Data result = DataNull();

    if(input->witnessFlag == OP_P2WSH)
        result = DatasLast(input->witnessStack);

    if(!input->witnessFlag)
        result = DatasLast(TransactionInputScriptStack(input));

    if(firstError(result).bytes)
        result = DataNull();

    return result;
}

Datas TransactionInputSignatures(const TransactionInput *input)
{
    if(TransactionInputValidationScript(input).bytes || TransactionInputPublicKey(input).bytes) {

        if(input->witnessFlag && input->witnessStack.count)
            return DatasRemoveLast(DatasCopy(input->witnessStack));

        Datas scriptStack = TransactionInputScriptStack(input);

        if(scriptStack.count)
            return DatasRemoveLast(DatasCopy(scriptStack));
    }
    else {

        if(input->witnessStack.count)
            return input->witnessStack;

        return TransactionInputScriptStack(input);
    }

    return DatasNew();
}

Dict TransactionInputGetScriptTokensPushDataSet(TransactionInput *input)
{
    if(!DictCount(input->scriptTokensPushDataSet)) {

        Dict set = DictNew();

        ScriptTokens scriptTokens = scriptToTokens(input->scriptData);

        for(int i = 0; i < scriptTokens.count; i++)
            DictAdd(&set, ScriptTokenI(scriptTokens, i).data, DataNull());

        for(int i = 0; i < input->witnessStack.count; i++)
            DictAdd(&set, input->witnessStack.ptr[i], DataNull());

        ScriptTokens fundingTokens = scriptToTokens(input->fundingOutput.script);

        for(int i = 0; i < fundingTokens.count; i++)
            DictAdd(&set, ScriptTokenI(fundingTokens, i).data, DataNull());

        input->scriptTokensPushDataSet = DictUntrack(set);
    }

    return input->scriptTokensPushDataSet;
}


#pragma mark -- TransactionOutput


Dict TransactionOutputGetScriptTokensPushDataSet(TransactionOutput *output)
{
    if(!DictCount(output->scriptTokensPushDataSet)) {

        ScriptTokens tokens = scriptToTokens(output->script);

        Dict set = DictNew();

        for(int i = 0; i < tokens.count; i++) {

            Data data = ScriptTokenI(tokens, i).data;

            if(data.length)
                DictAdd(&set, data, DataNull());
        }

        output->scriptTokensPushDataSet = DictUntrack(set);
    }

    return output->scriptTokensPushDataSet;
}

Data TransactionOutputData(const TransactionOutput *output)
{
    Data data = uint64D(output->value);

    data = DataAddCopy(data, varIntD(output->script.length));

    data = DataAddCopy(data, output->script);

    return data;
}
