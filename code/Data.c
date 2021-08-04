#include "Data.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#define DATA_LOG(...)

#ifndef DATA_LOG
#define DATA_LOG(...) printf(__VA_ARGS__)
#endif

#ifdef __ANDROID__
static void *reallocf(void *ptr, size_t size)
{
    void *result = realloc(ptr, size);

    if(!result)
        free(ptr);

    return result;
}
#endif

static Data DatasMemory(Datas datas);
static Datas DatasAddRefFlexible(Datas datas, Data data, int tracked, int isTrackingMemoryItself);
static void DatasFreeFlexible(Datas datas, int includingTracked);
static void DataDebugTrackingAdd(Data data);
static void DataDebugTrackingRemove(Data data);

static Data *debugTracker = NULL;
static int debugTrackerSize = 0;

static volatile int allocatedCount = 0;

static __thread struct {
    Datas *datas;
    int count;
} trackStacks = { 0, 0 };

static Data DataNewFlexible(unsigned int length, int tracked)
{
    Data data = { malloc(length), length};

    DATA_LOG("malloc(%d) = %p\n", length, data.bytes);

    if(data.bytes)
        allocatedCount++;

    // Debug aide -- set fresh memory to 333333 when allocated this way
    memset(data.bytes, 0x33, length);

    DataDebugTrackingAdd(data);

    if(tracked)
        DataTrack(data);

    return data;
}

Data DataNew(unsigned int length)
{
    return DataNewFlexible(length, 1);
}

Data DataNewUntracked(unsigned int length)
{
    return DataNewFlexible(length, 0);
}

Data DataZero(unsigned int length)
{
    Data data = DataNew(length);

    memset(data.bytes, 0, data.length);

    return data;
}

Data DataNull()
{
    Data data = { NULL, 0 };

    return data;
}

Data DataInt(int32_t value)
{
    return DataCopy((void*)&value, sizeof(value));
}

int32_t DataGetInt(Data data)
{
    if(!data.bytes)
        return 0;

    if(data.length != sizeof(int32_t))
        abort();

    return *(int32_t*)data.bytes;
}

Data DataPtr(void *ptr)
{
    return DataCopy(&ptr, sizeof(ptr));
}

void *DataGetPtr(Data data)
{
    if(!data.bytes)
        return 0;

    if(data.length != sizeof(void*))
        abort();

    return *(void**)data.bytes;
}

Data DataDict(Dict dict)
{
    dict = DictUntrack(dict);

    return DataCopyData(DataRaw(dict));
}

Data DataDictStayTracked(Dictionary dict)
{
    return DataCopyData(DataRaw(dict));
}

Dict DataGetDict(Data data)
{
    if(!data.bytes)
        return DictNew();

    if(data.length != sizeof(Dict))
        abort();

    return DictTrack(*(Dict*)data.bytes);
}

Dictionary DataGetDictUntracked(Data data)
{
    if(!data.bytes)
        return DictNew();

    if(data.length != sizeof(Dict))
        abort();

    return *(Dict*)data.bytes;
}

Data DataDatas(Datas datas)
{
    datas = DatasUntrack(datas);

    return DataCopyData(DataRaw(datas));
}

Datas DataGetDatas(Data data)
{
    if(!data.bytes)
        return DatasNew();

    if(data.length != sizeof(Datas))
        abort();

    return DatasTrack(*(Datas*)data.bytes);
}

Data DataLong(int64_t value)
{
    return DataCopy((void*)&value, sizeof(value));
}

int64_t DataGetLong(Data data)
{
    if(!data.bytes)
        return 0;

    if(data.length != sizeof(int64_t))
        abort();

    return *(int64_t*)data.bytes;
}

Data DataCopy(const void *bytesPtr, unsigned int length)
{
    if(!bytesPtr)
        return DataNull();

    const char *bytes = bytesPtr;

    Data data = DataNew(length);

    memcpy(data.bytes, bytes, length);

    return data;
}

Data DataCopyData(Data data)
{
    return DataCopy(data.bytes, data.length);
}

Data DataRef(void *bytesPtr, unsigned int length)
{
    char *bytes = bytesPtr;

    return (Data){ bytes, length };
}

Data DataTrimFront(Data data, unsigned int removalCount)
{
    memmove(data.bytes, data.bytes + removalCount, data.length - removalCount);

    return DataResize(data, data.length - removalCount);
}

Data DataSetByte(Data data, char byte, unsigned int index)
{
    if(index >= data.length)
        abort();

    data.bytes[index] = byte;

    return data;
}

Data DataCopyDataPart(Data data, unsigned int start, unsigned int length)
{
    Data result = DataNew(length);

    memcpy(result.bytes, data.bytes + start, length);

    return result;
}

Data DataDelete(Data data, unsigned int start, unsigned int length)
{
    if(!data.bytes)
        return data;

    memmove(data.bytes + start, data.bytes + start + length, data.length - start - length);

    return DataShrink(data, length);
}

static Data DataResizeFlexible(Data data, unsigned int length, int tracked, int isTrackingMemoryItself)
{
    if(data.length == length)
        return data;

    Data oldData = data;

    if(!length) {

        free(data.bytes);
        data.bytes = NULL;
    }
    else {

        data.bytes = reallocf(data.bytes, length);
    }

    DataDebugTrackingRemove(oldData);

    if(data.bytes)
        DataDebugTrackingAdd(data);

    DATA_LOG("reallocf(%p, %d, tracked: %d) = %p\n", oldData.bytes, length, tracked, data.bytes);

    if(!oldData.bytes && data.bytes)
        allocatedCount++;

    // Debug aide -- set memory to 666666 when added view resize.
    if(length > data.length)
        memset(data.bytes + data.length, 0x66, length - data.length);

    data.length = length;

    if(trackStacks.count > 0 && oldData.bytes != data.bytes) {

        if(tracked && !oldData.bytes)
            return DataTrack(data);
        else if(!isTrackingMemoryItself && DataIsTracked(oldData))
            return DataReplaceTrack(oldData, data);
    }

    return data;
}

Data DataResize(Data data, unsigned int length)
{
    return DataResizeFlexible(data, length, 1, 0);
}

Data DataGrow(Data data, unsigned int addedLength)
{
    return DataResize(data, data.length + addedLength);
}

Data DataShrink(Data data, unsigned int lessLength)
{
    return DataResize(data, data.length - lessLength);
}

Data DataAdd(Data first, Data second)
{
    first = DataAppend(first, second);

    second = DataFree(second);

    return first;
}

Data DataAppend(Data first, Data second)
{
    unsigned int oldLength = first.length;

    first = DataResize(first, first.length + second.length);

    memcpy(first.bytes + oldLength, second.bytes, second.length);

    return first;
}

Data DataAddCopy(Data first, Data second)
{
    Data result = DataNew(first.length + second.length);

    memcpy(result.bytes, first.bytes, first.length);
    memcpy(result.bytes + first.length, second.bytes, second.length);

    return result;
}

Data DataInsert(Data data, unsigned int index, Data insert)
{
    Data oldData = data;

    data = DataGrow(data, insert.length);

    memmove(data.bytes + index + insert.length, data.bytes + index, oldData.length - index);
    memcpy(data.bytes + index, insert.bytes, insert.length);

    return data;
}

Data DataFlipEndianCopy(Data data)
{
    Data result = DataNew(data.length);

    for(int i = 0; i < data.length; i++)
        result.bytes[i] = data.bytes[data.length - 1 - i];

    return result;
}

int DataAllocatedCount()
{
    return allocatedCount;
}

void DataDebugTrackingStart(char *buffer, int bufSize)
{
    debugTracker = (void*)buffer;
    debugTrackerSize = bufSize;

    memset(buffer, 0, bufSize);
}

void DataDebugTrackingPrintAll()
{
    Data *last = (Data*)((char*)debugTracker + debugTrackerSize - sizeof(Data));

    for(Data *ptr = debugTracker; ptr < last && ptr->bytes; ptr++) {

        printf("Data { %p, %d }\n", ptr->bytes, ptr->length);
    }
}

static void DataDebugTrackingAdd(Data data)
{
    if(!data.bytes)
        abort();

    if(debugTrackerSize < sizeof(Data) * 2)
        return;

    Data *last = (Data*)((char*)debugTracker + debugTrackerSize - sizeof(Data));

    for(Data *ptr = debugTracker; ptr < last; ptr++) {

        if(ptr->bytes == data.bytes)
            abort();

        if(!ptr->bytes) {

            *ptr = data;
            return;
        }
    }

    abort();
}

static void DataDebugTrackingRemove(Data data)
{
    if(debugTrackerSize < sizeof(Data) * 2)
        return;

    if(!data.bytes)
        return;

    Data *end = (Data*)((char*)debugTracker + debugTrackerSize);

    for(Data *ptr = debugTracker; ptr <= end - sizeof(Data) * 2 && ptr->bytes; ptr++) {

        if(ptr->bytes == data.bytes) {

            memmove(ptr, ptr + 1, (char*)end - (char*)(ptr + 1));
            return;
        }
    }

    abort();
}

void DataDebugTrackingEnd()
{
    debugTracker = NULL;
    debugTrackerSize = 0;
}

static Datas currentTrackStack()
{
    if(!trackStacks.count)
        abort();

    return trackStacks.datas[trackStacks.count - 1];
}

static void setCurrentTrackStack(Datas datas)
{
    trackStacks.datas[trackStacks.count - 1] = datas;
}

void DataTrackPush()
{
    trackStacks.count++;

    void *oldPtr = trackStacks.datas;

    trackStacks.datas = reallocf(trackStacks.datas, sizeof(Datas) * trackStacks.count);

    DataDebugTrackingRemove(DataRef(oldPtr, 0));
    DataDebugTrackingAdd(DataRef(trackStacks.datas, sizeof(Datas) * trackStacks.count));

    DATA_LOG("trackStacks-reallocf(%p, %d) = %p\n", oldPtr, (int)(sizeof(Datas) * trackStacks.count), trackStacks.datas);

    if(!oldPtr && trackStacks.datas)
        allocatedCount++;

    setCurrentTrackStack(DatasNew());

    DATA_LOG("DataTrackPush(%p)\n", currentTrackStack().ptr);
}

Data DataTrack(Data data)
{
    if(!data.bytes)
        return data;

    if(trackStacks.count > 0) {

        if(DatasHasData(currentTrackStack(), data))
            abort();

        setCurrentTrackStack(DatasAddRefFlexible(currentTrackStack(), data, 0, 1));

        DATA_LOG("DataTrack(%p)\n", data.bytes);
    }

    return data;
}

int DataIsTracked(Data data)
{
    if(trackStacks.count < 1)
        return 0;

    return DatasHasData(currentTrackStack(), data);
}

Data DataReplaceTrack(Data oldData, Data newData)
{
    DATA_LOG("DataReplaceTrack(%p, %p)\n", oldData.bytes, newData.bytes);

    if(!DatasHasData(currentTrackStack(), oldData))
        abort();

    Datas datas = DatasReplaceData(currentTrackStack(), oldData, newData);

    if(!DatasHasData(currentTrackStack(), newData))
        abort();

    setCurrentTrackStack(datas);

    return newData;
}

void DatasPrintAll(Datas datas)
{
    DATA_LOG("Datas(%p [%d elements]) ", datas.ptr, datas.count);

    for(int i = 0; i < datas.count; i++)
        DATA_LOG("{%p %d}", datas.ptr[i].bytes, datas.ptr[i].length);

    DATA_LOG("\n");
}

Data DataUntrack(Data data)
{
    if(trackStacks.count > 0)
        setCurrentTrackStack(DatasRemoveTake(currentTrackStack(), data));

    return data;
}

Data DataUntrackCopy(Data data)
{
    return DataUntrack(DataCopyData(data));
}

Data DataTranscend(Data data)
{
    if(!data.bytes)
        return data;

    if(trackStacks.count > 0)
        setCurrentTrackStack(DatasRemoveTake(currentTrackStack(), data));

    if(trackStacks.count > 1) {

        Datas datas = trackStacks.datas[trackStacks.count - 2];

        datas = DatasAddRefFlexible(datas, data, 0, 0);

        trackStacks.datas[trackStacks.count - 2] = datas;
    }

    return data;
}

void DataTrackPop()
{
    if(trackStacks.count < 1)
        abort(); // More data track pops than pushes.

    DATA_LOG("DataTrackPop(%p)\n", currentTrackStack().ptr);

    DatasFreeFlexible(currentTrackStack(), 1);

    trackStacks.count--;

    if(!trackStacks.datas)
        abort();

    if(trackStacks.count == 0) {

        allocatedCount--;

        DATA_LOG("DataTrack-free(%p)\n", trackStacks.datas);

        free(trackStacks.datas);

        DataDebugTrackingRemove(DataRef(trackStacks.datas, 0));

        trackStacks.datas = NULL;
    }
    else {

        void *oldPtr = trackStacks.datas;

        trackStacks.datas = reallocf(trackStacks.datas, sizeof(Datas) * trackStacks.count);

        DataDebugTrackingRemove(DataRef(oldPtr, 0));
        DataDebugTrackingAdd(DataRef(oldPtr, sizeof(Datas) * trackStacks.count));

        DATA_LOG("DataTrack-reallocf(%p, %d) = %p\n", oldPtr, (int)(sizeof(Datas) * trackStacks.count), trackStacks.datas);
    }
}

int DataTrackCount()
{
    if(trackStacks.count < 1)
        return 0;

    return currentTrackStack().count;
}

Data DTPush(Data data)
{
    DataTrackPush();

    return data;
}

Data DTPop(Data data)
{
    DataUntrack(data);

    DataTrackPop();

    DataTrack(data);

    return data;
}

Datas DTPopDatas(Datas datas)
{
    for(int i = 0; i < datas.count; i++)
        DataUntrack(datas.ptr[i]);

    DataUntrack(DatasMemory(datas));

    DataTrackPop();

    DataTrack(DatasMemory(datas));

    for(int i = 0; i < datas.count; i++)
        DataTrack(datas.ptr[i]);

    return datas;
}

int DTPopi(int value)
{
    DataTrackPop();

    return value;
}

Data DTPopNull()
{
    DataTrackPop();

    return DataNull();
}

Data DataFreeFlexible(Data data, int untrack)
{
    if(trackStacks.count > 0)
        if(DatasHasData(currentTrackStack(), data) > 1)
            abort();

    if(untrack) {
        
        DataUntrack(data);

        if(trackStacks.count > 0)
            if(DatasHasData(currentTrackStack(), data))
                abort();
    }

    if(data.bytes)
        allocatedCount--;

    DATA_LOG("free(%p)\n", data.bytes);

    memset(data.bytes, 0x55, data.length);

    free(data.bytes);

    DataDebugTrackingRemove(data);

    data.bytes = NULL;
    data.length = 0;

    return data;
}

Data DataFree(Data data)
{
    return DataFreeFlexible(data, 1);
}

static Data DatasMemory(Datas datas)
{
    return (Data){ (void*)datas.ptr, datas.count * sizeof(Data) };
}

Datas DatasNew()
{
    Datas datas = { NULL, 0 };

    return datas;
}

Datas DatasNull()
{
    return DatasOneCopy(DataNull());
}

int DatasIsNull(Datas datas)
{
    if(datas.count != 1)
        return 0;

    return datas.ptr[0].bytes == NULL && datas.ptr[0].length == 0;
}

Datas DatasOneCopy(Data one)
{
    Datas datas = DatasNew();

    datas = DatasAddCopy(datas, one);

    return datas;
}

Datas DatasTwoCopy(Data one, Data two)
{
    Datas datas = DatasNew();

    datas = DatasAddCopy(datas, one);
    datas = DatasAddCopy(datas, two);

    return datas;
}

Datas DatasThreeCopy(Data one, Data two, Data three)
{
    Datas datas = DatasNew();

    datas = DatasAddCopy(datas, one);
    datas = DatasAddCopy(datas, two);
    datas = DatasAddCopy(datas, three);

    return datas;
}

Datas DatasTrack(Datas datas)
{
    if(!datas.count) {

        datas.ptr = (void*)DataResizeFlexible(DatasMemory(datas), 0, 0, 0).bytes;
    }
    else {

        for(int i = 0; i < datas.count; i++)
            datas.ptr[i] = DataTrack(datas.ptr[i]);
    }

    DataTrack(DatasMemory(datas));

    return datas;
}

Datas DatasUntrack(Datas datas)
{
    if(!datas.count) {

        datas.ptr = (void*)DataResizeFlexible(DatasMemory(datas), 0, 0, 0).bytes;
    }
    else {

        for(int i = 0; i < datas.count; i++)
            datas.ptr[i] = DataUntrack(datas.ptr[i]);
    }

    DataUntrack(DatasMemory(datas));

    return datas;
}

Datas DatasUntrackCopy(Datas datas)
{
    return DatasUntrack(DatasCopy(datas));
}

Datas DatasCopy(Datas datas)
{
    Datas result = DatasNew();

    for(int i = 0; i < datas.count; i++)
        result = DatasAddCopy(result, datas.ptr[i]);

    return result;
}

int DataCompare(Data dataA, Data dataB)
{
    int result = memcmp(dataA.bytes, dataB.bytes, MIN(dataA.length, dataB.length));

    if(dataA.length == dataB.length || result != 0)
        return result;

    return dataA.length - dataB.length;
}

int DataEqual(Data dataA, Data dataB)
{
    return DataCompare(dataA, dataB) == 0;
}

int DatasEqual(Datas datasA, Datas datasB)
{
    if(datasA.count != datasB.count)
        return 0;

    for(int i = 0; i < datasA.count; i++)
        if(!DataEqual(datasA.ptr[i], datasB.ptr[i]))
            return 0;

    return 1;
}

Data DatasSerialize(Datas datas)
{
    Data data = DataNew(0);

    FORDATAIN(itr, datas) {

        data = DataAppend(data, DataRaw(itr->length));
        data = DataAppend(data, DataRef(itr->bytes, itr->length));
    }

    return data;
}

Datas DatasDeserialize(Data data)
{
    Datas result = DatasNew();

    typeof(data.length) length = 0;

    for(int i = 0; i < data.length; i += length) {

        if(sizeof(data.length) > data.length - i)
            break;

        length = *(typeof(length)*)&data.bytes[i];

        i += sizeof(length);

        if(length > data.length - i)
            break;

        Data element = DataCopy(data.bytes + i, length);

        result = DatasAddRef(result, element);
    }

    return result;
}

Datas DatasAddCopy(Datas datas, Data data)
{
    return DatasAddRef(datas, DataCopyData(data));
}

static Datas DatasAddRefFlexible(Datas datas, Data data, int tracked, int isTrackingMemoryItself)
{
    if(!tracked && DatasHasData(datas, data))
        abort();

    int newCount = datas.count + 1;

    datas.ptr = (void*)DataResizeFlexible(DatasMemory(datas), sizeof(Data) * newCount, tracked, isTrackingMemoryItself).bytes;
    datas.count = newCount;

    datas.ptr[datas.count - 1] = data;

    return datas;
}

Datas DatasAddRef(Datas datas, Data data)
{
    return DatasAddRefFlexible(datas, data, 1, 0);
}

Datas DatasAddCopyIndex(Datas datas, Data data, int index)
{
    return DatasAddRefIndex(datas, DataCopyData(data), index);
}

static Datas DatasAddRefIndexFlexible(Datas datas, Data data, int index, int tracked)
{
    int oldCount = datas.count;

    datas = DatasAddRefFlexible(datas, DataNull(), tracked, 0);

    DATA_LOG("memmove(%p, %p, %d)\n", datas.ptr + index + 1, datas.ptr + index, (int)(sizeof(Data) * (oldCount - index)));

    memmove(datas.ptr + index + 1, datas.ptr + index, sizeof(Data) * (oldCount - index));

    datas.ptr[index] = data;

    return datas;
}

Datas DatasAddRefIndex(Datas datas, Data data, int index)
{
    return DatasAddRefIndexFlexible(datas, data, index, 1);
}

Datas DatasAddCopyFront(Datas datas, Data data)
{
    return DatasAddRefFront(datas, DataCopyData(data));
}

Datas DatasAddRefFront(Datas datas, Data data)
{
    datas = DatasAddRefFlexible(datas, DataNull(), 1, 0);

    memmove(datas.ptr + 1, datas.ptr, sizeof(Data) * (datas.count - 1));

    datas.ptr[0] = data;

    return datas;
}

Datas DatasAddDatasCopy(Datas datas, Datas additions)
{
    for(int i = 0; i < additions.count; i++)
        datas = DatasAddCopy(datas, additions.ptr[i]);

    return datas;
}

Datas DatasRemove(Datas datas, Data data)
{
    Datas result = DatasRemoveTake(datas, data);

    data = DataFree(data);

    return result;
}

Datas DatasRemoveAll(Datas datas)
{
    while(datas.count)
        datas = DatasRemoveIndex(datas, datas.count - 1);

    return datas;
}

Datas DatasRemoveTake(Datas datas, Data data)
{
    DATA_LOG("DatasRemoveTake(%p, %p)\n", datas.ptr, data.bytes);

    int removedCount = 0;

    for(int i = 0; i < datas.count; i++) {
        if(datas.ptr[i].bytes == data.bytes) {

            DATA_LOG("memmove(%p, %p, %d)\n", datas.ptr + i, datas.ptr + i + 1, (int)(sizeof(Data) * (datas.count - i - 1)));

            removedCount++;
            memmove(datas.ptr + i, datas.ptr + i + 1, sizeof(Data) * (datas.count - i - 1));

            DatasPrintAll(datas);
        }
    }

    DATA_LOG("%d matches found for remove take\n", removedCount);

    if(removedCount > 1)
        abort();

    if(removedCount) {

        datas.count = datas.count - removedCount;

        datas.ptr = (void*)DataResize(DatasMemory(datas), sizeof(Data) * datas.count).bytes;
    }

    return datas;
}

Datas DatasRemoveIndex(Datas datas, int index)
{
    if(index < 0 || index >= datas.count)
        abort();

    Data data = datas.ptr[index];

    Datas result = DatasRemoveIndexTake(datas, index);

    data = DataFree(data);

    return result;
}

Datas DatasRemoveIndexTake(Datas datas, int i)
{
    DATA_LOG("DatasRemoveIndexTake(%p, %d)\n", datas.ptr, i);

    if(i < 0 || i >= datas.count)
        abort();

    DATA_LOG("memmove(%p, %p, %d)\n", datas.ptr + i, datas.ptr + i + 1, (int)(sizeof(Data) * (datas.count - i - 1)));

    memmove(datas.ptr + i, datas.ptr + i + 1, sizeof(Data) * (datas.count - i - 1));

    datas.count = datas.count - 1;
    datas.ptr = (void*)DataResize(DatasMemory(datas), sizeof(Data) * datas.count).bytes;

    return datas;
}

Datas DatasRemoveLast(Datas datas)
{
    return DatasRemoveIndex(datas, datas.count - 1);
}

Datas DatasReplaceIndexCopy(Datas datas, int index, Data data)
{
    datas = DatasRemoveIndex(datas, index);
    return DatasAddCopyIndex(datas, data, index);
}

Datas DatasReplaceIndexRef(Datas datas, int index, Data data)
{
    datas = DatasRemoveIndex(datas, index);
    return DatasAddRefIndex(datas, data, index);
}

Datas DatasTranscend(Datas datas)
{
    if(!datas.ptr)
        return datas;

    for(int i = 0; i < datas.count; i++)
        datas.ptr[i] = DataTranscend(datas.ptr[i]);

    DataTranscend(DatasMemory(datas));

    return datas;
}

static __thread int (*dataCompareCallback)(Data dataA, Data dataB) = NULL;
static __thread const void *dataCompareLastTouchedPtr = NULL;
static __thread int dataCompareLastResult = 0;

static int compareDatas(const void *a, const void *b)
{
    dataCompareLastTouchedPtr = b;

    int result = dataCompareCallback(*(Data*)a, *(Data*)b);

    dataCompareLastResult = result;

    return result;
}

Datas DatasSort(Datas datas, int (*compare)(Data dataA, Data dataB))
{
    if(dataCompareCallback)
        abort();

    dataCompareCallback = compare ?: DataCompare;

    qsort(datas.ptr, datas.count, sizeof(Data), compareDatas);

    dataCompareCallback = NULL;

    return datas;
}

Data *DatasSearchFlexible(Datas datas, Data key, int (*compare)(Data dataA, Data dataB))
{
    if(dataCompareCallback)
        abort();

    dataCompareCallback = compare ?: DataCompare;

    Data *value = bsearch(&key, datas.ptr, datas.count, sizeof(Data), compareDatas);

    dataCompareCallback = NULL;

    return value;
}

Data DatasSearch(Datas datas, Data key, int (*compare)(Data dataA, Data dataB))
{
    Data *ptr = DatasSearchFlexible(datas, key, compare);

    return ptr ? *ptr : DataNull();
}

int DatasSearchCheck(Datas datas, Data key, int (*compare)(Data dataA, Data dataB))
{
    return DatasSearchFlexible(datas, key, compare) != NULL;
}

Data DatasFirst(Datas datas)
{
    if(!datas.count)
        return DataNull();

    return datas.ptr[0];
}

Data DatasAt(Datas datas, int index)
{
    if(!datas.count)
        return DataNull();

    return datas.ptr[index];
}

Data DatasLast(Datas datas)
{
    if(!datas.count)
        return DataNull();

    return datas.ptr[datas.count - 1];
}

Data DatasRandom(Datas datas)
{
    if(!datas.count)
        return DataNull();

    return DatasIndex(datas, arc4random_uniform(datas.count));
}

Datas DatasRandomSubarray(Datas datas, int count)
{
    Datas result = DatasNew();

    while(result.count < MIN(count, datas.count)) {

        Data element = datas.ptr[arc4random_uniform((uint32_t)datas.count)];

        int hasElement = 0;

        FORDATAIN(itr, result)
            if(itr->bytes == element.bytes)
                hasElement = 1;

        if(!hasElement)
            result = DatasAddRef(result, element);
    }

    Datas final = DatasCopy(result);

    DataFree(DatasMemory(result));

    return final;
}

Data DatasTakeFirst(Datas *datas)
{
    Data data = DatasFirst(*datas);

    *datas = DatasRemoveIndexTake(*datas, 0);

    return data;
}

Data DatasTakeLast(Datas *datas)
{
    Data data = DatasLast(*datas);

    *datas = DatasRemoveIndexTake(*datas, datas->count - 1);

    return data;
}

Data DatasIndex(Datas datas, int index)
{
    if(index < 0 || index >= datas.count)
        abort();

    return datas.ptr[index];
}

int DatasHasData(Datas datas, Data data)
{
    int count = 0;

    for(int i = 0; i < datas.count; i++)
        if(datas.ptr[i].bytes == data.bytes)
            count++;

    return count;
}

int DatasHasMatchingData(Datas datas, Data data)
{
    int count = 0;

    for(int i = 0; i < datas.count; i++)
        if(datas.ptr[i].length == data.length)
            if(0 == memcmp(datas.ptr[i].bytes, data.bytes, data.length))
                count++;

    return count;
}

int DatasMatchingDataIndex(Datas datas, Data data)
{
    for(int i = 0; i < datas.count; i++)
        if(datas.ptr[i].length == data.length)
            if(0 == memcmp(datas.ptr[i].bytes, data.bytes, data.length))
                return i;

    return -1;
}

Datas DatasReplaceData(Datas datas, Data oldData, Data newData)
{
    if(oldData.bytes == NULL)
        return DatasAddRef(datas, newData);

    for(int i = 0; i < datas.count; i++)
        if(datas.ptr[i].bytes == oldData.bytes)
            datas.ptr[i] = newData;

    return datas;
}

static void DatasFreeFlexible(Datas datas, int includingTracked)
{
    if(!datas.ptr)
        return;

    if(includingTracked) {

        for(int i = 0; i < datas.count; i++)
            DataFreeFlexible(datas.ptr[i], 0);
    }
    else
        for(int i = 0; i < datas.count; i++)
            if(!DataIsTracked(datas.ptr[i]))
                DataFree(datas.ptr[i]);

    DataFree(DatasMemory(datas));
}

Datas DatasFree(Datas datas)
{
    DatasFreeFlexible(datas, 0);

    datas.ptr = 0;
    datas.count = 0;

    return datas;
}

Dictionary DictionaryNew(int (*compare)(Data dataA, Data dataB))
{
    Dictionary dict = { compare ?: DataCompare, DatasNew() };

    return dict;
}

// Optimization potential: There are plenty of ways to do this more efficently
Dictionary DictionaryIntersection(Dictionary dictA, Dictionary dictB)
{
    Dict *smaller = &dictA;
    Dict *larger = &dictB;

    if(DictCount(dictA) > DictCount(dictB)) {

        smaller = &dictB;
        larger = &dictA;
    }

    Dict result = DictNew();

    FORIN(DictionaryElement, item, smaller->keysAndValues)
        if(DictHasKey(*larger, item->key))
            DictAdd(&result, item->key, item->value);

    return result;
}

// Optimization potential: There are some ways to do this more efficently
// This is probably the right approach: https://en.wikipedia.org/wiki/Exponential_search
int DictionaryDoesIntersect(Dictionary dictA, Dictionary dictB)
{
    Dict *smaller = &dictA;
    Dict *larger = &dictB;

    if(DictCount(dictA) > DictCount(dictB)) {

        smaller = &dictB;
        larger = &dictA;
    }

    FORIN(DictionaryElement, item, smaller->keysAndValues)
        if(DictHasKey(*larger, item->key))
            return 1;

    return 0;
}

Dictionary DictionaryAddCopy(Dictionary dict, Data key, Data value)
{
    return DictionaryAddRef(dict, DataCopyData(key), DataCopyData(value));
}

static __thread Dictionary *curDict = NULL;

static int compareKeys(Data a, Data b)
{
    DictionaryElement *d1 = (DictionaryElement*)a.bytes;
    DictionaryElement *d2 = (DictionaryElement*)b.bytes;

    return curDict->compare(d1->key, d2->key);
}

Dictionary DictionaryRemove(Dictionary dict, Data key)
{
    DictionaryElement element = { key, DataNull() };

    if(curDict)
        abort();

    curDict = &dict;

    Data result = DatasSearch(dict.keysAndValues, DataRef((void*)&element, sizeof(element)), compareKeys);

    curDict = NULL;

    if(result.bytes) {

        DictionaryElement *element = (DictionaryElement*)result.bytes;

        DataFree(element->key);
        DataFree(element->value);

        dict.keysAndValues = DatasRemove(dict.keysAndValues, result);
    }

    return dict;
}

static Dictionary DictionaryAddRefFlexible(Dictionary dict, Data key, Data value, int tracked)
{
    DictionaryElement element = { key, value };

    if(curDict)
        abort();

    curDict = &dict;
    dataCompareLastTouchedPtr = NULL;

    Data result = DatasSearch(dict.keysAndValues, DataRef((void*)&element, sizeof(element)), compareKeys);

    int index = 0;

    if(dataCompareLastTouchedPtr) {

        index = (int)((Data*)dataCompareLastTouchedPtr - dict.keysAndValues.ptr);

        if(dataCompareLastResult > 0)
            index++;
    }

    curDict = NULL;

    if(result.bytes)
        dict.keysAndValues = DatasRemove(dict.keysAndValues, result);

    Data elementData = DataCopy((void*)&element, sizeof(element));

    if(!tracked)
        DataUntrack(elementData);

    dict.keysAndValues = DatasAddRefIndexFlexible(dict.keysAndValues, elementData, index, tracked);

    return dict;
}

Dictionary DictionaryAddRef(Dictionary dict, Data key, Data value)
{
    return DictionaryAddRefFlexible(dict, key, value, 1);
}

Dictionary DictionaryAddRefUntracked(Dictionary dict, Data key, Data value)
{
    return DictionaryAddRefFlexible(dict, key, value, 0);
}

Data *DictionaryGetValueFlexible(Dictionary dict, Data key)
{
    DictionaryElement element = { key, DataNull() };

    if(curDict)
        abort();

    curDict = &dict;

    Data result = DatasSearch(dict.keysAndValues, DataRef((void*)&element, sizeof(element)), compareKeys);

    curDict = NULL;

    if(!result.bytes)
        return NULL;

    return &((DictionaryElement*)result.bytes)->value;
}

Data DictionaryGetValue(Dictionary dict, Data key)
{
    Data *result = DictionaryGetValueFlexible(dict, key);

    if(result)
        return *result;

    return DataNull();
}

int DictionaryHasKey(Dictionary dict, Data key)
{
    return DictionaryGetValueFlexible(dict, key) != NULL;
}

DictionaryElement DictionaryGetElement(Dictionary dict, int index)
{
    if(index < 0 || index > dict.keysAndValues.count)
        abort();

    return *(DictionaryElement*)dict.keysAndValues.ptr[index].bytes;
}

unsigned int DictionaryCount(Dictionary dict)
{
    return dict.keysAndValues.count;
}

Dictionary DictionaryIndex(Datas datas, int index)
{
    return *(Dictionary*)DatasIndex(datas, index).bytes;
}

Data DictionarySerialize(Dictionary dict)
{
    Datas datas = DatasNew();

    FORINDICT(item, dict) {

        datas = DatasAddRef(datas, item->key);
        datas = DatasAddRef(datas, item->value);
    }

    return DatasSerialize(datas);
}

Dictionary DictionaryDeserialize(Data data)
{
    Datas datas = DatasDeserialize(data);

    if(datas.count % 2)
        abort();

    Dict dict = DictNew();

    for(int i = 0; i < datas.count; i += 2) {

        DictionaryElement ele = { datas.ptr[i], datas.ptr[i + 1] };

        dict.keysAndValues = DatasAddCopy(dict.keysAndValues, DataRaw(ele));
    }

    return dict;
}

Dictionary DictionaryCopy(Dictionary dict)
{
    Dictionary result = DictionaryNew(dict.compare);

    for(int i = 0; i < DictCount(dict); i++) {

        DictionaryElement element = DictGetI(dict, i);

        result = DictionaryAddCopy(result, element.key, element.value);
    }

    return result;
}

Dictionary DictionaryTrack(Dictionary dict)
{
    for(int i = 0; i < DictCount(dict); i++) {

        DictionaryElement element = DictGetI(dict, i);

        DataTrack(element.key);
        DataTrack(element.value);
    }

    DatasTrack(dict.keysAndValues);

    return dict;
}

Dictionary DictionaryUntrack(Dictionary dict)
{
    for(int i = 0; i < DictCount(dict); i++) {

        DictionaryElement element = DictGetI(dict, i);

        DataUntrack(element.key);
        DataUntrack(element.value);
    }

    DatasUntrack(dict.keysAndValues);

    return dict;
}

Dict DictNew()
{
    return DictionaryNew(NULL);
}

Dict DictNull()
{
    return DictOne(DataNull(), DataNull());
}

int DictIsNull(Dict dict)
{
    if(DictCount(dict) != 1)
        return 0;

    DictionaryElement item = *(DictionaryElement*)dict.keysAndValues.ptr[0].bytes;

    if(item.key.bytes != NULL || item.key.length != 0)
        return 0;

    if(item.value.bytes != NULL || item.value.length != 0)
        return 0;

    return 1;
}

void DictFree(Dictionary dict)
{
    DictionaryFree(dict);
}

Dict DictIntersect(Dict dictA, Dict dictB)
{
    return DictionaryIntersection(dictA, dictB);
}

int DictDoesIntersect(Dict dictA, Dict dictB)
{
    return DictionaryDoesIntersect(dictA, dictB);
}

Dict DictOne(Data key, Data value)
{
    Dict dict = DictNew();

    DictAdd(&dict, key, value);

    return dict;
}

Dict DictOneS(const char *key, Data value)
{
    return DictOne(StringRef((char*)key), value);
}

Dict DictTwoKeys(Data key1, Data key2)
{
    Dict dict = DictNew();

    DictAdd(&dict, key1, DataNull());
    DictAdd(&dict, key2, DataNull());

    return dict;
}

Dict DictTwo(Data key1, Data value1, Data key2, Data value2)
{
    Dict dict = DictNew();

    DictAdd(&dict, key1, value1);
    DictAdd(&dict, key2, value2);

    return dict;
}

Dict *DictAdd(Dict *dict, Data key, Data value)
{
    *dict = DictionaryAddCopy(*dict, key, value);

    return dict;
}

Dict *DictAddS(Dict *dict, const char *key, Data value)
{
    String str = DataUntrack(StringNew(key));

    *dict = DictionaryAddCopy(*dict, str, value);

    DataFree(str);

    return dict;
}

Dict *DictSet(Dict *dict, Data key, Data value)
{
    return DictAdd(dict, key, value);
}

Dict *DictSetS(Dict *dict, const char *key, Data value)
{
    return DictAddS(dict, key, value);
}

Dict *DictAddDict(Dict *dict, Dict addition)
{
    FORIN(DictionaryElement, item, addition.keysAndValues)
        DictAdd(dict, item->key, item->value);

    return dict;
}

Dict *DictRemove(Dict *dict, Data key)
{
    *dict = DictionaryRemove(*dict, key);

    return dict;
}

Dict *DictRemoveS(Dict *dict, const char *key)
{
    String str = DataUntrack(StringNew(key));

    DictRemove(dict, str);

    DataFree(str);

    return dict;
}

Dict *DictRemoveIndex(Dict *dict, int index)
{
    DictionaryElement *element = (DictionaryElement*)dict->keysAndValues.ptr[index].bytes;

    DataFree(element->key);
    DataFree(element->value);

    dict->keysAndValues = DatasRemoveIndex(dict->keysAndValues, index);

    return dict;
}

Dict DictCopy(Dict dict)
{
    return DictionaryCopy(dict);
}

Data DictGet(Dict dict, Data key)
{
    return DictionaryGetValue(dict, key);
}

Data DictGetS(Dict dict, const char *key)
{
    String str = DataUntrack(StringNew(key));

    Data result = DictionaryGetValue(dict, str);

    DataFree(str);

    return result;
}

int DictHasKey(Dict dict, Data key)
{
    return DictionaryHasKey(dict, key);
}

Datas DictAllKeysRef(Dict dict)
{
    Datas result = DatasNew();

    FORIN(DictionaryElement, item, dict.keysAndValues)
        result = DatasAddRef(result, item->key);

    return result;
}

Datas DictAllKeysCopy(Dict dict)
{
    return DatasCopy(DictAllKeysRef(dict));
}

DictionaryElement DictGetI(Dict dict, int index)
{
    return DictionaryGetElement(dict, index);
}

Data DictSerialize(Dict dict)
{
    return DictionarySerialize(dict);
}

Dict DictDeserialize(Data data)
{
    return DictionaryDeserialize(data);
}

Dict DictTrack(Dict dict)
{
    return DictionaryTrack(dict);
}

Dict DictUntrack(Dict dict)
{
    return DictionaryUntrack(dict);
}

Dict DictUntrackCopy(Dict dict)
{
    return DictionaryUntrack(DictionaryCopy(dict));
}

unsigned int DictCount(Dict dict)
{
    return DictionaryCount(dict);
}

Dictionary DictIndex(Datas datas, int index)
{
    return DictionaryIndex(datas, index);
}

void DictionaryFree(Dictionary dict)
{
    for(int i = 0; i < DictCount(dict); i++) {

        DictionaryElement element = DictGetI(dict, i);

        DataFree(element.key);
        DataFree(element.value);
    }

    DatasFree(dict.keysAndValues);
}

static String StringMutateEnsureNullByte(String string)
{
    if(string.length && 0 == string.bytes[string.length - 1])
        return string;
    
    string = DataGrow(string, 1);
    
    string.bytes[string.length - 1] = 0;
    
    return string;
}

String StringNew(const char *str)
{
    String data = DataNew(str ? (uint32_t)strlen(str) + 1 : 1);
    *data.bytes = 0;

    if(str)
        strcpy(data.bytes, str);

    return data;
}

String StringRef(char *str)
{
    return DataRef(str, (uint32_t)strlen(str) + 1);
}

String StringCopy(String string)
{
    String result = DataCopyData(string);

    if(!result.length || result.bytes[result.length - 1])
        result = DataAdd(result, StringNew(""));

    return result;
}

String StringAdd(String string, const char *secondString)
{
    unsigned int oldLen = string.length;

    string = DataGrow(string, (uint32_t)strlen(secondString));

    strcpy(string.bytes + oldLen - 1, secondString);

    return StringMutateEnsureNullByte(string);
}

String StringAddRaw(const char *string, const char *secondString)
{
    return StringAdd(StringNew(string), secondString);
}

String StringPrefix(const char *prefix, String string)
{
    string = DataCopyData(string);

    unsigned int oldLen = string.length;

    string = DataGrow(string, (uint32_t)strlen(prefix));

    memmove(string.bytes + strlen(prefix), string.bytes, oldLen);
    memcpy(string.bytes, prefix, strlen(prefix));

    return StringMutateEnsureNullByte(string);
}

String StringLowercase(String string)
{
    string = DataCopyData(string);

    for(unsigned int i = 0; i < string.length; i++)
        string.bytes[i] = tolower(string.bytes[i]);

    return StringMutateEnsureNullByte(string);
}

int StringEqual(String string, const char *secondString)
{
    return 0 == strcmp(string.bytes, secondString);
}

int StringHasPrefix(String string, const char *secondString)
{
    if(string.length < strlen(secondString) + 1)
        return 0;

    return 0 == memcmp(string.bytes, secondString, strlen(secondString));
}

Datas StringComponents(String string, char seperator)
{
    Datas result = DatasOneCopy(string);

    for(int i = 0; i < DatasLast(result).length; i++) {

        Data data = DatasLast(result);

        if(data.bytes[i] == seperator) {

            // Turn sepeator character into a NULL byte
            Data left = DataSetByte(DataCopyDataPart(data, 0, i + 1), 0, i);

            // Put this left portion into the current end spot.
            result.ptr[result.count - 1] = left;

            // Push a new end value with the right portion
            result = DatasAddRef(result, DataCopyDataPart(data, i + 1, data.length - 1 - i));

            DataFree(data);

            i = 0;
        }
    }

    return result;
}

String StringIndex(Datas datas, int index)
{
    return DatasIndex(datas, index);
}
