#ifndef MERKLEBLOCK_H
#define MERKLEBLOCK_H

#include "Data.h"

typedef struct MerkleBlock {

    Data data;

    Data hashCache;

} MerkleBlock;

MerkleBlock MerkleBlockNew(Data dataTake);
void MerkleBlockTrack(MerkleBlock *block);
void MerkleBlockUntrack(MerkleBlock *block);
void MerkleBlockFree(MerkleBlock *block);

int MerkleBlockValid(MerkleBlock *merkleBlock);

int32_t blockVersion(MerkleBlock *merkleBlock);
Data blockHash(MerkleBlock *merkleBlock);
Data blockPrevHash(MerkleBlock *merkleBlock);
Data merkleRoot(MerkleBlock *merkleBlock);
uint32_t blockTimestamp(MerkleBlock *merkleBlock);
uint32_t blockBits(MerkleBlock *merkleBlock);
uint32_t blockNonce(MerkleBlock *merkleBlock);

int hasMerkleData(MerkleBlock *merkleBlock);

uint32_t MerkleBlockTransactionCount(MerkleBlock *merkleBlock);

Data allMerkleHashes(MerkleBlock *merkleBlock); // The memory for each merkle hash is corrupted when 'data' is released
Datas merkleHashes(MerkleBlock *merkleBlock); // The memory for each merkle hash is corrupted when 'data' is released
Data merkleFlags(MerkleBlock *merkleBlock);

Data calculatedMerkleRoot(MerkleBlock *merkleBlock);
Datas matchingTxIdsIfValidRoot(MerkleBlock *merkleBlock); // Returns all matching transaction hashes or nil if merkle root does not match.

#endif
