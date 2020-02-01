#ifndef base58_h
#define base58_h

#ifdef __cplusplus
extern "C" {
#endif

#include "Data.h"

// You must free results from these methods

String Base58Encode(Data data);
Data Base58Decode(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* base58_h */
