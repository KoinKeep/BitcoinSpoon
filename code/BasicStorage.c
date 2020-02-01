#include "BasicStorage.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

static struct {
    pthread_mutex_t mutex;
    String filename;

} bs = { PTHREAD_MUTEX_INITIALIZER, {0} };

void basicStorageSetup(String filename)
{
    if(pthread_mutex_lock(&bs.mutex) != 0)
        abort();

    DataFree(bs.filename);
    bs.filename = DataUntrack(StringCopy(filename));

    if(pthread_mutex_unlock(&bs.mutex) != 0)
        abort();
}

static void writeItem(FILE *file, Data item)
{
    uint32_t len = item.length;

    size_t val = fwrite(&len, 1, sizeof(len), file);

    if(val != sizeof(len))
        abort();

    if(fwrite(item.bytes, 1, len, file) != len)
        abort();
}

static Data readItem(FILE *file, uint32_t maxSize)
{
    uint32_t len = 0;

    if(fread(&len, 1, sizeof(len), file) != sizeof(len))
        return DataNull();

    if(len > maxSize)
        return DataNull();

    Data result = DataNew(len);

    if(fread(result.bytes, 1, len, file) != len)
        return DataNull();

    return result;
}

static Dict loadEverything()
{
    if(!bs.filename.bytes)
        abort();

    FILE *file = fopen(bs.filename.bytes, "r");

    Dict dict = DictNew();

    while(file) {

        String key = readItem(file, BS_MAX_KEYSIZE);

        if(!key.length)
            break;

        Data value = readItem(file, BS_MAX_DATASIZE);

        DictAdd(&dict, key, value);
    }

    if(file)
        fclose(file);

    return dict;
}

static void saveEverything(Dict dict)
{
    if(!bs.filename.bytes)
        abort();

    FILE *file = fopen(bs.filename.bytes, "w");

    if(!file)
        abort();

    FORIN(DictionaryElement, item, dict.keysAndValues) {

        if(item->key.length > BS_MAX_KEYSIZE) {

            fclose(file);
            abort();
        }

        if(item->value.length > BS_MAX_DATASIZE) {

            fclose(file);
            abort();
        }

        writeItem(file, item->key);
        writeItem(file, item->value);
    }

    writeItem(file, DataNull());

    fclose(file);
}

void basicStorageSave(String key, Data data)
{
    if(pthread_mutex_lock(&bs.mutex) != 0)
        abort();

    DataTrackPush();

    Dict dict = loadEverything();

    DictAdd(&dict, StringCopy(key), data);

    saveEverything(dict);

    DataTrackPop();

    if(pthread_mutex_unlock(&bs.mutex) != 0)
        abort();
}

Data basicStorageLoad(String key)
{
    if(pthread_mutex_lock(&bs.mutex) != 0)
        abort();

    Dict dict = loadEverything();

    Data result = DictGet(dict, StringCopy(key));

    if(pthread_mutex_unlock(&bs.mutex) != 0)
        abort();

    return DataCopyData(result);
}

void bsSetup(const char *filename)
{
    String str = StringNew((char*)filename);

    basicStorageSetup(str);

    DataFree(str);
}

void bsSave(const char *key, Data data)
{
    basicStorageSave(StringRef((char*)key), data);
}

Data bsLoad(const char *key)
{
    return basicStorageLoad(StringRef((char*)key));
}

void basicStorageDeleteAll()
{
    if(pthread_mutex_lock(&bs.mutex) != 0)
        abort();

    DataTrackPush();

    saveEverything(DictNew());

    DataTrackPop();

    if(pthread_mutex_unlock(&bs.mutex) != 0)
        abort();
}
