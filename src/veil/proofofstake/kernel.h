// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2019 The PIVX developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KERNEL_H
#define BITCOIN_KERNEL_H

#include "validation.h"
class StakeInput;

bool CheckStake(const CDataStream& ssUniqueID, CAmount nValueIn, const uint64_t nStakeModifier, const uint256& bnTarget, unsigned int nTimeBlockFrom, unsigned int& nTimeTx, uint256& hashProofOfStake);
bool stakeTargetHit(arith_uint256 hashProofOfStake, int64_t nValueIn, arith_uint256 bnTargetPerCoinDay);
bool Stake(StakeInput* stakeInput, unsigned int nBits, unsigned int nTimeBlockFrom, unsigned int& nTimeTx, const CBlockIndex* pindexBest, uint256& hashProofOfStake, bool fWeightStake);
bool CheckProofOfStake(CBlockIndex* pindexCheck, const CTransactionRef txRef, const uint32_t& nBits, const unsigned int& nTimeBlock, uint256& hashProofOfStake, std::unique_ptr<StakeInput>& stake);

#endif // BITCOIN_KERNEL_H
