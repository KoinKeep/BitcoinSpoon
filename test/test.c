#include "../code/BTCUtil.h"
#include "../code/Transaction.h"
#include "../code/MerkleBlock.h"
#include "../code/Node.h"
#include "../code/NodeManager.h"
#include "../code/Database.h"
#include "../code/WorkQueue.h"
#include "../code/BasicStorage.h"
#include "../code/KeyManager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>

//#define TEST_MANUAL_NODE_CONNECTION
//#define DEBUG_DATA_TRACKING

#define AssertEqualData(d1, d2) \
do { if(!DataEqual((d1), (d2))) { \
    printf("[%s %s:%d] %s != %s\n", __FUNCTION__, __FILE__, __LINE__, (char*)toHex(d1).bytes, (char*)toHex(d2).bytes); \
    abort(); } \
} while(0)

#define AssertNotEqualData(d1, d2) \
do { if(DataEqual((d1), (d2))) { \
    printf("[%s %s:%d] %s != %s\n", __FUNCTION__, __FILE__, __LINE__, (char*)toHex(d1).bytes, (char*)toHex(d2).bytes); \
    abort(); } \
} while(0)

#define AssertEqualDatas(d1, d2) \
do { if(!DatasEqual((d1), (d2))) { \
    printf("[%s %s:%d]\n", __FUNCTION__, __FILE__, __LINE__); \
    abort(); } \
} while(0)

#define AssertNotEqualDatas(d1, d2) \
do { if(DatasEqual((d1), (d2))) { \
    printf("[%s %s:%d]\n", __FUNCTION__, __FILE__, __LINE__); \
    abort(); } \
} while(0)

#define AssertTrue(condition) \
do { if(!(condition)) { \
    printf("[%s %s:%d] condition not true (%s)\n", __FUNCTION__, __FILE__, __LINE__, #condition); \
    abort(); } \
} while(0)

#define AssertZero(condition) \
do { if((condition) != 0) { \
    printf("[%s %s:%d] condition not zero (%s is %d)\n", __FUNCTION__, __FILE__, __LINE__, #condition, (condition)); \
    abort(); } \
} while(0)

#define AssertEqual(condition, value) \
do { if((condition) != (value)) { \
    printf("[%s %s:%d] condition not equal (%s is %d but should be %d)\n", __FUNCTION__, __FILE__, __LINE__, #condition, (int)(condition), (int)(value)); \
    abort(); } \
} while(0)

void testBasicStorage(); void testWorkQueueSimple(); void testWorkQueue(); void testWorkQueueThreading(); void testStringComponents(); void testDictionary(); void testData(); void testDatas(); void testHex(); void testRipemd160(); void testHexEncoding(); void testTransactionParsing(); void testSignatures(); void testSegwitSigningExample(); void testSegwitAddresses(); void testSegwitAddressCreation(); void testp2pkhTransaction(); void testp2shTransaction(); void testp2wpkTransaction(); void testp2wshTransaction(); void testInputTypeTest(); void testPubKeySearch(); void testMultisigSearch(); void testEasySign(); void testRemoteSign(); void testSecpDiffie(); void testSecpAdd(); void testDataPadding(); void testEncryptedMessage(); void testTweak(); void testMnemonic(); void testSha(); void testHmacShaBasic(); void testHmacSha(); void testBip39tests(); void testManualHdWallet(); void testHdWallet(); void testHdWalletVector1(); void testHdWalletVector3(); void testBloomFilters(); void testTxSort(); void testBip174(); void testAddressParsing(); void testDecryptBip38(); void testManaulNodeConnection();

struct {
    void (*testFunction)();
    const char *name;
} testFunctions[] =
{
    /* TODO: Do a memory test on Transaction leveraging TransactionTrack, TransactionUntrack, TransactionCopy etc */
    { testDictionary, "testDictionary" },
    { testBasicStorage, "testBasicStorage" },
    { testWorkQueueSimple, "testWorkQueueSimple" },
    { testWorkQueue, "testWorkQueue" },
    { testWorkQueueThreading, "testWorkQueueThreading" },
    { testStringComponents, "testStringComponents"},
    { testData, "testData" },
    { testDatas, "testDatas" },
    { testHex, "testHex" },
    { testRipemd160, "testRipemd160" },
    { testHexEncoding, "testHexEncoding" },
     { testTransactionParsing, "testTransactionParsing" },
     { testSignatures, "testSignatures" },
     { testSegwitSigningExample, "testSegwitSigningExample" },
     { testSegwitAddresses, "testSegwitAddresses" },
     { testSegwitAddressCreation, "testSegwitAddressCreation" },
    // { testp2pkhTransaction, "testp2pkhTransaction" },
    // { testp2shTransaction, "testp2shTransaction" },
    // { testp2wpkTransaction, "testp2wpkTransaction" },
    // { testp2wshTransaction, "testp2wshTransaction" },
     { testInputTypeTest, "testInputTypeTest" },
     { testPubKeySearch, "testPubKeySearch" },
     { testMultisigSearch, "testMultisigSearch" },
     { testEasySign, "testEasySign" },
     { testRemoteSign, "testRemoteSign" },
     { testSecpDiffie, "testSecpDiffie" },
     { testSecpAdd, "testSecpAdd" },
     { testDataPadding, "testDataPadding" },
     { testEncryptedMessage, "testEncryptedMessage" },
     { testTweak, "testTweak" },
     { testMnemonic, "testMnemonic" },
     { testSha, "testSha" },
     { testHmacShaBasic, "testHmacShaBasic" },
     { testHmacSha, "testHmacSha" },
     { testBip39tests, "testBip39tests" },
     { testManualHdWallet, "testManualHdWallet" },
     { testHdWallet, "testHdWallet" },
     { testHdWalletVector1, "testHdWalletVector1" },
     { testHdWalletVector3, "testHdWalletVector3" },
     { testTxSort, "testTxSort" },
    // { testBip174, "testBip174" },
     { testAddressParsing, "testAddressParsing" },
     { testDecryptBip38, "testDecryptBip38" },
     { testBloomFilters, "testBloomFilters" },
#ifdef TEST_MANUAL_NODE_CONNECTION
     { testManaulNodeConnection, "testManaulNodeConnection" },
#endif
    { NULL, NULL, }
};

void testBasicStorage()
{
    bsSave("one", DataInt(1));
    bsSave("two", DataInt(2));

    AssertEqualData(bsLoad("one"), DataInt(1));
    AssertEqualData(bsLoad("two"), DataInt(2));

    bsSave("three", DataInt(3));

    AssertEqualData(bsLoad("three"), DataInt(3));

    Data zero = DataZero(5000);
    Data eight = DataNew(5000);

    memset(eight.bytes, 8, eight.length);

    bsSave("zero", zero);
    bsSave("eight", eight);

    AssertEqualData(bsLoad("zero"), zero);
    AssertEqualData(bsLoad("eight"), eight);

    AssertEqualData(bsLoad("one"), DataInt(1));
    AssertEqualData(bsLoad("two"), DataInt(2));
    AssertEqualData(bsLoad("three"), DataInt(3));
}

static void *testWorkQueueThread(void *arg)
{
    WorkQueue *workQueue = arg;

    WorkQueueExecuteAll(arg);

    return NULL;
}

static void testWorkQueueSimpleWorker(Dict dict)
{
    printf("Simple worker executed\n");
}

void testWorkQueueSimple()
{
    WorkQueue workQueue = WorkQueueNew();

    WorkQueueAdd(&workQueue, testWorkQueueSimpleWorker, (Dict){0, 0});

    pthread_t thread;

    if(pthread_create(&thread, NULL, testWorkQueueThread, (void*)&workQueue) != 0)
        abort();

    WorkQueueWaitUntilEmpty(&workQueue);

    printf("testWorkQueueSimple finished\n");

    WorkQueueFree(workQueue);
}

static void testWorkQueueWorker(Dict dict)
{
    int *value = (int*)DataGetPtr(DictGetS(dict, "value"));
    int multiplier = DataGetInt(DictGetS(dict, "multiplier"));
    int addition = DataGetInt(DictGetS(dict, "addition"));

    printf("%d * (%d) -> ", *value, multiplier);

    *value *= multiplier;

    printf("%d\n", *value);

    printf("%d + (%d) -> ", *value, addition);

    *value += addition;

    printf("%d\n", *value);
}

void testWorkQueue()
{
    WorkQueue workQueue = WorkQueueNew();

    int value = 1;

    Dict dict1 = DictOneS("value", DataPtr(&value));
    Dict dict2 = DictOneS("value", DataPtr(&value));
    Dict dict3 = DictOneS("value", DataPtr(&value));

    DictAddS(&dict1, "multiplier", DataInt(1));
    DictAddS(&dict1, "addition", DataInt(4));

    DictAddS(&dict2, "multiplier", DataInt(5));
    DictAddS(&dict2, "addition", DataInt(25));

    DictAddS(&dict3, "multiplier", DataInt(2));
    DictAddS(&dict3, "addition", DataInt(-67));

    WorkQueueAdd(&workQueue, testWorkQueueWorker, dict1);
    WorkQueueAdd(&workQueue, testWorkQueueWorker, dict2);
    WorkQueueAdd(&workQueue, testWorkQueueWorker, dict3);

    pthread_t thread;

    if(pthread_create(&thread, NULL, testWorkQueueThread, (void*)&workQueue) != 0)
        abort();

    WorkQueueWaitUntilEmpty(&workQueue);

    WorkQueueFree(workQueue);

    AssertEqual(value, 33);
}

void testWorkQueueThreading()
{
    const char *name = "Work Queue Test Thread";

    WorkQueue *workQueue = WorkQueueThreadNamed(name);

    int value = 1;

    Dict dict1 = DictOneS("value", DataPtr(&value));
    Dict dict2 = DictOneS("value", DataPtr(&value));
    Dict dict3 = DictOneS("value", DataPtr(&value));

    DictAddS(&dict1, "multiplier", DataInt(1));
    DictAddS(&dict1, "addition", DataInt(4));

    DictAddS(&dict2, "multiplier", DataInt(5));
    DictAddS(&dict2, "addition", DataInt(25));

    DictAddS(&dict3, "multiplier", DataInt(2));
    DictAddS(&dict3, "addition", DataInt(-67));

    WorkQueueAdd(workQueue, testWorkQueueWorker, dict1);
    WorkQueueAdd(workQueue, testWorkQueueWorker, dict2);
    WorkQueueAdd(workQueue, testWorkQueueWorker, dict3);

    WorkQueueWaitUntilEmpty(workQueue);
    WorkQueueThreadWaitAndDestroy(name);

    AssertEqual(value, 33);
}

void testStringComponents()
{
    Datas result = StringComponents(StringNew(",a,bbbbbbbbbb,c,"), ',');

    AssertEqual(result.count, 5);

    AssertEqualData(StringIndex(result, 0), StringNew(""));
    AssertEqualData(StringIndex(result, 1), StringNew("a"));
    AssertEqualData(StringIndex(result, 2), StringNew("bbbbbbbbbb"));
    AssertEqualData(StringIndex(result, 3), StringNew("c"));
    AssertEqualData(StringIndex(result, 4), StringNew(""));
}

void testDictionary()
{
    Dictionary dict = DictionaryNew(0);

    dict = DictionaryAddCopy(dict, DataInt(0), StringNew("one"));
    dict = DictionaryAddCopy(dict, DataInt(1), StringNew("two"));
    dict = DictionaryAddCopy(dict, DataInt(2), StringNew("three"));
    dict = DictionaryAddCopy(dict, DataInt(3), StringNew("four"));

    AssertEqualData(DictionaryGetValue(dict, DataInt(2)), StringNew("three"));

    dict = DictionaryAddCopy(dict, DataInt(0), StringNew("zero"));
    dict = DictionaryAddCopy(dict, DataInt(3), StringNew("three"));

    AssertEqualData(DictionaryGetValue(dict, DataInt(0)), StringNew("zero"));
    AssertEqualData(DictionaryGetValue(dict, DataInt(1)), StringNew("two"));
    AssertEqualData(DictionaryGetValue(dict, DataInt(2)), StringNew("three"));
    AssertEqualData(DictionaryGetValue(dict, DataInt(3)), StringNew("three"));

    AssertEqual(DictionaryCount(dict), 4);

    int count = DataTrackCount();

    Dictionary copy = DictionaryCopy(dict);

    DictionaryUntrack(copy);

    AssertEqual(DataTrackCount(), count);

    DictionaryFree(copy);

    Datas arrayOfDicts = DatasNew();

    DataTrackPush();

    Dict testDict = DictUntrack(DictOneS("ten", DataInt(10)));

    arrayOfDicts = DatasUntrack(DatasAddCopy(arrayOfDicts, DataDict(testDict)));

    DataTrackPop();

    AssertEqual(arrayOfDicts.count, 1);

    Dict testDictRef = DataGetDict(arrayOfDicts.ptr[0]);

    AssertEqualData(DictGetS(testDictRef, "ten"), DataInt(10));

    DatasFree(arrayOfDicts);
}

void testData()
{
    int startingCount = DataAllocatedCount();

    DataTrackPush();

    Data data = DataNew(10345);

    DataUntrack(data);

    Datas datas = DatasNew();

    datas = DatasAddRef(datas, data);

    DataTrackPop();

    AssertEqual(DataAllocatedCount() - startingCount, 1);

    DataFree(data);

    DataTrackPush();
    DataTrackPush();

    Datas dataGrabber = DatasAddRef(DatasNew(), StringNew("Hello Heaven"));

    DatasTranscend(dataGrabber);

    DataTrackPop();

    strcpy(dataGrabber.ptr[0].bytes, "Goodbye Karl");

    Data str = StringNew("a");

    str = DataInsert(str, 0, DataRef("!", 1));

    AssertEqualData(str, StringNew("!a"));

    str = DataDelete(str, 1, 1);

    AssertEqualData(str, StringNew("!"));
    
    DataTrackPop();

    AssertEqual(DataAllocatedCount() - startingCount, 0);
}

void testDatas()
{
    Datas datas = DatasNew();

    datas = DatasAddCopy(datas, DataInt(0));
    datas = DatasAddCopy(datas, DataInt(1));
    datas = DatasAddCopy(datas, DataInt(2));
    datas = DatasAddCopy(datas, DataInt(3));

    AssertEqual(DataGetInt(datas.ptr[0]), (0));
    AssertEqual(DataGetInt(datas.ptr[1]), (1));
    AssertEqual(DataGetInt(datas.ptr[2]), (2));
    AssertEqual(DataGetInt(datas.ptr[3]), (3));

    datas = DatasAddCopyIndex(datas, DataInt(22), datas.count - 1);

    AssertEqual(DataGetInt(datas.ptr[0]), (0));
    AssertEqual(DataGetInt(datas.ptr[1]), (1));
    AssertEqual(DataGetInt(datas.ptr[2]), (2));
    AssertEqual(DataGetInt(datas.ptr[3]), (22));
    AssertEqual(DataGetInt(datas.ptr[4]), (3));

    datas = DatasAddCopyIndex(datas, DataInt(11), 2);

    AssertEqual(DataGetInt(datas.ptr[0]), (0));
    AssertEqual(DataGetInt(datas.ptr[1]), (1));
    AssertEqual(DataGetInt(datas.ptr[2]), (11));
    AssertEqual(DataGetInt(datas.ptr[3]), (2));
    AssertEqual(DataGetInt(datas.ptr[4]), (22));
    AssertEqual(DataGetInt(datas.ptr[5]), (3));

    int i = 0;

    FORIN(int, value, datas) {

        if(i == 0)
            AssertEqual(*value, (0));

        if(i == 1)
            AssertEqual(*value, (1));

        if(i == 2)
            AssertEqual(*value, (11));

        if(i == 3)
            AssertEqual(*value, (2));

        if(i == 4)
            AssertEqual(*value, (22));

        if(i == 5)
            AssertEqual(*value, (3));

        i++;
    }

    AssertEqual(i, 6);
}

void testHex()
{
    const char *str = "9c1185a5c5e9fc54612808977ee8f548b2258d31";

    Data value = fromHex(str);
    String result = toHex(value);

    AssertEqualData(StringNew(str), result);
}

void testRipemd160()
{
    char *messageHashes[] =
    {
        "", "9c1185a5c5e9fc54612808977ee8f548b2258d31",
        "a", "0bdc9d2d256b3ee9daae347be6f4dc835a467ffe",
        "abc", "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc",
        "message digest", "5d0689ef49d2fae572b881b123a85ffa21595f36",
        "abcdefghijklmnopqrstuvwxyz", "f71c27109c692c1b56bbdceb5b9d2865b3708dbc",
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "12a053384a9c0c88e405a06c27dcf49ada62eb2b",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "b0e20b6e3116640286ed3a87a5713079b21f5189",
        "12345678901234567890123456789012345678901234567890123456789012345678901234567890", "9b752e45573d4b39f4dbd3323cab82bf63326bfb",
        NULL
    };

    for(int i = 0; messageHashes[i];) {

        Data hash = ripemd160(DataCopy(messageHashes[i], strlen(messageHashes[i])));

        i++;

        Data value = fromHex(messageHashes[i++]);

        AssertEqualData(hash, value);
    }
}

void testHexEncoding()
{
    String initial = StringNew("aBcdef0123456789FEDBCA618237468701");
    String lowercase = StringLowercase(initial);

    String result = toHex(fromHex(initial.bytes));

    AssertEqualData(lowercase, result);
}

void testTransactionParsing()
{
    Datas transactions = DatasNew();

    transactions = DatasAddRef(transactions, StringNew("01000000000102fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f00000000494830450221008b9d1dc26ba6a9cb62127b02742fa9d754cd3bebf337f7a55d114c8e5cdd30be022040529b194ba3f9281a99f2b1c0a19c0489bc22ede944ccf4ecbab4cc618ef3ed01eeffffffef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a0100000000ffffffff02202cb206000000001976a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac9093510d000000001976a9143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac000247304402203609e17b84f6a7d30c80bfa610b5b4542f32a8a0d5447a12fb1366d7f01cc44a0220573a954c4518331561406f90300e8f3358f51928d43c212a8caed02de67eebee0121025476c2e83188368da1ff3e292e7acafcdb3566bb0ad253f62fc70f07aeee635711000000"));
    transactions = DatasAddRef(transactions, StringNew("01000000000101db6b1b20aa0fd7b23880be2ecbd4a98130974cf4748fb66092ac4d3ceb1a5477010000001716001479091972186c449eb1ded22b78e40d009bdf0089feffffff02b8b4eb0b000000001976a914a457b684d7f0d539a46a45bbc043f35b59d0d96388ac0008af2f000000001976a914fd270b1ee6abcaea97fea7ad0402e8bd8ad6d77c88ac02473044022047ac8e878352d3ebbde1c94ce3a10d057c24175747116f8288e5d794d12d482f0220217f36a485cae903c713331d877c1f64677e3622ad4010726870540656fe9dcb012103ad1d8e89212f0b92c74d23bb710c00662ad1470198ac48c43f7d6f93a2a2687392040000"));
    transactions = DatasAddRef(transactions, StringNew("01000000000102fe3dc9208094f3ffd12645477b3dc56f60ec4fa8e6f5d67c565d1c6b9216b36e000000004847304402200af4e47c9b9629dbecc21f73af989bdaa911f7e6f6c2e9394588a3aa68f81e9902204f3fcf6ade7e5abb1295b6774c8e0abd94ae62217367096bc02ee5e435b67da201ffffffff0815cf020f013ed6cf91d29f4202e8a58726b1ac6c79da47c23d1bee0a6925f80000000000ffffffff0100f2052a010000001976a914a30741f8145e5acadf23f751864167f32e0963f788ac000347304402200de66acf4527789bfda55fc5459e214fa6083f936b430a762c629656216805ac0220396f550692cd347171cbc1ef1f51e15282e837bb2b30860dc77c8f78bc8501e503473044022027dc95ad6b740fe5129e7e62a75dd00f291a2aeb1200b84b09d9e3789406b6c002201a9ecd315dd6a0e632ab20bbb98948bc0c6fb204f2c286963bb48517a7058e27034721026dccc749adc2a9d0d89497ac511f760f45c47dc5ed9cf352a58ac706453880aeadab210255a9626aebf5e29c0e6538428ba0d1dcf6ca98ffdf086aa8ced5e0d0215ea465ac00000000"));
    transactions = DatasAddRef(transactions, StringNew("01000000000102e9b542c5176808107ff1df906f46bb1f2583b16112b95ee5380665ba7fcfc0010000000000ffffffff80e68831516392fcd100d186b3c2c7b95c80b53c77e77c35ba03a66b429a2a1b0000000000ffffffff0280969800000000001976a914de4b231626ef508c9a74a8517e6783c0546d6b2888ac80969800000000001976a9146648a8cd4531e1ec47f35916de8e259237294d1e88ac02483045022100f6a10b8604e6dc910194b79ccfc93e1bc0ec7c03453caaa8987f7d6c3413566002206216229ede9b4d6ec2d325be245c5b508ff0339bf1794078e20bfe0babc7ffe683270063ab68210392972e2eb617b2388771abe27235fd5ac44af8e61693261550447a4c3e39da98ac024730440220032521802a76ad7bf74d0e2c218b72cf0cbc867066e2e53db905ba37f130397e02207709e2188ed7f08f4c952d9d13986da504502b8c3be59617e043552f506c46ff83275163ab68210392972e2eb617b2388771abe27235fd5ac44af8e61693261550447a4c3e39da98ac00000000"));
    transactions = DatasAddRef(transactions, StringNew("0100000000010136641869ca081e70f394c6948e8af409e18b619df2ed74aa106c1ca29787b96e0100000023220020a16b5755f7f6f96dbd65f5f0d6ab9418b89af4b1f14a1bb8a09062c35f0dcb54ffffffff0200e9a435000000001976a914389ffce9cd9ae88dcc0631e88a821ffdbe9bfe2688acc0832f05000000001976a9147480a33f950689af511e6e84c138dbbd3c3ee41588ac080047304402206ac44d672dac41f9b00e28f4df20c52eeb087207e8d758d76d92c6fab3b73e2b0220367750dbbe19290069cba53d096f44530e4f98acaa594810388cf7409a1870ce01473044022068c7946a43232757cbdf9176f009a928e1cd9a1a8c212f15c1e11ac9f2925d9002205b75f937ff2f9f3c1246e547e54f62e027f64eefa2695578cc6432cdabce271502473044022059ebf56d98010a932cf8ecfec54c48e6139ed6adb0728c09cbe1e4fa0915302e022007cd986c8fa870ff5d2b3a89139c9fe7e499259875357e20fcbb15571c76795403483045022100fbefd94bd0a488d50b79102b5dad4ab6ced30c4069f1eaa69a4b5a763414067e02203156c6a5c9cf88f91265f5a942e96213afae16d83321c8b31bb342142a14d16381483045022100a5263ea0553ba89221984bd7f0b13613db16e7a70c549a86de0cc0444141a407022005c360ef0ae5a5d4f9f2f87a56c1546cc8268cab08c73501d6b3be2e1e1a8a08824730440220525406a1482936d5a21888260dc165497a90a15669636d8edca6b9fe490d309c022032af0c646a34a44d1f4576bf6a4a74b67940f8faa84c7df9abe12a01a11e2b4783cf56210307b8ae49ac90a048e9b53357a2354b3334e9c8bee813ecb98e99a7e07e8c3ba32103b28f0c28bfab54554ae8c658ac5c3e0ce6e79ad336331f78c428dd43eea8449b21034b8113d703413d57761b8b9781957b8c0ac1dfe69f492580ca4195f50376ba4a21033400f6afecb833092a9a21cfdf1ed1376e58c5d1f47de74683123987e967a8f42103a6d48b1131e94ba04d9737d61acdaa1322008af9602b3b14862c07a1789aac162102d8b661b0b3302ee2f162b09e07a55ad5dfbe673a9f01d9f0c19617681024306b56ae00000000"));
    transactions = DatasAddRef(transactions, StringNew("0100000000010169c12106097dc2e0526493ef67f21269fe888ef05c7a3a5dacab38e1ac8387f14c1d000000ffffffff01010000000000000000034830450220487fb382c4974de3f7d834c1b617fe15860828c7f96454490edd6d891556dcc9022100baf95feb48f845d5bfc9882eb6aeefa1bc3790e39f59eaa46ff7f15ae626c53e012102a9781d66b61fb5a7ef00ac5ad5bc6ffc78be7b44a566e3c87870e1079368df4c4aad4830450220487fb382c4974de3f7d834c1b617fe15860828c7f96454490edd6d891556dcc9022100baf95feb48f845d5bfc9882eb6aeefa1bc3790e39f59eaa46ff7f15ae626c53e0100000000"));
    transactions = DatasAddRef(transactions, StringNew("010000000001019275cb8d4a485ce95741c013f7c0d28722160008021bb469a11982d47a6628964c1d000000ffffffff0101000000000000000007004830450220487fb382c4974de3f7d834c1b617fe15860828c7f96454490edd6d891556dcc9022100baf95feb48f845d5bfc9882eb6aeefa1bc3790e39f59eaa46ff7f15ae626c53e0148304502205286f726690b2e9b0207f0345711e63fa7012045b9eb0f19c2458ce1db90cf43022100e89f17f86abc5b149eba4115d4f128bcf45d77fb3ecdd34f594091340c0395960101022102966f109c54e85d3aee8321301136cedeb9fc710fdef58a9de8a73942f8e567c021034ffc99dd9a79dd3cb31e2ab3e0b09e0e67db41ac068c625cd1f491576016c84e9552af4830450220487fb382c4974de3f7d834c1b617fe15860828c7f96454490edd6d891556dcc9022100baf95feb48f845d5bfc9882eb6aeefa1bc3790e39f59eaa46ff7f15ae626c53e0148304502205286f726690b2e9b0207f0345711e63fa7012045b9eb0f19c2458ce1db90cf43022100e89f17f86abc5b149eba4115d4f128bcf45d77fb3ecdd34f594091340c039596017500000000"));

    for(int i = 0; i < transactions.count; i++) {

        String hex = transactions.ptr[i];

        Transaction transaction = TransactionNew(fromHex(hex.bytes));

        String endHex = toHex(TransactionData(transaction));

        AssertEqualData(hex, endHex);
    }
}

void testSignatures()
{
    Data key = DataNew(0);
    Data payload = DataNew(0);

    for(int i = 0; i < 8; i++) {

        key = DataAdd(key, uint32D(arc4random()));
        payload = DataAdd(payload, uint32D(arc4random()));
    }

    Data pub = pubKey(key);

    Data sig = signAll(payload, key);

    AssertTrue(verify(sig, payload, pub));

    uint8_t byte = ((uint8_t*)payload.bytes)[0] + 1;

    *payload.bytes += 1;

    AssertTrue(!verify(sig, payload, pub));
}

void testSegwitSigningExample()
{
    // Test comes from BIP 143
    // https://github.com/bitcoin/bips/blob/master/bip-0143.mediawiki

    Datas privKeys = DatasNew();

    privKeys = DatasAddRef(privKeys, fromHex("bbc27228ddcb9209d7fd6f36b02f7dfa6252af40bb2f1cbc7a557da8027ff866"));
    privKeys = DatasAddRef(privKeys, fromHex("619c335025c7f4012e556c2a58b2506e30b8511b53ade95ea316fd8c3286feb9"));

    Datas pubKeys = DatasNew();

    pubKeys = DatasAddRef(pubKeys, pubKey(privKeys.ptr[0]));
    pubKeys = DatasAddRef(pubKeys, pubKey(privKeys.ptr[1]));

    AssertEqualData(pubKeys.ptr[1], fromHex("025476c2e83188368da1ff3e292e7acafcdb3566bb0ad253f62fc70f07aeee6357"));

    Transaction trans = TransactionEmpty();

    trans.version = 1;
    trans.locktime = 17;

    TransactionInputAt(&trans, 0)->previousTransactionHash = fromHex("fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f");
    TransactionInputAt(&trans, 0)->outputIndex = 0;
    TransactionInputAt(&trans, 0)->sequence = 0xffffffee;

    TransactionInputAt(&trans, 1)->previousTransactionHash = fromHex("ef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a");
    TransactionInputAt(&trans, 1)->outputIndex = 1;
    TransactionInputAt(&trans, 1)->sequence = 0xffffffff;

    TransactionInputAt(&trans, 1)->fundingOutput = (TransactionOutput){ 0 };
    TransactionInputAt(&trans, 1)->fundingOutput.script = fromHex("00141d0f172a0ecb48aee1be1f2687d2963ae33f71a1");
    TransactionInputAt(&trans, 1)->fundingOutput.value = 600000000;

    TransactionOutputAt(&trans, 0)->script = fromHex("76a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac");
    TransactionOutputAt(&trans, 0)->value = uint64read(fromHex("202cb20600000000"));

    TransactionOutputAt(&trans, 1)->script = fromHex("76a9143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac");
    TransactionOutputAt(&trans, 1)->value = uint64read(fromHex("9093510d00000000"));

    // Since we are computing the second input, we set it's scriptData to the correct witness script value

    TransactionInputAt(&trans, 1)->scriptData = p2wpkhImpliedScript(pubKeys.ptr[1]);

    Data correctPreimage = fromHex("0100000096b827c8483d4e9b96712b6713a7b68d6e8003a781feba36c31143470b4efd3752b0a642eea2fb7ae638c36f6252b6750293dbe574a806984b8e4d8548339a3bef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a010000001976a9141d0f172a0ecb48aee1be1f2687d2963ae33f71a188ac0046c32300000000ffffffff863ef3e1a92afbfdb97f31ad0fc7683ee943e9abcf2501590ff8f6551f47e5e51100000001000000");

    Data digest = DataNew(0);

    digest = DataAddCopy(digest, uint32D(trans.version));

    Data prevoutsD = DataNew(0);

    for(int i = 0; i < trans.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)trans.inputs.ptr[i].bytes;

        prevoutsD = DataAddCopy(prevoutsD, input->previousTransactionHash);
        prevoutsD = DataAddCopy(prevoutsD, uint32D(input->outputIndex));
    }

    AssertEqualData(DataCopyDataPart(correctPreimage, 0, digest.length), digest);

    digest = DataAddCopy(digest, hash256(prevoutsD));

    AssertEqualData(DataCopyDataPart(correctPreimage, 0, digest.length), digest);

    Data sequence = DataNew(0);

    for(int i = 0; i < trans.inputs.count; i++) {

        TransactionInput *input = (TransactionInput*)trans.inputs.ptr[i].bytes;

        sequence = DataAddCopy(sequence, uint32D(input->sequence));
    }

    digest = DataAddCopy(digest, hash256(sequence));

    AssertEqualData(DataCopyDataPart(correctPreimage, 0, digest.length), digest);

    digest = DataAddCopy(digest, ((TransactionInput*)trans.inputs.ptr[1].bytes)->previousTransactionHash);
    digest = DataAddCopy(digest, uint32D(((TransactionInput*)trans.inputs.ptr[1].bytes)->outputIndex));

    AssertEqualData(DataCopyDataPart(correctPreimage, 0, digest.length), digest);

    digest = DataAddCopy(digest, varIntD(((TransactionInput*)trans.inputs.ptr[1].bytes)->scriptData.length));
    digest = DataAddCopy(digest, ((TransactionInput*)trans.inputs.ptr[1].bytes)->scriptData);

    AssertEqualData(DataCopyDataPart(correctPreimage, 0, digest.length), digest);

    digest = DataAddCopy(digest, uint64D(((TransactionInput*)trans.inputs.ptr[1].bytes)->fundingOutput.value));

    AssertEqualData(DataCopyDataPart(correctPreimage, 0, digest.length), digest);

    digest = DataAddCopy(digest, uint32D(((TransactionInput*)trans.inputs.ptr[1].bytes)->sequence));

    AssertEqualData(DataCopyDataPart(correctPreimage, 0, digest.length), digest);

    Data output = DataNew(0);

    for(int i = 0; i < trans.outputs.count; i++) {

        TransactionOutput *transOut = (TransactionOutput*)trans.outputs.ptr[i].bytes;

        output = DataAddCopy(output, TransactionOutputData(transOut));
    }

    digest = DataAddCopy(digest, hash256(output));

    AssertEqualData(DataCopyDataPart(correctPreimage, 0, digest.length), digest);

    digest = DataAddCopy(digest, uint32D(trans.locktime));

    AssertEqualData(DataCopyDataPart(correctPreimage, 0, digest.length), digest);

    digest = DataAddCopy(digest, uint32D(0x01)); // SIGHASH_ALL

    AssertEqualData(correctPreimage, digest);

    // Digest complete. Now let's compare it to -[Transaction witnessDigest]

    AssertEqualData(digest, TransactionWitnessDigest(trans, 1));

    Data sigHash = hash256(digest);

    AssertEqualData(sigHash, fromHex("c37af31116d1b27caf68aae9e3ac82f1477929014d5b917657d0eb49478cb670"));

    Data sig = signAll(digest, privKeys.ptr[1]);

    AssertEqualData(sig, fromHex("304402203609e17b84f6a7d30c80bfa610b5b4542f32a8a0d5447a12fb1366d7f01cc44a0220573a954c4518331561406f90300e8f3358f51928d43c212a8caed02de67eebee01"));

    // Cheat for first input, second input is what we're testing.
    ((TransactionInput*)trans.inputs.ptr[0].bytes)->scriptData = scriptPush(fromHex("30450221008b9d1dc26ba6a9cb62127b02742fa9d754cd3bebf337f7a55d114c8e5cdd30be022040529b194ba3f9281a99f2b1c0a19c0489bc22ede944ccf4ecbab4cc618ef3ed01"));

    TransactionInput *input2 = (TransactionInput*)trans.inputs.ptr[1].bytes;

    AssertEqualData(TransactionData(trans), fromHex("0100000002fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f00000000494830450221008b9d1dc26ba6a9cb62127b02742fa9d754cd3bebf337f7a55d114c8e5cdd30be022040529b194ba3f9281a99f2b1c0a19c0489bc22ede944ccf4ecbab4cc618ef3ed01eeffffffef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a010000001976a9141d0f172a0ecb48aee1be1f2687d2963ae33f71a188acffffffff02202cb206000000001976a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac9093510d000000001976a9143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac11000000"));

    input2->scriptData = DataNull();

    AssertEqualData(TransactionData(trans), fromHex("0100000002fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f00000000494830450221008b9d1dc26ba6a9cb62127b02742fa9d754cd3bebf337f7a55d114c8e5cdd30be022040529b194ba3f9281a99f2b1c0a19c0489bc22ede944ccf4ecbab4cc618ef3ed01eeffffffef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a0100000000ffffffff02202cb206000000001976a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac9093510d000000001976a9143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac11000000"));

    input2->witnessStack = DatasAddCopy(DatasAddCopy(DatasNew(), sig), pubKeys.ptr[1]);

    AssertEqualData(input2->witnessStack.ptr[0], sig);
    AssertEqualData(input2->witnessStack.ptr[1], pubKeys.ptr[1]);

    AssertEqualData(TransactionInputWitnessData(input2), DataAddCopy(DataAddCopy(varIntD(2), scriptPush(sig)), scriptPush(pubKeys.ptr[1])));

    // Signed transaction. Let's compre it to the Native P2WPKH example result in VIP 143

    AssertEqualData(TransactionData(trans), fromHex("01000000000102fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f00000000494830450221008b9d1dc26ba6a9cb62127b02742fa9d754cd3bebf337f7a55d114c8e5cdd30be022040529b194ba3f9281a99f2b1c0a19c0489bc22ede944ccf4ecbab4cc618ef3ed01eeffffffef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a0100000000ffffffff02202cb206000000001976a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac9093510d000000001976a9143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac000247304402203609e17b84f6a7d30c80bfa610b5b4542f32a8a0d5447a12fb1366d7f01cc44a0220573a954c4518331561406f90300e8f3358f51928d43c212a8caed02de67eebee0121025476c2e83188368da1ff3e292e7acafcdb3566bb0ad253f62fc70f07aeee635711000000"));
}

void testSegwitAddresses()
{
    const char *scripts[] =
    {
        "BC1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KV8F3T4", "751e76e8199196d454941c45d1b3a323f1433bd6",
        "tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q0sl5k7", "1863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262",
        NULL
    };

    for(int i = 0; scripts[i]; i+= 2) {

        String address = StringNew(scripts[i]);
        Data data = fromHex(scripts[i + 1]);

        String computedAddress = toSegwit(data, StringLowercase(DataCopyDataPart(address, 0, 2)).bytes);

        AssertEqualData(StringLowercase(address), computedAddress);

        AssertEqualData(fromSegwit(address), data);
    }

    // This public key comes from BIP 173
    // https://github.com/bitcoin/bips/blob/master/bip-0173.mediawiki#examples

    String bip173PubKey = StringNew("0279BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798");
    Data script = DataAdd(scriptPush(fromHex(bip173PubKey.bytes)), uint8D(OP_CHECKSIG));

    Data hash = hash160(fromHex(bip173PubKey.bytes));
    Data sh = sha256(script);

    String bcpk = toSegwit(hash, "bc");
    String tbpk = toSegwit(hash, "tb");

    String bcsh = toSegwit(sh, "bc");
    String tbsh = toSegwit(sh, "tb");

    AssertEqualData(bcpk, StringNew("bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4"));
    AssertEqualData(tbpk, StringNew("tb1qw508d6qejxtdg4y5r3zarvary0c5xw7kxpjzsx"));
    AssertEqualData(bcsh, StringNew("bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3"));
    AssertEqualData(tbsh, StringNew("tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q0sl5k7"));

    AssertEqualData(bcpk, p2wpkhAddress(fromHex(bip173PubKey.bytes)));
    AssertEqualData(tbpk, p2wpkhAddressTestNet(fromHex(bip173PubKey.bytes)));
    AssertEqualData(bcsh, p2wshAddress(script));
    AssertEqualData(tbsh, p2wshAddressTestNet(script));
}

void testSegwitAddressCreation()
{
    // Address currently has 0.15983105
    // Receiving address is tb1qmaj4tg4zmcw6rclnpu8p238wm7x5h2mthggpqf
    Data key = fromHex("736bdb6991aad17a26f7141eeded7c9595acef1dacef3ebd7c35f57936672603");

    TransactionOutput prevOutput = { 0 };

    prevOutput.script = fromHex("0014999090b10b3a50bda404c79d5b31852caf8474b4");
    prevOutput.value = 15983105;

    String receivingAddress = toSegwit(hash160(pubKey(key)), "tb");

    AssertEqualData(receivingAddress, StringNew("tb1qmaj4tg4zmcw6rclnpu8p238wm7x5h2mthggpqf"));

    Transaction trans = TransactionEmpty();

    TransactionInputAt(&trans, 0)->previousTransactionHash = DataFlipEndianCopy(fromHex("631f334ac1def3658ab1259c84ed223bb146b38c13f4b4289c8117e189025701"));
    TransactionInputAt(&trans, 0)->outputIndex = 0;
    TransactionInputAt(&trans, 0)->sequence = 0xffffffff;

    TransactionOutputAt(&trans, 0)->script = DataAddCopy(fromHex("0014"), hash160(key));
    TransactionOutputAt(&trans, 0)->value = prevOutput.value - 300;

    TransactionInputAt(&trans, 0)->scriptData = p2wpkhImpliedScript(pubKey(key));

    Data sig = signAll(TransactionWitnessDigestFlexible(trans, 0, 15983105), key);

    AssertTrue(validSignature(sig));

    TransactionInputAt(&trans, 0)->witnessStack = DatasTwoCopy(sig, pubKey(key));

    AssertTrue(verify(sig, TransactionWitnessDigestFlexible(trans, 0, 15983105), pubKey(key)));
}
/*
void testp2pkhTransaction()
{
    // Address mvvGKNAxRajPzSN4eRTfUcVpZfsvVvvBoC

    Data key = fromHex("848410F39E14DBF22D118575CA074BA704BAB9A9772ACA6C9141CB5EEE78451A");
    Data pubKeyFull = pubKeyFull(key);

    AssertTrue([p2pkhAddress(pubKeyFull) isEqual:"1GQK2K5ycZJ9DKtSvrVHehHVhgHDaspJqU"]);
    AssertTrue([p2pkhAddressTestNet(pubKeyFull) isEqual:"mvvGKNAxRajPzSN4eRTfUcVpZfsvVvvBoC"]);
    AssertTrue([pubKeyFull isEqual:fromHex("0466D4680980A368AD5C67C145ACEBA7078B475625F361ECE6745FE3DC7C6BA0B35BC25717867CE63539D626EA3F579CF3F035A30248337284AF8EFBE182B9889F")]);

    String address = p2pkhAddressTestNet(pubKeyFull);

    Transaction *trans = [Transaction new];

    NSURL *url = [NSURL URLWithString:["https://testnet.blockchain.info/unspent?active=" stringByAppendingString:address]];

    Data data = [NSData dataWithContentsOfURL:url];

    AssertTrue(data);

    if(!data)
        return;

    NSArray *unspent = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil]["unspent_outputs"];

    int i = 0;

    uint64_t usableMoney = 0;

    for(NSDictionary *item in unspent) {

        [trans inputAt:i].previousTransactionHash = fromHex:item["tx_hash"]];
        [trans inputAt:i].outputIndex = [item["tx_output_n"] unsignedIntValue];
        [trans inputAt:i].sequence = 0xffffffff;

        usableMoney += uint64read:[fromHex:item["value_hex"]] flipEndian]];

        i++;
    }

    usableMoney -= 200; // Miner fee

    int splitCount = arc4random_uniform(10) + 1;

    for(int i = 0; i < splitCount; i++) {

        [trans outputAt:i].script = p2pkhPubScriptFromPubKey(pubKeyFull);
        [trans outputAt:i].value = usableMoney / splitCount;
    }

    AssertTrue([trans.outputs[0].script isEqual:fromHex("76a914bf7f7f8911b0836362c197d7946861fc5dd00d4d88ac")]);

    NSMutableArray *sigs = [NSMutableArray new];

    for(int i = 0; i < trans.inputs.count; i++) {

        trans.inputs[i].scriptData = fromHex:unspent[i]["script"]];

        [sigs addObject:sign(trans.digest key:key)];

        trans.inputs[i].scriptData = nil;
    }

    for(int i = 0; i < trans.inputs.count; i++) {

        trans.inputs[i].scriptData = [scriptPush:sigs[i]] dataWith:scriptPush(pubKeyFull)];
    }

    AssertTrue([trans.tx isEqual:TransactionData(trans)]);

    // Try sending it in using blockcypher

    NSDictionary *cyperObj = @{ "tx": toHex(TransactionData(trans)) };

    NSMutableURLRequest *req = [NSMutableURLRequest new];

    req.URL = [NSURL URLWithString:"https://api.blockcypher.com/v1/btc/test3/txs/push"];
    req.HTTPMethod = "POST";
    req.HTTPBody = [NSJSONSerialization dataWithJSONObject:cyperObj options:0 error:nil];

    NSURLSession *session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]];

    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    NSURLSessionDataTask *task = [session dataTaskWithRequest:req completionHandler:^(Data data, NSURLResponse *response, NSError *error) {

        id obj = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];

        if(obj)
            NSLog("Result: %", obj);
        else if(data)
            NSLog("Result (error): %", [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]);
        else
            NSLog("Response (error): %", response);

        AssertTrue([obj["tx"]["hash"] isEqual:toHex(trans.txid.flipEndian)]);

        dispatch_semaphore_signal(semaphore);
    }];

    BOOL publishTransaction = NO;

    publishTransaction = YES;

    if(publishTransaction) {

        [task resume];

        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    }
}

void testp2shTransaction()
{
    // Address 2Mu81jVdLznTK97T6XUYRmC1srHeKSMY1K6

    Data key = fromHex("848410F39E14DBF22D118575CA074BA704BAB9A9772ACA6C9141CB5EEE78451A");
    Data pubKeyFull = pubKeyFull(key);

    Data redeemScript = [scriptPush(pubKeyFull) dataWith:uint8D(OP_CHECKSIG)];
    String address = p2shAddressTestNet(redeemScript);

    Transaction *trans = [Transaction new];

    NSURL *url = [NSURL URLWithString:["https://testnet.blockchain.info/unspent?active=" stringByAppendingString:address]];

    Data data = [NSData dataWithContentsOfURL:url];

    AssertTrue(data);

    if(!data)
        return;

    NSArray *unspent = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil]["unspent_outputs"];

    uint64_t usableMoney = 0;

    for(int i = 0; i < unspent.count; i++) {

        NSDictionary *item = unspent[i];

        [trans inputAt:i].previousTransactionHash = fromHex:item["tx_hash"]];
        [trans inputAt:i].outputIndex = [item["tx_output_n"] unsignedIntValue];
        [trans inputAt:i].sequence = 0xffffffff;

        usableMoney += uint64read:[fromHex:item["value_hex"]] flipEndian]];
    }

    usableMoney -= 600; // Miner fee

    int splitCount = arc4random_uniform(5) + 1;

    splitCount = 1;

    for(int i = 0; i < splitCount; i++) {

        [trans outputAt:i].script = redeemScript;
        [trans outputAt:i].value = usableMoney / splitCount;
    }

    NSMutableArray *sigs = [NSMutableArray new];

    for(int i = 0; i < trans.inputs.count; i++) {

        trans.inputs[i].scriptData = redeemScript;
        [sigs addObject:sign(trans.digest key:key)];
        trans.inputs[i].scriptData = nil;
    }

    for(int i = 0; i < trans.inputs.count; i++)
        trans.inputs[i].scriptData = [scriptPush:sigs[i]] dataWith:scriptPush(redeemScript)];

    AssertTrue([trans.tx isEqual:TransactionData(trans)]);

    NSDictionary *cyperObj = @{ "tx": toHex(TransactionData(trans)) };

    NSMutableURLRequest *req = [NSMutableURLRequest new];

    req.URL = [NSURL URLWithString:"https://api.blockcypher.com/v1/btc/test3/txs/push"];
    req.HTTPMethod = "POST";
    req.HTTPBody = [NSJSONSerialization dataWithJSONObject:cyperObj options:0 error:nil];

    NSURLSession *session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]];

    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    NSURLSessionDataTask *task = [session dataTaskWithRequest:req completionHandler:^(Data data, NSURLResponse *response, NSError *error) {

        id obj = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];

        if(obj)
            NSLog("Result: %", obj);
        else
            NSLog("Response (error): %", response);

        AssertTrue([obj["tx"]["hash"] isEqual:toHex(trans.txid.flipEndian)]);

        dispatch_semaphore_signal(semaphore);
    }];

    BOOL publishTransaction = NO;

    //publishTransaction = YES;

    if(publishTransaction) {

        [task resume];

        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    }
}

void testp2wpkTransaction()
{
    // Address tb1q4re8l9em7p9xgpday7tx4dhy3mmcc80p8dzh4d

    Data key = fromHex("848410F39E14DBF22D118575CA074BA704BAB9A9772ACA6C9141CB5EEE78451A");
    Data pubKey = pubKey(key);

    String address = p2wpkhAddressTestNet(pubKey);

    AssertEqualData(address, StringNew("tb1q4re8l9em7p9xgpday7tx4dhy3mmcc80p8dzh4d"));

    Transaction *trans = [Transaction new];

    NSURL *unspentUrl = [NSURL URLWithString:[NSString stringWithFormat:"https://testnet-api.smartbit.com.au/v1/blockchain/address/%@/unspent", address]];

    Data unspentData = [NSData dataWithContentsOfURL:unspentUrl];

    NSArray *unspent = [NSJSONSerialization JSONObjectWithData:unspentData options:0 error:nil]["unspent"];

    uint64_t usableMoney = 0;

    for(int i = 0; i < unspent.count; i++) {

        NSDictionary *item = unspent[i];

        [trans inputAt:i].previousTransactionHash = [fromHex:item["txid"]] flipEndian];
        [trans inputAt:i].outputIndex = [item["n"] unsignedIntValue];
        [trans inputAt:i].sequence = 0xffffffff;

        [trans inputAt:i].fundingOutput = [TransactionOutput new];

        [trans inputAt:i].fundingOutput.script = fromHex:item["script_pub_key"]["hex"]];
        [trans inputAt:i].fundingOutput.value = [item["value_int"] unsignedLongLongValue];

        usableMoney += [trans inputAt:i].fundingOutput.value;
    }

    usableMoney -= 1200; // Miner fee

    int splitCount = arc4random_uniform(3) + 1;

    for(int i = 0; i < splitCount; i++) {

        [trans outputAt:i].script = p2wpkhPubScriptFromPubKey(pubKey);
        [trans outputAt:i].value = usableMoney / splitCount;
    }

    for(int i = 0; i < trans.inputs.count; i++) {

        trans.inputs[i].scriptData = scriptPush(p2wpkhImpliedScript(pubKey));

        trans.inputs[i].witnessStack =
        @[
          sign:[trans witnessDigest:i] key:key],
          pubKey,
          ];

        trans.inputs[i].scriptData = nil;
    }

    NSDictionary *cyperObj = @{ "hex": toHex(TransactionData(trans)) };

    NSMutableURLRequest *req = [NSMutableURLRequest new];

    req.URL = [NSURL URLWithString:"https://testnet-api.smartbit.com.au/v1/blockchain/pushtx"];
    req.HTTPMethod = "POST";
    req.HTTPBody = [NSJSONSerialization dataWithJSONObject:cyperObj options:0 error:nil];

    NSURLSession *session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]];

    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    NSURLSessionDataTask *task = [session dataTaskWithRequest:req completionHandler:^(Data data, NSURLResponse *response, NSError *error) {

        id obj = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];

        if(obj)
            NSLog("Result: %", obj);
        else
            NSLog("Response (error): %", response);

        AssertTrue([obj["txid"] isEqual:toHex(trans.txid.flipEndian)]);

        dispatch_semaphore_signal(semaphore);
    }];

    BOOL publishTransaction = NO;

    //publishTransaction = YES;

    if(publishTransaction) {

        [task resume];

        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    }
}

void testp2wshTransaction()
{
    // Address is tb1qtqgexnaqnenhfqmg3sj09jg8flxuth7ju74996lrc9ve66ugnpwq2dkc2h

    Data key = fromHex("848410F39E14DBF22D118575CA074BA704BAB9A9772ACA6C9141CB5EEE78451A");
    Data pubKey = pubKey(key);

    Data witnessScript = [scriptPush(pubKey) dataWith:uint8D(OP_CHECKSIG)];

    String address = p2wshAddressTestNet(witnessScript);

    AssertEqualData(address, StringNew("tb1qtqgexnaqnenhfqmg3sj09jg8flxuth7ju74996lrc9ve66ugnpwq2dkc2h"));

    Transaction *trans = [Transaction new];

    NSURL *unspentUrl = [NSURL URLWithString:[NSString stringWithFormat:"https://testnet-api.smartbit.com.au/v1/blockchain/address/%@/unspent", address]];

    Data unspentData = [NSData dataWithContentsOfURL:unspentUrl];

    NSArray *unspent = [NSJSONSerialization JSONObjectWithData:unspentData options:0 error:nil]["unspent"];

    uint64_t usableMoney = 0;

    for(int i = 0; i < unspent.count; i++) {

        NSDictionary *item = unspent[i];

        [trans inputAt:i].previousTransactionHash = [fromHex:item["txid"]] flipEndian];
        [trans inputAt:i].outputIndex = [item["n"] unsignedIntValue];
        [trans inputAt:i].sequence = 0xffffffff;

        [trans inputAt:i].fundingOutput = [TransactionOutput new];

        [trans inputAt:i].fundingOutput.script = fromHex:item["script_pub_key"]["hex"]];
        [trans inputAt:i].fundingOutput.value = [item["value_int"] unsignedLongLongValue];

        usableMoney += [trans inputAt:i].fundingOutput.value;
    }

    usableMoney -= 1200; // Miner fee

    int splitCount = arc4random_uniform(4) + 1;

    for(int i = 0; i < splitCount; i++) {

        [trans outputAt:i].script = p2wshPubScriptWithScript(witnessScript);
        [trans outputAt:i].value = usableMoney / splitCount;
    }

    for(int i = 0; i < trans.inputs.count; i++) {

        id obj = trans.inputs[i].scriptData;

        trans.inputs[i].scriptData = witnessScript;

        trans.inputs[i].witnessStack =
        @[
          sign:[trans witnessDigest:i] key:key],
          witnessScript,
          ];

        trans.inputs[i].scriptData = obj;
    }

    NSDictionary *cyperObj = @{ "hex": toHex(TransactionData(trans)) };

    NSMutableURLRequest *req = [NSMutableURLRequest new];

    req.URL = [NSURL URLWithString:"https://testnet-api.smartbit.com.au/v1/blockchain/pushtx"];
    req.HTTPMethod = "POST";
    req.HTTPBody = [NSJSONSerialization dataWithJSONObject:cyperObj options:0 error:nil];

    NSURLSession *session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]];

    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    NSURLSessionDataTask *task = [session dataTaskWithRequest:req completionHandler:^(Data data, NSURLResponse *response, NSError *error) {

        id obj = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];

        if(obj)
            NSLog("Result: %", obj);
        else
            NSLog("Response (error): %", response);

        AssertTrue([obj["txid"] isEqual:toHex(trans.txid.flipEndian)]);

        dispatch_semaphore_signal(semaphore);
    }];

    BOOL publishTransaction = NO;

    //publishTransaction = YES;

    if(publishTransaction) {

        [task resume];

        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    }
}
*/
void testInputTypeTest()
{
    // Transactions come from BIP 143 examples

    // Native P2WPKH
    Transaction trans = TransactionNew(fromHex("0100000002fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f0000000000eeffffffef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a0100000000ffffffff02202cb206000000001976a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac9093510d000000001976a9143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac11000000"));

    TransactionInputAt(&trans, 0)->fundingOutput.script = fromHex("2103c9f4836b9a4f77fc0d81f7bcb01b7f1b35916864b9476c241ce9fc198bd25432ac");
    TransactionInputAt(&trans, 0)->fundingOutput.value = 625000000;

    TransactionInputAt(&trans, 1)->fundingOutput.script = fromHex("00141d0f172a0ecb48aee1be1f2687d2963ae33f71a1");
    TransactionInputAt(&trans, 1)->fundingOutput.value = 600000000;

    // The first input comes from an ordinary P2PK:
    AssertEqual(TransactionInputType(TransactionInputAt(&trans, 0)), TransactionInputTypePayToPubkey);

    // The second input comes from a P2WPKH witness program:
    AssertEqual(TransactionInputType(TransactionInputAt(&trans, 1)), TransactionInputTypePayToPubkeyWitness);

    // P2SH-P2WPKH
    trans = TransactionNew(fromHex("0100000001db6b1b20aa0fd7b23880be2ecbd4a98130974cf4748fb66092ac4d3ceb1a54770100000000feffffff02b8b4eb0b000000001976a914a457b684d7f0d539a46a45bbc043f35b59d0d96388ac0008af2f000000001976a914fd270b1ee6abcaea97fea7ad0402e8bd8ad6d77c88ac92040000"));

    TransactionInputAt(&trans, 0)->fundingOutput.script = fromHex("a9144733f37cf4db86fbc2efed2500b4f4e49f31202387");
    TransactionInputAt(&trans, 0)->fundingOutput.value = 1000000000;

    TransactionInputAt(&trans, 0)->scriptData = fromHex("16001479091972186c449eb1ded22b78e40d009bdf0089");

    // The input comes from a P2SH-P2WPKH witness program:
    AssertEqual(TransactionInputType(TransactionInputAt(&trans, 0)), TransactionInputTypePayToScriptHash);

    // Native P2WSH
    trans = TransactionNew(fromHex("0100000002fe3dc9208094f3ffd12645477b3dc56f60ec4fa8e6f5d67c565d1c6b9216b36e0000000000ffffffff0815cf020f013ed6cf91d29f4202e8a58726b1ac6c79da47c23d1bee0a6925f80000000000ffffffff0100f2052a010000001976a914a30741f8145e5acadf23f751864167f32e0963f788ac00000000"));

    TransactionInputAt(&trans, 0)->fundingOutput.script = fromHex("21036d5c20fa14fb2f635474c1dc4ef5909d4568e5569b79fc94d3448486e14685f8ac");
    TransactionInputAt(&trans, 0)->fundingOutput.value = 156250000;

    TransactionInputAt(&trans, 1)->fundingOutput.script = fromHex("00205d1b56b63d714eebe542309525f484b7e9d6f686b3781b6f61ef925d66d6f6a0");
    TransactionInputAt(&trans, 1)->fundingOutput.value = 4900000000;

    // The first input comes from an ordinary P2PK:
    AssertEqual(TransactionInputType(TransactionInputAt(&trans, 0)), TransactionInputTypePayToPubkey);

    // The second input comes from a native P2WSH witness program:
    AssertEqual(TransactionInputType(TransactionInputAt(&trans, 1)), TransactionInputTypePayToScriptWitness);

    // P2SH-P2WSH
    trans = TransactionNew(fromHex("010000000136641869ca081e70f394c6948e8af409e18b619df2ed74aa106c1ca29787b96e0100000000ffffffff0200e9a435000000001976a914389ffce9cd9ae88dcc0631e88a821ffdbe9bfe2688acc0832f05000000001976a9147480a33f950689af511e6e84c138dbbd3c3ee41588ac00000000"));

    TransactionInputAt(&trans, 0)->fundingOutput.script = fromHex("a9149993a429037b5d912407a71c252019287b8d27a587");
    TransactionInputAt(&trans, 0)->fundingOutput.value = 987654321;

    TransactionInputAt(&trans, 0)->scriptData = fromHex("220020a16b5755f7f6f96dbd65f5f0d6ab9418b89af4b1f14a1bb8a09062c35f0dcb54");

    // The input comes from a P2SH-P2WSH 6-of-6 multisig witness program:
    AssertEqual(TransactionInputType(TransactionInputAt(&trans, 0)), TransactionInputTypePayToScriptHash);
}

void testPubKeySearch()
{
    Data multisigScript = fromHex("56210307b8ae49ac90a048e9b53357a2354b3334e9c8bee813ecb98e99a7e07e8c3ba32103b28f0c28bfab54554ae8c658ac5c3e0ce6e79ad336331f78c428dd43eea8449b21034b8113d703413d57761b8b9781957b8c0ac1dfe69f492580ca4195f50376ba4a21033400f6afecb833092a9a21cfdf1ed1376e58c5d1f47de74683123987e967a8f42103a6d48b1131e94ba04d9737d61acdaa1322008af9602b3b14862c07a1789aac162102d8b661b0b3302ee2f162b09e07a55ad5dfbe673a9f01d9f0c19617681024306b56ae");

    Datas pubKeys = DatasNew();

    pubKeys = DatasAddCopy(pubKeys, fromHex("0307b8ae49ac90a048e9b53357a2354b3334e9c8bee813ecb98e99a7e07e8c3ba3"));
    pubKeys = DatasAddCopy(pubKeys, fromHex("03b28f0c28bfab54554ae8c658ac5c3e0ce6e79ad336331f78c428dd43eea8449b"));
    pubKeys = DatasAddCopy(pubKeys, fromHex("034b8113d703413d57761b8b9781957b8c0ac1dfe69f492580ca4195f50376ba4a"));
    pubKeys = DatasAddCopy(pubKeys, fromHex("033400f6afecb833092a9a21cfdf1ed1376e58c5d1f47de74683123987e967a8f4"));
    pubKeys = DatasAddCopy(pubKeys, fromHex("03a6d48b1131e94ba04d9737d61acdaa1322008af9602b3b14862c07a1789aac16"));
    pubKeys = DatasAddCopy(pubKeys, fromHex("02d8b661b0b3302ee2f162b09e07a55ad5dfbe673a9f01d9f0c19617681024306b"));

    AssertEqualDatas(pubKeys, allPubKeys(multisigScript));
}

void testMultisigSearch()
{
    Data multisigScript = fromHex("56210307b8ae49ac90a048e9b53357a2354b3334e9c8bee813ecb98e99a7e07e8c3ba32103b28f0c28bfab54554ae8c658ac5c3e0ce6e79ad336331f78c428dd43eea8449b21034b8113d703413d57761b8b9781957b8c0ac1dfe69f492580ca4195f50376ba4a21033400f6afecb833092a9a21cfdf1ed1376e58c5d1f47de74683123987e967a8f42103a6d48b1131e94ba04d9737d61acdaa1322008af9602b3b14862c07a1789aac162102d8b661b0b3302ee2f162b09e07a55ad5dfbe673a9f01d9f0c19617681024306b56ae");

    Datas pubKeys = DatasNew();

    pubKeys = DatasAddCopy(pubKeys, fromHex("0307b8ae49ac90a048e9b53357a2354b3334e9c8bee813ecb98e99a7e07e8c3ba3"));
    pubKeys = DatasAddCopy(pubKeys, fromHex("03b28f0c28bfab54554ae8c658ac5c3e0ce6e79ad336331f78c428dd43eea8449b"));
    pubKeys = DatasAddCopy(pubKeys, fromHex("034b8113d703413d57761b8b9781957b8c0ac1dfe69f492580ca4195f50376ba4a"));
    pubKeys = DatasAddCopy(pubKeys, fromHex("033400f6afecb833092a9a21cfdf1ed1376e58c5d1f47de74683123987e967a8f4"));
    pubKeys = DatasAddCopy(pubKeys, fromHex("03a6d48b1131e94ba04d9737d61acdaa1322008af9602b3b14862c07a1789aac16"));
    pubKeys = DatasAddCopy(pubKeys, fromHex("02d8b661b0b3302ee2f162b09e07a55ad5dfbe673a9f01d9f0c19617681024306b"));

    ScriptTokens tokens = allCheckSigs(multisigScript);

    AssertEqual(tokens.count, 1);
    AssertEqual(ScriptTokenI(tokens, 0).op, OP_CHECKMULTISIG);
    AssertEqualDatas(ScriptTokenI(tokens, 0).pubKeys, pubKeys);
    AssertEqual(ScriptTokenI(tokens, 0).neededSigs, pubKeys.count);
}

void testEasySign()
{
    // Transactions come from BIP 143 examples

    Transaction trans;
    Datas signatureItems;

    // Native P2WPKH
    trans = TransactionNew(fromHex("0100000002fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f0000000000eeffffffef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a0100000000ffffffff02202cb206000000001976a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac9093510d000000001976a9143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac11000000"));

    TransactionInputSetFundingOutput(TransactionInputAt(&trans, 0), 625000000, fromHex("2103c9f4836b9a4f77fc0d81f7bcb01b7f1b35916864b9476c241ce9fc198bd25432ac"));

    TransactionInputSetFundingOutput(TransactionInputAt(&trans, 1), 600000000, fromHex("00141d0f172a0ecb48aee1be1f2687d2963ae33f71a1"));

    signatureItems = DatasTwoCopy(fromHex("bbc27228ddcb9209d7fd6f36b02f7dfa6252af40bb2f1cbc7a557da8027ff866"), fromHex("619c335025c7f4012e556c2a58b2506e30b8511b53ade95ea316fd8c3286feb9"));

    Datas effected = DatasNew();

    trans = TransactionSign(trans, signatureItems, &effected);

    AssertEqual(effected.count, trans.inputs.count);

    // Failure here now???
    AssertEqualData(TransactionData(trans), fromHex("01000000000102fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f00000000494830450221008b9d1dc26ba6a9cb62127b02742fa9d754cd3bebf337f7a55d114c8e5cdd30be022040529b194ba3f9281a99f2b1c0a19c0489bc22ede944ccf4ecbab4cc618ef3ed01eeffffffef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a0100000000ffffffff02202cb206000000001976a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac9093510d000000001976a9143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac000247304402203609e17b84f6a7d30c80bfa610b5b4542f32a8a0d5447a12fb1366d7f01cc44a0220573a954c4518331561406f90300e8f3358f51928d43c212a8caed02de67eebee0121025476c2e83188368da1ff3e292e7acafcdb3566bb0ad253f62fc70f07aeee635711000000"));

    // P2SH-P2WPKH
    trans = TransactionNew(fromHex("0100000001db6b1b20aa0fd7b23880be2ecbd4a98130974cf4748fb66092ac4d3ceb1a54770100000000feffffff02b8b4eb0b000000001976a914a457b684d7f0d539a46a45bbc043f35b59d0d96388ac0008af2f000000001976a914fd270b1ee6abcaea97fea7ad0402e8bd8ad6d77c88ac92040000"));

    TransactionInputSetFundingOutput(TransactionInputAt(&trans, 0), 1000000000, fromHex("a9144733f37cf4db86fbc2efed2500b4f4e49f31202387"));

    signatureItems = DatasTwoCopy(fromHex("eb696a065ef48a2192da5b28b694f87544b30fae8327c4510137a922f32c6dcf"), fromHex("001479091972186c449eb1ded22b78e40d009bdf0089"));

    trans = TransactionSign(trans, signatureItems, &effected);

    AssertEqual(effected.count, trans.inputs.count);

    // SHOULD BE
//    (lldb) po [trans inputAt:0].witnessStack
//    <__NSArrayI 0x600003c27120>(
//    <30440220 47ac8e87 8352d3eb bde1c94c e3a10d05 7c241757 47116f82 88e5d794 d12d482f 0220217f 36a485ca e903c713 331d877c 1f64677e 3622ad40 10726870 540656fe 9dcb01>,
//    <03ad1d8e 89212f0b 92c74d23 bb710c00 662ad147 0198ac48 c43f7d6f 93a2a268 73>
//    )

    // ACTUALLY IS
//    (lldb) p TransactionInputAt(&trans, 0)->witnessStack
//    (Datas) $2 = {
//      ptr = 0x0000000000000000
//      count = 0
//    }

    AssertEqualData(TransactionData(trans), fromHex("01000000000101db6b1b20aa0fd7b23880be2ecbd4a98130974cf4748fb66092ac4d3ceb1a5477010000001716001479091972186c449eb1ded22b78e40d009bdf0089feffffff02b8b4eb0b000000001976a914a457b684d7f0d539a46a45bbc043f35b59d0d96388ac0008af2f000000001976a914fd270b1ee6abcaea97fea7ad0402e8bd8ad6d77c88ac02473044022047ac8e878352d3ebbde1c94ce3a10d057c24175747116f8288e5d794d12d482f0220217f36a485cae903c713331d877c1f64677e3622ad4010726870540656fe9dcb012103ad1d8e89212f0b92c74d23bb710c00662ad1470198ac48c43f7d6f93a2a2687392040000"));

    // Native P2WSH
    // This example uses SIGHASH_SINGLE and CODESEPERATOR neither of which we support so skip it

    // P2SH-P2WSH
    trans = TransactionNew(fromHex("010000000136641869ca081e70f394c6948e8af409e18b619df2ed74aa106c1ca29787b96e0100000000ffffffff0200e9a435000000001976a914389ffce9cd9ae88dcc0631e88a821ffdbe9bfe2688acc0832f05000000001976a9147480a33f950689af511e6e84c138dbbd3c3ee41588ac00000000"));

    TransactionInputSetFundingOutput(TransactionInputAt(&trans, 0), 987654321, fromHex("a9149993a429037b5d912407a71c252019287b8d27a587"));

    signatureItems = DatasNew();
    signatureItems = DatasAddCopy(signatureItems, fromHex("0020a16b5755f7f6f96dbd65f5f0d6ab9418b89af4b1f14a1bb8a09062c35f0dcb54"));
    signatureItems = DatasAddCopy(signatureItems, fromHex("56210307b8ae49ac90a048e9b53357a2354b3334e9c8bee813ecb98e99a7e07e8c3ba32103b28f0c28bfab54554ae8c658ac5c3e0ce6e79ad336331f78c428dd43eea8449b21034b8113d703413d57761b8b9781957b8c0ac1dfe69f492580ca4195f50376ba4a21033400f6afecb833092a9a21cfdf1ed1376e58c5d1f47de74683123987e967a8f42103a6d48b1131e94ba04d9737d61acdaa1322008af9602b3b14862c07a1789aac162102d8b661b0b3302ee2f162b09e07a55ad5dfbe673a9f01d9f0c19617681024306b56ae"));
    signatureItems = DatasAddCopy(signatureItems, fromHex("11fa3d25a17cbc22b29c44a484ba552b5a53149d106d3d853e22fdd05a2d8bb3"));
    signatureItems = DatasAddCopy(signatureItems, fromHex("77bf4141a87d55bdd7f3cd0bdccf6e9e642935fec45f2f30047be7b799120661"));
    signatureItems = DatasAddCopy(signatureItems, fromHex("14af36970f5025ea3e8b5542c0f8ebe7763e674838d08808896b63c3351ffe49"));
    signatureItems = DatasAddCopy(signatureItems, fromHex("fe9a95c19eef81dde2b95c1284ef39be497d128e2aa46916fb02d552485e0323"));
    signatureItems = DatasAddCopy(signatureItems, fromHex("428a7aee9f0c2af0cd19af3cf1c78149951ea528726989b2e83e4778d2c3f890"));
    signatureItems = DatasAddCopy(signatureItems, fromHex("730fff80e1413068a05b57d6a58261f07551163369787f349438ea38ca80fac6"));

    // ^ Purposely put the first key for multisig in the wrong position (last).

    trans = TransactionSign(trans, signatureItems, &effected);

    AssertEqual(effected.count, trans.inputs.count);

    AssertEqualData(TransactionInputAt(&trans, 0)->witnessStack.ptr[0], DataNew(0));

    // The first input in example is signed SIGHASH_ALL
    AssertEqualData(TransactionInputAt(&trans, 0)->witnessStack.ptr[1], fromHex("304402206ac44d672dac41f9b00e28f4df20c52eeb087207e8d758d76d92c6fab3b73e2b0220367750dbbe19290069cba53d096f44530e4f98acaa594810388cf7409a1870ce01"));

    // Test that the keys were sorted to the correct positions
    AssertNotEqualData(TransactionInputAt(&trans, 0)->witnessStack.ptr[6], fromHex("304402206ac44d672dac41f9b00e28f4df20c52eeb087207e8d758d76d92c6fab3b73e2b0220367750dbbe19290069cba53d096f44530e4f98acaa594810388cf7409a1870ce01"));
}

void testRemoteSign()
{
    // Transactions come from BIP 143 examples

    Transaction trans;
    Datas items;
    Data digest, sig;

    // Native P2WPKH
    trans = TransactionNew(fromHex("0100000002fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f0000000000eeffffffef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a0100000000ffffffff02202cb206000000001976a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac9093510d000000001976a9143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac11000000"));

    TransactionInputSetFundingOutput(TransactionInputAt(&trans, 0), 625000000, fromHex("2103c9f4836b9a4f77fc0d81f7bcb01b7f1b35916864b9476c241ce9fc198bd25432ac"));

    TransactionInputSetFundingOutput(TransactionInputAt(&trans, 1), 600000000, fromHex("00141d0f172a0ecb48aee1be1f2687d2963ae33f71a1"));

    Datas privKeys = DatasTwoCopy(fromHex("bbc27228ddcb9209d7fd6f36b02f7dfa6252af40bb2f1cbc7a557da8027ff866"), fromHex("619c335025c7f4012e556c2a58b2506e30b8511b53ade95ea316fd8c3286feb9"));

    Datas effected = DatasNew();

    trans = TransactionAddPubKey(trans, pubKey(privKeys.ptr[0]), &effected);

    AssertEqual(effected.count, 0);

    trans = TransactionAddPubKey(trans, pubKey(privKeys.ptr[1]), &effected);

    AssertEqual(effected.count, 1);

    digest = DatasFirst(TransactionDigestsFor(trans, pubKey(privKeys.ptr[0])));

    AssertTrue(digest.bytes);

    sig = signAll(digest, privKeys.ptr[0]);

    AssertTrue(sig.bytes);

    trans = TransactionAddSignature(trans, sig, &effected);

    AssertEqual(effected.count, 1);

    digest = DatasFirst(TransactionDigestsFor(trans, pubKey(privKeys.ptr[1])));

    AssertTrue(digest.bytes);

    sig = signAll(digest, privKeys.ptr[1]);

    AssertTrue(sig.bytes);

    trans = TransactionAddSignature(trans, sig, &effected);

    AssertEqual(effected.count, 1);

    AssertTrue(TransactionFinalize(&trans));

    AssertEqualData(TransactionData(trans), fromHex("01000000000102fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f00000000494830450221008b9d1dc26ba6a9cb62127b02742fa9d754cd3bebf337f7a55d114c8e5cdd30be022040529b194ba3f9281a99f2b1c0a19c0489bc22ede944ccf4ecbab4cc618ef3ed01eeffffffef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a0100000000ffffffff02202cb206000000001976a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac9093510d000000001976a9143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac000247304402203609e17b84f6a7d30c80bfa610b5b4542f32a8a0d5447a12fb1366d7f01cc44a0220573a954c4518331561406f90300e8f3358f51928d43c212a8caed02de67eebee0121025476c2e83188368da1ff3e292e7acafcdb3566bb0ad253f62fc70f07aeee635711000000"));

    // P2SH-P2WPKH
    trans = TransactionNew(fromHex("0100000001db6b1b20aa0fd7b23880be2ecbd4a98130974cf4748fb66092ac4d3ceb1a54770100000000feffffff02b8b4eb0b000000001976a914a457b684d7f0d539a46a45bbc043f35b59d0d96388ac0008af2f000000001976a914fd270b1ee6abcaea97fea7ad0402e8bd8ad6d77c88ac92040000"));

    TransactionInputSetFundingOutput(TransactionInputAt(&trans, 0), 1000000000, fromHex("a9144733f37cf4db86fbc2efed2500b4f4e49f31202387"));

    items = DatasTwoCopy(fromHex("eb696a065ef48a2192da5b28b694f87544b30fae8327c4510137a922f32c6dcf"), fromHex("001479091972186c449eb1ded22b78e40d009bdf0089"));

    effected = DatasNew();

    trans = TransactionAddScript(trans, items.ptr[1], &effected);

    AssertEqual(effected.count, 1);

    effected = DatasNew();

    trans = TransactionAddPubKey(trans, pubKey(items.ptr[0]), &effected);

    AssertEqual(effected.count, 1);

    digest = DatasFirst(TransactionDigestsFor(trans, pubKey(items.ptr[0])));

    sig = signAll(digest, items.ptr[0]);

    effected = DatasNew();

    trans = TransactionAddSignature(trans, sig, &effected);

    AssertEqual(effected.count, 1);

    AssertTrue(TransactionFinalize(&trans));

    AssertEqualData(TransactionData(trans), fromHex("01000000000101db6b1b20aa0fd7b23880be2ecbd4a98130974cf4748fb66092ac4d3ceb1a5477010000001716001479091972186c449eb1ded22b78e40d009bdf0089feffffff02b8b4eb0b000000001976a914a457b684d7f0d539a46a45bbc043f35b59d0d96388ac0008af2f000000001976a914fd270b1ee6abcaea97fea7ad0402e8bd8ad6d77c88ac02473044022047ac8e878352d3ebbde1c94ce3a10d057c24175747116f8288e5d794d12d482f0220217f36a485cae903c713331d877c1f64677e3622ad4010726870540656fe9dcb012103ad1d8e89212f0b92c74d23bb710c00662ad1470198ac48c43f7d6f93a2a2687392040000"));

    // Native P2WSH
    // This example uses SIGHASH_SINGLE and CODESEPERATOR neither of which we support so skip it

    // P2SH-P2WSH
    trans = TransactionNew(fromHex("010000000136641869ca081e70f394c6948e8af409e18b619df2ed74aa106c1ca29787b96e0100000000ffffffff0200e9a435000000001976a914389ffce9cd9ae88dcc0631e88a821ffdbe9bfe2688acc0832f05000000001976a9147480a33f950689af511e6e84c138dbbd3c3ee41588ac00000000"));

    TransactionInputSetFundingOutput(TransactionInputAt(&trans, 0), 987654321, fromHex("a9149993a429037b5d912407a71c252019287b8d27a587"));

    items = DatasNew();

    items = DatasAddCopy(items, fromHex("0020a16b5755f7f6f96dbd65f5f0d6ab9418b89af4b1f14a1bb8a09062c35f0dcb54"));
    items = DatasAddCopy(items, fromHex("56210307b8ae49ac90a048e9b53357a2354b3334e9c8bee813ecb98e99a7e07e8c3ba32103b28f0c28bfab54554ae8c658ac5c3e0ce6e79ad336331f78c428dd43eea8449b21034b8113d703413d57761b8b9781957b8c0ac1dfe69f492580ca4195f50376ba4a21033400f6afecb833092a9a21cfdf1ed1376e58c5d1f47de74683123987e967a8f42103a6d48b1131e94ba04d9737d61acdaa1322008af9602b3b14862c07a1789aac162102d8b661b0b3302ee2f162b09e07a55ad5dfbe673a9f01d9f0c19617681024306b56ae"));
    items = DatasAddCopy(items, fromHex("11fa3d25a17cbc22b29c44a484ba552b5a53149d106d3d853e22fdd05a2d8bb3"));
    items = DatasAddCopy(items, fromHex("77bf4141a87d55bdd7f3cd0bdccf6e9e642935fec45f2f30047be7b799120661"));
    items = DatasAddCopy(items, fromHex("14af36970f5025ea3e8b5542c0f8ebe7763e674838d08808896b63c3351ffe49"));
    items = DatasAddCopy(items, fromHex("fe9a95c19eef81dde2b95c1284ef39be497d128e2aa46916fb02d552485e0323"));
    items = DatasAddCopy(items, fromHex("428a7aee9f0c2af0cd19af3cf1c78149951ea528726989b2e83e4778d2c3f890"));
    items = DatasAddCopy(items, fromHex("730fff80e1413068a05b57d6a58261f07551163369787f349438ea38ca80fac6"));

    // ^ Purposely put the first key for multisig in the wrong position (last).

    Datas itemsPortion = DatasTwoCopy(items.ptr[0], items.ptr[1]);

    effected = DatasNew();

    trans = TransactionAddScripts(trans, itemsPortion, &effected);

    AssertEqual(effected.count, 1);

    for(int i = 2; i < items.count; i++) {

        digest = DatasFirst(TransactionDigestsFor(trans, pubKey(items.ptr[i])));

        sig = signAll(digest, items.ptr[i]);

        effected = DatasNew();

        trans = TransactionAddSignature(trans, sig, &effected);

        AssertEqual(effected.count, 1);
    }

    AssertTrue(TransactionFinalize(&trans));

    AssertEqualData(TransactionInputAt(&trans, 0)->witnessStack.ptr[0], DataNew(0));

    // The first input in example is signed SIGHASH_ALL
    AssertEqualData(TransactionInputAt(&trans, 0)->witnessStack.ptr[1], fromHex("304402206ac44d672dac41f9b00e28f4df20c52eeb087207e8d758d76d92c6fab3b73e2b0220367750dbbe19290069cba53d096f44530e4f98acaa594810388cf7409a1870ce01"));

    // Test that the keys were sorted to the correct positions
    AssertNotEqualData(TransactionInputAt(&trans, 0)->witnessStack.ptr[6], fromHex("304402206ac44d672dac41f9b00e28f4df20c52eeb087207e8d758d76d92c6fab3b73e2b0220367750dbbe19290069cba53d096f44530e4f98acaa594810388cf7409a1870ce01"));
}

void testSecpDiffie()
{
    uint8_t privKey1[32];
    uint8_t privKey2[32];

    for(int i = 0; i < sizeof(privKey1); i++) {

        privKey1[i] = arc4random_uniform(0xff);
        privKey2[i] = arc4random_uniform(0xff);
    }

    Data privKey1D = DataCopy((void*)privKey1, sizeof(privKey1));
    Data privKey2D = DataCopy((void*)privKey2, sizeof(privKey2));

    Data pubKey1 = pubKey(privKey1D);
    Data pubKey2 = pubKey(privKey2D);

    Data sharedPriv1 = ecdhKey(privKey1D, pubKey2);
    Data sharedPriv2 = ecdhKey(privKey2D, pubKey1);

    AssertEqual(sharedPriv1.length, 32);
    AssertEqualData(sharedPriv1, sharedPriv2);
}

void testSecpAdd()
{
    Data privKey = fromHex("7c3b1f5b57c4d80aef742ffe4de929b6e423b90e5504b6b595c0422df21e979f");
    Data pub = pubKey(privKey);

    uint8_t tweak[32] = {0};

    tweak[0] = 1;

    Data tweakedPrivKey = addToPrivKey(DataCopy((void*)tweak, sizeof(tweak)), privKey);
    Data tweakedPubKey = addToPubKey(DataCopy((void*)tweak, sizeof(tweak)), pub);

    AssertEqualData(tweakedPubKey, pubKeyFull(tweakedPrivKey));
}

void testDataPadding()
{
    uint8_t buffer[64] =
    {
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        5, 5,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        5, 5,
    };

    for(int i = 1; i < sizeof(buffer); i++) {

        Data padded = padData16(DataCopy((void*)buffer, i));

        AssertEqual(padded.length % 16, 0);

        Data unpadded = unpadData16(padded);

        AssertEqualData(DataCopy((void*)buffer, i), unpadded);
    }
}

void testEncryptedMessage()
{
    Datas privKeys = DatasNew();

    for(int i = 0; i < 2; i++) {

        Data privKey = DataNew(0);

        for(int i = 0; i < 32; i++)
            privKey = DataAdd(privKey, uint8D(arc4random_uniform(0xff)));

        privKeys = DatasAddCopy(privKeys, privKey);
    }

    Data sharedKey1 = ecdhKeyRotate(privKeys.ptr[0], pubKey(privKeys.ptr[1]), 2);
    Data sharedKey2 = ecdhKeyRotate(privKeys.ptr[1], pubKey(privKeys.ptr[0]), 2);

    AssertEqualData(sharedKey1, sharedKey2);

    Data simple = DataNew(1);

    *simple.bytes = 9;

    Data payload = encryptAES(simple, sharedKey1, 0);
    Data result = decryptAES(payload, sharedKey1, 0);

    AssertEqualData(simple, result);

    for(int i = 0; i < 130; i++) {

        String message = StringNew("The quick brown fox jumped over the lazy dog. The quick brown fox jumped over the lazy dog.");

        Data payload = encryptAES(message, sharedKey1, i);
        Data result = decryptAES(payload, sharedKey2, i);

        AssertEqualData(result, message);
    }
}

void testTweak()
{
    uint8_t privKey[32] = {
        0xb1, 0x20, 0xfd, 0x7, 0xa5, 0xa5, 0xa5, 0xa5,
        0xad, 0xb3, 0x0, 0x8, 0xa5, 0xa5, 0xa5, 0xa5,
        0xa5, 0xa5, 0xa5, 0xa5, 0x0, 0x0, 0x0, 0x0,
        0x60, 0x0, 0x0, 0x80, 0x40, 0x95, 0xfc, 0x7
    };

    Data priv = DataCopy((void*)privKey, sizeof(privKey));
    Data pub = pubKey(priv);

    uint8_t tweakValue[32] = {
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    };

    Data tweak = DataCopy((void*)tweakValue, sizeof(tweakValue));

    Data privTwo = addToPrivKey(tweak, priv);
    Data pubTwo = addToPubKey(tweak, pub);

    pubTwo = publicKeyCompress(publicKeyParse(pubTwo));

    Data privThree = multiplyWithPrivKey(priv, tweak);
    Data pubThree = multiplyWithPubKey(tweak, pub);

    pubThree = publicKeyCompress(publicKeyParse(pubThree));

    AssertEqualData(pubKey(privTwo), pubTwo);
    AssertEqualData(pubKey(privThree), pubThree);

    AssertEqualData(toHex(privTwo), StringNew("5bcba7b250505050585daab35050505195a17368fb620a6f4ad84c9e1b0a6570"));
    AssertEqualData(toHex(pubTwo), StringNew("03b97e2c0e6036e31881aba8291aa482597895cf2396fd8ca99e3c3def6d58271d"));
    AssertEqualData(toHex(pubKeyFull(privTwo)), StringNew("04b97e2c0e6036e31881aba8291aa482597895cf2396fd8ca99e3c3def6d58271dad3d303262af016a49146a4a1f347334acbfca330b2588cc81325ee253d44c85"));

    AssertEqualData(toHex(privThree), StringNew("499a39a285f715d86285391ff0387828d4263b8ee0c00eadbbfdf90db8532815"));
    AssertEqualData(toHex(pubThree), StringNew("034072177c5ef1e27161a0726e430113a9177b94b092d9afc3278b1645454cfa61"));
    AssertEqualData(toHex(pubKeyFull(privThree)), StringNew("044072177c5ef1e27161a0726e430113a9177b94b092d9afc3278b1645454cfa611482483b097797dd70c301456a6965dd749da48666b5246c796eafa59ec42169"));
}

void testMnemonic()
{
    uint8_t privKey[32] = {
        0xb1, 0x20, 0xfd, 0x7, 0xa5, 0xa5, 0xa5, 0xa5,
        0xad, 0xb3, 0x0, 0x8, 0xa5, 0xa5, 0xa5, 0xa5,
        0xa5, 0xa5, 0xa5, 0xa5, 0x0, 0x0, 0x0, 0x0,
        0x60, 0x0, 0x0, 0x80, 0x40, 0x95, 0xfc, 0x7
    };

    Data priv = DataCopy((void*)privKey, sizeof(privKey));

    AssertEqual(priv.length, 32);

    String mnemonic = toMnemonic(priv);
    Data parsedPriv = fromMnemonic(mnemonic);

    AssertEqualData(priv, parsedPriv);
}

void testSha()
{
    uint8_t payload[] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    Data data = DataCopy(payload, sizeof(payload));

    Data r1 = sha256(data);

    AssertEqualData(r1, fromHex("66840dda154e8a113c31dd0ad32f7f3a366a80e8136979d8f5a101d3d29d6f72"));
    
    Data r2 = sha512(data);

    AssertEqualData(r2, fromHex("1818cc2acd207880a07afc360fd0da87e51ccf17e7c604c4eb16be5788322724c298e1fcc66eb293926993141ef0863c09eda383188cf5df49b910aacac17ec5"));
}

void testHmacShaBasic()
{
    Data key = fromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    Data data = fromHex("4869205468657265");
    Data expectedResult = fromHex("87aa7cdea5ef619d4ff0b4241a1d6cb0"
                              "2379f4e2ce4ec2787ad0b30545e17cde"
                              "daa833b7d6b8a702038b274eaea3f4e4"
                              "be9d914eeb61f1702e696c203a126854");

    Data result = hmacSha512(key, data);

    AssertEqualData(result, expectedResult);
}

void testHmacSha()
{
    uint8_t payloadOne[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t payloadTwo[] = { 8, 7, 6, 5, 4, 3, 2, 1 };

    Data one = DataCopy(payloadOne, sizeof(payloadOne));
    Data two = DataCopy(payloadTwo, sizeof(payloadTwo));

    Data a256 = hmacSha256(one, two);
    Data b256 = hmacSha256(two, one);

    Data a512 = hmacSha512(one, two);
    Data b512 = hmacSha512(two, one);

    AssertEqualData(a256, fromHex("26e7185b764a6fa38b045bc23d34b37e918dc9f28e7b6587dc09ac6fcb8b2ed9"));
    AssertEqualData(b256, fromHex("91b20e0066aa1c46b2578c27979bdd964fd367e60882591849250288bac8546c"));

    AssertEqualData(a512, fromHex("c0bce4d782bd8e6df728ae142527974e070c4d9012788306f972f8f0aa40bfc378dda7991cfeba638a780a3caca8d174334f69f879262b962ac8d27dd485f000"));
    AssertEqualData(b512, fromHex("61ebba410749c4a981317e102ec7fb2f9f6881f4b92a8f4fa4f01bc7b7e2f5e1d4e6c9067f399fa06bf00924e8a7653703bcc42d012f761b6083d50d02ad795f"));
}

void testBip39tests()
{
    String strings[] =
    {
        StringNew("00000000000000000000000000000000"),
        StringNew("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"),
        StringNew("c55257c360c07c72029aebc1b53c05ed0362ada38ead3e3e9efa3708e53495531f09a6987599d18264c1e1c92f2cf141630c7a3c4ab7c81b2f001698e7463b04"),
        StringNew("xprv9s21ZrQH143K3h3fDYiay8mocZ3afhfULfb5GX8kCBdno77K4HiA15Tg23wpbeF1pLfs1c5SPmYHrEpTuuRhxMwvKDwqdKiGJS9XFKzUsAF"),
        StringNew("7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f"),
        StringNew("legal winner thank year wave sausage worth useful legal winner thank yellow"),
        StringNew("2e8905819b8723fe2c1d161860e5ee1830318dbf49a83bd451cfb8440c28bd6fa457fe1296106559a3c80937a1c1069be3a3a5bd381ee6260e8d9739fce1f607"),
        StringNew("xprv9s21ZrQH143K2gA81bYFHqU68xz1cX2APaSq5tt6MFSLeXnCKV1RVUJt9FWNTbrrryem4ZckN8k4Ls1H6nwdvDTvnV7zEXs2HgPezuVccsq"),
        StringNew("80808080808080808080808080808080"),
        StringNew("letter advice cage absurd amount doctor acoustic avoid letter advice cage above"),
        StringNew("d71de856f81a8acc65e6fc851a38d4d7ec216fd0796d0a6827a3ad6ed5511a30fa280f12eb2e47ed2ac03b5c462a0358d18d69fe4f985ec81778c1b370b652a8"),
        StringNew("xprv9s21ZrQH143K2shfP28KM3nr5Ap1SXjz8gc2rAqqMEynmjt6o1qboCDpxckqXavCwdnYds6yBHZGKHv7ef2eTXy461PXUjBFQg6PrwY4Gzq"),
        StringNew("ffffffffffffffffffffffffffffffff"),
        StringNew("zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo wrong"),
        StringNew("ac27495480225222079d7be181583751e86f571027b0497b5b5d11218e0a8a13332572917f0f8e5a589620c6f15b11c61dee327651a14c34e18231052e48c069"),
        StringNew("xprv9s21ZrQH143K2V4oox4M8Zmhi2Fjx5XK4Lf7GKRvPSgydU3mjZuKGCTg7UPiBUD7ydVPvSLtg9hjp7MQTYsW67rZHAXeccqYqrsx8LcXnyd"),
        StringNew("000000000000000000000000000000000000000000000000"),
        StringNew("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon agent"),
        StringNew("035895f2f481b1b0f01fcf8c289c794660b289981a78f8106447707fdd9666ca06da5a9a565181599b79f53b844d8a71dd9f439c52a3d7b3e8a79c906ac845fa"),
        StringNew("xprv9s21ZrQH143K3mEDrypcZ2usWqFgzKB6jBBx9B6GfC7fu26X6hPRzVjzkqkPvDqp6g5eypdk6cyhGnBngbjeHTe4LsuLG1cCmKJka5SMkmU"),
        StringNew("7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f"),
        StringNew("legal winner thank year wave sausage worth useful legal winner thank year wave sausage worth useful legal will"),
        StringNew("f2b94508732bcbacbcc020faefecfc89feafa6649a5491b8c952cede496c214a0c7b3c392d168748f2d4a612bada0753b52a1c7ac53c1e93abd5c6320b9e95dd"),
        StringNew("xprv9s21ZrQH143K3Lv9MZLj16np5GzLe7tDKQfVusBni7toqJGcnKRtHSxUwbKUyUWiwpK55g1DUSsw76TF1T93VT4gz4wt5RM23pkaQLnvBh7"),
        StringNew("808080808080808080808080808080808080808080808080"),
        StringNew("letter advice cage absurd amount doctor acoustic avoid letter advice cage absurd amount doctor acoustic avoid letter always"),
        StringNew("107d7c02a5aa6f38c58083ff74f04c607c2d2c0ecc55501dadd72d025b751bc27fe913ffb796f841c49b1d33b610cf0e91d3aa239027f5e99fe4ce9e5088cd65"),
        StringNew("xprv9s21ZrQH143K3VPCbxbUtpkh9pRG371UCLDz3BjceqP1jz7XZsQ5EnNkYAEkfeZp62cDNj13ZTEVG1TEro9sZ9grfRmcYWLBhCocViKEJae"),
        StringNew("ffffffffffffffffffffffffffffffffffffffffffffffff"),
        StringNew("zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo when"),
        StringNew("0cd6e5d827bb62eb8fc1e262254223817fd068a74b5b449cc2f667c3f1f985a76379b43348d952e2265b4cd129090758b3e3c2c49103b5051aac2eaeb890a528"),
        StringNew("xprv9s21ZrQH143K36Ao5jHRVhFGDbLP6FCx8BEEmpru77ef3bmA928BxsqvVM27WnvvyfWywiFN8K6yToqMaGYfzS6Db1EHAXT5TuyCLBXUfdm"),
        StringNew("0000000000000000000000000000000000000000000000000000000000000000"),
        StringNew("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon art"),
        StringNew("bda85446c68413707090a52022edd26a1c9462295029f2e60cd7c4f2bbd3097170af7a4d73245cafa9c3cca8d561a7c3de6f5d4a10be8ed2a5e608d68f92fcc8"),
        StringNew("xprv9s21ZrQH143K32qBagUJAMU2LsHg3ka7jqMcV98Y7gVeVyNStwYS3U7yVVoDZ4btbRNf4h6ibWpY22iRmXq35qgLs79f312g2kj5539ebPM"),
        StringNew("7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f"),
        StringNew("legal winner thank year wave sausage worth useful legal winner thank year wave sausage worth useful legal winner thank year wave sausage worth title"),
        StringNew("bc09fca1804f7e69da93c2f2028eb238c227f2e9dda30cd63699232578480a4021b146ad717fbb7e451ce9eb835f43620bf5c514db0f8add49f5d121449d3e87"),
        StringNew("xprv9s21ZrQH143K3Y1sd2XVu9wtqxJRvybCfAetjUrMMco6r3v9qZTBeXiBZkS8JxWbcGJZyio8TrZtm6pkbzG8SYt1sxwNLh3Wx7to5pgiVFU"),
        StringNew("8080808080808080808080808080808080808080808080808080808080808080"),
        StringNew("letter advice cage absurd amount doctor acoustic avoid letter advice cage absurd amount doctor acoustic avoid letter advice cage absurd amount doctor acoustic bless"),
        StringNew("c0c519bd0e91a2ed54357d9d1ebef6f5af218a153624cf4f2da911a0ed8f7a09e2ef61af0aca007096df430022f7a2b6fb91661a9589097069720d015e4e982f"),
        StringNew("xprv9s21ZrQH143K3CSnQNYC3MqAAqHwxeTLhDbhF43A4ss4ciWNmCY9zQGvAKUSqVUf2vPHBTSE1rB2pg4avopqSiLVzXEU8KziNnVPauTqLRo"),
        StringNew("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
        StringNew("zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo vote"),
        StringNew("dd48c104698c30cfe2b6142103248622fb7bb0ff692eebb00089b32d22484e1613912f0a5b694407be899ffd31ed3992c456cdf60f5d4564b8ba3f05a69890ad"),
        StringNew("xprv9s21ZrQH143K2WFF16X85T2QCpndrGwx6GueB72Zf3AHwHJaknRXNF37ZmDrtHrrLSHvbuRejXcnYxoZKvRquTPyp2JiNG3XcjQyzSEgqCB"),
        StringNew("9e885d952ad362caeb4efe34a8e91bd2"),
        StringNew("ozone drill grab fiber curtain grace pudding thank cruise elder eight picnic"),
        StringNew("274ddc525802f7c828d8ef7ddbcdc5304e87ac3535913611fbbfa986d0c9e5476c91689f9c8a54fd55bd38606aa6a8595ad213d4c9c9f9aca3fb217069a41028"),
        StringNew("xprv9s21ZrQH143K2oZ9stBYpoaZ2ktHj7jLz7iMqpgg1En8kKFTXJHsjxry1JbKH19YrDTicVwKPehFKTbmaxgVEc5TpHdS1aYhB2s9aFJBeJH"),
        StringNew("6610b25967cdcca9d59875f5cb50b0ea75433311869e930b"),
        StringNew("gravity machine north sort system female filter attitude volume fold club stay feature office ecology stable narrow fog"),
        StringNew("628c3827a8823298ee685db84f55caa34b5cc195a778e52d45f59bcf75aba68e4d7590e101dc414bc1bbd5737666fbbef35d1f1903953b66624f910feef245ac"),
        StringNew("xprv9s21ZrQH143K3uT8eQowUjsxrmsA9YUuQQK1RLqFufzybxD6DH6gPY7NjJ5G3EPHjsWDrs9iivSbmvjc9DQJbJGatfa9pv4MZ3wjr8qWPAK"),
        StringNew("68a79eaca2324873eacc50cb9c6eca8cc68ea5d936f98787c60c7ebc74e6ce7c"),
        StringNew("hamster diagram private dutch cause delay private meat slide toddler razor book happy fancy gospel tennis maple dilemma loan word shrug inflict delay length"),
        StringNew("64c87cde7e12ecf6704ab95bb1408bef047c22db4cc7491c4271d170a1b213d20b385bc1588d9c7b38f1b39d415665b8a9030c9ec653d75e65f847d8fc1fc440"),
        StringNew("xprv9s21ZrQH143K2XTAhys3pMNcGn261Fi5Ta2Pw8PwaVPhg3D8DWkzWQwjTJfskj8ofb81i9NP2cUNKxwjueJHHMQAnxtivTA75uUFqPFeWzk"),
        StringNew("c0ba5a8e914111210f2bd131f3d5e08d"),
        StringNew("scheme spot photo card baby mountain device kick cradle pact join borrow"),
        StringNew("ea725895aaae8d4c1cf682c1bfd2d358d52ed9f0f0591131b559e2724bb234fca05aa9c02c57407e04ee9dc3b454aa63fbff483a8b11de949624b9f1831a9612"),
        StringNew("xprv9s21ZrQH143K3FperxDp8vFsFycKCRcJGAFmcV7umQmcnMZaLtZRt13QJDsoS5F6oYT6BB4sS6zmTmyQAEkJKxJ7yByDNtRe5asP2jFGhT6"),
        StringNew("6d9be1ee6ebd27a258115aad99b7317b9c8d28b6d76431c3"),
        StringNew("horn tenant knee talent sponsor spell gate clip pulse soap slush warm silver nephew swap uncle crack brave"),
        StringNew("fd579828af3da1d32544ce4db5c73d53fc8acc4ddb1e3b251a31179cdb71e853c56d2fcb11aed39898ce6c34b10b5382772db8796e52837b54468aeb312cfc3d"),
        StringNew("xprv9s21ZrQH143K3R1SfVZZLtVbXEB9ryVxmVtVMsMwmEyEvgXN6Q84LKkLRmf4ST6QrLeBm3jQsb9gx1uo23TS7vo3vAkZGZz71uuLCcywUkt"),
        StringNew("9f6a2878b2520799a44ef18bc7df394e7061a224d2c33cd015b157d746869863"),
        StringNew("panda eyebrow bullet gorilla call smoke muffin taste mesh discover soft ostrich alcohol speed nation flash devote level hobby quick inner drive ghost inside"),
        StringNew("72be8e052fc4919d2adf28d5306b5474b0069df35b02303de8c1729c9538dbb6fc2d731d5f832193cd9fb6aeecbc469594a70e3dd50811b5067f3b88b28c3e8d"),
        StringNew("xprv9s21ZrQH143K2WNnKmssvZYM96VAr47iHUQUTUyUXH3sAGNjhJANddnhw3i3y3pBbRAVk5M5qUGFr4rHbEWwXgX4qrvrceifCYQJbbFDems"),
        StringNew("23db8160a31d3e0dca3688ed941adbf3"),
        StringNew("cat swing flag economy stadium alone churn speed unique patch report train"),
        StringNew("deb5f45449e615feff5640f2e49f933ff51895de3b4381832b3139941c57b59205a42480c52175b6efcffaa58a2503887c1e8b363a707256bdd2b587b46541f5"),
        StringNew("xprv9s21ZrQH143K4G28omGMogEoYgDQuigBo8AFHAGDaJdqQ99QKMQ5J6fYTMfANTJy6xBmhvsNZ1CJzRZ64PWbnTFUn6CDV2FxoMDLXdk95DQ"),
        StringNew("8197a4a47f0425faeaa69deebc05ca29c0a5b5cc76ceacc0"),
        StringNew("light rule cinnamon wrap drastic word pride squirrel upgrade then income fatal apart sustain crack supply proud access"),
        StringNew("4cbdff1ca2db800fd61cae72a57475fdc6bab03e441fd63f96dabd1f183ef5b782925f00105f318309a7e9c3ea6967c7801e46c8a58082674c860a37b93eda02"),
        StringNew("xprv9s21ZrQH143K3wtsvY8L2aZyxkiWULZH4vyQE5XkHTXkmx8gHo6RUEfH3Jyr6NwkJhvano7Xb2o6UqFKWHVo5scE31SGDCAUsgVhiUuUDyh"),
        StringNew("066dca1a2bb7e8a1db2832148ce9933eea0f3ac9548d793112d9a95c9407efad"),
        StringNew("all hour make first leader extend hole alien behind guard gospel lava path output census museum junior mass reopen famous sing advance salt reform"),
        StringNew("26e975ec644423f4a4c4f4215ef09b4bd7ef924e85d1d17c4cf3f136c2863cf6df0a475045652c57eb5fb41513ca2a2d67722b77e954b4b3fc11f7590449191d"),
        StringNew("xprv9s21ZrQH143K3rEfqSM4QZRVmiMuSWY9wugscmaCjYja3SbUD3KPEB1a7QXJoajyR2T1SiXU7rFVRXMV9XdYVSZe7JoUXdP4SRHTxsT1nzm"),
        StringNew("f30f8c1da665478f49b001d94c5fc452"),
        StringNew("vessel ladder alter error federal sibling chat ability sun glass valve picture"),
        StringNew("2aaa9242daafcee6aa9d7269f17d4efe271e1b9a529178d7dc139cd18747090bf9d60295d0ce74309a78852a9caadf0af48aae1c6253839624076224374bc63f"),
        StringNew("xprv9s21ZrQH143K2QWV9Wn8Vvs6jbqfF1YbTCdURQW9dLFKDovpKaKrqS3SEWsXCu6ZNky9PSAENg6c9AQYHcg4PjopRGGKmdD313ZHszymnps"),
        StringNew("c10ec20dc3cd9f652c7fac2f1230f7a3c828389a14392f05"),
        StringNew("scissors invite lock maple supreme raw rapid void congress muscle digital elegant little brisk hair mango congress clump"),
        StringNew("7b4a10be9d98e6cba265566db7f136718e1398c71cb581e1b2f464cac1ceedf4f3e274dc270003c670ad8d02c4558b2f8e39edea2775c9e232c7cb798b069e88"),
        StringNew("xprv9s21ZrQH143K4aERa2bq7559eMCCEs2QmmqVjUuzfy5eAeDX4mqZffkYwpzGQRE2YEEeLVRoH4CSHxianrFaVnMN2RYaPUZJhJx8S5j6puX"),
        StringNew("f585c11aec520db57dd353c69554b21a89b20fb0650966fa0a9d6f74fd989d8f"),
        StringNew("void come effort suffer camp survey warrior heavy shoot primary clutch crush open amazing screen patrol group space point ten exist slush involve unfold"),
        StringNew("01f5bced59dec48e362f2c45b5de68b9fd6c92c6634f44d6d40aab69056506f0e35524a518034ddc1192e1dacd32c1ed3eaa3c3b131c88ed8e7e54c49a5d0998"),
        StringNew("xprv9s21ZrQH143K39rnQJknpH1WEPFJrzmAqqasiDcVrNuk926oizzJDDQkdiTvNPr2FYDYzWgiMiC63YmfPAa2oPyNB23r2g7d1yiK6WpqaQS"),
        DataNull()
    };

    for(int i = 0; strings[i].bytes; i += 4) {

        Data key = fromHex(strings[i].bytes);
        String mnemonic = strings[i + 1];
        Data root = fromHex(strings[i + 2].bytes);
        String xpriv = strings[i + 3];

        AssertEqualData(toMnemonic(key), mnemonic);
        AssertEqualData(fromMnemonic(mnemonic), key);

        Data seedPhrase = DataCopy("Bitcoin seed", strlen("Bitcoin seed"));

        Data seed = PBKDF2(toMnemonic(key).bytes, "TREZOR");

        Data tmp = hmacSha512(seedPhrase, seedPhrase);

        Data I = hmacSha512(seedPhrase, seed);

        Data privKey = DataCopyDataPart(I, 0, 32);
        Data chainCode = DataCopyDataPart(I, 32, 32);

        Data mPrivRaw = hdWalletPriv(privKey, chainCode);

        String mPriv =  base58Encode(mPrivRaw);

        AssertEqualData(seed, root);
        AssertEqualData(xpriv, mPriv);
    }
}

void testManualHdWallet()
{
    Data seedPhrase = DataCopy("Bitcoin seed", strlen("Bitcoin seed"));

    Data I = hmacSha512(seedPhrase, fromHex("fffcf9f6f3f0edeae7e4e1dedbd8d5d2cfccc9c6c3c0bdbab7b4b1aeaba8a5a29f9c999693908d8a8784817e7b7875726f6c696663605d5a5754514e4b484542"));

    Data privKey = DataCopyDataPart(I, 0, 32);
    Data chainCode = DataCopyDataPart(I, 32, 32);
    Data pub = pubKey(privKey);

    Data mPubRaw, mPrivRaw;
    String mPub, mPriv, kPub, kPriv;

    mPubRaw = hdWalletPub(pub, chainCode);
    mPrivRaw =  hdWalletPriv(privKey, chainCode);

    mPub = base58Encode(mPubRaw);
    mPriv = base58Encode(mPrivRaw);

    AssertEqualData(mPub, StringNew("xpub661MyMwAqRbcFW31YEwpkMuc5THy2PSt5bDMsktWQcFF8syAmRUapSCGu8ED9W6oDMSgv6Zz8idoc4a6mr8BDzTJY47LJhkJ8UB7WEGuduB"));
    AssertEqualData(mPriv, StringNew("xprv9s21ZrQH143K31xYSDQpPDxsXRTUcvj2iNHm5NUtrGiGG5e2DtALGdso3pGz6ssrdK4PFmM8NSpSBHNqPqm55Qn3LqFtT2emdEXVYsCzC2U"));

    kPub = base58Encode(childKeyDerivation(mPubRaw, 0));
    kPriv = base58Encode(childKeyDerivation(mPrivRaw, 0));

    AssertEqualData(kPub, StringNew("xpub69H7F5d8KSRgmmdJg2KhpAK8SR3DjMwAdkxj3ZuxV27CprR9LgpeyGmXUbC6wb7ERfvrnKZjXoUmmDznezpbZb7ap6r1D3tgFxHmwMkQTPH"));
    AssertEqualData(kPriv, StringNew("xprv9vHkqa6EV4sPZHYqZznhT2NPtPCjKuDKGY38FBWLvgaDx45zo9WQRUT3dKYnjwih2yJD9mkrocEZXo1ex8G81dwSM1fwqWpWkeS3v86pgKt"));

    kPub = base58Encode(publicHdWallet(ckd(ckd(mPrivRaw, 0), 2147483647 | 0x80000000)));
    kPriv = base58Encode(ckd(ckd(mPrivRaw, 0), 2147483647 | 0x80000000));

    AssertEqualData(kPub, StringNew("xpub6ASAVgeehLbnwdqV6UKMHVzgqAG8Gr6riv3Fxxpj8ksbH9ebxaEyBLZ85ySDhKiLDBrQSARLq1uNRts8RuJiHjaDMBU4Zn9h8LZNnBC5y4a"));
    AssertEqualData(kPriv, StringNew("xprv9wSp6B7kry3Vj9m1zSnLvN3xH8RdsPP1Mh7fAaR7aRLcQMKTR2vidYEeEg2mUCTAwCd6vnxVrcjfy2kRgVsFawNzmjuHc2YmYRmagcEPdU9"));

    kPub = base58Encode(ckd(publicHdWallet(ckd(ckd(mPrivRaw, 0), 2147483647 | 0x80000000)), 1));
    kPriv = base58Encode(ckd(ckd(ckd(mPrivRaw, 0), 2147483647 | 0x80000000), 1));

    AssertEqualData(kPub, StringNew("xpub6DF8uhdarytz3FWdA8TvFSvvAh8dP3283MY7p2V4SeE2wyWmG5mg5EwVvmdMVCQcoNJxGoWaU9DCWh89LojfZ537wTfunKau47EL2dhHKon"));
    AssertEqualData(kPriv, StringNew("xprv9zFnWC6h2cLgpmSA46vutJzBcfJ8yaJGg8cX1e5StJh45BBciYTRXSd25UEPVuesF9yog62tGAQtHjXajPPdbRCHuWS6T8XA2ECKADdw4Ef"));
}

void testHdWallet()
{
    Data seedPhrase = DataCopy("Bitcoin seed", strlen("Bitcoin seed"));

    Data I = hmacSha512(seedPhrase, fromHex("fffcf9f6f3f0edeae7e4e1dedbd8d5d2cfccc9c6c3c0bdbab7b4b1aeaba8a5a29f9c999693908d8a8784817e7b7875726f6c696663605d5a5754514e4b484542"));

    Data privKey = DataCopyDataPart(I, 0, 32);
    Data chainCode = DataCopyDataPart(I, 32, 32);
    Data pub = pubKey(privKey);

    Data mPubRaw, mPrivRaw;
    String mPub, mPriv, kPub, kPriv;

    mPubRaw = hdWalletPub(pub, chainCode);
    mPrivRaw =  hdWalletPriv(privKey, chainCode);

    mPub = base58Encode(hdWallet(mPubRaw, "m"));
    mPriv =  base58Encode(hdWallet(mPrivRaw, "m"));

    AssertEqualData(mPub, StringNew("xpub661MyMwAqRbcFW31YEwpkMuc5THy2PSt5bDMsktWQcFF8syAmRUapSCGu8ED9W6oDMSgv6Zz8idoc4a6mr8BDzTJY47LJhkJ8UB7WEGuduB"));
    AssertEqualData(mPriv, StringNew("xprv9s21ZrQH143K31xYSDQpPDxsXRTUcvj2iNHm5NUtrGiGG5e2DtALGdso3pGz6ssrdK4PFmM8NSpSBHNqPqm55Qn3LqFtT2emdEXVYsCzC2U"));

    kPub = base58Encode(hdWallet(mPubRaw, "m/0"));
    kPriv = base58Encode(hdWallet(mPrivRaw, "/0"));

    AssertEqualData(kPub, StringNew("xpub69H7F5d8KSRgmmdJg2KhpAK8SR3DjMwAdkxj3ZuxV27CprR9LgpeyGmXUbC6wb7ERfvrnKZjXoUmmDznezpbZb7ap6r1D3tgFxHmwMkQTPH"));
    AssertEqualData(kPriv, StringNew("xprv9vHkqa6EV4sPZHYqZznhT2NPtPCjKuDKGY38FBWLvgaDx45zo9WQRUT3dKYnjwih2yJD9mkrocEZXo1ex8G81dwSM1fwqWpWkeS3v86pgKt"));

    kPub = base58Encode(publicHdWallet(hdWallet(mPrivRaw, "0/2147483647'")));
    kPriv = base58Encode(hdWallet(mPrivRaw, "/0/2147483647'"));

    AssertEqualData(kPub, StringNew("xpub6ASAVgeehLbnwdqV6UKMHVzgqAG8Gr6riv3Fxxpj8ksbH9ebxaEyBLZ85ySDhKiLDBrQSARLq1uNRts8RuJiHjaDMBU4Zn9h8LZNnBC5y4a"));
    AssertEqualData(kPriv, StringNew("xprv9wSp6B7kry3Vj9m1zSnLvN3xH8RdsPP1Mh7fAaR7aRLcQMKTR2vidYEeEg2mUCTAwCd6vnxVrcjfy2kRgVsFawNzmjuHc2YmYRmagcEPdU9"));

    kPub = base58Encode(publicHdWallet(hdWallet(mPrivRaw, "0/2147483647'/1")));
    kPriv = base58Encode(hdWallet(mPrivRaw, "m/0/2147483647'/1"));

    AssertEqualData(kPub, StringNew("xpub6DF8uhdarytz3FWdA8TvFSvvAh8dP3283MY7p2V4SeE2wyWmG5mg5EwVvmdMVCQcoNJxGoWaU9DCWh89LojfZ537wTfunKau47EL2dhHKon"));
    AssertEqualData(kPriv, StringNew("xprv9zFnWC6h2cLgpmSA46vutJzBcfJ8yaJGg8cX1e5StJh45BBciYTRXSd25UEPVuesF9yog62tGAQtHjXajPPdbRCHuWS6T8XA2ECKADdw4Ef"));

    kPub = base58Encode(hdWallet(publicHdWallet(hdWallet(mPrivRaw, "0/2147483647'")), "1"));

    AssertEqualData(kPub, StringNew("xpub6DF8uhdarytz3FWdA8TvFSvvAh8dP3283MY7p2V4SeE2wyWmG5mg5EwVvmdMVCQcoNJxGoWaU9DCWh89LojfZ537wTfunKau47EL2dhHKon"));

    kPub = base58Encode(hdWallet(publicHdWallet(hdWallet(mPrivRaw, "0/2147483647'")), "/1"));

    AssertEqualData(kPub, StringNew("xpub6DF8uhdarytz3FWdA8TvFSvvAh8dP3283MY7p2V4SeE2wyWmG5mg5EwVvmdMVCQcoNJxGoWaU9DCWh89LojfZ537wTfunKau47EL2dhHKon"));

    kPub = base58Encode(publicHdWallet(hdWallet(mPrivRaw, "0/2147483647'/1/2147483646'")));
    kPriv = base58Encode(hdWallet(mPrivRaw, "/0/2147483647'/1/2147483646'"));

    AssertEqualData(kPub, StringNew("xpub6ERApfZwUNrhLCkDtcHTcxd75RbzS1ed54G1LkBUHQVHQKqhMkhgbmJbZRkrgZw4koxb5JaHWkY4ALHY2grBGRjaDMzQLcgJvLJuZZvRcEL"));
    AssertEqualData(kPriv, StringNew("xprvA1RpRA33e1JQ7ifknakTFpgNXPmW2YvmhqLQYMmrj4xJXXWYpDPS3xz7iAxn8L39njGVyuoseXzU6rcxFLJ8HFsTjSyQbLYnMpCqE2VbFWc"));

    kPub = base58Encode(publicHdWallet(hdWallet(mPrivRaw, "0/2147483647'/1/2147483646'/2")));
    kPriv = base58Encode(hdWallet(mPrivRaw, "/0/2147483647'/1/2147483646'/2"));

    AssertEqualData(kPub, StringNew("xpub6FnCn6nSzZAw5Tw7cgR9bi15UV96gLZhjDstkXXxvCLsUXBGXPdSnLFbdpq8p9HmGsApME5hQTZ3emM2rnY5agb9rXpVGyy3bdW6EEgAtqt"));
    AssertEqualData(kPriv, StringNew("xprvA2nrNbFZABcdryreWet9Ea4LvTJcGsqrMzxHx98MMrotbir7yrKCEXw7nadnHM8Dq38EGfSh6dqA9QWTyefMLEcBYJUuekgW4BYPJcr9E7j"));
}

void testHdWalletVector1()
{
    Data seedPhrase = DataCopy("Bitcoin seed", strlen("Bitcoin seed"));

    Data I = hmacSha512(seedPhrase, fromHex("000102030405060708090a0b0c0d0e0f"));

    Data privKey = DataCopyDataPart(I, 0, 32);
    Data chainCode = DataCopyDataPart(I, 32, 32);
    Data pub = pubKey(privKey);

    Data mPubRaw, mPrivRaw;
    String mPub, mPriv, kPub, kPriv;

    mPubRaw = hdWalletPub(pub, chainCode);
    mPrivRaw =  hdWalletPriv(privKey, chainCode);

    mPub = base58Encode(hdWallet(mPubRaw, "m"));
    mPriv =  base58Encode(hdWallet(mPrivRaw, "m"));

    AssertEqualData(mPub, StringNew("xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8"));
    AssertEqualData(mPriv, StringNew("xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHi"));

    kPub = base58Encode(publicHdWallet(hdWallet(mPrivRaw, "m/0'")));
    kPriv = base58Encode(hdWallet(mPrivRaw, "/0'"));

    AssertEqualData(kPub, StringNew("xpub68Gmy5EdvgibQVfPdqkBBCHxA5htiqg55crXYuXoQRKfDBFA1WEjWgP6LHhwBZeNK1VTsfTFUHCdrfp1bgwQ9xv5ski8PX9rL2dZXvgGDnw"));
    AssertEqualData(kPriv, StringNew("xprv9uHRZZhk6KAJC1avXpDAp4MDc3sQKNxDiPvvkX8Br5ngLNv1TxvUxt4cV1rGL5hj6KCesnDYUhd7oWgT11eZG7XnxHrnYeSvkzY7d2bhkJ7"));

    kPub = base58Encode(publicHdWallet(hdWallet(mPrivRaw, "m/0'/1")));
    kPriv = base58Encode(hdWallet(mPrivRaw, "/0'/1"));

    AssertEqualData(kPub, StringNew("xpub6ASuArnXKPbfEwhqN6e3mwBcDTgzisQN1wXN9BJcM47sSikHjJf3UFHKkNAWbWMiGj7Wf5uMash7SyYq527Hqck2AxYysAA7xmALppuCkwQ"));
    AssertEqualData(kPriv, StringNew("xprv9wTYmMFdV23N2TdNG573QoEsfRrWKQgWeibmLntzniatZvR9BmLnvSxqu53Kw1UmYPxLgboyZQaXwTCg8MSY3H2EU4pWcQDnRnrVA1xe8fs"));

    kPub = base58Encode(publicHdWallet(hdWallet(mPrivRaw, "m/0'/1/2'")));
    kPriv = base58Encode(hdWallet(mPrivRaw, "/0'/1/2'"));

    AssertEqualData(kPub, StringNew("xpub6D4BDPcP2GT577Vvch3R8wDkScZWzQzMMUm3PWbmWvVJrZwQY4VUNgqFJPMM3No2dFDFGTsxxpG5uJh7n7epu4trkrX7x7DogT5Uv6fcLW5"));
    AssertEqualData(kPriv, StringNew("xprv9z4pot5VBttmtdRTWfWQmoH1taj2axGVzFqSb8C9xaxKymcFzXBDptWmT7FwuEzG3ryjH4ktypQSAewRiNMjANTtpgP4mLTj34bhnZX7UiM"));

    kPub = base58Encode(publicHdWallet(hdWallet(mPrivRaw, "m/0'/1/2'/2")));
    kPriv = base58Encode(hdWallet(mPrivRaw, "/0'/1/2'/2"));

    AssertEqualData(kPub, StringNew("xpub6FHa3pjLCk84BayeJxFW2SP4XRrFd1JYnxeLeU8EqN3vDfZmbqBqaGJAyiLjTAwm6ZLRQUMv1ZACTj37sR62cfN7fe5JnJ7dh8zL4fiyLHV"));
    AssertEqualData(kPriv, StringNew("xprvA2JDeKCSNNZky6uBCviVfJSKyQ1mDYahRjijr5idH2WwLsEd4Hsb2Tyh8RfQMuPh7f7RtyzTtdrbdqqsunu5Mm3wDvUAKRHSC34sJ7in334"));

    kPub = base58Encode(publicHdWallet(hdWallet(mPrivRaw, "m/0'/1/2'/2/1000000000")));
    kPriv = base58Encode(hdWallet(mPrivRaw, "/0'/1/2'/2/1000000000"));

    AssertEqualData(kPub, StringNew("xpub6H1LXWLaKsWFhvm6RVpEL9P4KfRZSW7abD2ttkWP3SSQvnyA8FSVqNTEcYFgJS2UaFcxupHiYkro49S8yGasTvXEYBVPamhGW6cFJodrTHy"));
    AssertEqualData(kPriv, StringNew("xprvA41z7zogVVwxVSgdKUHDy1SKmdb533PjDz7J6N6mV6uS3ze1ai8FHa8kmHScGpWmj4WggLyQjgPie1rFSruoUihUZREPSL39UNdE3BBDu76"));
}

void testHdWalletVector3()
{
    Data seedPhrase = DataCopy("Bitcoin seed", strlen("Bitcoin seed"));

    Data I = hmacSha512(seedPhrase, fromHex("4b381541583be4423346c643850da4b320e46a87ae3d2a4e6da11eba819cd4acba45d239319ac14f863b8d5ab5a0d0c64d2e8a1e7d1457df2e5a3c51c73235be"));

    Data privKey = DataCopyDataPart(I, 0, 32);
    Data chainCode = DataCopyDataPart(I, 32, 32);
    Data pub = pubKey(privKey);

    Data mPubRaw, mPrivRaw;
    String mPub, mPriv, kPub, kPriv;

    mPubRaw = hdWalletPub(pub, chainCode);
    mPrivRaw =  hdWalletPriv(privKey, chainCode);

    mPub = base58Encode(hdWallet(mPubRaw, "m"));
    mPriv =  base58Encode(hdWallet(mPrivRaw, "m"));

    AssertEqualData(mPub, StringNew("xpub661MyMwAqRbcEZVB4dScxMAdx6d4nFc9nvyvH3v4gJL378CSRZiYmhRoP7mBy6gSPSCYk6SzXPTf3ND1cZAceL7SfJ1Z3GC8vBgp2epUt13"));
    AssertEqualData(mPriv, StringNew("xprv9s21ZrQH143K25QhxbucbDDuQ4naNntJRi4KUfWT7xo4EKsHt2QJDu7KXp1A3u7Bi1j8ph3EGsZ9Xvz9dGuVrtHHs7pXeTzjuxBrCmmhgC6"));

    kPub = base58Encode(publicHdWallet(hdWallet(mPrivRaw, "m/0'")));
    kPriv = base58Encode(hdWallet(mPrivRaw, "/0'"));

    AssertEqualData(kPub, StringNew("xpub68NZiKmJWnxxS6aaHmn81bvJeTESw724CRDs6HbuccFQN9Ku14VQrADWgqbhhTHBaohPX4CjNLf9fq9MYo6oDaPPLPxSb7gwQN3ih19Zm4Y"));
    AssertEqualData(kPriv, StringNew("xprv9uPDJpEQgRQfDcW7BkF7eTya6RPxXeJCqCJGHuCJ4GiRVLzkTXBAJMu2qaMWPrS7AANYqdq6vcBcBUdJCVVFceUvJFjaPdGZ2y9WACViL4L"));
}

void testBloomFilters()
{
    Datas elements = DatasNew();

    float desiredFalsePercent = 1;

    for(int i = 0; i < 1000; i++)
        elements = DatasAddCopy(elements, uint32D(arc4random()));

    Data bloom = bloomFilter((int)elements.count, desiredFalsePercent / 100, 0);

    for(int i = 0; i < elements.count; i++)
        AssertTrue(bloomFilterAddElement(bloom, elements.ptr[i]));

    for(int i = 0; i < elements.count; i++)
        AssertTrue(bloomFilterCheckElement(bloom, elements.ptr[i]));

    int falsePositive = 0;

    for(int i = 0; i < elements.count; i++) {

        Data falseData = DataAddCopy(elements.ptr[i], uint32D(arc4random()));

        if(bloomFilterCheckElement(bloom, falseData))
            falsePositive++;
    }

    float falsePercent = 100.0f * falsePositive / elements.count;
    float errorRate = fabsf(desiredFalsePercent - falsePercent);

    AssertTrue(errorRate < 1);
}

void testTxSort()
{
    Data data = fromHex("0100000011aad553bb1650007e9982a8ac79d227cd8c831e1573b11f25573a37664e5f3e64000000006a47304402205438cedd30ee828b0938a863e08d810526123746c1f4abee5b7bc2312373450c02207f26914f4275f8f0040ab3375bacc8c5d610c095db8ed0785de5dc57456591a601210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffffc26f3eb7932f7acddc5ddd26602b77e7516079b03090a16e2c2f5485d1fde028000000006b483045022100f81d98c1de9bb61063a5e6671d191b400fda3a07d886e663799760393405439d0220234303c9af4bad3d665f00277fe70cdd26cd56679f114a40d9107249d29c979401210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffff456a9e597129f5df2e11b842833fc19a94c563f57449281d3cd01249a830a1f0000000006a47304402202310b00924794ef68a8f09564fd0bb128838c66bc45d1a3f95c5cab52680f166022039fc99138c29f6c434012b14aca651b1c02d97324d6bd9dd0ffced0782c7e3bd01210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffff571fb3e02278217852dd5d299947e2b7354a639adc32ec1fa7b82cfb5dec530e000000006b483045022100d276251f1f4479d8521269ec8b1b45c6f0e779fcf1658ec627689fa8a55a9ca50220212a1e307e6182479818c543e1b47d62e4fc3ce6cc7fc78183c7071d245839df01210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffff5d8de50362ff33d3526ac3602e9ee25c1a349def086a7fc1d9941aaeb9e91d38010000006b4830450221008768eeb1240451c127b88d89047dd387d13357ce5496726fc7813edc6acd55ac022015187451c3fb66629af38fdb061dfb39899244b15c45e4a7ccc31064a059730d01210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffff60ad3408b89ea19caf3abd5e74e7a084344987c64b1563af52242e9d2a8320f3000000006b4830450221009be4261ec050ebf33fa3d47248c7086e4c247cafbb100ea7cee4aa81cd1383f5022008a70d6402b153560096c849d7da6fe61c771a60e41ff457aac30673ceceafee01210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffffe9b483a8ac4129780c88d1babe41e89dc10a26dedbf14f80a28474e9a11104de010000006b4830450221009bc40eee321b39b5dc26883f79cd1f5a226fc6eed9e79e21d828f4c23190c57e022078182fd6086e265589105023d9efa4cba83f38c674a499481bd54eee196b033f01210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffffe28db9462d3004e21e765e03a45ecb147f136a20ba8bca78ba60ebfc8e2f8b3b000000006a47304402200fb572b7c6916515452e370c2b6f97fcae54abe0793d804a5a53e419983fae1602205191984b6928bf4a1e25b00e5b5569a0ce1ecb82db2dea75fe4378673b53b9e801210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffff7a1ef65ff1b7b7740c662ab6c9735ace4a16279c23a1db5709ed652918ffff54010000006a47304402206bc218a925f7280d615c8ea4f0131a9f26e7fc64cff6eeeb44edb88aba14f1910220779d5d67231bc2d2d93c3c5ab74dcd193dd3d04023e58709ad7ffbf95161be6201210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffff850cecf958468ca7ffa6a490afe13b8c271b1326b0ddc1fdfdf9f3c7e365fdba000000006a473044022047df98cc26bd2bfdc5b2b97c27aead78a214810ff023e721339292d5ce50823d02205fe99dc5f667908974dae40cc7a9475af7fa6671ba44f64a00fcd01fa12ab523012102ca46fa75454650afba1784bc7b079d687e808634411e4beff1f70e44596308a1ffffffff8640e312040e476cf6727c60ca3f4a3ad51623500aacdda96e7728dbdd99e8a5000000006a47304402205566aa84d3d84226d5ab93e6f253b57b3ef37eb09bb73441dae35de86271352a02206ee0b7f800f73695a2073a2967c9ad99e19f6ddf18ce877adf822e408ba9291e01210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffff91c1889c5c24b93b56e643121f7a05a34c10c5495c450504c7b5afcb37e11d7a000000006b483045022100df61d45bbaa4571cdd6c5c822cba458cdc55285cdf7ba9cd5bb9fc18096deb9102201caf8c771204df7fd7c920c4489da7bc3a60e1d23c1a97e237c63afe53250b4a01210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffff2470947216eb81ea0eeeb4fe19362ec05767db01c3aa3006bb499e8b6d6eaa26010000006a473044022031501a0b2846b8822a32b9947b058d89d32fc758e009fc2130c2e5effc925af70220574ef3c9e350cef726c75114f0701fd8b188c6ec5f84adce0ed5c393828a5ae001210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffff0abcd77d65cc14363f8262898335f184d6da5ad060ff9e40bf201741022c2b40010000006b483045022100a6ac110802b699f9a2bff0eea252d32e3d572b19214d49d8bb7405efa2af28f1022033b7563eb595f6d7ed7ec01734e17b505214fe0851352ed9c3c8120d53268e9a01210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffffa43bebbebf07452a893a95bfea1d5db338d23579be172fe803dce02eeb7c037d010000006b483045022100ebc77ed0f11d15fe630fe533dc350c2ddc1c81cfeb81d5a27d0587163f58a28c02200983b2a32a1014bab633bfc9258083ac282b79566b6b3fa45c1e6758610444f401210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffffb102113fa46ce949616d9cda00f6b10231336b3928eaaac6bfe42d1bf3561d6c010000006a473044022010f8731929a55c1c49610722e965635529ed895b2292d781b183d465799906b20220098359adcbc669cd4b294cc129b110fe035d2f76517248f4b7129f3bf793d07f01210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffffb861fab2cde188499758346be46b5fbec635addfc4e7b0c8a07c0a908f2b11b4000000006a47304402207328142bb02ef5d6496a210300f4aea71f67683b842fa3df32cae6c88b49a9bb022020f56ddff5042260cfda2c9f39b7dec858cc2f4a76a987cd2dc25945b04e15fe01210391064d5b2d1c70f264969046fcff853a7e2bfde5d121d38dc5ebd7bc37c2b210ffffffff027064d817000000001976a9144a5fba237213a062f6f57978f796390bdcf8d01588ac00902f50090000001976a9145be32612930b8323add2212a4ec03c1562084f8488ac00000000");

    Transaction tx = TransactionNew(data);

    tx = TransactionSort(tx);

    Data correctInputs[] =
    {
        fromHex("0e53ec5dfb2cb8a71fec32dc9a634a35b7e24799295ddd5278217822e0b31f57"), DataInt(0),
        fromHex("26aa6e6d8b9e49bb0630aac301db6757c02e3619feb4ee0eea81eb1672947024"), DataInt(1),
        fromHex("28e0fdd185542f2c6ea19030b0796051e7772b6026dd5ddccd7a2f93b73e6fc2"), DataInt(0),
        fromHex("381de9b9ae1a94d9c17f6a08ef9d341a5ce29e2e60c36a52d333ff6203e58d5d"), DataInt(1),
        fromHex("3b8b2f8efceb60ba78ca8bba206a137f14cb5ea4035e761ee204302d46b98de2"), DataInt(0),
        fromHex("402b2c02411720bf409eff60d05adad684f135838962823f3614cc657dd7bc0a"), DataInt(1),
        fromHex("54ffff182965ed0957dba1239c27164ace5a73c9b62a660c74b7b7f15ff61e7a"), DataInt(1),
        fromHex("643e5f4e66373a57251fb173151e838ccd27d279aca882997e005016bb53d5aa"), DataInt(0),
        fromHex("6c1d56f31b2de4bfc6aaea28396b333102b1f600da9c6d6149e96ca43f1102b1"), DataInt(1),
        fromHex("7a1de137cbafb5c70405455c49c5104ca3057a1f1243e6563bb9245c9c88c191"), DataInt(0),
        fromHex("7d037ceb2ee0dc03e82f17be7935d238b35d1deabf953a892a4507bfbeeb3ba4"), DataInt(1),
        fromHex("a5e899dddb28776ea9ddac0a502316d53a4a3fca607c72f66c470e0412e34086"), DataInt(0),
        fromHex("b4112b8f900a7ca0c8b0e7c4dfad35c6be5f6be46b3458974988e1cdb2fa61b8"), DataInt(0),
        fromHex("bafd65e3c7f3f9fdfdc1ddb026131b278c3be1af90a4a6ffa78c4658f9ec0c85"), DataInt(0),
        fromHex("de0411a1e97484a2804ff1dbde260ac19de841bebad1880c782941aca883b4e9"), DataInt(1),
        fromHex("f0a130a84912d03c1d284974f563c5949ac13f8342b8112edff52971599e6a45"), DataInt(0),
        fromHex("f320832a9d2e2452af63154bc687493484a0e7745ebd3aaf9ca19eb80834ad60"), DataInt(0),
      };

    Data correctOutputs[] =
    {
        DataLong(400057456), fromHex("76a9144a5fba237213a062f6f57978f796390bdcf8d01588ac"),
        DataLong(40000000000), fromHex("76a9145be32612930b8323add2212a4ec03c1562084f8488ac"),
    };

    for(int i = 0; i < 17 * 2; i += 2) {

        AssertEqualData(correctInputs[i], DataFlipEndianCopy(TransactionInputAt(&tx, i / 2)->previousTransactionHash));
        AssertEqualData(correctInputs[i + 1], DataInt(TransactionInputAt(&tx, i / 2)->outputIndex));
    }

    for(int i = 0; i < 2 * 2; i += 2) {

        AssertEqualData(correctOutputs[i], DataLong(TransactionOutputAt(&tx, i / 2)->value));
        AssertEqualData(correctOutputs[i + 1], TransactionOutputAt(&tx, i / 2)->script);
    }

    data = fromHex("010000000255605dc6f5c3dc148b6da58442b0b2cd422be385eab2ebea4119ee9c268d28350000000049483045022100aa46504baa86df8a33b1192b1b9367b4d729dc41e389f2c04f3e5c7f0559aae702205e82253a54bf5c4f65b7428551554b2045167d6d206dfe6a2e198127d3f7df1501ffffffff55605dc6f5c3dc148b6da58442b0b2cd422be385eab2ebea4119ee9c268d2835010000004847304402202329484c35fa9d6bb32a55a70c0982f606ce0e3634b69006138683bcd12cbb6602200c28feb1e2555c3210f1dddb299738b4ff8bbe9667b68cb8764b5ac17b7adf0001ffffffff0200e1f505000000004341046a0765b5865641ce08dd39690aade26dfbf5511430ca428a3089261361cef170e3929a68aee3d8d4848b0c5111b0a37b82b86ad559fd2a745b44d8e8d9dfdc0cac00180d8f000000004341044a656f065871a353f216ca26cef8dde2f03e8c16202d2e8ad769f02032cb86a5eb5e56842e92e19141d60a01928f8dd2c875a390f67c1f6c94cfc617c0ea45afac00000000");

    tx = TransactionNew(data);

    tx = TransactionSort(tx);

    Data correctInputs2[] =
    {
        fromHex("35288d269cee1941eaebb2ea85e32b42cdb2b04284a56d8b14dcc3f5c65d6055"), DataInt(0),
        fromHex("35288d269cee1941eaebb2ea85e32b42cdb2b04284a56d8b14dcc3f5c65d6055"), DataInt(1),
    };

    Data correctOutputs2[] =
    {
        DataLong(100000000), fromHex("41046a0765b5865641ce08dd39690aade26dfbf5511430ca428a3089261361cef170e3929a68aee3d8d4848b0c5111b0a37b82b86ad559fd2a745b44d8e8d9dfdc0cac"),
        DataLong(2400000000), fromHex("41044a656f065871a353f216ca26cef8dde2f03e8c16202d2e8ad769f02032cb86a5eb5e56842e92e19141d60a01928f8dd2c875a390f67c1f6c94cfc617c0ea45afac"),
    };

    for(int i = 0; i < 2 * 2; i += 2) {

        AssertEqualData(correctInputs2[i], DataFlipEndianCopy(TransactionInputAt(&tx, i / 2)->previousTransactionHash));
        AssertEqualData(correctInputs2[i + 1], DataInt(TransactionInputAt(&tx, i / 2)->outputIndex));
    }

    for(int i = 0; i < 2 * 2; i += 2) {

        AssertEqualData(correctOutputs2[i], DataLong(TransactionOutputAt(&tx, i / 2)->value));
        AssertEqualData(correctOutputs2[i + 1], TransactionOutputAt(&tx, i / 2)->script);
    }
}
/*
void testBip174()
{
    Data hdWallet = base58Dencode("tprv8ZgxMBicQKsPd9TeAdPADNnSyH9SSUUbTVeFszDE23Ki6TBB5nCefAdHkK8Fm3qMQR6sHwA56zqRmKmxnHk37JkiFzvncDqoKmPWubu7hDF");

    Data masterHash = [hash160(pubKeyFromHdWallet(hdWallet)) subdataWithRange:NSMakeRange(0, 4)];

    Transaction *transaction = [Transaction new];

    transaction.version = 2;
    transaction.locktime = 0;

    TransactionInputAt(&trans, 0)->sequence = 0xffffffff;
    TransactionInputAt(&trans, 0)->previousTransactionHash = [fromHex("75ddabb27b8845f5247975c8a5ba7c6f336c4570708ebe230caf6db5217ae858") flipEndian];

    TransactionInputAt(&trans, 1)->sequence = 0xffffffff;
    TransactionInputAt(&trans, 1)->previousTransactionHash = [fromHex("1dea7cd05979072a3578cab271c02244ea8a090bbb46aa680a65ecd027048d83") flipEndian];
    TransactionInputAt(&trans, 1)->outputIndex = 1;

    [transaction outputAt:0].script = fromHex("0014d85c2b71d0060b09c9886aeb815e50991dda124d");
    [transaction outputAt:0].value = 149990000;

    [transaction outputAt:1].script = fromHex("001400aea9a2e5f0f876a588df5546e8742d1d87008f");
    [transaction outputAt:1].value = 100000000;

    Data psbt = psbtFromTx(transaction.data);

    AssertEqualData(toHex(psbt), "70736274ff01009a020000000258e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd750000000000ffffffff838d0427d0ec650a68aa46bb0b098aea4422c071b2ca78352a077959d07cea1d0100000000ffffffff0270aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f000000000000000000");

    psbt = psbt(psbt
                   input:0
                     add:uint8D(PSBT_IN_NON_WITNESS_UTXO)
                   value:fromHex("0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f618765000000"));

    psbt = psbt(psbt input:0 add:uint8D(PSBT_IN_SIGHASH_TYPE) value:uint32D(0x01));

    psbt = psbt(psbt
                   input:0
                     add:uint8D(PSBT_IN_REDEEM_SCRIPT)
                   value:fromHex("5221029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f2102dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d752ae"));

    NSArray<NSData*> *indicies1 = ckdIndicesFromPath:[NSString stringWithFormat:"0'/0'/0'"]];
    Data hdWallet1 = hdWallet(hdWallet, "0'/0'/0'");

    psbt = psbt:psbt
                   input:0
                     add:[uint8D(PSBT_IN_BIP32_DERIVATION) dataWith:pubKeyFromHdWallet(hdWallet1)]
                   value:[masterHash dataWith:implode(indicies1)]];

    NSArray<NSData*> *indicies2 = ckdIndicesFromPath:[NSString stringWithFormat:"0'/0'/1'"]];
    Data hdWallet2 = hdWallet(hdWallet, "0'/0'/1'");

    psbt = psbt:psbt
                   input:0
                     add:[uint8D(PSBT_IN_BIP32_DERIVATION) dataWith:pubKeyFromHdWallet(hdWallet2)]
                   value:[masterHash dataWith:implode(indicies2)]];

    Transaction *prevTx = TransactionNew(fromHex("0200000000010158e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd7501000000171600145f275f436b09a8cc9a2eb2a2f528485c68a56323feffffff02d8231f1b0100000017a914aed962d6654f9a2b36608eb9d64d2b260db4f1118700c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e88702483045022100a22edcc6e5bc511af4cc4ae0de0fcd75c7e04d8c1c3a8aa9d820ed4b967384ec02200642963597b9b1bc22c75e9f3e117284a962188bf5e8a74c895089046a20ad770121035509a48eb623e10aace8bfd0212fdb8a8e5af3c94b0b133b95e114cab89e4f7965000000"));

    psbt = psbt:psbt input:1 add:uint8D(PSBT_IN_WITNESS_UTXO) value:prevTx.outputs[1].data];

    psbt = psbt(psbt input:1 add:uint8D(PSBT_IN_SIGHASH_TYPE) value:uint32D(0x01));

    psbt = psbt(psbt
                   input:1
                     add:uint8D(PSBT_IN_REDEEM_SCRIPT)
                   value:fromHex("00208c2353173743b595dfb4a07b72ba8e42e3797da74e87fe7d9d7497e3b2028903"));

    psbt = psbt(psbt
                   input:1
                     add:uint8D(PSBT_IN_WITNESS_SCRIPT)
                   value:fromHex("522103089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc21023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7352ae"));

    indicies1 = ckdIndicesFromPath:[NSString stringWithFormat:"0'/0'/3'"]];
    hdWallet1 = hdWallet(hdWallet, "0'/0'/3'");

    psbt = psbt:psbt
                   input:1
                     add:[uint8D(PSBT_IN_BIP32_DERIVATION) dataWith:pubKeyFromHdWallet(hdWallet1)]
                   value:[masterHash dataWith:implode(indicies1)]];

    indicies2 = ckdIndicesFromPath:[NSString stringWithFormat:"0'/0'/2'"]];
    hdWallet2 = hdWallet(hdWallet, "0'/0'/2'");

    psbt = psbt:psbt
                   input:1
                     add:[uint8D(PSBT_IN_BIP32_DERIVATION) dataWith:pubKeyFromHdWallet(hdWallet2)]
                   value:[masterHash dataWith:implode(indicies2)]];

    indicies1 = ckdIndicesFromPath:[NSString stringWithFormat:"0'/0'/4'"]];
    hdWallet1 = hdWallet(hdWallet, "0'/0'/4'");

    psbt = psbt:psbt
                  output:0
                     add:[uint8D(PSBT_OUT_BIP32_DERIVATION) dataWith:pubKeyFromHdWallet(hdWallet1)]
                   value:[masterHash dataWith:implode(indicies1)]];

    indicies2 = ckdIndicesFromPath:[NSString stringWithFormat:"0'/0'/5'"]];
    hdWallet2 = hdWallet(hdWallet, "0'/0'/5'");

    psbt = psbt:psbt
                  output:1
                     add:[uint8D(PSBT_OUT_BIP32_DERIVATION) dataWith:pubKeyFromHdWallet(hdWallet2)]
                   value:[masterHash dataWith:implode(indicies2)]];

    AssertEqualData(toHex(psbt), "70736274ff01009a020000000258e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd750000000000ffffffff838d0427d0ec650a68aa46bb0b098aea4422c071b2ca78352a077959d07cea1d0100000000ffffffff0270aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f00000000000100bb0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f618765000000010304010000000104475221029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f2102dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d752ae2206029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f10d90c6a4f000000800000008000000080220602dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d710d90c6a4f0000008000000080010000800001012000c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e8870103040100000001042200208c2353173743b595dfb4a07b72ba8e42e3797da74e87fe7d9d7497e3b2028903010547522103089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc21023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7352ae2206023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7310d90c6a4f000000800000008003000080220603089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc10d90c6a4f00000080000000800200008000220203a9a4c37f5996d3aa25dbac6b570af0650394492942460b354753ed9eeca5877110d90c6a4f000000800000008004000080002202027f6399757d2eff55a136ad02c684b1838b6556e5f1b6b34282a94b6b5005109610d90c6a4f00000080000000800500008000");
}
*/
void testAddressParsing()
{
    const char *addresses[] =
    {
        "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", // Very first BTC transaction (but misinterpreted as a p2pkh which appears to be common)
        "1AKDDsfTh8uY4X3ppy1m7jw1fVMBSMkzjP", // P2PKH
        "34nSkinWC9rDDJiUY438qQN1JHmGqBHGW7", // P2SH
        "35M4xrHFk3Mfne2ihzv5h9GLFmULjtwr5k", // P2SH (this one caused problems in the wild)
        "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", // P2WPKH
        "bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3", // P2WSH
        NULL
    };

    for(int i = 0; addresses[i]; i++) {

        String address = StringNew(addresses[i]);

        Data pubScript = addressToPubScript(address);
        String result = pubScriptToAddress(pubScript);

        AssertEqualData(result, address);
    }
}

void testDecryptBip38()
{
    Data data = base58Dencode("124jx21o1yne1Ud9r9KBgQRd4shYfD9ZK");
    Data pubScript = addressToPubScript(StringNew("124jx21o1yne1Ud9r9KBgQRd4shYfD9ZK"));
    // 1124jx21o1yne1Ud9r9KBgQRd4shYfD9ZK

    pubScript = fromHex("76a914003389d279e2176e4a3ed6b99101c3a999eb64e188ac");

    String address = pubScriptToAddress(pubScript);

    AssertEqualData(addressToPubScript(address), pubScript);
}

static void newAddress(Node *node, String ip, uint16_t port, uint64_t date, uint64_t services)
{
    printf("New address found! %s:%d\n", ip.bytes, port);
}

static void blockHeaders(struct Node *node, Datas headers)
{
    printf("%s got %d block headers!\n", NodeDescription(node).bytes, headers.count);

    if(headers.count) {

        MerkleBlock block = MerkleBlockNew(headers.ptr[headers.count - 1]);

        Datas hashes = DatasOneCopy(blockHash(&block));

        NodeGetHeaders(node, hashes, DataNull());

        MerkleBlockFree(&block);
    }
}

static void tx(struct Node *node, Data tx)
{
    printf("%s got tx: [%d bytes]", NodeDescription(node).bytes, tx.length);
}

void testManaulNodeConnection()
{
    Node node = NodeNew(StringNew("92.62.231.253:8333"));

    node.delegate.newAddress = newAddress;
    node.delegate.blockHeaders = blockHeaders;
    node.delegate.tx = tx;

    NodeConnect(&node);

    NodeSendVersion(&node, 0);

    for(int i = 0; i < 13; i++) {

        int oldReadyValue = node.connectionReady;

        DataTrackPush();

        NodeProcessPackets(&node);

        if(!oldReadyValue && node.connectionReady) {

            printf("%s The node is ready!\n", NodeDescription(&node).bytes);

            NodeGetHeaders(&node, DatasNew(), DataNull());
        }

        sleep(1);

        String str = NodeDescription(&node);

        printf("%s\n", str.bytes);

        DataTrackPop();
    }

    NodeClose(&node);
    NodeFree(&node);
}

int main()
{
    BTCUtilStartup();

    testData();
    AssertZero(DataAllocatedCount());

    bsSetup("/tmp/bs.basicStorage");

    AssertEqual(DataAllocatedCount(), 1);

    for(int i = 0; testFunctions[i].testFunction; i++) {

        printf("Testing %s\n", testFunctions[i].name);

        char buffer[1024 * 1024];

#ifdef DEBUG_DATA_TRACKING
        DataDebugTrackingStart(buffer, sizeof(buffer));
#endif

        basicStorageDeleteAll();

        DataTrackPush();

        testFunctions[i].testFunction();

        DataTrackPop();

        if(DataAllocatedCount() != 1) {

            printf("%s() leaked %d memory object(s). Dumping all memory objects.\n", testFunctions[i].name, DataAllocatedCount());
#ifdef DEBUG_DATA_TRACKING
            DataDebugTrackingPrintAll();
#else
            printf("Can't dump because DEBUG_DATA_TRACKING is disabled.\n");
#endif
            abort();
            return 1;
        }

#ifdef DEBUG_DATA_TRACKING
        DataDebugTrackingEnd();
#endif

        printf("Test passed!\n");
    }

    printf("All tests passed!\n");

    BTCUtilShutdown();

    return 0;
}
