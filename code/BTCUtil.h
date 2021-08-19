#ifndef BTCUTIL_H
#define BTCUTIL_H

#include <stdint.h>
#include "Data.h"
#include "ScriptToken.h"

#define BTCUTILAssert(...)

void BTCUtilStartup();
void BTCUtilShutdown();

uint64_t readVarInt(const uint8_t **ptr, const uint8_t *end);
uint64_t readPushData(const uint8_t ** ptr, const uint8_t *end);

int isPushData(const uint8_t *ptr, const uint8_t *end);

Data scriptPush(Data data);

Datas readPushes(Data script);
Data writePushes(Datas items);

uint8_t uint8readP(const uint8_t **ptr, const uint8_t *end);
uint16_t uint16readP(const uint8_t **ptr, const uint8_t *end);
uint32_t uint32readP(const uint8_t ** ptr, const uint8_t *end);
uint64_t uint64readP(const uint8_t **ptr, const uint8_t *end);

uint8_t uint8read(Data data);
uint16_t uint16read(Data data);
uint32_t uint32read(Data data);
uint64_t uint64read(Data data);

Data readBytes(uint64_t bytes, const uint8_t **ptr, const uint8_t *end);
Data readBytesUnsafe(uint64_t bytes, const uint8_t **ptr, const uint8_t *end);

Data sha256(Data data);
Data sha512(Data data);
Data ripemd160(Data data);
Data hash160(Data data);
Data hash256(Data data);

Data hmacSha256(Data key, Data data);
Data hmacSha512(Data key, Data data);

uint32_t murmur3(Data key, uint32_t seed);

#define BLOOM_UPDATE_NONE 0
#define BLOOM_UPDATE_ALL 1
#define BLOOM_UPDATE_P2PUBKEY_ONLY 2

#define BLOOM_ALLOW_LOW_ESTIMATE 1024 // By default small estimatedElements count is rounded up to 3. This flag disables that.

Data bloomFilterArray(Datas searchElements, float falsePositiveRate, int flag);
Data bloomFilter(int estimatedElements, float falsePositiveRate, int flag);

// bloomFilters never change size by definition, no need to return it.
// Returns 0 on failure (ie invalid bloomFilter)
int bloomFilterAddElement(Data bloomFilter, Data element);
int bloomFilterCheckElement(Data bloomFilter, Data element);

Data PBKDF2(const char *sentence, const char *passphrase);

int hdWalletVerify(Data hdWallet);

Data hdWalletPriv(Data masterKey, Data chainCode);
Data hdWalletPub(Data masterKey, Data chainCode);
Data hdWalletPrivTestNet(Data masterKey, Data chainCode);
Data hdWalletPubTestNet(Data masterKey, Data chainCode);

Data hdWalletSetDepth(Data hdWallet, uint8_t depth);

// 32 bytes means private key, 33 means public key.
Data anyKeyFromHdWallet(Data hdWallet);
Datas anyKeysFromHdWallets(Datas hdWallets);

Data publicHdWallet(Data priveHdWallet);
Data pubKeyFromHdWallet(Data hdWallet);
Data chainCodeFromHdWallet(Data hdWallet);

Datas pubKeysFromHdWallets(Datas hdWallets);

Data privKeyToHdWallet(Data privKey, const char *passphrase);

Data hdWallet(Data hdWallet, const char *path);

// Returns an array of uint32_t indices (stored in Datas) for 'path'.
Datas ckdIndicesFromPath(const char *path);

Data childKeyDerivation(Data hdWallet, uint32_t index);

// Shorthand for childKeyDerivation.
Data ckd(Data hdWallet, uint32_t index);

Data implode(Datas items);

String base58Encode(Data data);
Data base58Dencode(const char *string);

Data pubKey(Data privateKey);
Data pubKeyHash(Data privateKey);
Data pubKeyFull(Data privateKey);

Data pubKeyFromPubKey64(Data pubKey64);

Data pubKeyExpand(Data publicKey);
Data publicKeyCompress(Data publicKey);
Data publicKeyParse(Data keyData);

// These return DER signatures
Data signAll(Data unhashedMsg, Data privateKey); // Defaults to SIGHASH_ALL
Data sign(Data unhashedMsg, Data privateKey, uint8_t sigHashTypeFlag);
int verify(Data derSignature, Data unhashedMsg, Data compressedPublicKey);

Data signatureFromSig64(Data sig64);

Data compressSignature(Data derSignature);

Data ecdhKey(Data privateKey, Data publicKey);

// Tweaks the resulting key by 'amount' -- amount is typically the message number
Data ecdhKeyRotate(Data privateKey, Data publicKey, uint32_t amount);

// Tweak must be 32 bytes
Data addToPubKey(Data tweak, Data pubKey);
Data addToPrivKey(Data tweak, Data privKey);

// Tweak must be 32 bytes
Data multiplyWithPubKey(Data tweak, Data pubKey);
Data multiplyWithPrivKey(Data tweak, Data privKey);

Data addPubKeys(Data keyA, Data keyB);

Data padData16(Data dataIn);
Data unpadData16(Data data);

// AES encryption
Data encryptAES(Data data, Data privKey, uint64_t nonce);
Data decryptAES(Data data, Data privKey, uint64_t nonce);

int validPublicKey(Data keyData);
int validSignature(Data signature);

// Returns all the public keys found in 'script' in the order they were found.
Datas allPubKeys(Data script);

// Returns all checksig / multisig ops
ScriptTokens allCheckSigs(Data script);

Data multisigScript(Datas pubKeys);

Data vaultScript(Data masterPubKey, Datas pubKeys);

Data encodeScriptNum(int64_t value);

Data jimmyScript(Data jimmyPubKey, Data receiverPubKey, uint32_t currentBlockHeight);

// Segwit address in bech32 format
String toSegwit(Data program, const char *prefix);
Data fromSegwit(String string);
String segwitPrefix(String string);
int segwitVersion(String string);

Data uint8D(uint8_t number);
Data uint16D(uint16_t number);
Data uint32D(uint32_t number);
Data uint64D(uint64_t number);
Data varIntD(uint64_t number);

String makeUuid();

String toHex(Data data);
Data fromHex(const char *string);

String formatBitcoinAmount(int64_t amount);

// Mnemonics are not thread safe.
String toMnemonic(Data data);
Data fromMnemonic(String mnemonic);

Data addressToPubScript(String address);
String pubScriptToAddress(Data pubScript);

// "Pay to public key hash" -- oldest standard transaction
String p2pkhAddress(Data publicKey);
String p2pkhAddressTestNet(Data publicKey);
Data p2pkhPubScript(Data hash);
Data p2pkhPubScriptFromPubKey(Data publicKey);

// "Pay to script hash" -- 2nd oldest transaction type
String p2shAddress(Data scriptData);
String p2shAddressTestNet(Data scriptData);
Data p2shPubScriptWithScript(Data scriptData);
Data p2shPubScript(Data scriptHash);

// "Pay to witness public key hash" replaces 'p2pkh'
String p2wpkhAddress(Data publicKey);
String p2wpkhAddressTestNet(Data publicKey);
Data p2wpkhPubScript(Data hash);
Data p2wpkhPubScriptFromPubKey(Data publicKey);
Data p2wpkhImpliedScript(Data publicKey);
Data nestedP2wpkhScript(Data pubKey);

// "Pay to witness script hash" replaces 'p2sh'
String p2wshAddress(Data scriptData);
String p2wshAddressTestNet(Data scriptData);
Data p2wshPubScriptWithScript(Data scriptData);
Data p2wshPubScript(Data hash);
Data nestedP2wshScript(Data script);
Data p2wshHashFromPubScript(Data pubScript);

String scriptToString(Data script);

String firstError(Data script);

ScriptTokens scriptToTokens(Data script);
ScriptTokens scriptToTokensUnsafe(Data script);

/*
 TODO

#define PSBT_GLOBAL_UNSIGNED_TX 0
#define PSBT_IN_NON_WITNESS_UTXO 0
#define PSBT_IN_WITNESS_UTXO 1
#define PSBT_IN_PARTIAL_SIG 2
#define PSBT_IN_SIGHASH_TYPE 3
#define PSBT_IN_REDEEM_SCRIPT 4
#define PSBT_IN_WITNESS_SCRIPT 5
#define PSBT_IN_BIP32_DERIVATION 6
#define PSBT_IN_FINAL_SCRIPTSIG 7
#define PSBT_IN_FINAL_SCRIPTWITNESS 8
#define PSBT_OUT_REDEEM_SCRIPT 0
#define PSBT_OUT_WITNESS_SCRIPT 1
#define PSBT_OUT_BIP32_DERIVATION 2

Dictionaries psbtGlobals(Data psbt);
Dictionaries psbtInputs(Data psbt);
Dictionaries psbtOutputs(Data psbt);

// @[
//   @[ @{} ], // globals
//   @[ @{}, @{}, ... ], // inputs
//   @[ @{}, @{}, ... ], // outputs
// ]
DictionaryOfDictionaries psbtAll(Data psbt);

Data psbtAddInput(Data psbt, int inputIndex, Data key, Data value);
Data psbtAddOutput(Data psbt, int outputIndex, Data key, Data value);

Data psbtFromTx(Data transactionData);
Data txFromPsbt(Data psbt);
*/

#endif
