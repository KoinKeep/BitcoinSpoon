# Bitcoin Spoon

Bitcoin Spoon is a lightweight library for managing Bitcoin transactions that has minimal dependencies.

Why Bitcoin Spoon? Because a spoon is not a fork. Bitcoin Spoon was written from scratch to run on more limited hardware.

* Compile with ./compile.sh
* Test by running ./test/test
* Run the example with ./test/example
* Check out the code in [test/example.c](https://github.com/KoinKeep/BitcoinSpoon/blob/master/test/example.c) to learn how to use the library.

## Getting Started

### Optionally you may initialize these items. If you initialize them, they must be done first
* `#include "Database.h"`
* `databaseRootPath = "/tmp" // Folder where live and testnet dbs are stored (cannot end in a slash)`
* `#include "KeyManager.h"`
* `keyManagerKeyDirectory = "/tmp" // Folder where live & testnet key files are stored`
* `static void myKeyManagerCustomEntropy(char *buf, int length) { ... }`
* `KeyManagerCustomEntropy = myKeyManagerCustomEntropy;`
* `Data myKeyManagerUniqueData() { ... }`
* `KeyManagerUniqueData = myKeyManagerUniqueData`

### Your program must initialize a few modules before using the library. Those are:
* `#include "BTCUtil.h"`
* `BTCUtilStartup()`
* `#include "BasicStorage.h"`
* `basicStorageSetup(StringRef("/tmp/bs.basicStorage"))`
* `bsSave("testnet", DataInt((int)testnet)) // 1 for testnet, 0 for mainnet`
* `#include "KeyManager.h"`
* `KMInit()`
* `KMSetTestnet(&km, (int)testnet) // 1 for testnet, 0 for mainnet`
* `#include "TransactionTracker.h"`
* `tracker = TTNew((int)testnet) // 1 for testnet, 0 for mainnet`
* `#include "Database.h"`
* `database = DatabaseNew()`
* `#include "NodeManager.h"`
* `NodeManager = NodeManagerNew(walletCreationDate)`
*  `walletCreationDate` is a unix timestamp of when this wallet was first created. This limits how far back in the blockchain we will scan.
* `NodeManager.testnet = (int)testnet // 1 for testnet, 0 for mainnet`

### Your program must have a main loop. That main loop needs to call out to a few things repeatedly on some form of "main thread". Those things are:
* Each loop must start with `DataTrackPush()` and end with `DataTrackPop()`
** This enables semi-automatic memory tracking during the loop, freeing objects during `DataTrackPop`.
* `#include "Notifications.h"`
* `NotificationsProcess()`
* `#include "NodeManager.h"`
* `NodeManagerProcessNodes(&NodeManager)`

## Usage

### Connecting to the Bitcoin network
* `NodeManagerConnectNodes(&NodeManager)`
*  This will connect to 8 nodes and peridocially replace node connections with new ones.

## Generate master key
* If `KMMasterPrivKey(&km).bytes == NULL`, then generate the master private key
*  `KMGenerateMasterPrivKey(&km)`

## Listing accounts
* `KMNamedKeyCount(&km)`
* `KMKeyName(&km, (uint32_t)index)`

## Adding account
* `KMSetKeyName(&km, "Account Name", KMNamedKeyCount(&km))`

## Listing vaults
* `KMVaultNames(&km)`
*  Indicies are consistent amount vault retrieval methods

## Adding vault
* TBD

## Generate recieving addresses
* `Data hdWalletData = KMHdWalletIndex(&km, (uint32_t)index)`
* `hdWalletData = hdWallet(hdWalletData, "0'/0")`
* `hdWalletData = TTUnusedWallet(tracker, hdWalletData, (unsigned int)0) // Increment 0 to get lookahead addresses`
* `Data pubKey = pubKeyFromHdWallet(hdWalletData)`
* `String address = p2pkhAddress(pubKey)`
*  Or, for testnet: `String address = p2pkhAddressTestNet(pubKey)`

## Getting transactions (After blockchain has synced)
* `TransactionAnalyzer ta = TTAnalyzerFor(tracker, KMHdWalletIndex(&km, (uint32_t)walletIndex))`
* `TATotalBalance(&ta)`
* `Datas/*TAEvent*/ events = TAEvents(&ta, (TAEventType)typeMask)`
*  Some options for typeMask:
```
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
```
* `TAAnalyzerForTransactionsMatching(&ta, custom_filter)`
* `TATotalAmount((Datas/*TAEvent*/)events)`
* `TAPaymentCandidate paymentCandidate = TAPaymentCandidateSearch(&ta, (Datas/*TAEvent*/)events)`
* `TAPaymentCandidateRemainder(&paymentCandidate)`
* If you want to keep a `TransactionAnalyzer` for longer than one loop, you must:
*  `DataUntrack(ta)` to capture it, and later,
*  `DataTrack(ta)` when you are done with it.

## Making a transaction
* `Transaction trans = TransactionEmpty()`
* `Data outputScript = addressToPubScript((String)destinationAddress)`
*  You should *always* verify the outputScript matches the destinationAddress to prevent funds being permanently lost
*  `if(!DataEqual(pubScriptToAddress(outputScript), destinationAddress)) abort()`
* `TTAddOutput(outputScript, (uint64_t)amount) // amount is in satoshies`
* You should already have a `TransactionAnalyzer ta` from above
* `Datas/*TAEvent*/ unspents = TAEvents(&ta, TAEventTypeUnspent)`
* `TAPaymentCandidate payment = TAPaymentCandidateSearch((uint64_t)amount + (uint64_t)transactionFee, unspents)`
* `if(payment.amount < (uint64_t)amount + (uint64_t)transactionFee) abort() // Insufficent funds`
* ```FORIN(TAEvent, event, payment.events) {
    Data prevHash = TransactionTxid(event->transaction);
    Data pubScript = TransactionOutputOrNilAt(&event->transaction, event->outputIndex)->script;
    uint64_t value = TransactionOutputOrNilAt(&event->transaction, event->outputIndex)->value;

    TransactionAddInput(&trans, prevHash, event->outputIndex, pubScript, value)->sequence = 0;
}```
* `Data hdWalletData = KMHdWalletIndex(&km, (uint32_t)index) // This will be used for change & signing`
* `Data hdWalletData = hdWallet(hdWalletData, "0'")`
* If we have change in `payment.remainder` (which we almost always will), then we must make an output back to ourselves as change
*  `Data changeWallet = TTUnusedWallet(hdWallet(hdWalletData, "1"), (unsigned int)0)`
*  `Data changePubScript = p2wpkhPubScriptFromPubKey(pubKeyFromHdWallet(changeWallet))`
*  `trans = TransactionAddOutput(trans, changePubScript, payment.remainder)`
* Now we sign the transaction
* `Datas hdWallets = TTAllActiveDerivations(hdWallet(hdWalletData, "0")) // all primary wallets`
* `hdWallets = DatasAddDatasCopy(hdWallets, TTAllActiveDerivations(hdWallet(hdWalletData, "1"))) // all change wallets`
* `trans = TransactionSort(trans)`
* `Datas signatures = DatasNew()`
* `trans = TransactionSign(trans, anyKeysFromHdWallets(hdWallets), &signatures)`
* if `signatures.count` is equal to `trans.inputs.count` then our transaction is valid & can be published!

## Publishing a transaction
* `static void mySendTxResult(NodeManagerErrorType result, void *ptr) { ... }`
* `NodeManagerSendTx(&NodeManager, trans, mySendTxResult, NULL)`

## Getting notified of stuff happening
* `void myListener(Dict dict) { ... }`
* `NotificationsAddListener(EventName, myListener)`

## List of some interesting notifiction event names:
* `NodeManagerBlockchainSyncChange`
* `NodeConnectionStatusChanged`
* `TransactionTrackerTransactionAdded`
* `DatabaseNewTxNotification`
* `DatabaseNewBlockNotification`
* `DatabaseNodeListChangedNotification`
