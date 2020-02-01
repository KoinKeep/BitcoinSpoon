#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "BTCUtil.h"

typedef struct Transaction {

    // Not found in data. Only here for caching convenience.
    int32_t height;

    int32_t version;

    Datas inputs;
    Datas outputs;

    uint32_t locktime;

} Transaction;

Transaction TransactionEmpty();
Transaction TransactionNew(Data data);

String TransactionDescription(Transaction tx);

void TransactionTrack(Transaction *transaction);
void TransactionUntrack(Transaction *transaction);

Transaction TransactionCopy(Transaction transaction);

Data TransactionData(Transaction transaction);

uint64_t TransactionUsedValue(Transaction transaction);
uint64_t TransactionUsableValue(Transaction transaction);

// Compares 'data' values.
int TransactionEqual(Transaction t1, Transaction t2);

struct TransactionInput;

struct TransactionInput *TransactionAddInput(Transaction *transaction, Data txHash, uint32_t index, Data pubScript, uint64_t value);
Transaction TransactionAddOutput(Transaction transaction, Data pubScript, uint64_t value);

// Sorts inputs and outputs as per BIP69
Transaction TransactionSort(Transaction transaction);

// Signs and / or adds scripts to matching inputs.
// Effected inputs are returned roughly in the order provided in privKeysAndScripts
// with normal keys and scripts first, followed by keys used in meta-scripts.
Transaction TransactionSign(Transaction transaction, Datas privKeysAndScripts, Datas *effectedInputs);

// Not yet implemented
int TransactionSignaturesNeeded(Transaction transaction);


/*********** Remote Signing Interface ***********/


// Returns the effected inputs
Transaction TransactionAddScript(Transaction transaction, Data script, Datas *effectedInputs);
Transaction TransactionAddScripts(Transaction transaction, Datas scripts, Datas *effectedInputs); // Tries every order of adding scripts

// Returns the effected inputs
Transaction TransactionAddPubKey(Transaction transaction, Data thePubKey, Datas *effectedInputs);
Transaction TransactionAddPubKeys(Transaction transaction, Datas pubKeys, Datas *effectedInputs);

// Returns the digests to be signed by pubKey
Datas TransactionDigestsFor(Transaction transaction, Data thePubKey);

// NOTE: This method is incomplete. It only removes digests for signed witness script transactions.
Datas TransactionDigestsForFlexible(Transaction transaction, Data thePubKey, int exceptSigned);

// Returns all pubkeys in all witness elements and / or scripts.
Datas TransactionWitnessPubKeys(Transaction transaction);

// NOTE: This method is incomplete. It only validates witness signatures.
int TransactionValidateSignatures(Transaction transaction);
int TransactionValidateSignature(Transaction transaction, int index);

Transaction TransactionAddSignature(Transaction transaction, Data signature, Datas *effectedInputs);

// Sorts all signatures and adds in extra elements for MULTISIG operators
// Called automatically by 'sign' but you must call this if you're using -addSignature:
// Returns 0 on failure.
int TransactionFinalize(Transaction *transaction);


/************** Low-level Interface *************/

Data TransactionTx(Transaction transaction);
Data TransactionWtx(Transaction transaction);

Data TransactionTxid(Transaction transaction);
Data TransactionWtxid(Transaction transaction);

Data TransactionDigest(Transaction transaction);
Data TransactionWitnessDigest(Transaction transaction, int inputIndex); // Reads value from input.fundingOutput.value
Data TransactionWitnessDigestFlexible(Transaction transaction, int inputIndex, uint64_t consumedValue);


typedef struct TransactionOutput {

    Data script; // UNSAFE: copy if needed
    uint64_t value;

    Dict scriptTokensPushDataSet;

} TransactionOutput;

typedef struct TransactionInput {

    // This value is not serialized normally, it is only here for convenient storage
    // when processing scripts.
    TransactionOutput fundingOutput;

    Data previousTransactionHash;  // UNSAFE: copy if needed
    uint32_t outputIndex;
    Data scriptData; // UNSAFE: copy if needed
    uint32_t sequence;
    Datas witnessStack; // UNSAFE: deep copy each Data if needed

    // Note the witness flag is a guess.The previous transaction's output
    // determines the true witness type
    enum OP witnessFlag;

    // Access with
    Dict scriptTokensPushDataSet;

} TransactionInput;

// Creates inputs up to 'index' if needed, then returns input at index
TransactionInput *TransactionInputAt(Transaction *transaction, int index);

// Creates outputs up to 'index' if needed, then returns output at index
TransactionOutput *TransactionOutputAt(Transaction *transaction, int index);

// Bounds checks before looking up.
TransactionInput *TransactionInputOrNilAt(Transaction *transaction, int index);
TransactionOutput *TransactionOutputOrNilAt(Transaction *transaction, int index);

enum TransactionInputType
{
    TransactionInputTypeUnknown = 0,
    TransactionInputTypePayToPubkey = 1,
    TransactionInputTypePayToPubkeyHash = 2,
    TransactionInputTypePayToScriptHash = 4,
    TransactionInputTypePayToPubkeyWitness = 8,
    TransactionInputTypePayToScriptWitness = 16,

    TransactionInputTypeLegacyMask = TransactionInputTypePayToPubkey | TransactionInputTypePayToPubkeyHash | TransactionInputTypePayToScriptHash,
};

enum TransactionInputType TransactionInputType(const TransactionInput *input);

TransactionInput *TransactionInputSetFundingOutput(TransactionInput *input, uint64_t value, Data script);

// UNSAFE: deep copy each NSData if needed
Datas TransactionInputScriptStack(const TransactionInput *input);

Data TransactionInputData(const TransactionInput *input);
Data TransactionInputWitnessData(const TransactionInput *input);

Datas TransactionInputPublicKeys(const TransactionInput *input);

// These are just guesses for showing the user -- don't rely on them.
Data TransactionInputPublicKey(const TransactionInput *input);
Data TransactionInputValidationScript(const TransactionInput *input);
Datas TransactionInputSignatures(const TransactionInput *input);

// Generates 'scriptTokensPushDataSet' the first time it is called on this input, caching the result
// Subsequent calls return the cached value.
Dict TransactionInputGetScriptTokensPushDataSet(TransactionInput *input);

Data TransactionOutputData(const TransactionOutput *output);

// Generates 'scriptTokensPushDataSet' the first time it is called on this output, caching the result
// Subsequent calls return the cached value.
Dict TransactionOutputGetScriptTokensPushDataSet(TransactionOutput *output);

#endif
