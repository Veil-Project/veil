#include <chainparams.h>
#include <tinyformat.h>
#include "witness.h"
#include <util.h>
#include <wallet/lrucache.h>
#include <wallet/walletutil.h>
#include <wallet/walletdb.h>

void CoinWitnessData::SetNull()
{
    coin = nullptr;
    pAccumulator = nullptr;
    pWitness = nullptr;
    nMintsAdded = 0;
    nHeightMintAdded = 0;
    nHeightCheckpoint = 0;
    nHeightAccStart = 0;
    nHeightPrecomputed = 0;
}

CoinWitnessData::CoinWitnessData()
{
    SetNull();
}

std::string CoinWitnessData::ToString()
{
    return strprintf("Mints Added: %d\n"
            "Height Mint added: %d\n"
            "Height Checkpoint: %d\n"
            "Height Acc Start: %d\n"
            "Height Precomputed To: %d\n"
            "Amount: %s\n"
            "Demon: %d\n", nMintsAdded, nHeightMintAdded, nHeightCheckpoint, nHeightAccStart, nHeightPrecomputed, coin->getValue().GetHex(), coin->getDenomination());
}

CoinWitnessData::CoinWitnessData(CZerocoinMint& mint)
{
    SetNull();
    denom = mint.GetDenomination();
    libzerocoin::ZerocoinParams* paramsCoin = Params().Zerocoin_Params();
    coin = std::unique_ptr<libzerocoin::PublicCoin>(new libzerocoin::PublicCoin(paramsCoin, mint.GetValue(), denom));
    libzerocoin::Accumulator accumulator1(Params().Zerocoin_Params(), denom);
    pWitness = std::unique_ptr<libzerocoin::AccumulatorWitness>(new libzerocoin::AccumulatorWitness(Params().Zerocoin_Params(), accumulator1, *coin));
    pAccumulator = std::unique_ptr<libzerocoin::Accumulator>(new libzerocoin::Accumulator(Params().Zerocoin_Params(), denom));
    nHeightAccStart = mint.GetHeight();
}

CoinWitnessData::CoinWitnessData(CoinWitnessCacheData& data)
{
    SetNull();
    denom = data.denom;
    libzerocoin::ZerocoinParams* paramsCoin = Params().Zerocoin_Params();
    coin = std::unique_ptr<libzerocoin::PublicCoin>(new libzerocoin::PublicCoin(paramsCoin, data.coinAmount, data.coinDenom));
    pAccumulator = std::unique_ptr<libzerocoin::Accumulator>(new libzerocoin::Accumulator(Params().Zerocoin_Params(), denom, data.accumulatorAmount));
    pWitness = std::unique_ptr<libzerocoin::AccumulatorWitness>(new libzerocoin::AccumulatorWitness(Params().Zerocoin_Params(), *pAccumulator, *coin));
    nMintsAdded = data.nMintsAdded;
    nHeightMintAdded = data.nHeightMintAdded;
    nHeightCheckpoint = data.nHeightCheckpoint;
    nHeightAccStart = data.nHeightAccStart;
    nHeightPrecomputed = data.nHeightPrecomputed;
    txid = data.txid;
}

CoinWitnessData::CoinWitnessData(const CoinWitnessData& other)
{
    SetNull();
    denom = other.denom;
    libzerocoin::ZerocoinParams* paramsCoin = Params().Zerocoin_Params();
    coin = std::unique_ptr<libzerocoin::PublicCoin>(new libzerocoin::PublicCoin(paramsCoin, other.coin->getValue(), other.denom));
    pAccumulator = std::unique_ptr<libzerocoin::Accumulator>(new libzerocoin::Accumulator(Params().Zerocoin_Params(), denom, other.pAccumulator->getValue()));
    pWitness = std::unique_ptr<libzerocoin::AccumulatorWitness>(new libzerocoin::AccumulatorWitness(Params().Zerocoin_Params(), *pAccumulator, *coin));
    nMintsAdded = other.nMintsAdded;
    nHeightMintAdded = other.nHeightMintAdded;
    nHeightCheckpoint = other.nHeightCheckpoint;
    nHeightAccStart = other.nHeightAccStart;
    nHeightPrecomputed = other.nHeightPrecomputed;
    txid = other.txid;
}

CoinWitnessData& CoinWitnessData::operator=(const CoinWitnessData& other)
{
    SetNull();
    denom = other.denom;
    libzerocoin::ZerocoinParams* paramsCoin = Params().Zerocoin_Params();
    coin = std::unique_ptr<libzerocoin::PublicCoin>(new libzerocoin::PublicCoin(paramsCoin, other.coin->getValue(), other.denom));
    pAccumulator = std::unique_ptr<libzerocoin::Accumulator>(new libzerocoin::Accumulator(Params().Zerocoin_Params(), denom, other.pAccumulator->getValue()));
    pWitness = std::unique_ptr<libzerocoin::AccumulatorWitness>(new libzerocoin::AccumulatorWitness(Params().Zerocoin_Params(), *pAccumulator, *coin));
    nMintsAdded = other.nMintsAdded;
    nHeightMintAdded = other.nHeightMintAdded;
    nHeightCheckpoint = other.nHeightCheckpoint;
    nHeightAccStart = other.nHeightAccStart;
    nHeightPrecomputed = other.nHeightPrecomputed;
    txid = other.txid;
    return *this;
}

void CoinWitnessData::SetHeightMintAdded(int nHeight)
{
    nHeightMintAdded = nHeight;
    nHeightCheckpoint = nHeight + (10 - (nHeight % 10));
    nHeightAccStart = nHeight - (nHeight % 10);
}



void CoinWitnessCacheData::SetNull()
{
    nMintsAdded = 0;
    nHeightMintAdded = 0;
    nHeightCheckpoint = 0;
    nHeightAccStart = 0;
    nHeightPrecomputed = 0;
    coinAmount = CBigNum(0);
    coinDenom = libzerocoin::CoinDenomination::ZQ_ERROR;
    accumulatorAmount = CBigNum(0);
    accumulatorDenom = libzerocoin::CoinDenomination::ZQ_ERROR;

}

CoinWitnessCacheData::CoinWitnessCacheData()
{
    SetNull();
}

CoinWitnessCacheData::CoinWitnessCacheData(CoinWitnessData* coinWitnessData)
{
    SetNull();
    denom = coinWitnessData->denom;
    txid = coinWitnessData->txid;
    nMintsAdded = coinWitnessData->nMintsAdded;
    nHeightMintAdded = coinWitnessData->nHeightMintAdded;
    nHeightCheckpoint = coinWitnessData->nHeightCheckpoint;
    nHeightAccStart = coinWitnessData->nHeightAccStart;
    nHeightPrecomputed = coinWitnessData->nHeightPrecomputed;
    coinAmount = coinWitnessData->coin->getValue();
    coinDenom = coinWitnessData->coin->getDenomination();
    accumulatorAmount = coinWitnessData->pAccumulator->getValue();
    accumulatorDenom = coinWitnessData->pAccumulator->getDenomination();
}

CPrecomputeDB::CPrecomputeDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetWalletDir() / "precomputes", nCacheSize, fMemory, fWipe)
{
}

bool CPrecomputeDB::LoadPrecomputes(LRUCache* lru)
{

    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    char type = 'P';
    pcursor->Seek(std::make_pair(type, uint256()));

    std::pair<unsigned char, uint256> key;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();

        if (pcursor->GetKey(key) && key.first == type) {

            CoinWitnessCacheData data;
            if (!pcursor->GetValue(data)) {
                return error("%s: cannot parse CCoins record", __func__);
            }

            lru->AddNew(key.second, data);
            if (lru->Size() == PRECOMPUTE_LRU_CACHE_SIZE)
                break;

            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}

bool CPrecomputeDB::LoadPrecomputes(std::set<uint256> setHashes)
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    char type = 'P';
    pcursor->Seek(std::make_pair(type, uint256()));

    std::pair<unsigned char, uint256> key;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();

        if (pcursor->GetKey(key) && key.first == type) {
            setHashes.insert(key.second);
            pcursor->Next();
        } else {
            break;
        }

    }

    return true;
}

void CPrecomputeDB::EraseAllPrecomputes()
{
    std::set<uint256> setHashes;
    LoadPrecomputes(setHashes);

    for (auto hash : setHashes)
        ErasePrecompute(hash);
}

bool CPrecomputeDB::WritePrecompute(const uint256& hash, const CoinWitnessCacheData& data)
{
    return Write(std::make_pair('P', hash), data);
}
bool CPrecomputeDB::ReadPrecompute(const uint256& hash, CoinWitnessCacheData& data)
{
    return Read(std::make_pair('P', hash), data);
}
bool CPrecomputeDB::ErasePrecompute(const uint256& hash)
{
    return Erase(std::make_pair('P', hash));
}

