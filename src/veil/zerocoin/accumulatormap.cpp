// Copyright (c) 2017-2019 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "accumulatormap.h"
#include "accumulators.h"
#include "txdb.h"
#include "libzerocoin/Denominations.h"
#include "validation.h"

using namespace libzerocoin;
using namespace std;

//Construct accumulators for all denominations
AccumulatorMap::AccumulatorMap(libzerocoin::ZerocoinParams* params)
{
    this->params = params;
    for (auto& denom : zerocoinDenomList) {
        unique_ptr<Accumulator> uptr(new Accumulator(params, denom));
        mapAccumulators.insert(make_pair(denom, std::move(uptr)));
    }

    for (auto denom : libzerocoin::zerocoinDenomList)
        setUnusedDenominations.emplace(denom);
}

//Reset each accumulator to its default state
void AccumulatorMap::Reset()
{
    Reset(params);
}

void AccumulatorMap::Reset(libzerocoin::ZerocoinParams* params2)
{
    this->params = params2;
    mapAccumulators.clear();
    setUnusedDenominations.clear();
    for (auto& denom : zerocoinDenomList) {
        unique_ptr<Accumulator> uptr(new Accumulator(params2, denom));
        mapAccumulators.insert(make_pair(denom, std::move(uptr)));
        setUnusedDenominations.emplace(denom);
    }
}

//Load a checksums matched to each accumulator from the database
bool AccumulatorMap::Load(const std::map<libzerocoin::CoinDenomination, uint256>& mapCheckpoints)
{
    for (auto& mi : mapCheckpoints) {
        if (mi.second == uint256())
            continue;

        CBigNum bnValue;
        if (!pzerocoinDB->ReadAccumulatorValue(mi.second, bnValue))
            return error("%s : cannot find checksum %s", __func__, mi.second.GetHex());

        mapAccumulators.at(mi.first)->setValue(bnValue);
        setUnusedDenominations.erase(mi.first);
    }
    return true;
}

//Add a zerocoin to the accumulator of its denomination.
bool AccumulatorMap::Accumulate(const PublicCoin& pubCoin, bool fSkipValidation)
{
    CoinDenomination denom = pubCoin.getDenomination();
    if (denom == CoinDenomination::ZQ_ERROR)
        return false;

    setUnusedDenominations.erase(denom);
    if (fSkipValidation) {
        mapAccumulators.at(denom)->increment(pubCoin.getValue());
        return true;
    }

    return mapAccumulators.at(denom)->accumulate(pubCoin);
}

libzerocoin::Accumulator AccumulatorMap::GetAccumulator(libzerocoin::CoinDenomination denom)
{
    return libzerocoin::Accumulator(params, denom, GetValue(denom));
}

//Get the value of a specific accumulator
CBigNum AccumulatorMap::GetValue(CoinDenomination denom)
{
    if (denom == CoinDenomination::ZQ_ERROR)
        return CBigNum(0);
    return mapAccumulators.at(denom)->getValue();
}

//Calculate a 32bit checksum of each accumulator value. Concatenate checksums into arith_unit256
std::map<libzerocoin::CoinDenomination, uint256> AccumulatorMap::GetCheckpoints(bool fShowZeroIfEmpty)
{
    std::map<libzerocoin::CoinDenomination, uint256> mapCheckpoints;

    //Prevent possible overflows from future changes to the list and forgetting to update this code
    assert(zerocoinDenomList.size() == 4);
    for (auto& denom : zerocoinDenomList) {
        if (fShowZeroIfEmpty && setUnusedDenominations.count(denom)) {
            mapCheckpoints.emplace(std::make_pair(denom, uint256()));
            continue;
        }

        CBigNum bnValue = mapAccumulators.at(denom)->getValue();
        auto hashChecksum = GetChecksum(bnValue);
        mapCheckpoints.emplace(std::make_pair(denom, hashChecksum));
    }

    return mapCheckpoints;
}


