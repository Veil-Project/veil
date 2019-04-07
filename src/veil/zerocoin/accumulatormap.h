// Copyright (c) 2017-2019 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_ACCUMULATORMAP_H
#define PIVX_ACCUMULATORMAP_H

#include "libzerocoin/Accumulator.h"
#include "libzerocoin/Coin.h"
#include "arith_uint256.h"

//A map with an accumulator for each denomination
class AccumulatorMap
{
private:
    libzerocoin::ZerocoinParams* params;
    std::map<libzerocoin::CoinDenomination, std::unique_ptr<libzerocoin::Accumulator> > mapAccumulators;
    std::set<libzerocoin::CoinDenomination> setUnusedDenominations;
public:
    explicit AccumulatorMap(libzerocoin::ZerocoinParams* params);
    bool Load(const std::map<libzerocoin::CoinDenomination, uint256>& mapCheckpoints);
    bool Accumulate(const libzerocoin::PublicCoin& pubCoin, bool fSkipValidation = false);
    libzerocoin::Accumulator GetAccumulator(libzerocoin::CoinDenomination denom);
    CBigNum GetValue(libzerocoin::CoinDenomination denom);
    std::map<libzerocoin::CoinDenomination, uint256> GetCheckpoints(bool fShowZeroIfEmpty = false);
    void Reset();
    void Reset(libzerocoin::ZerocoinParams* params2);
};
#endif //PIVX_ACCUMULATORMAP_H
