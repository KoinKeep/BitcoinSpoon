//
//  KeyManager.h
//  KoinKeep
//
//  Created by Dustin Dettmer on 10/18/18.
//  Copyright © 2018 Dustin. All rights reserved.
//

#import "Data.h"

extern const char *keyManagerKeyDirectory;

// Override here with your best entropy for the platform you're on
extern void (*KeyManagerCustomEntropy)(char *buf, int length); // Defaults to arc4random_buf

// Return the best unique user data you can get on this device
extern Data (*KeyManagerUniqueData)(); // Defaults to time(0)

// public keys are of type XY with no prefix byte
typedef struct KeyManager {

    int testnet;

    int hasMasterPrivKey;

    uint32_t noncePrefix;

    Datas privKeyHashCache;
    Dict/*Data:Data*/ vaultMasterHdWalletCache;
    Data mainSeed;

} KeyManager;

extern KeyManager km;

void KMInit();

void KMSetTestnet(KeyManager *self, int testnet);

void KMImportMasterPrivKey(KeyManager *self, Data key);
void KMGenerateMasterPrivKey(KeyManager *self);

Data KMMasterPrivKey(KeyManager *self);
Data KMMasterPubKey(KeyManager *self);
Data KMMasterPrivKeyHash(KeyManager *self); // Double sha256 of masterPrivateKey

uint64_t KMNextNonce(KeyManager *self);

Data KMEncKey(KeyManager *self, Data privKey);
Data KMCommPrivKey(KeyManager *self, Data privKey);

/* Primary key management */

Datas KMAllPrivKeyHashes(KeyManager *self);
Datas KMAllPrivKeys(KeyManager *self);
Data KMPrivKeyForHash(KeyManager *self, Data hash);
Data KMPrivKeyAtIndex(KeyManager *self, int index);

// For vault wallets append these paths:
// receiving address /0/n
// change address /1/n
Data KMVaultMasterHdWalletAtIndex(KeyManager *self, int index);

//TODO: methods that modify keys must also modify key names.
// We'll disable methods for modifying keys / key order for now.

//int swapPrivKey(KeyManager *self, int indexA withIndex:(int)indexB;
int KMAddPrivKey(KeyManager *self, Data privKey); // Returns the index

//Datas allHiddenPrivKeyHashes;
//int hidePrivKey(KeyManager *self, int index); // Returns the hidden index
//int unhidePrivKey(KeyManager *self, int)hiddenIndex; // Returns the index
//void deleteHiddenKey(KeyManager *self, int)hiddenIndex;

/* hdWallet equivilent of primary keys */

// For phone wallets append these paths:
// receiving address /0'/0/n
// change address /0'/1/n
Data KMHdWalletIndex(KeyManager *self, uint32_t index);

// Returns all local hdwallet roots, both those used for phone wallets (0)
// and those for vaults (1). The receiving addresses (0) and change address (1)
// derivcations are put in the array.
Datas KMAllHdWalletPubRoots(KeyManager *self);

String KMKeyName(KeyManager *self, uint32_t index);
void KMSetKeyName(KeyManager *self, String name, uint32_t index);
uint32_t KMNamedKeyCount(KeyManager *self);

// Returns -1 if index was not found.
int64_t KMIndexForKeyNamed(KeyManager *self, String name);

/* External hdWallet routines */

typedef enum {
    KeyManagerHdWalletTypeKoinKeep = 0,
    KeyManagerHdWalletTypeManual,
} KeyManagerHdWalletType;

extern const char *knownHDWalletDataKey;
extern const char *knownHDWalletTypeKeepKey;

// Each key is a uuid string, value is a Dict with the above keys ^ set
Dictionary KMKnownHDWallets(KeyManager *self);

Data KMHdWalletFrom(KeyManager *self, String uuid);
String KMUuidFromHDWallet(KeyManager *self, Data hdWalletData);
KeyManagerHdWalletType KMHdWalletType(KeyManager *self, String uuid);

void KMSetHDWalletForUUID(KeyManager *self, Data hdWalletData, String uuid, KeyManagerHdWalletType type);

Data KMEncKeyUuid(KeyManager *self, String uuid);
Data KMCommKeyUuid(KeyManager *self, String uuid);

Data KMPrivKeyHashForDevice(KeyManager *self, String uuid);
void KMSetPrivKeyHashForDevice(KeyManager *self, Data hash, String uuid);

/* Vault routines */

void KMAddVaultObserver(KeyManager *self, Data masterHdWallet, Datas hdWallets, String name);
void KMAddVault(KeyManager *self, Datas hdWallets, String name);
void KMRemoveVault(KeyManager *self, uint32_t index);

Datas KMVaultMasterHdWallets(KeyManager *self);
Datas/*Datas*/ KMVaultHdWallets(KeyManager *self);
Datas/*Datas*/ KMVaultAllHdWallets(KeyManager *self); // includes master and children
Datas/*String*/ KMVaultNames(KeyManager *self);

Data KMVaultScriptDerivation(KeyManager *self, uint32_t index, String path);

/* Sort / hide routines */

// NOTE: These are not currently used for anything (TODO)

// An array of SHA256 hash's of identifying data. The identifying data is the hdWallet or, in the case of multiple
// hdWallets, all hdWallets concatinated together.
Datas KMSortHashes(KeyManager *self);
void KMSetSortHashes(KeyManager *self, Datas sortHashes);

Datas KMHiddenHashes(KeyManager *self);
void KMSetHiddenHashes(KeyManager *self, Datas hiddenHashes);
