// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <version.h>
#include <tinyformat.h>
#include "zchain.h"
#include "libzerocoin/Params.h"
#include "txdb.h"
#include "chainparams.h"
#include "validation.h"
#include "consensus/validation.h"
#include "primitives/zerocoin.h"
#include "ui_interface.h"
#include "mintmeta.h"

#include <boost/thread.hpp>

// 6 comes from OPCODE (1) + vch.size() (1) + BIGNUM size (4)
#define SCRIPT_OFFSET 6
// For Script size (BIGNUM/Uint256 size)
#define BIGNUM_SIZE   4

bool BlockToMintValueVector(const CBlock& block, const libzerocoin::CoinDenomination denom, vector<CBigNum>& vValues)
{
    CBigNum bnMod;
    bnMod.SetDec(Params().Zerocoin_Modulus());
    auto zerocoinParams = Params().Zerocoin_Params();

    for (const CTransactionRef tx : block.vtx) {
        if(!tx->IsZerocoinMint())
            continue;

        for (const auto& pout : tx->vpout) {
            if(!pout->IsZerocoinMint())
                continue;

            CValidationState state;
            libzerocoin::PublicCoin coin(zerocoinParams);
            if(!OutputToPublicCoin(pout.get(), coin))
                return false;

            if (coin.getDenomination() != denom)
                continue;

            vValues.push_back(coin.getValue());
        }
    }

    return true;
}

bool BlockToPubcoinList(const CBlock& block, std::list<libzerocoin::PublicCoin>& listPubcoins)
{
    auto zerocoinParams = Params().Zerocoin_Params();

    for (const CTransactionRef tx : block.vtx) {
        if(!tx->IsZerocoinMint())
            continue;

        for (unsigned int i = 0; i < tx->vpout.size(); i++) {
            const auto pout = tx->vpout[i];
            if(!pout->IsZerocoinMint())
                continue;

            CValidationState state;
            libzerocoin::PublicCoin pubCoin(zerocoinParams);
            if(!OutputToPublicCoin(pout.get(), pubCoin))
                return false;

            listPubcoins.emplace_back(pubCoin);
        }
    }

    return true;
}

bool ThreadedBatchVerify(const std::vector<libzerocoin::SerialNumberSoKProof>* pvProofs, int nThreads)
{
    int64_t nMaxThreads = gArgs.GetArg("-threadbatchverify", DEFAULT_BATCHVERIFY_THREADS);
    if (nThreads != -1)
        nMaxThreads = nThreads;

    // Assume that it doesn't give any gain to multithread, unless each thread has at least 6 proofs
    int nThreadEfficiency = 7;

    std::vector<std::vector<const libzerocoin::SerialNumberSoKProof*>> vProofGroups(1);
    int nThreadsUsed = 1;
    if ((int)pvProofs->size() > nThreadEfficiency)
        nThreadsUsed = pvProofs->size() / nThreadEfficiency;
    if (nThreadsUsed > nMaxThreads)
        nThreadsUsed = nMaxThreads;

    vProofGroups.resize(nThreadsUsed);
    std::vector<uint8_t> vReturn(nThreadsUsed);
    int nThreadSelected = 0;

    for (unsigned int i = 0; i < pvProofs->size(); i++) {
        vProofGroups[nThreadSelected].emplace_back(&pvProofs->at(i));
        nThreadSelected++;
        if (nThreadSelected >= nThreadsUsed)
            nThreadSelected = 0;
    }

    boost::thread_group* pthreadgroup = new boost::thread_group();
    for (unsigned int i = 0; i < vProofGroups.size(); i++) {
        pthreadgroup->create_thread(boost::bind(&libzerocoin::SerialNumberSoKProof::BatchVerify, vProofGroups[i], &vReturn[i]));
    }

    pthreadgroup->join_all();

    for (auto ret : vReturn) {
        if (ret != 1)
            return false;
    }

    return true;
}

bool TxToPubcoinHashSet(const CTransaction* tx, std::set<uint256>& setHashes)
{
    for (unsigned int i = 0; i < tx->vpout.size(); i++) {
        const auto pout = tx->vpout[i];
        if(!pout->IsZerocoinMint())
            continue;

        CValidationState state;
        libzerocoin::PublicCoin pubCoin(Params().Zerocoin_Params());
        if(!OutputToPublicCoin(pout.get(), pubCoin))
            return false;

        uint256 hash = GetPubCoinHash(pubCoin.getValue());
        setHashes.emplace(hash);
    }
    return true;
}

bool TxToSerialHashSet(const CTransaction* tx, std::set<uint256>& setHashes)
{
    for (const CTxIn& in :tx->vin) {
        auto spend = TxInToZerocoinSpend(in);
        if (spend)
            setHashes.emplace(GetSerialHash(spend->getCoinSerialNumber()));
        else
            return false;
    }
    return true;
}

//return a list of zerocoin mints contained in a specific block
bool BlockToZerocoinMintList(const CBlock& block, std::list<CZerocoinMint>& vMints)
{
    CBigNum bnMod;
    bnMod.SetDec(Params().Zerocoin_Modulus());
    libzerocoin::ZerocoinParams zerocoinParams(bnMod);

    for (const CTransactionRef tx : block.vtx) {
        if(!tx->IsZerocoinMint())
            continue;

        for (unsigned int i = 0; i < tx->vpout.size(); i++) {
            const auto pout = tx->vpout[i];
            if(!pout->IsZerocoinMint())
                continue;

            CValidationState state;
            libzerocoin::PublicCoin pubCoin(&zerocoinParams);
            if(!OutputToPublicCoin(pout.get(), pubCoin))
                return false;

            //version should not actually matter here since it is just a reference to the pubcoin, not to the privcoin
            uint8_t version = 1;
            CZerocoinMint mint = CZerocoinMint(pubCoin.getDenomination(), pubCoin.getValue(), 0, 0, false, version, nullptr);
            mint.SetTxHash(tx->GetHash());
            vMints.push_back(mint);
        }
    }

    return true;
}

void FindMints(std::vector<CMintMeta> vMintsToFind, std::vector<CMintMeta>& vMintsToUpdate, std::vector<CMintMeta>& vMissingMints)
{
    auto zerocoinParams = Params().Zerocoin_Params();

    // see which mints are in our public zerocoin database. The mint should be here if it exists, unless
    // something went wrong
    for (CMintMeta meta : vMintsToFind) {
        uint256 txHash;
        if (!pzerocoinDB->ReadCoinMint(meta.hashPubcoin, txHash)) {
            vMissingMints.push_back(meta);
            continue;
        }

        // make sure the txhash and block height meta data are correct for this mint
        CTransactionRef tx;
        uint256 hashBlock;
        if (!GetTransaction(txHash, tx, Params().GetConsensus(), hashBlock, true)) {
            vMissingMints.push_back(meta);
            continue;
        }

        if (!mapBlockIndex.count(hashBlock)) {
            vMissingMints.push_back(meta);
            continue;
        }

        //see if this mint is spent
        uint256 hashTxSpend;
        bool fSpent = pzerocoinDB->ReadCoinSpend(meta.hashSerial, hashTxSpend);

        //if marked as spent, check that it actually made it into the chain
        CTransactionRef txSpend;
        uint256 hashBlockSpend;
        if (fSpent && !GetTransaction(hashTxSpend, txSpend, Params().GetConsensus(), hashBlockSpend, true)) {
            meta.isUsed = false;
            vMintsToUpdate.push_back(meta);
            continue;
        }

        //The mint has been incorrectly labelled as spent in zerocoinDB and needs to be undone
        int nHeightTx = 0;
        uint256 hashSerial = meta.hashSerial;
        uint256 txidSpend;
        if (fSpent && !IsSerialInBlockchain(hashSerial, nHeightTx, txidSpend)) {
            meta.isUsed = false;
            vMintsToUpdate.push_back(meta);
            continue;
        }

        // is the denomination correct?
        for (auto& out : tx->vpout) {
            if (!out->IsZerocoinMint())
                continue;
            libzerocoin::PublicCoin pubcoin(zerocoinParams);
            CValidationState state;
            OutputToPublicCoin(out.get(), pubcoin);
            if (GetPubCoinHash(pubcoin.getValue()) == meta.hashPubcoin && pubcoin.getDenomination() != meta.denom) {
                meta.denom = pubcoin.getDenomination();
                vMintsToUpdate.emplace_back(meta);
            }
        }

        // if meta data is correct, then no need to update
        if (meta.txid == txHash && meta.nHeight == mapBlockIndex[hashBlock]->nHeight && meta.isUsed == fSpent)
            continue;

        //mark this mint for update
        meta.txid = txHash;
        meta.nHeight = mapBlockIndex[hashBlock]->nHeight;
        meta.isUsed = fSpent;
        LogPrintf("%s: found updates for pubcoinhash = %s\n", __func__, meta.hashPubcoin.GetHex());

        vMintsToUpdate.push_back(meta);
    }
}

int GetZerocoinStartHeight()
{
    return 0;
}

bool GetZerocoinMint(const CBigNum& bnPubcoin, uint256& txHash)
{
    txHash = uint256();
    return pzerocoinDB->ReadCoinMint(bnPubcoin, txHash);
}

bool IsPubcoinInBlockchain(const uint256& hashPubcoin, int& nHeightTx, uint256& txid, CBlockIndex* pindexChain)
{
    txid = uint256();
    if (!pzerocoinDB->ReadCoinMint(hashPubcoin, txid))
        return false;
    CTransactionRef txRef;
    return IsTransactionInChain(txid, nHeightTx, txRef, Params().GetConsensus(), pindexChain);
}

bool IsPubcoinSpendInBlockchain(const uint256& hashPubcoin, int& nHeightTx, uint256& txid, CBlockIndex* pindexChain)
{
    uint256 hashBlock;
    if (!pzerocoinDB->ReadPubcoinSpend(hashPubcoin, txid, hashBlock))
        return false;

    return IsBlockHashInChain(hashBlock, nHeightTx, pindexChain);
}

bool IsSerialKnown(const CBigNum& bnSerial)
{
    uint256 txHash;
    return pzerocoinDB->ReadCoinSpend(bnSerial, txHash);
}

bool IsSerialInBlockchain(const CBigNum& bnSerial, int& nHeightTx, const CBlockIndex* pindex)
{
    return IsSerialInBlockchain(GetSerialHash(bnSerial), nHeightTx, pindex);
}

bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, const CBlockIndex* pindex)
{
    uint256 txHash;
    // if not in zerocoinDB then its not in the blockchain
    if (!pzerocoinDB->ReadCoinSpend(hashSerial, txHash))
        return false;

    CTransactionRef txRef;
    return IsTransactionInChain(txHash, nHeightTx, txRef, Params().GetConsensus(), pindex);
}

bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend)
{
    CTransactionRef tx;
    return IsSerialInBlockchain(hashSerial, nHeightTx, txidSpend, tx);
}

bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend, CTransactionRef& txRef)
{
    txidSpend = uint256();
    // if not in zerocoinDB then its not in the blockchain
    if (!pzerocoinDB->ReadCoinSpend(hashSerial, txidSpend))
        return false;

    return IsTransactionInChain(txidSpend, nHeightTx, txRef, Params().GetConsensus());
}

std::string ReindexZerocoinDB()
{
    auto zerocoinParams = Params().Zerocoin_Params();

    if (!pzerocoinDB->WipeCoins("spends") || !pzerocoinDB->WipeCoins("mints")) {
        return _("Failed to wipe zerocoinDB");
    }

    uiInterface.ShowProgress(_("Reindexing zerocoin database..."), 0, false);

    CBlockIndex* pindex = chainActive[0];
    std::map<libzerocoin::CoinSpend, uint256> mapSpends;
    std::map<libzerocoin::PublicCoin, uint256> mapMints;
    while (pindex) {
        uiInterface.ShowProgress(_("Reindexing zerocoin database..."), std::max(1, std::min(99,
                (int)((double) (pindex->nHeight) / (double)(chainActive.Height()) * 100))), false);

        if (pindex->nHeight % 1000 == 0)
            LogPrintf("Reindexing zerocoin : block %d...\n", pindex->nHeight);

        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
            return _("Reindexing zerocoin failed");
        }

        for (const CTransactionRef tx : block.vtx) {
            for (unsigned int i = 0; i < tx->vin.size(); i++) {
                if (tx->IsCoinBase())
                    break;

                if (tx->ContainsZerocoins()) {
                    uint256 txid = tx->GetHash();
                    //Record Serials
                    if (tx->IsZerocoinSpend()) {
                        for (auto& in : tx->vin) {
                            if (!in.IsZerocoinSpend())
                                continue;

                            auto spend = TxInToZerocoinSpend(in);
                            if (spend)
                                mapSpends.emplace(*spend, txid);
                        }
                    }

                    //Record mints
                    if (tx->IsZerocoinMint()) {
                        for (auto& out : tx->vpout) {
                            if (!out->IsZerocoinMint())
                                continue;

                            CValidationState state;
                            libzerocoin::PublicCoin coin(zerocoinParams);
                            OutputToPublicCoin(out.get(), coin);
                            mapMints.emplace(coin, txid);
                        }
                    }
                }
            }
        }

        // Flush the zerocoinDB to disk every 100 blocks
        if (pindex->nHeight % 100 == 0) {
            if ((!mapSpends.empty() && !pzerocoinDB->WriteCoinSpendBatch(mapSpends)) || (!mapMints.empty()
                && !pzerocoinDB->WriteCoinMintBatch(mapMints)))
                return _("Error writing zerocoinDB to disk");
            mapSpends.clear();
            mapMints.clear();
        }

        pindex = chainActive.Next(pindex);
    }
    uiInterface.ShowProgress("", 100, false);

    // Final flush to disk in case any remaining information exists
    if ((!mapSpends.empty() && !pzerocoinDB->WriteCoinSpendBatch(mapSpends)) || (!mapMints.empty() &&
        !pzerocoinDB->WriteCoinMintBatch(mapMints)))
        return _("Error writing zerocoinDB to disk");

    uiInterface.ShowProgress("", 100, false);

    return "";
}

bool RemoveSerialFromDB(const CBigNum& bnSerial)
{
    return pzerocoinDB->EraseCoinSpend(bnSerial);
}

std::shared_ptr<libzerocoin::CoinSpend> TxInToZerocoinSpend(const CTxIn& txin)
{
    // extract the CoinSpend from the txin
    try {
        std::vector<char, zero_after_free_allocator<char> > dataTxIn;
        dataTxIn.insert(dataTxIn.end(), txin.scriptSig.begin() + BIGNUM_SIZE, txin.scriptSig.end());
        CDataStream serializedCoinSpend(dataTxIn, SER_NETWORK, PROTOCOL_VERSION);
        libzerocoin::CoinSpend spend(Params().Zerocoin_Params(), serializedCoinSpend);
        return std::make_shared<libzerocoin::CoinSpend>(spend);
    } catch (const std::exception& e) {
        error("%s: Failed to convert CTxIn to ZerocoinSpend. %s", __func__, e.what());
    }

    return nullptr;
}

bool OutputToPublicCoin(const CTxOutBase* out, libzerocoin::PublicCoin& coin)
{
    if (!out->IsZerocoinMint())
        return false;

    CBigNum publicZerocoin;
    vector<unsigned char> vchZeroMint;
    vchZeroMint.insert(vchZeroMint.end(), out->GetPScriptPubKey()->begin() + SCRIPT_OFFSET,
                       out->GetPScriptPubKey()->begin() + out->GetPScriptPubKey()->size());
    publicZerocoin.setvch(vchZeroMint);

    libzerocoin::CoinDenomination denomination = libzerocoin::AmountToZerocoinDenomination(out->GetValue());
    if (denomination == libzerocoin::ZQ_ERROR)
        return error("TxOutToPublicCoin : txout.nValue is not correct");
    try {
        libzerocoin::PublicCoin checkPubCoin(Params().Zerocoin_Params(), publicZerocoin, denomination);
        coin = checkPubCoin;
    } catch (std::runtime_error& e) {
        return error("%s: failed to create pubcoin: %s", __func__, e.what());
    }

    return true;
}

bool TxOutToPublicCoin(const CTxOut& txout, libzerocoin::PublicCoin& pubCoin)
{
    CBigNum publicZerocoin;
    vector<unsigned char> vchZeroMint;
    vchZeroMint.insert(vchZeroMint.end(), txout.scriptPubKey.begin() + SCRIPT_OFFSET,
                       txout.scriptPubKey.begin() + txout.scriptPubKey.size());
    publicZerocoin.setvch(vchZeroMint);

    libzerocoin::CoinDenomination denomination = libzerocoin::AmountToZerocoinDenomination(txout.nValue);
    if (denomination == libzerocoin::ZQ_ERROR)
        return error("TxOutToPublicCoin : txout.nValue is not correct");

    try {
        libzerocoin::PublicCoin checkPubCoin(Params().Zerocoin_Params(), publicZerocoin, denomination);
        pubCoin = checkPubCoin;
    } catch (std::runtime_error& e) {
        return error("%s: failed to create pubcoin", __func__, e.what());
    }

    return true;
}

//return a list of zerocoin spends contained in a specific block, list may have many denominations
std::list<libzerocoin::CoinDenomination> ZerocoinSpendListFromBlock(const CBlock& block)
{
    std::list<libzerocoin::CoinDenomination> vSpends;
    for (const CTransactionRef tx : block.vtx) {
        if (!tx->IsZerocoinSpend())
            continue;

        for (const CTxIn& txin : tx->vin) {
            if (!txin.IsZerocoinSpend())
                continue;

            libzerocoin::CoinDenomination c = libzerocoin::IntToZerocoinDenomination(txin.nSequence&CTxIn::SEQUENCE_LOCKTIME_MASK);
            vSpends.push_back(c);
        }
    }
    return vSpends;
}
