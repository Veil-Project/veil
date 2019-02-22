// Copyright (c) 2017-2019 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "accumulators.h"
#include "accumulatormap.h"
#include "chainparams.h"
#include "txdb.h"
#include "validation.h"
#include "init.h"
#include "ui_interface.h"
#include "veil/zerocoin/zchain.h"
#include "primitives/zerocoin.h"
#include "shutdown.h"
#include "veil/zerocoin/witness.h"

using namespace libzerocoin;

std::map<uint256, CBigNum> mapAccumulatorValues;
std::list<uint256> listAccCheckpointsNoDB;

uint256 GetChecksum(const CBigNum &bnValue)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << bnValue;
    return  Hash(ss.begin(), ss.end());
}

// Find the first occurrence of a certain accumulator checksum. Return 0 if not found.
int GetChecksumHeight(uint256 hashChecksum, CoinDenomination denomination)
{
    CBlockIndex* pindex = chainActive[0];
    if (!pindex)
        return 0;

    //Search through blocks to find the checksum
    while (pindex) {
        if (pindex->GetAccumulatorHash(denomination) == hashChecksum)
            return pindex->nHeight;

        //Skip forward in groups of 10 blocks since checkpoints only change every 10 blocks
        if (pindex->nHeight % 10 == 0) {
            if (pindex->nHeight + 10 > chainActive.Height())
                return 0;
            pindex = chainActive[pindex->nHeight + 10];
            continue;
        }

        pindex = chainActive.Next(pindex);
    }

    return 0;
}

bool GetAccumulatorValueFromChecksum(const uint256& hashChecksum, bool fMemoryOnly, CBigNum& bnAccValue)
{
    if (mapAccumulatorValues.count(hashChecksum)) {
        bnAccValue = mapAccumulatorValues.at(hashChecksum);
        return true;
    }

    if (fMemoryOnly)
        return false;

    if (!pzerocoinDB->ReadAccumulatorValue(hashChecksum, bnAccValue)) {
        bnAccValue = 0;
        return false;
    }

    return true;
}

bool GetAccumulatorValueFromDB(uint256 nCheckpoint, CoinDenomination denom, CBigNum& bnAccValue)
{
    return GetAccumulatorValueFromChecksum(nCheckpoint, false, bnAccValue);
}

void AddAccumulatorChecksum(const uint256& hashChecksum, const CBigNum &bnValue)
{
    pzerocoinDB->WriteAccumulatorValue(hashChecksum, bnValue);
    mapAccumulatorValues.emplace(std::make_pair(hashChecksum, bnValue));
}

void DatabaseChecksums(AccumulatorMap& mapAccumulators)
{
    for (auto& denom : zerocoinDenomList) {
        CBigNum bnValue = mapAccumulators.GetValue(denom);
        auto hashChecksum = GetChecksum(bnValue);
        AddAccumulatorChecksum(hashChecksum, bnValue);
    }
}

bool EraseChecksum(uint256 hashChecksum)
{
    //erase from both memory and database
    mapAccumulatorValues.erase(hashChecksum);
    return pzerocoinDB->EraseAccumulatorValue(hashChecksum);
}

bool EraseAccumulatorValues(const uint256& nCheckpointErase, const uint256& nCheckpointPrevious)
{
    //if the previous checksum is the same, then it should remain in the database and map
    if (nCheckpointErase == nCheckpointPrevious)
        return false;

    return EraseChecksum(nCheckpointErase);
}

bool LoadAccumulatorValuesFromDB(const std::map<libzerocoin::CoinDenomination, uint256>& mapCheckpoints)
{
    for (auto& checkpoint : mapCheckpoints) {
        auto hash = checkpoint.second;

        //if read is not successful then we are not in a state to verify zerocoin transactions
        CBigNum bnValue;
        if (!pzerocoinDB->ReadAccumulatorValue(hash, bnValue)) {
            if (!count(listAccCheckpointsNoDB.begin(), listAccCheckpointsNoDB.end(), hash))
                listAccCheckpointsNoDB.push_back(hash);
            LogPrintf("%s : Missing databased value for checksum %d", __func__, hash.GetHex());
            return false;
        }
        mapAccumulatorValues.insert(std::make_pair(hash, bnValue));
    }
    return true;
}

//Erase accumulator checkpoints for a certain block range
bool EraseCheckpoints(int nStartHeight, int nEndHeight)
{
    if (chainActive.Height() < nStartHeight)
        return false;

    nEndHeight = min(chainActive.Height(), nEndHeight);

    CBlockIndex* pindex = chainActive[nStartHeight];

    auto mapCheckpointsPrev = pindex->pprev->mapAccumulatorHashes;
    while (pindex) {
        //Do not erase the hash if it is the same as the previous block
        for (auto pairPrevious : mapCheckpointsPrev) {
            auto hashChecksum = pindex->GetAccumulatorHash(pairPrevious.first);
            if (hashChecksum != pairPrevious.second)
                EraseChecksum(hashChecksum);
        }

        LogPrintf("%s : erasing checksums for block %d\n", __func__, pindex->nHeight);

        if (pindex->nHeight + 1 <= nEndHeight)
            pindex = chainActive.Next(pindex);
        else
            break;
    }

    return true;
}

bool InitializeAccumulators(const int nHeight, int& nHeightCheckpoint, AccumulatorMap& mapAccumulators)
{
    if (nHeight < 20)
        return error("%s: height is below zerocoin activated", __func__);

    //after v2_start, accumulators need to use v2 params
    mapAccumulators.Reset(Params().Zerocoin_Params());

    //Use the previous block's checkpoint to initialize the accumulator's state
    auto mapCheckpointPrev = chainActive[nHeight - 1]->mapAccumulatorHashes;
    bool fLoad = false;
    for (auto accPair: mapCheckpointPrev) {
        if (accPair.second != uint256()) {
            mapAccumulators.Reset();
            fLoad = true;
            break;
        }
    }

    if (!fLoad) {
        mapAccumulators.Reset();
    } else if (!mapAccumulators.Load(mapCheckpointPrev))
        return error("%s: failed to reset to previous checkpoint", __func__);

    nHeightCheckpoint = nHeight;
    return true;
}

//Get checkpoint value for a specific block height
bool CalculateAccumulatorCheckpoint(int nHeight, std::map<libzerocoin::CoinDenomination, uint256>& mapCheckpoints, AccumulatorMap& mapAccumulators)
{
    //the checkpoint is updated every ten blocks, return current active checkpoint if not update block
    if (nHeight % 10 != 0 || nHeight == 10) {
        mapCheckpoints = chainActive[nHeight - 1]->mapAccumulatorHashes;
        return true;
    }

    //set the accumulators to last checkpoint value
    int nHeightCheckpoint;
    mapAccumulators.Reset();
    if (!InitializeAccumulators(nHeight, nHeightCheckpoint, mapAccumulators))
        return error("%s: failed to initialize accumulators", __func__);

    //Accumulate all coins over the last ten blocks that havent been accumulated (height - 20 through height - 11)
    int nTotalMintsFound = 0;
    CBlockIndex *pindex = chainActive[nHeightCheckpoint - 20];

    if (!pindex)
        return false;

    while (pindex->nHeight < nHeight - 10) {
        //grab mints from this block
        CBlock block;
        if(!ReadBlockFromDisk(block, pindex, Params().GetConsensus()))
            return error("%s: failed to read block from disk", __func__);

        std::list<PublicCoin> listPubcoins;
        if (!BlockToPubcoinList(block, listPubcoins))
            return error("%s: failed to get zerocoin mintlist from block %d", __func__, pindex->nHeight);

        nTotalMintsFound += listPubcoins.size();

        //add the pubcoins to accumulator
        for (const PublicCoin& pubcoin : listPubcoins) {
            if(!mapAccumulators.Accumulate(pubcoin, true))
                return error("%s: failed to add pubcoin to accumulator at height %d", __func__, pindex->nHeight);
        }

        pindex = chainActive.Next(pindex);
    }

    // if there were no new mints found, the accumulator checkpoint will be the same as the last checkpoint
    if (nTotalMintsFound == 0) {
        mapCheckpoints = chainActive[nHeight - 1]->mapAccumulatorHashes;
    }
    else
        mapCheckpoints = mapAccumulators.GetCheckpoints();

    //LogPrintf("%s checkpoint=%s\n", __func__, nCheckpoint.GetHex());
    return true;
}

std::string PrintAccumulatorCheckpoints(std::map<libzerocoin::CoinDenomination ,uint256> mapAccumulatorHashes)
{
    std::string strRet;
    for (auto p : mapAccumulatorHashes) {
        strRet += strprintf("Denom:%d Hash:%s\n", p.first, p.second.GetHex());
    }
    return strRet;
}

bool ValidateAccumulatorCheckpoint(const CBlock& block, CBlockIndex* pindex, AccumulatorMap& mapAccumulators)
{
    if (pindex->nHeight % 10 == 0 && pindex->nHeight > 10) {
        std::map<libzerocoin::CoinDenomination, uint256> mapCheckpointCalculated;

        if (!CalculateAccumulatorCheckpoint(pindex->nHeight, mapCheckpointCalculated, mapAccumulators))
            return error("%s : failed to calculate accumulator checkpoint", __func__);

        for (auto checkpointPair: mapAccumulators.GetCheckpoints(true)) {
            if (checkpointPair.second != block.mapAccumulatorHashes.at(checkpointPair.first))
                return error("%s : accumulator does not match calculated value. block=%s calculated=%s", __func__, pindex->mapAccumulatorHashes.at(checkpointPair.first).GetHex(), checkpointPair.second.GetHex());
        }

        return true;
    }

    if (block.mapAccumulatorHashes != pindex->pprev->mapAccumulatorHashes)
        return error("%s : new accumulator checkpoint generated on a block that is not multiple of 10", __func__);

    return true;
}

void RandomizeSecurityLevel(int& nSecurityLevel)
{
    //security level: this is an important prevention of tracing the coins via timing. Security level represents how many checkpoints
    //of accumulated coins are added *beyond* the checkpoint that the mint being spent was added too. If each spend added the exact same
    //amounts of checkpoints after the mint was accumulated, then you could know the range of blocks that the mint originated from.
    if (nSecurityLevel < 100) {
        //add some randomness to the user's selection so that it is not always the same
        nSecurityLevel += CBigNum::randBignum(10).getint();

        //security level 100 represents adding all available coins that have been accumulated - user did not select this
        if (nSecurityLevel >= 100)
            nSecurityLevel = 99;
    }
}

//Compute how many coins were added to an accumulator up to the end height
int ComputeAccumulatedCoins(int nHeightEnd, libzerocoin::CoinDenomination denom)
{
    LOCK(cs_main);
    CBlockIndex* pindex = chainActive[GetZerocoinStartHeight()];
    int n = 0;
    while (pindex->nHeight < nHeightEnd) {
        n += count(pindex->vMintDenominationsInBlock.begin(), pindex->vMintDenominationsInBlock.end(), denom);
        pindex = chainActive.Next(pindex);
    }

    return n;
}

int AddBlockMintsToAccumulator(const libzerocoin::PublicCoin& coin, const int nHeightMintAdded, const CBlockIndex* pindex,
                           libzerocoin::Accumulator* accumulator, bool isWitness)
{
    // if this block contains mints of the denomination that is being spent, then add them to the witness
    if (!pindex->MintedDenomination(coin.getDenomination()))
        return 0;

    int nMintsAdded = 0;
    int nHeight = pindex->nHeight;
    list<PublicCoin> listPubcoins;
    //Do not keep cs_main locked during modular exponentiation (unless this is already locked from the validation)
    {
        //grab mints from this block
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus()))
            return error("%s: failed to read block from disk while adding pubcoins to witness", __func__);

        if (!BlockToPubcoinList(block, listPubcoins))
            return error("%s: failed to get zerocoin mintlist from block %n\n", __func__, pindex->nHeight);
    }

    //add the mints to the witness
    for (const PublicCoin& pubcoin : listPubcoins) {
        if (pubcoin.getDenomination() != coin.getDenomination())
            continue;

        if (isWitness && nHeight == nHeightMintAdded && pubcoin.getValue() == coin.getValue())
            continue;

        accumulator->increment(pubcoin.getValue());
        ++nMintsAdded;
    }

    return nMintsAdded;
}

bool GetAccumulatorValue(int& nHeight, const libzerocoin::CoinDenomination denom, CBigNum& bnAccValue)
{
    if (nHeight > chainActive.Height())
        return error("%s: height %d is more than active chain height", __func__, nHeight);

    //Every situation except for about 20 blocks should use this method
    uint256 nCheckpointBeforeMint = chainActive[nHeight]->GetAccumulatorHash(denom);
    return GetAccumulatorValueFromDB(nCheckpointBeforeMint, denom, bnAccValue);
}

void AccumulateRange(CoinWitnessData* coinWitness, int nHeightEnd)
{
    int nHeightStart = std::max(coinWitness->nHeightAccStart, coinWitness->nHeightPrecomputed + 1);
    CBlockIndex* pindex = chainActive[nHeightStart];
    LogPrintf("%s:%d Accumulate %d until height %d\n", __func__, __LINE__, nHeightStart, nHeightEnd);
    libzerocoin::PublicCoin pubCoinSelected = *coinWitness->coin;
    while (pindex && pindex->nHeight <= nHeightEnd) {
        coinWitness->nMintsAdded += AddBlockMintsToAccumulator(pubCoinSelected, coinWitness->nHeightMintAdded, pindex, coinWitness->pAccumulator.get(), true);
        coinWitness->nHeightPrecomputed = pindex->nHeight;

        pindex = chainActive.Next(pindex);
    }
}

bool GenerateAccumulatorWitness(CoinWitnessData* coinwitness, AccumulatorMap& mapAccumulators, int nSecurityLevel, string& strError, CBlockIndex* pindexCheckpoint)
{
    CBigNum bnAccValue = 0;
    int nHeightStop = 0;
    libzerocoin::PublicCoin coin = *coinwitness->coin;
    {
        LOCK(cs_main);

        //If there is a Acc End height filled in, then this has already been partially accumulated.
        if (!coinwitness->nHeightPrecomputed) {
            LogPrintf("Starting precompute for mint %s accumulated height=%d\n", coinwitness->coin->getValue().GetHex().substr(0, 6), coinwitness->nHeightMintAdded);
            coinwitness->pAccumulator = std::unique_ptr<Accumulator>(new Accumulator(Params().Zerocoin_Params(), coinwitness->denom));
            coinwitness->pWitness = std::unique_ptr<AccumulatorWitness>(new AccumulatorWitness(Params().Zerocoin_Params(), *coinwitness->pAccumulator, coin));
        } else {
            LogPrintf("%s: Using precomputed coinspend witness. \n", __func__);
        }

        uint256 txid;
        if (!pzerocoinDB->ReadCoinMint(coin.getValue(), coinwitness->txid))
            return error("%s failed to find mint %s in blockchain db", __func__, GetPubCoinHash((*coinwitness->coin).getValue()).GetHex());

        CTransactionRef txMinted;
        uint256 hashBlock;
        if (!GetTransaction(coinwitness->txid, txMinted, Params().GetConsensus(), hashBlock, true))
            return error("%s failed to read tx %s", __func__, coinwitness->txid.GetHex());

        int nHeightTest;
        if (!IsBlockHashInChain(hashBlock, nHeightTest))
            return error("%s: mint tx %s is not in chain", __func__, txid.GetHex());

        coinwitness->SetHeightMintAdded(mapBlockIndex[hashBlock]->nHeight);

        //Get the accumulator that is right before the cluster of blocks containing our mint was added to the accumulator
        bnAccValue = 0;
        if (!coinwitness->nHeightPrecomputed && GetAccumulatorValue(coinwitness->nHeightCheckpoint, coin.getDenomination(), bnAccValue)) {
            LogPrintf("%s: using new accumulator from checkpoint block %d\n", __func__, coinwitness->nHeightCheckpoint);
            libzerocoin::Accumulator witnessAccumulator(Params().Zerocoin_Params(), coinwitness->denom, bnAccValue);
            coinwitness->pAccumulator->setValue(witnessAccumulator.getValue());
        }

        //add the pubcoins from the blockchain up to the next checksum starting from the block
        int nChainHeight = chainActive.Height();
        nHeightStop = nChainHeight % 10;
        nHeightStop = nChainHeight - nHeightStop - 20; // at least two checkpoints deep

        //If looking for a specific checkpoint
        if (pindexCheckpoint) {
            nHeightStop = pindexCheckpoint->nHeight - 10;
            nHeightStop -= nHeightStop % 10;
            LogPrintf("%s: using checkpoint height %d\n", __func__, pindexCheckpoint->nHeight);
        }

        if (nHeightStop <= coinwitness->nHeightPrecomputed)
            return error("%s: trying to accumulate bad block range, start=%d end=%d", __func__, coinwitness->nHeightPrecomputed, nHeightStop);
    }

    AccumulateRange(coinwitness, nHeightStop - 1);
    mapAccumulators.Load(chainActive[nHeightStop + 10]->mapAccumulatorHashes);
    coinwitness->pWitness->resetValue(*coinwitness->pAccumulator, coin);
    if(!coinwitness->pWitness->VerifyWitness(mapAccumulators.GetAccumulator(coinwitness->denom), coin))
        return error("%s: failed to verify witness", __func__);

    // A certain amount of accumulated coins are required
    if (coinwitness->nMintsAdded < Params().Zerocoin_RequiredAccumulation()) {
        strError = _(strprintf("Less than %d mints added, unable to create spend", Params().Zerocoin_RequiredAccumulation()).c_str());
        return error("%s : %s", __func__, strError);
    }

    // calculate how many mints of this denomination existed in the accumulator we initialized
    coinwitness->nMintsAdded += ComputeAccumulatedCoins(coinwitness->nHeightAccStart, coin.getDenomination());
    LogPrintf("%s : %d mints added to witness\n", __func__, coinwitness->nMintsAdded);

    return true;
}

map<CoinDenomination, int> GetMintMaturityHeight()
{
    map<CoinDenomination, pair<int, int > > mapDenomMaturity;
    for (auto denom : libzerocoin::zerocoinDenomList)
        mapDenomMaturity.insert(make_pair(denom, make_pair(0, 0)));

    int nConfirmedHeight = chainActive.Height() - Params().Zerocoin_MintRequiredConfirmations();

    // A mint need to get to at least the min maturity height before it will spend.
    int nMinimumMaturityHeight = nConfirmedHeight - (nConfirmedHeight % 10);
    CBlockIndex* pindex = chainActive[nConfirmedHeight];

    while (pindex) {
        bool isFinished = true;
        for (auto denom : libzerocoin::zerocoinDenomList) {
            //If the denom has not already had a mint added to it, then see if it has a mint added on this block
            if (mapDenomMaturity.at(denom).first < Params().Zerocoin_RequiredAccumulation()) {
                mapDenomMaturity.at(denom).first += count(pindex->vMintDenominationsInBlock.begin(),
                                                          pindex->vMintDenominationsInBlock.end(), denom);

                //if mint was found then record this block as the first block that maturity occurs.
                if (mapDenomMaturity.at(denom).first >= Params().Zerocoin_RequiredAccumulation())
                    mapDenomMaturity.at(denom).second = std::min(pindex->nHeight, nMinimumMaturityHeight);

                //Signal that we are finished
                isFinished = false;
            }
        }

        if (isFinished)
            break;
        pindex = chainActive[pindex->nHeight - 1];
    }

    //Generate final map
    map<CoinDenomination, int> mapRet;
    for (auto denom : libzerocoin::zerocoinDenomList)
        mapRet.insert(make_pair(denom, mapDenomMaturity.at(denom).second));

    return mapRet;
}
