// Copyright (c) 2017-2019 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_ACCUMULATORS_H
#define PIVX_ACCUMULATORS_H

#include "libzerocoin/Accumulator.h"
#include "libzerocoin/Coin.h"
#include "libzerocoin/Denominations.h"
#include "primitives/zerocoin.h"
#include "accumulatormap.h"
#include "chain.h"
#include "arith_uint256.h"

class CBlockIndex;
class CoinWitnessData;

std::map<libzerocoin::CoinDenomination, int> GetMintMaturityHeight();
bool GenerateAccumulatorWitness(CoinWitnessData* coinwitness, AccumulatorMap& mapAccumulators, int nSecurityLevel, std::string& strError, CBlockIndex* pindexCheckpoint = nullptr);
bool GetAccumulatorValueFromDB(uint256 nCheckpoint, libzerocoin::CoinDenomination denom, CBigNum& bnAccValue);
bool GetAccumulatorValueFromChecksum(const uint256& hashChecksum, bool fMemoryOnly, CBigNum& bnAccValue);
void AddAccumulatorChecksum(const uint256 nChecksum, const CBigNum &bnValue, bool fMemoryOnly);
bool CalculateAccumulatorCheckpoint(int nHeight, std::map<libzerocoin::CoinDenomination, uint256>& mapCheckpoints, AccumulatorMap& mapAccumulators);
void DatabaseChecksums(AccumulatorMap& mapAccumulators);
bool LoadAccumulatorValuesFromDB(const uint256 nCheckpoint);
bool EraseAccumulatorValues(const uint256& nCheckpointErase, const uint256& nCheckpointPrevious);
uint256 GetChecksum(const CBigNum &bnValue);
int GetChecksumHeight(uint256 nChecksum, libzerocoin::CoinDenomination denomination);
bool ValidateAccumulatorCheckpoint(const CBlock& block, CBlockIndex* pindex, AccumulatorMap& mapAccumulators);
void AccumulateRange(CoinWitnessData* coinWitness, int nHeightEnd);

#endif //PIVX_ACCUMULATORS_H
