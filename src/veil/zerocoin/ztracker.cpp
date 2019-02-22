// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/deterministicmint.h>
#include <logging.h>
#include "ztracker.h"
#include "util.h"
#include "sync.h"
#include "txdb.h"
#include "wallet/walletdb.h"
#include "validation.h"
#include "chainparams.h"
#include "wallet/wallet.h"
#include "accumulators.h"
#include "mintmeta.h"
#include <txmempool.h>

using namespace std;

CzTracker::CzTracker(CWallet* wallet)
{
    this->walletDatabase = wallet->database;
    WalletBatch walletdb(*walletDatabase);

    mapSerialHashes.clear();
    mapPendingSpends.clear();
    fInitialized = false;
}

CzTracker::~CzTracker()
{
    mapSerialHashes.clear();
    mapPendingSpends.clear();
}

void CzTracker::Init()
{
    //Load all CZerocoinMints and CDeterministicMints from the database
    if (!fInitialized) {
        ListMints(false, false, true);
        fInitialized = true;
    }
}

bool CzTracker::Archive(CMintMeta& meta)
{
    if (mapSerialHashes.count(meta.hashSerial))
        mapSerialHashes.at(meta.hashSerial).isArchived = true;

    WalletBatch walletdb(*walletDatabase);
    CZerocoinMint mint;
    if (walletdb.ReadZerocoinMint(meta.hashPubcoin, mint)) {
        if (!WalletBatch(*walletDatabase).ArchiveMintOrphan(mint))
            return error("%s: failed to archive zerocoinmint", __func__);
    } else {
        //failed to read mint from DB, try reading deterministic
        CDeterministicMint dMint;
        if (!walletdb.ReadDeterministicMint(meta.hashPubcoin, dMint))
            return error("%s: could not find pubcoinhash %s in db", __func__, meta.hashPubcoin.GetHex());
        if (!walletdb.ArchiveDeterministicOrphan(dMint))
            return error("%s: failed to archive deterministic ophaned mint", __func__);
    }

    LogPrintf("%s: archived pubcoinhash %s\n", __func__, meta.hashPubcoin.GetHex());
    return true;
}

bool CzTracker::UnArchive(const PubCoinHash& hashPubcoin, bool isDeterministic)
{
    WalletBatch walletdb(*walletDatabase);
    if (isDeterministic) {
        CDeterministicMint dMint;
        if (!walletdb.UnarchiveDeterministicMint(hashPubcoin, dMint))
            return error("%s: failed to unarchive deterministic mint", __func__);
        Add(dMint, false);
    } else {
        CZerocoinMint mint;
        if (!walletdb.UnarchiveZerocoinMint(hashPubcoin, mint))
            return error("%s: failed to unarchivezerocoin mint", __func__);
        Add(mint, false);
    }

    LogPrintf("%s: unarchived %s\n", __func__, hashPubcoin.GetHex());
    return true;
}

CMintMeta CzTracker::Get(const SerialHash &hashSerial)
{
    if (!mapSerialHashes.count(hashSerial))
        return CMintMeta();

    return mapSerialHashes.at(hashSerial);
}

CMintMeta CzTracker::GetMetaFromPubcoin(const PubCoinHash& hashPubcoin)
{
    if (mapHashPubCoin.count(hashPubcoin) && mapSerialHashes.count(mapHashPubCoin.at(hashPubcoin)))
        return mapSerialHashes.at(mapHashPubCoin.at(hashPubcoin));

    return {};
}

bool CzTracker::GetMetaFromStakeHash(const uint256& hashStake, CMintMeta& meta) const
{
    for (auto& it : mapSerialHashes) {
        if (it.second.hashStake == hashStake) {
            meta = it.second;
            return true;
        }
    }

    return false;
}

std::vector<SerialHash> CzTracker::GetSerialHashes()
{
    vector<SerialHash> vHashes;
    for (auto it : mapSerialHashes) {
        if (it.second.isArchived)
            continue;

        vHashes.emplace_back(it.first);
    }


    return vHashes;
}

CAmount CzTracker::GetBalance(bool fConfirmedOnly, bool fUnconfirmedOnly) const
{
    CAmount nTotal = 0;
    //! zerocoin specific fields
    std::map<libzerocoin::CoinDenomination, unsigned int> myZerocoinSupply;
    for (auto& denom : libzerocoin::zerocoinDenomList) {
        myZerocoinSupply.insert(make_pair(denom, 0));
    }

    {
        //LOCK(cs_pivtracker);
        // Get Unused coins
        for (auto& it : mapSerialHashes) {
            CMintMeta meta = it.second;
            if (meta.isUsed || meta.isArchived)
                continue;
            bool fConfirmed = ((meta.nHeight <= chainActive.Height()) && !(meta.nHeight == 0));
            if (fConfirmedOnly && !fConfirmed)
                continue;
            if (fUnconfirmedOnly && fConfirmed)
                continue;

            nTotal += libzerocoin::ZerocoinDenominationToAmount(meta.denom);
            myZerocoinSupply.at(meta.denom)++;
        }
    }

    if (nTotal < 0 ) nTotal = 0; // Sanity never hurts

    return nTotal;
}

CAmount CzTracker::GetUnconfirmedBalance() const
{
    return GetBalance(false, true);
}

std::vector<CMintMeta> CzTracker::GetMints(bool fConfirmedOnly) const
{
    vector<CMintMeta> vMints;
    for (auto& it : mapSerialHashes) {
        CMintMeta mint = it.second;
        if (mint.isArchived || mint.isUsed)
            continue;
        bool fConfirmed = (mint.nHeight <= chainActive.Height());
        if (fConfirmed)
            mint.nMemFlags |= MINT_CONFIRMED;
        if (fConfirmedOnly && !fConfirmed)
            continue;
        vMints.emplace_back(mint);
    }
    return vMints;
}

//Does a mint in the tracker have this txid
bool CzTracker::HasMintTx(const uint256& txid)
{
    for (auto it : mapSerialHashes) {
        if (it.second.txid == txid)
            return true;
    }

    return false;
}

bool CzTracker::HasPubcoin(const CBigNum &bnValue) const
{
    // Check if this mint's pubcoin value belongs to our mapSerialHashes (which includes hashpubcoin values)
    uint256 hash = GetPubCoinHash(bnValue);
    return HasPubcoinHash(hash);
}

bool CzTracker::HasPubcoinHash(const PubCoinHash& hashPubcoin) const
{
    if (mapHashPubCoin.count(hashPubcoin))
        return true;

    return false;
}

bool CzTracker::HasSerial(const CBigNum& bnSerial) const
{
    SerialHash hash = GetSerialHash(bnSerial);
    return HasSerialHash(hash);
}

bool CzTracker::HasSerialHash(const SerialHash& hashSerial) const
{
    auto it = mapSerialHashes.find(hashSerial);
    return it != mapSerialHashes.end();
}

bool CzTracker::UpdateZerocoinMint(const CZerocoinMint& mint)
{
    if (!HasSerial(mint.GetSerialNumber()))
        return error("%s: mint %s is not known", __func__, mint.GetValue().GetHex());

    SerialHash hashSerial = GetSerialHash(mint.GetSerialNumber());

    //Update the meta object
    CMintMeta meta = Get(hashSerial);
    meta.isUsed = mint.IsUsed();
    meta.denom = mint.GetDenomination();
    meta.nHeight = mint.GetHeight();
    mapSerialHashes.at(hashSerial) = meta;

    //Write to db
    return WalletBatch(*walletDatabase).WriteZerocoinMint(mint);
}

bool CzTracker::UpdateState(const CMintMeta& meta)
{
    WalletBatch walletdb(*walletDatabase);

    if (meta.isDeterministic) {
        CDeterministicMint dMint;
        if (!walletdb.ReadDeterministicMint(meta.hashPubcoin, dMint)) {
            // Check archive just in case
            if (!meta.isArchived)
                return error("%s: failed to read deterministic mint from database", __func__);

            // Unarchive this mint since it is being requested and updated
            if (!walletdb.UnarchiveDeterministicMint(meta.hashPubcoin, dMint))
                return error("%s: failed to unarchive deterministic mint from database", __func__);
        }

        dMint.SetTxHash(meta.txid);
        dMint.SetHeight(meta.nHeight);
        dMint.SetUsed(meta.isUsed);
        dMint.SetDenomination(meta.denom);
        dMint.SetStakeHash(meta.hashStake);

        if (!walletdb.WriteDeterministicMint(dMint))
            return error("%s: failed to update deterministic mint when writing to db", __func__);
    } else {
        CZerocoinMint mint;
        if (!walletdb.ReadZerocoinMint(meta.hashPubcoin, mint))
            return error("%s: failed to read mint from database", __func__);

        mint.SetTxHash(meta.txid);
        mint.SetHeight(meta.nHeight);
        mint.SetUsed(meta.isUsed);
        mint.SetDenomination(meta.denom);

        if (!walletdb.WriteZerocoinMint(mint))
            return error("%s: failed to write mint to database", __func__);
    }

    mapSerialHashes[meta.hashSerial] = meta;

    return true;
}

void CzTracker::Add(const CDeterministicMint& dMint, bool isNew, bool isArchived)
{
    CMintMeta meta;
    meta.hashPubcoin = dMint.GetPubcoinHash();
    meta.nHeight = dMint.GetHeight();
    meta.nVersion = dMint.GetVersion();
    meta.txid = dMint.GetTxHash();
    meta.isUsed = dMint.IsUsed();
    meta.hashSerial = dMint.GetSerialHash();
    meta.hashStake = dMint.GetStakeHash();
    meta.denom = dMint.GetDenomination();
    meta.isArchived = isArchived;
    meta.isDeterministic = true;
    mapSerialHashes[meta.hashSerial] = meta;
    mapHashPubCoin[meta.hashPubcoin] = meta.hashSerial;

    if (isNew)
        WalletBatch(*walletDatabase).WriteDeterministicMint(dMint);
}

void CzTracker::Add(const CZerocoinMint& mint, bool isNew, bool isArchived)
{
    CMintMeta meta;
    meta.hashPubcoin = GetPubCoinHash(mint.GetValue());
    meta.nHeight = mint.GetHeight();
    meta.nVersion = libzerocoin::ExtractVersionFromSerial(mint.GetSerialNumber());
    meta.txid = mint.GetTxHash();
    meta.isUsed = mint.IsUsed();
    meta.hashSerial = GetSerialHash(mint.GetSerialNumber());
    uint256 nSerial = mint.GetSerialNumber().getuint256();
    meta.hashStake = Hash(nSerial.begin(), nSerial.end());
    meta.denom = mint.GetDenomination();
    meta.isArchived = isArchived;
    meta.isDeterministic = false;
    mapSerialHashes[meta.hashSerial] = meta;
    mapHashPubCoin[meta.hashPubcoin] = meta.hashSerial;

    if (isNew)
        WalletBatch(*walletDatabase).WriteZerocoinMint(mint);
}

void CzTracker::SetPubcoinUsed(const PubCoinHash& hashPubcoin, const uint256& txid)
{
    if (!HasPubcoinHash(hashPubcoin))
        return;
    CMintMeta meta = GetMetaFromPubcoin(hashPubcoin);
    meta.isUsed = true;
    mapPendingSpends.insert(make_pair(meta.hashSerial, txid));
    UpdateState(meta);
}

void CzTracker::SetPubcoinNotUsed(const PubCoinHash& hashPubcoin)
{
    if (!HasPubcoinHash(hashPubcoin))
        return;
    CMintMeta meta = GetMetaFromPubcoin(hashPubcoin);
    meta.isUsed = false;

    if (mapPendingSpends.count(meta.hashSerial))
        mapPendingSpends.erase(meta.hashSerial);

    UpdateState(meta);
}

void CzTracker::RemovePending(const uint256& txid)
{
    arith_uint256 hashSerial;
    for (auto it : mapPendingSpends) {
        if (it.second == txid) {
            hashSerial = UintToArith256(it.first);
            break;
        }
    }


    if (hashSerial > arith_uint256())
        mapPendingSpends.erase(ArithToUint256(hashSerial));
}

bool CzTracker::UpdateStatusInternal(const std::set<uint256>& setMempool, const std::map<uint256, uint256>& mapMempoolSerials, CMintMeta& mint)
{
    //! Check whether this mint has been spent and is considered 'pending' or 'confirmed'
    // If there is not a record of the block height, then look it up and assign it
    uint256 txidMint;
    bool isMintInChain = pzerocoinDB->ReadCoinMint(mint.hashPubcoin, txidMint);

    //See if there is internal record of spending this mint (note this is memory only, would reset on restart)
    bool isPendingSpendInternal = static_cast<bool>(mapPendingSpends.count(mint.hashSerial));
    bool isPendingSpendMempool = static_cast<bool>(mapMempoolSerials.count(mint.hashSerial));
    bool isPendingSpend = isPendingSpendInternal || isPendingSpendMempool;

    // See if there is a blockchain record of spending this mint
    uint256 txidSpend;
    bool isConfirmedSpend = pzerocoinDB->ReadCoinSpend(mint.hashSerial, txidSpend);

    // Double check the mempool for pending spend
    if (isPendingSpend) {
        if (isPendingSpendInternal) {
            uint256 txidPendingSpend = mapPendingSpends.at(mint.hashSerial);

            // Remove internal pendingspend status if it is confirmed or is not found at all in the mempool
            if ((!setMempool.count(txidPendingSpend) && !isPendingSpendMempool) || isConfirmedSpend) {
                RemovePending(txidPendingSpend);
                isPendingSpend = false;
                LogPrintf("%s : Pending txid %s removed because not in mempool\n", __func__, txidPendingSpend.GetHex());
            } else if (isPendingSpendMempool) {
                // Mempool has this serial in it, but our internal status does not display it as pending spend
                // This could happen as easily as restarting the application
                mapPendingSpends.emplace(mint.hashSerial, mapMempoolSerials.at(mint.hashSerial));
            }
        }
    }

    if (isPendingSpend)
        mint.nMemFlags |= MINT_PENDINGSPEND;

    bool isUsed = isPendingSpend || isConfirmedSpend;

    if (!mint.nHeight || !isMintInChain || isUsed != mint.isUsed) {
        CTransactionRef txRef;
        uint256 hashBlock;

        // Txid will be marked 0 if there is no knowledge of the final tx hash yet
        if (mint.txid == uint256()) {
            if (!isMintInChain) {
                LogPrintf("%s : Failed to find mint in zerocoinDB %s\n", __func__, mint.hashPubcoin.GetHex().substr(0, 6));
                mint.isArchived = true;
                Archive(mint);
                return true;
            }
            mint.txid = txidMint;
        }

        if (isPendingSpend)
            return true;

        // Check the transaction associated with this mint
        if (HeadersAndBlocksSynced() && !GetTransaction(mint.txid, txRef, Params().GetConsensus(), hashBlock, true)) {
            LogPrintf("%s : Failed to find tx for mint txid=%s\n", __func__, mint.txid.GetHex());
            mint.isArchived = true;
            Archive(mint);
            return true;
        }

        // An orphan tx if hashblock is in mapBlockIndex but not in chain active
        if (mapBlockIndex.count(hashBlock) && !chainActive.Contains(mapBlockIndex.at(hashBlock))) {
            LogPrintf("%s : Found orphaned mint txid=%s\n", __func__, mint.txid.GetHex());
            mint.isUsed = false;
            mint.nHeight = 0;
            if (txRef->IsCoinStake()) {
                mint.isArchived = true;
                Archive(mint);
            }

            return true;
        }

        // Check that the mint has correct used status
        if (mint.isUsed != isUsed) {
            LogPrintf("%s : Set mint %s isUsed to %d\n", __func__, mint.hashPubcoin.GetHex(), isUsed);
            mint.isUsed = isUsed;
            return true;
        }
    }

    return false;
}

uint8_t CzTracker::GetMintMemFlags(const CMintMeta& mint, int nBestHeight, const std::map<libzerocoin::CoinDenomination, int>& mapMaturity)
{
    uint8_t nMemFlags = 0;
    if (mint.nHeight && mint.nHeight <= nBestHeight)
        nMemFlags |= MINT_CONFIRMED;

    // Not mature
    if (mapMaturity.count(mint.denom) && mint.nHeight < mapMaturity.at(mint.denom) && mint.nHeight < nBestHeight - Params().Zerocoin_MintRequiredConfirmations())
        nMemFlags |= MINT_MATURE;

    return nMemFlags;
}

std::set<CMintMeta> CzTracker::ListMints(bool fUnusedOnly, bool fMatureOnly, bool fUpdateStatus)
{
    WalletBatch walletdb(*walletDatabase);
    if (fUpdateStatus) {
        std::list<CZerocoinMint> listMintsDB = walletdb.ListMintedCoins();
        for (auto& mint : listMintsDB) {
            Add(mint);
        }
        LogPrintf("%s: added %d zerocoinmints from DB\n", __func__, listMintsDB.size());

        std::list<CDeterministicMint> listDeterministicDB = walletdb.ListDeterministicMints();
        for (auto& dMint : listDeterministicDB) {
            Add(dMint);
        }
        LogPrintf("%s: added %d deterministic zerocoins from DB\n", __func__, listDeterministicDB.size());
    }

    std::vector<CMintMeta> vOverWrite;
    std::set<CMintMeta> setMints;
    std::set<uint256> setMempoolTx;
    std::map<uint256, uint256> mapMempoolSerials; //serialhash, txid
    {
        LOCK(mempool.cs);
        mempool.GetTransactions(setMempoolTx);
        mempool.GetSerials(mapMempoolSerials);
    }

    int nBestHeight = chainActive.Height();

    std::map<libzerocoin::CoinDenomination, int> mapMaturity = GetMintMaturityHeight();
    for (auto& it : mapSerialHashes) {
        CMintMeta mint = it.second;

        //This is only intended for unarchived coins
        if (mint.isArchived)
            continue;

        // Update the metadata of the mints if requested
        if (fUpdateStatus && UpdateStatusInternal(setMempoolTx, mapMempoolSerials, mint)) {
            if (mint.isArchived)
                continue;

            // Mint was updated, queue for overwrite
            vOverWrite.emplace_back(mint);
        }

        if (fUnusedOnly && mint.isUsed)
            continue;

        // Not confirmed
        mint.nMemFlags = GetMintMemFlags(mint, nBestHeight, mapMaturity);
        if (fMatureOnly) {
            if (!(mint.nMemFlags & MINT_CONFIRMED) || !(mint.nMemFlags & MINT_MATURE))
                continue;
        }

        setMints.insert(mint);
    }

    //overwrite any updates
    for (CMintMeta& meta : vOverWrite)
        UpdateState(meta);

    return setMints;
}

bool CzTracker::HasSpendCache(const uint256& hashSerial)
{
    AssertLockHeld(cs_readlock);
    return mapSpendCache.count(hashSerial) > 0;
}

CoinWitnessData* CzTracker::CreateSpendCache(const uint256& hashSerial)
{
    AssertLockHeld(cs_modify_lock);
    if (!mapSpendCache.count(hashSerial)) {
        //Create if not exists
        std::unique_ptr<CoinWitnessData> uptr(new CoinWitnessData());
        mapSpendCache.insert(std::make_pair(hashSerial, std::move(uptr)));
    }
    return mapSpendCache.at(hashSerial).get();
}

CoinWitnessData* CzTracker::GetSpendCache(const uint256& hashSerial)
{
    AssertLockHeld(cs_readlock);
    if (!mapSpendCache.count(hashSerial)) {
        //Create if not exists
        std::unique_ptr<CoinWitnessData> uptr(new CoinWitnessData());
        mapSpendCache.insert(std::make_pair(hashSerial, std::move(uptr)));
    }

    return mapSpendCache.at(hashSerial).get();
}

bool CzTracker::GetCoinWitness(const uint256& hashSerial, CoinWitnessData& data)
{
    AssertLockHeld(cs_readlock);
    data.SetNull();

    if (!mapSpendCache.count(hashSerial))
        return false;

    auto pwitness = mapSpendCache.at(hashSerial).get();

    auto denom = pwitness->denom;
    data.denom = denom;
    data.coin = std::unique_ptr<libzerocoin::PublicCoin>(new libzerocoin::PublicCoin(Params().Zerocoin_Params(), pwitness->coin->getValue(), denom));
    data.pAccumulator = std::unique_ptr<libzerocoin::Accumulator>(new libzerocoin::Accumulator(Params().Zerocoin_Params(), denom, pwitness->pAccumulator->getValue()));
    data.pWitness = std::unique_ptr<libzerocoin::AccumulatorWitness>(new libzerocoin::AccumulatorWitness(Params().Zerocoin_Params(), *data.pAccumulator.get(), *data.coin));
    data.nHeightCheckpoint = pwitness->nHeightCheckpoint;
    data.nHeightMintAdded = pwitness->nHeightMintAdded;
    data.nHeightAccStart = pwitness->nHeightAccStart;
    data.nHeightPrecomputed = pwitness->nHeightPrecomputed;
    data.txid = pwitness->txid;

    return true;
}

bool CzTracker::ClearSpendCache()
{
    AssertLockHeld(cs_modify_lock);
    if (!mapSpendCache.empty()) {
        mapSpendCache.clear();
        return true;
    }

    return false;
}

void CzTracker::Clear()
{
    mapSerialHashes.clear();
    mapHashPubCoin.clear();
}
