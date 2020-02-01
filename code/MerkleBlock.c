#include "MerkleBlock.h"
#include "BTCUtil.h"

static int calculateTreeWidth(MerkleBlock *block, int height);

MerkleBlock MerkleBlockNew(Data data)
{
    MerkleBlock block = { 0 };

    block.data = data;

    return block;
}

void MerkleBlockTrack(MerkleBlock *block)
{
    DataTrack(block->data);
    DataTrack(block->hashCache);
}

void MerkleBlockUntrack(MerkleBlock *block)
{
    DataUntrack(block->data);
    DataUntrack(block->hashCache);
}

void MerkleBlockFree(MerkleBlock *block)
{
    block->data = DataFree(block->data);
    block->hashCache = DataFree(block->hashCache);

    *block = (MerkleBlock) { 0 };
}

int MerkleBlockValid(MerkleBlock *block)
{
    int result = block->data.length >= 80;

    Datas hashes = merkleHashes(block);

    // As per https://github.com/bitcoin/bitcoin/blob/bccb4d29a8080bf1ecda1fc235415a11d903a680/src/consensus/merkle.cpp
    // the last two hashes cannot be the same! This protects against a vulnerability related to dupilicate txids (see link).
    if(hashes.count > 1)
        result = result && !DataEqual(hashes.ptr[hashes.count - 1], hashes.ptr[hashes.count - 2]);

    return result;
}

int32_t blockVersion(MerkleBlock *block)
{
    if(block->data.length < 80)
        return 0;

    return *(int32_t*)block->data.bytes;
}

Data blockHash(MerkleBlock *block)
{
    if(!block)
        return DataNull();

    if(block->hashCache.bytes)
        return block->hashCache;

    if(block->data.length < 80)
        return DataNull();

    block->hashCache = DataFree(block->hashCache);
    block->hashCache = DataUntrack(hash256(DataCopyDataPart(block->data, 0, 80)));

    if(DataIsTracked(block->data))
        DataTrack(block->hashCache);

    return block->hashCache;
}

Data blockPrevHash(MerkleBlock *block)
{
    if(block->data.length < 80)
        return DataNull();

    return DataCopyDataPart(block->data, 4, 32);
}

Data merkleRoot(MerkleBlock *block)
{
    if(block->data.length < 80)
        return DataNull();

    return DataCopyDataPart(block->data, 36, 32);
}

uint32_t blockTimestamp(MerkleBlock *block)
{
    if(block->data.length < 80)
        return 0;

    return *(uint32_t*)DataCopyDataPart(block->data, 68, 4).bytes;
}

uint32_t blockBits(MerkleBlock *block)
{
    if(block->data.length < 80)
        return 0;

    return *(uint32_t*)DataCopyDataPart(block->data, 72, 4).bytes;
}

uint32_t blockNonce(MerkleBlock *block)
{
    if(block->data.length < 80)
        return 0;

    return *(uint32_t*)DataCopyDataPart(block->data, 76, 4).bytes;
}

uint32_t MerkleBlockTransactionCount(MerkleBlock *block)
{
    if(block->data.length < 84)
        return 0;

    return *(uint32_t*)DataCopyDataPart(block->data, 80, 4).bytes;
}

int hasMerkleData(MerkleBlock *block)
{
    return block->data.length > 84;
}

Data allMerkleHashes(MerkleBlock *block)
{
    if(block->data.length < 85)
        return DataNull();

    const uint8_t *ptr = (uint8_t*)block->data.bytes;
    const uint8_t *end = ptr + block->data.length;

    ptr += 84;

    uint64_t count = readVarInt(&ptr, end);

    if(end - ptr < 32 * count)
        return DataNull();

    return DataRef((void*)ptr, (uint32_t)(32 * count));
}

Datas merkleHashes(MerkleBlock *block)
{
    if(block->data.length < 85)
        return DatasNew();

    const uint8_t *ptr = (uint8_t*)block->data.bytes;
    const uint8_t *end = ptr + block->data.length;

    ptr += 84;

    Datas array = DatasNew();

    uint64_t count = readVarInt(&ptr, end);

    for(int i = 0; i < count; i++) {

        if(end - ptr < 32)
            return DatasNew();

        array = DatasAddRef(array, DataRef((void*)ptr, 32));

        ptr += 32;
    }

    return array;
}

Data merkleFlags(MerkleBlock *block)
{
    const uint8_t *ptr = (uint8_t*)block->data.bytes;
    const uint8_t *end = ptr + block->data.length;

    ptr += 84;

    uint64_t hashCount = readVarInt(&ptr, end);

    if(end - ptr < hashCount * 32)
        return DataNull();

    ptr += hashCount * 32;

    uint64_t flagBytes = readVarInt(&ptr, end);

    if(end - ptr != flagBytes)
        return DataNull();

    return DataCopy(ptr, (uint32_t)flagBytes);
}

static int calculateTreeWidth(MerkleBlock *block, int height)
{
    return (MerkleBlockTransactionCount(block) + (1 << height) - 1) >> height;
}

Data nextElement(Datas *array)
{
    if(!array->count)
        return DataNull();

    Data result = DatasFirst(*array);

    *array = DatasRemoveIndexTake(*array, 0);

    return result;
}

static Data calculatedMerkleRootFlexible(Data flags, int *i, Datas *hashes, int height, Datas *matches)
{
    if(*i >= flags.length * 8)
        return DataNull();

    int flag = ((uint8_t*)flags.bytes)[*i / 8] & (1 << *i % 8);

    ++*i;

    if(flag) {

        if(height == 0) {

            Data element = nextElement(hashes);

            if(matches)
                *matches = DatasAddRef(*matches, element);

            return element;
        }

        Data left = calculatedMerkleRootFlexible(flags, i, hashes, height - 1, matches);

        Data right = calculatedMerkleRootFlexible(flags, i, hashes, height - 1, matches);

        if(!right.bytes)
            right = left;

        return hash256(DataAddCopy(left, right));
    }
    else {

        return nextElement(hashes);
    }
}

static Data calculatedMerkleRootMaestro(MerkleBlock *block, Datas *matches)
{
    if(!MerkleBlockValid(block))
        return DataNull();

    Data flags = merkleFlags(block);

    int i = 0;

    int height = 0;

    while(calculateTreeWidth(block, height) > 1)
        height++;

    Datas hashes = DatasCopy(merkleHashes(block));

    return calculatedMerkleRootFlexible(flags, &i, &hashes, height, matches);
}

Data calculatedMerkleRoot(MerkleBlock *block)
{
    return calculatedMerkleRootMaestro(block, 0);
}

Datas matchingTxIdsIfValidRoot(MerkleBlock *block)
{
    Datas matches = DatasNew();
    Data root = calculatedMerkleRootMaestro(block, &matches);

    if(!root.bytes || !merkleRoot(block).bytes || !DataEqual(root, merkleRoot(block)))
        return DatasNew();

    return matches;
}
