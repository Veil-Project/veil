// Copyright (c) 2017-2018 The PIVX developers
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

std::map<libzerocoin::CoinDenomination, int> GetMintMaturityHeight();
bool GenerateAccumulatorWitness(const libzerocoin::PublicCoin &coin, libzerocoin::Accumulator& accumulator, libzerocoin::AccumulatorWitness& witness, int nSecurityLevel, int& nMintsAdded, std::string& strError, CBlockIndex* pindexCheckpoint = nullptr);
bool GetAccumulatorValueFromDB(arith_uint256 nCheckpoint, libzerocoin::CoinDenomination denom, CBigNum& bnAccValue);
bool GetAccumulatorValueFromChecksum(arith_uint256 nChecksum, bool fMemoryOnly, CBigNum& bnAccValue);
void AddAccumulatorChecksum(const arith_uint256 nChecksum, const CBigNum &bnValue, bool fMemoryOnly);
bool CalculateAccumulatorCheckpoint(int nHeight, arith_uint256& nCheckpoint, AccumulatorMap& mapAccumulators);
void DatabaseChecksums(AccumulatorMap& mapAccumulators);
bool LoadAccumulatorValuesFromDB(const arith_uint256 nCheckpoint);
bool EraseAccumulatorValues(const arith_uint256& nCheckpointErase, const arith_uint256& nCheckpointPrevious);
arith_uint256 ParseChecksum(arith_uint256 nChecksum, libzerocoin::CoinDenomination denomination);
arith_uint256 GetChecksum(const CBigNum &bnValue);
int GetChecksumHeight(arith_uint256 nChecksum, libzerocoin::CoinDenomination denomination);
bool InvalidCheckpointRange(int nHeight);
bool ValidateAccumulatorCheckpoint(const CBlock& block, CBlockIndex* pindex, AccumulatorMap& mapAccumulators);

#endif //PIVX_ACCUMULATORS_H
