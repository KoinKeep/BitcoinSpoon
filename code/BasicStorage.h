#ifndef BASICSTORAGE_H
#define BASICSTORAGE_H

#include "Data.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=== Basic, run of the mill inefficent storage for small things ===*/

#define BS_MAX_KEYSIZE 1024
#define BS_MAX_DATASIZE 10 * 1024 * 1024

void basicStorageSetup(String filename);

void basicStorageSave(String key, Data data);
Data basicStorageLoad(String key);

// Shorthands for basicStorage save / load
void bsSetup(const char *filename);
void bsSave(const char *key, Data data);
Data bsLoad(const char *key);

void basicStorageDeleteAll(void);

#ifdef __cplusplus
}
#endif

#endif
