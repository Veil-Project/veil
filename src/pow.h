// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/params.h>

#include <stdint.h>

class CBlockHeader;
class CBlockIndex;
class uint256;
class randomx_vm;

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&,
                                    bool fProofOfStake, bool fProgPow);
unsigned int DarkGravityWave(const CBlockIndex* pindexLast, const Consensus::Params& params, bool fProofOfStake, bool fProgPow);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);

/** Check whether a block hash satisfies the prog-proof-of-work requirement specified by nBits */
bool CheckProgProofOfWork(const CBlockHeader& block, unsigned int nBits, const Consensus::Params&);


bool IsRandomXLightInit();
void InitRandomXLightCache(const int32_t& height);
void KeyBlockChanged(const uint256& new_block);
void CheckIfKeyShouldChange(const uint256& check_block);
void DeallocateRandomXLightCache();
uint256 GetCurrentKeyBlock();
uint256 GetKeyBlock(const uint32_t& nHeight);
randomx_vm* GetMyMachineMining();
randomx_vm* GetMyMachineValidating();

/** Check whether a block hash satisfies the prog-proof-of-work requirement specified by nBits */
bool CheckRandomXProofOfWork(const CBlockHeader& block, unsigned int nBits, const Consensus::Params&);

#endif // BITCOIN_POW_H
