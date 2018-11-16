#include <version.h>
#include "zchain.h"
#include "libzerocoin/Params.h"
#include "txdb.h"
#include "chainparams.h"
#include "validation.h"
#include "consensus/validation.h"
#include "primitives/zerocoin.h"
#include "ui_interface.h"

// 6 comes from OPCODE (1) + vch.size() (1) + BIGNUM size (4)
#define SCRIPT_OFFSET 6
// For Script size (BIGNUM/Uint256 size)
#define BIGNUM_SIZE   4

// todo: might have to add a check for this
bool ValidOutPoint(const COutPoint out, int nHeight)
{
    return true;
}

bool BlockToMintValueVector(const CBlock& block, const libzerocoin::CoinDenomination denom, vector<CBigNum>& vValues)
{
    CBigNum bnMod;
    bnMod.SetDec(Params().Zerocoin_Modulus());
    auto zerocoinParams = Params().Zerocoin_Params();

    for (const CTransactionRef tx : block.vtx) {
        if(!tx->IsZerocoinMint())
            continue;

        for (const CTxOut& txOut : tx->vout) {
            if(!txOut.scriptPubKey.IsZerocoinMint())
                continue;

            CValidationState state;
            libzerocoin::PublicCoin coin(zerocoinParams);
            if(!TxOutToPublicCoin(txOut, coin))
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

        for (unsigned int i = 0; i < tx->vout.size(); i++) {
            const CTxOut txOut = tx->vout[i];
            if(!txOut.scriptPubKey.IsZerocoinMint())
                continue;

            CValidationState state;
            libzerocoin::PublicCoin pubCoin(zerocoinParams);
            if(!TxOutToPublicCoin(txOut, pubCoin))
                return false;

            listPubcoins.emplace_back(pubCoin);
        }
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

        for (unsigned int i = 0; i < tx->vout.size(); i++) {
            const CTxOut txOut = tx->vout[i];
            if(!txOut.scriptPubKey.IsZerocoinMint())
                continue;

            CValidationState state;
            libzerocoin::PublicCoin pubCoin(&zerocoinParams);
            if(!TxOutToPublicCoin(txOut, pubCoin))
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
            LogPrintf("%s : cannot find tx %s\n", __func__, txHash.GetHex());
            vMissingMints.push_back(meta);
            continue;
        }

        if (!mapBlockIndex.count(hashBlock)) {
            LogPrintf("%s : cannot find block %s\n", __func__, hashBlock.GetHex());
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
            LogPrintf("%s : cannot find spend tx %s\n", __func__, hashTxSpend.GetHex());
            meta.isUsed = false;
            vMintsToUpdate.push_back(meta);
            continue;
        }

        //The mint has been incorrectly labelled as spent in zerocoinDB and needs to be undone
        int nHeightTx = 0;
        uint256 hashSerial = meta.hashSerial;
        uint256 txidSpend;
        if (fSpent && !IsSerialInBlockchain(hashSerial, nHeightTx, txidSpend)) {
            LogPrintf("%s : cannot find block %s. Erasing coinspend from zerocoinDB.\n", __func__, hashBlockSpend.GetHex());
            meta.isUsed = false;
            vMintsToUpdate.push_back(meta);
            continue;
        }

        // is the denomination correct?
        for (auto& out : tx->vout) {
            if (!out.IsZerocoinMint())
                continue;
            libzerocoin::PublicCoin pubcoin(zerocoinParams);
            CValidationState state;
            TxOutToPublicCoin(out, pubcoin);
            if (GetPubCoinHash(pubcoin.getValue()) == meta.hashPubcoin && pubcoin.getDenomination() != meta.denom) {
                LogPrintf("%s: found mismatched denom pubcoinhash = %s\n", __func__, meta.hashPubcoin.GetHex());
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

bool IsPubcoinInBlockchain(const uint256& hashPubcoin, uint256& txid)
{
    txid = uint256();
    return pzerocoinDB->ReadCoinMint(hashPubcoin, txid);
}

bool IsSerialKnown(const CBigNum& bnSerial)
{
    uint256 txHash;
    return pzerocoinDB->ReadCoinSpend(bnSerial, txHash);
}

bool IsSerialInBlockchain(const CBigNum& bnSerial, int& nHeightTx)
{
    uint256 txHash;
    // if not in zerocoinDB then its not in the blockchain
    if (!pzerocoinDB->ReadCoinSpend(bnSerial, txHash))
        return false;

    CTransactionRef txRef;
    return IsTransactionInChain(txHash, nHeightTx, txRef, Params().GetConsensus());
}

bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend)
{
    CTransactionRef tx;
    return IsSerialInBlockchain(hashSerial, nHeightTx, txidSpend, tx);
}

bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend, CTransactionRef txRef)
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
                            if (!in.scriptSig.IsZerocoinSpend())
                                continue;

                            libzerocoin::CoinSpend spend = TxInToZerocoinSpend(in);
                            mapSpends.emplace(spend, txid);
                        }
                    }

                    //Record mints
                    if (tx->IsZerocoinMint()) {
                        for (auto& out : tx->vout) {
                            if (!out.IsZerocoinMint())
                                continue;

                            CValidationState state;
                            libzerocoin::PublicCoin coin(zerocoinParams);
                            TxOutToPublicCoin(out, coin);
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

libzerocoin::CoinSpend TxInToZerocoinSpend(const CTxIn& txin)
{
    auto zerocoinParams = Params().Zerocoin_Params();

    // extract the CoinSpend from the txin
    std::vector<char, zero_after_free_allocator<char> > dataTxIn;
    dataTxIn.insert(dataTxIn.end(), txin.scriptSig.begin() + BIGNUM_SIZE, txin.scriptSig.end());
    CDataStream serializedCoinSpend(dataTxIn, SER_NETWORK, PROTOCOL_VERSION);

    libzerocoin::ZerocoinParams* paramsAccumulator = zerocoinParams;
    libzerocoin::CoinSpend spend(zerocoinParams, paramsAccumulator, serializedCoinSpend);

    return spend;
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

    libzerocoin::PublicCoin checkPubCoin(Params().Zerocoin_Params(), publicZerocoin, denomination);
    coin = checkPubCoin;

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

    libzerocoin::PublicCoin checkPubCoin(Params().Zerocoin_Params(), publicZerocoin, denomination);
    pubCoin = checkPubCoin;

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
            if (!txin.scriptSig.IsZerocoinSpend())
                continue;

            libzerocoin::CoinDenomination c = libzerocoin::IntToZerocoinDenomination(txin.nSequence&CTxIn::SEQUENCE_LOCKTIME_MASK);
            vSpends.push_back(c);
        }
    }
    return vSpends;
}