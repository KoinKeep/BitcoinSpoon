#ifndef DATA_H
#define DATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>

#define DataRaw(object) DataRef((void*)&(object), sizeof(object))

#define StringAddF(str, ...) \
({ char *ptr = 0; asprintf(&ptr, ##__VA_ARGS__); \
String res = StringAdd((str), ptr); \
free(ptr); res; })

#define StringF(...) StringAddF(StringNew(""), __VA_ARGS__)

#define FORDATAIN(data, datas) \
for(Data *data = (datas).ptr; data < (datas).ptr + (datas).count; data = data + 1)

#define FORINDICT(item, dict) \
FORIN(DictionaryElement, item, (dict).keysAndValues)

#define FORIN(type, value, array) \
for(type *value##DataPtr = (type *)(array).ptr, *value; \
    (Data*)value##DataPtr < (array).ptr + (array).count && (value = (type *)((Data*)value##DataPtr)->bytes); \
    value##DataPtr = (type *)((Data*)value##DataPtr + 1))

#include <stdint.h>

// All data is assumed to be non-overlapping in memory
// You are responsible for clean inputs (ie no buffer overflow protection etc).
typedef struct Data {
    char *bytes;
    uint32_t length;
} Data;

typedef struct Datas {
    Data *ptr;
    uint32_t count;
} Datas;

typedef struct DictionaryElement {
    Data key;
    Data value;
} DictionaryElement;

typedef struct Dictionary {
    int (*compare)(Data dataA, Data dataB);
    Datas keysAndValues;
} Dictionary;

/* If a DataTrack is pushed, all Data creation will be automatically tracked */
Data DataNew(unsigned int length);
Data DataNewUntracked(unsigned int length);
Data DataZero(unsigned int length); // Sets new data to 0
Data DataNull(void);
Data DataInt(int32_t value);
int32_t DataGetInt(Data data);
Data DataPtr(void *ptr);
void *DataGetPtr(Data data);
Data DataDict(Dictionary dict); // Untracks 'dict' and puts it in a Data object
Data DataDictStayTracked(Dictionary dict); // Doesn't untracks 'dict'
Dictionary DataGetDict(Data data); // Tracks the resulting Dict.
Dictionary DataGetDictUntracked(Data data);
Data DataDatas(Datas datas); // Untracks 'datas' and puts it in a Data object
Datas DataGetDatas(Data data); // Tracks the resulting Datas.
Data DataLong(int64_t value);
int64_t DataGetLong(Data data);
Data DataCopy(const void *bytes, unsigned int length);
Data DataCopyData(Data data);
Data DataRef(void *bytes, unsigned int length);
Data DataResize(Data data, unsigned int length);
Data DataGrow(Data data, unsigned int addedLength);
Data DataShrink(Data data, unsigned int lessLength);
Data DataTrimFront(Data data, unsigned int removalCount);
Data DataSetByte(Data data, char byte, unsigned int index);
Data DataCopyDataPart(Data data, unsigned int start, unsigned int length);
Data DataDelete(Data data, unsigned int start, unsigned int length);
Data DataAdd(Data first, Data second); // Appends second to first, destorying both data objects
Data DataAppend(Data first, Data second); // Appends second to first, modifying the first data object
Data DataAddCopy(Data first, Data second); // Appends second to first, preserving both data objects
Data DataInsert(Data data, unsigned int index, Data insert);
Data DataFlipEndianCopy(Data data);

int DataAllocatedCount(void); // Allocated count per thread

// Fills buffer with unfreed Data objects, ending with a NULL data object.
void DataDebugTrackingStart(char *buffer, int buffSize);
void DataDebugTrackingPrintAll(void);
void DataDebugTrackingEnd(void);

/* Basic usage */
void DataTrackPush(void); // Begins a new thread local track stack
void DataTrackPop(void); // Frees all tracked data and ends track stack

int DataTrackCount(void);

/* Shorthand methods for compact usage */
Data DTPush(Data data); // Exactly same as DataTrackPush() but returns data
Data DTPop(Data data); // Untracks data, pops, tracks the data again, and then returns data
Datas DTPopDatas(Datas datas); // DTPop on 'datas' array including all elements.
int DTPopi(int value); // Pops and returns value
Data DTPopNull(void); // Shorthand for DTPop(DataNull());

/* Advanced usage */
Data DataTrack(Data data); // Tracks this data current track stack
int DataIsTracked(Data data);
Data DataReplaceTrack(Data oldData, Data newData);
Data DataUntrack(Data data);
Data DataUntrackCopy(Data data); // Copies data and untracks the result.
Data DataTranscend(Data data); // Untracks 'data' and tracks it to senior data track (if any).

Data DataFree(Data data); // If data found in the current track stack -- it will be automatically removed

/* If a DataTrack was previously pushed, memory will be automatically tracked using DataTrack */
Datas DatasNew(void);
Datas DatasNull(void); // Same as DatasOneCopy(DataNull())
int DatasIsNull(Datas datas); // Checks that datas has on element of DataNull
Datas DatasOneCopy(Data one);
Datas DatasTwoCopy(Data one, Data two);
Datas DatasThreeCopy(Data one, Data two, Data three);

Datas DatasTrack(Datas datas);
Datas DatasUntrack(Datas datas);
Datas DatasUntrackCopy(Datas datas);

Datas DatasCopy(Datas datas);

// Returns 0 is the objects are equal -- memcmp style
int DataCompare(Data dataA, Data dataB);

// Returns 0 if the objects are *not* equal
int DataEqual(Data dataA, Data dataB);
int DatasEqual(Datas datasA, Datas datasB);

Data DatasSerialize(Datas datas);
Datas DatasDeserialize(Data data);
Datas DatasAddCopy(Datas datas, Data data);
Datas DatasAddRef(Datas datas, Data data);
Datas DatasAddCopyIndex(Datas datas, Data data, int index);
Datas DatasAddRefIndex(Datas datas, Data data, int index);
Datas DatasAddCopyFront(Datas datas, Data data);
Datas DatasAddRefFront(Datas datas, Data data);
Datas DatasAddDatasCopy(Datas datas, Datas additions);
Datas DatasRemove(Datas datas, Data data); // Frees "data"
Datas DatasRemoveAll(Datas datas); // Frees elements
Datas DatasRemoveTake(Datas datas, Data data); // Removes "data" without freeing it.
Datas DatasRemoveIndex(Datas datas, int index);
Datas DatasRemoveIndexTake(Datas datas, int index);
Datas DatasRemoveLast(Datas datas);
Datas DatasReplaceIndexCopy(Datas datas, int index, Data data);
Datas DatasReplaceIndexRef(Datas datas, int index, Data data);
Datas DatasSort(Datas datas, int (*compare)(Data dataA, Data dataB)); // use 'DataCompare'
Data DatasSearch(Datas datas, Data key, int (*compare)(Data dataA, Data dataB)); // 'datas' must be sorted first.
int DatasSearchCheck(Datas datas, Data key, int (*compare)(Data dataA, Data dataB));
Data DatasFirst(Datas datas);
Data DatasAt(Datas datas, int index);
Data DatasLast(Datas datas);
Data DatasRandom(Datas datas);
Datas DatasRandomSubarray(Datas datas, int count);
Data DatasTakeFirst(Datas *datas);
Data DatasTakeLast(Datas *datas);
Data DatasIndex(Datas datas, int index);
int DatasHasData(Datas datas, Data data); // Compares memory address
int DatasHasMatchingData(Datas datas, Data data); // Compares memory contents
int DatasMatchingDataIndex(Datas datas, Data data); // Compares memory contents
Datas DatasReplaceData(Datas datas, Data oldData, Data newData); // NULL oldData is equivilent of DatasAddRef
void DatasPrintAll(Datas datas);

Datas DatasTranscend(Datas datas); // Untracks 'datas' + all elements, then tracks them to senior data track (if any).

Datas DatasFree(Datas datas); // Frees all element "data"s that aren't tracked

Dictionary DictionaryNew(int (*compare)(Data dataA, Data dataB)); // NULL uses default sort 'DataCompare'
Dictionary DictionaryIntersection(Dictionary dictA, Dictionary dictB); // Uses values from 'A' ignoring 'B' values
int DictionaryDoesIntersect(Dictionary dictA, Dictionary dictB);
Dictionary DictionaryAddCopy(Dictionary dict, Data key, Data value);
Dictionary DictionaryAddRef(Dictionary dict, Data key, Data value);
Dictionary DictionaryAddRefUntracked(Dictionary dict, Data key, Data value);
Dictionary DictionaryRemove(Dictionary dict, Data key);
Data DictionaryGetValue(Dictionary dict, Data key);
int DictionaryHasKey(Dictionary dict, Data key);
DictionaryElement DictionaryGetElement(Dictionary dict, int index);
unsigned int DictionaryCount(Dictionary dict);
Dictionary DictionaryIndex(Datas datas, int index);

// Can only seralize / deserialize dicts with the default sort
Data DictionarySerialize(Dictionary dict);
Dictionary DictionaryDeserialize(Data data);
Dictionary DictionaryCopy(Dictionary dict);
Dictionary DictionaryTrack(Dictionary dict);
Dictionary DictionaryUntrack(Dictionary dict);

// Shorthand convenience methods
typedef Dictionary Dict;
Dict DictNew(void); // Uses default sort
Dict DictNull(void); // Same as DictOne(DataNull(), DataNull())
int DictIsNull(Dict dict); // Checks that dict has one element of DataNull:DataNull
void DictFree(Dictionary dict);
Dict DictIntersect(Dict dictA, Dict dictB); // Uses values from 'A' ignoring 'B' values
int DictDoesIntersect(Dict dictA, Dict dictB);
Dict DictOne(Data key, Data value); // Copies key and value
Dict DictOneS(const char *key, Data value);
Dict DictTwoKeys(Data key1, Data key2); // Uses DataNull() values
Dict DictTwo(Data key1, Data value1, Data key2, Data value2);
Dict *DictAdd(Dict *dict, Data key, Data value); // Shorthand for DictionaryAddCopy
Dict *DictAddS(Dict *dict, const char *key, Data value); // Turns key into a String and calls DictionaryAddCopy
Dict *DictSet(Dict *dict, Data key, Data value); // Same as DictAdd
Dict *DictSetS(Dict *dict, const char *key, Data value);
Dict *DictAddDict(Dict *dict, Dict addition);
Dict *DictRemove(Dict *dict, Data key);
Dict *DictRemoveS(Dict *dict, const char *key);
Dict *DictRemoveIndex(Dict *dict, int index);
Dict DictCopy(Dict dict);
Data DictGet(Dict dict, Data key);
Data DictGetS(Dict dict, const char *key); // Equivilent of DictionaryGetValue(dict, StringNew(key))
int DictHasKey(Dict dict, Data key);
Datas DictAllKeysRef(Dict dict);
Datas DictAllKeysCopy(Dict dict);
DictionaryElement DictGetI(Dict dict, int index);
Dict DictIndex(Datas datas, int index);
Data DictSerialize(Dict dict);
Dict DictDeserialize(Data data);
Dict DictTrack(Dict dict);
Dict DictUntrack(Dict dict);
Dict DictUntrackCopy(Dict dict);
unsigned int DictCount(Dict dict);

void DictionaryFree(Dictionary dict);

typedef Data String;

String StringNew(const char *str);
String StringRef(char *str);
String StringCopy(String string); // Makes a copy of string of string.length or string.length+1, adding an ending 0x00 if needed
String StringAdd(String string, const char *secondString);
String StringAddRaw(const char *string, const char *secondString);
String StringPrefix(const char *prefix, String string);
String StringLowercase(String string);
int StringEqual(String string, const char *secondString);
int StringHasPrefix(String string, const char *secondString);
Datas StringComponents(String string, char seperator);
String StringIndex(Datas datas, int index);

#ifdef __cplusplus
}
#endif

#endif
