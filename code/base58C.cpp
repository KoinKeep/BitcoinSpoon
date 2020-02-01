#include "base58.h"
#include "../libraries/base58/base58.hpp"
#include <string>
#include <vector>

extern "C" String Base58Encode(Data data)
{
    const uint8_t *ptr = (const uint8_t*)data.bytes;

    std::string result = EncodeBase58(ptr, ptr + data.length);

    return StringNew(result.c_str());
}

extern "C" Data Base58Decode(const char *str)
{
    std::vector<uint8_t> result;

    if(!DecodeBase58(str, result))
        return DataNull();

    return DataCopy((char*)&result[0], (uint32_t)result.size());
}
