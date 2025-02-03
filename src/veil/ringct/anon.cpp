// Copyright (c) 2017-2019 The Particl developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <veil/ringct/anon.h>

#include <assert.h>
#include <secp256k1.h>
#include <secp256k1_mlsag.h>

#include <veil/ringct/blind.h>
#include <veil/ringct/rctindex.h>
#include <txdb.h>
#include <util/system.h>
#include <validation.h>
#include <validationinterface.h>
#include <consensus/validation.h>
#include <chainparams.h>
#include <txmempool.h>


bool VerifyMLSAG(const CTransaction &tx, CValidationState &state)
{
    int rv;
    std::set<int64_t> setHaveI; // Anon prev-outputs can only be used once per transaction.
    std::set<CCmpPubKey> setHaveKI;
    bool fSplitCommitments = tx.vin.size() > 1;

    size_t nStandard = 0, nCt = 0, nRingCT = 0;
    CAmount nPlainValueOut = tx.GetPlainValueOut(nStandard, nCt, nRingCT);
    CAmount nTxFee = 0;
    if (!tx.GetCTFee(nTxFee) && !tx.IsCoinStake())
        return state.DoS(100, error("%s: bad-fee-output", __func__), REJECT_INVALID, "bad-fee-output");

    nPlainValueOut += nTxFee;

    // Get commitment for unblinded amount
    uint8_t zeroBlind[32];
    memset(zeroBlind, 0, 32);
    secp256k1_pedersen_commitment plainCommitment;
    if (nPlainValueOut > 0) {
        if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, &plainCommitment, zeroBlind, (uint64_t) nPlainValueOut,
                secp256k1_generator_h))
            return state.DoS(100, false, REJECT_INVALID, "bad-plain-commitment");
    }

    std::vector<const uint8_t*> vpInputSplitCommits;
    if (fSplitCommitments)
        vpInputSplitCommits.reserve(tx.vin.size());

    uint256 hashOutputs = tx.GetOutputsHash();
    for (const auto &txin : tx.vin) {
        if (!txin.IsAnonInput())
            return state.DoS(100, false, REJECT_MALFORMED, "bad-anon-input");

        uint32_t nInputs, nRingSize;
        txin.GetAnonInfo(nInputs, nRingSize);

        if (nInputs < 1 || nInputs > MAX_ANON_INPUTS)
            return state.DoS(100, false, REJECT_INVALID, "bad-anon-num-inputs");

        if (nRingSize < MIN_RINGSIZE || nRingSize > MAX_RINGSIZE)
            return state.DoS(100, false, REJECT_INVALID, "bad-anon-ringsize");

        uint256 txhash = tx.GetHash();

        size_t nCols = nRingSize;
        size_t nRows = nInputs + 1;

        if (txin.scriptData.stack.size() != 1)
            return state.DoS(100, false, REJECT_MALFORMED, "bad-anonin-dstack-size");

        if (txin.scriptWitness.stack.size() != 2)
            return state.DoS(100, false, REJECT_MALFORMED, "bad-anonin-wstack-size");

        const std::vector<uint8_t> &vKeyImages = txin.scriptData.stack[0];
        const std::vector<uint8_t> &vMI = txin.scriptWitness.stack[0];
        const std::vector<uint8_t> &vDL = txin.scriptWitness.stack[1];

        if (vKeyImages.size() != nInputs * 33)
            return state.DoS(100, false, REJECT_MALFORMED, "bad-anonin-keyimages-size");

        if (vDL.size() != (1 + (nInputs+1) * nRingSize) * 32 + (fSplitCommitments ? 33 : 0))
            return state.DoS(100, false, REJECT_MALFORMED, "bad-anonin-sig-size");

        std::vector<uint8_t> vM(nCols * nRows * 33);

        std::vector<secp256k1_pedersen_commitment> vCommitments;
        vCommitments.reserve(nCols * nInputs);
        std::vector<const uint8_t*> vpOutCommits;
        std::vector<const uint8_t*> vpInCommits(nCols * nInputs);

        if (fSplitCommitments) {
            vpOutCommits.push_back(&vDL[(1 + (nInputs+1) * nRingSize) * 32]);
            vpInputSplitCommits.push_back(&vDL[(1 + (nInputs+1) * nRingSize) * 32]);
        } else {
            if (nPlainValueOut > 0)
                vpOutCommits.push_back(plainCommitment.data);

            secp256k1_pedersen_commitment *pc;
            for (const auto &txout : tx.vpout) {
                if ((pc = txout->GetPCommitment()))
                    vpOutCommits.push_back(pc->data);
            }
        }

        size_t ofs = 0, nB = 0;
        for (size_t k = 0; k < nInputs; ++k) {
            for (size_t i = 0; i < nCols; ++i) {
                int64_t nIndex = 0;

                if (0 != GetVarInt(vMI, ofs, (uint64_t &) nIndex, nB))
                    return state.DoS(100, false, REJECT_MALFORMED, "bad-anonin-extract-i");

                ofs += nB;
                if (!setHaveI.insert(nIndex).second)
                    return state.DoS(100, false, REJECT_MALFORMED, "bad-anonin-dup-i");

                CAnonOutput ao;
                if (!pblocktree->ReadRCTOutput(nIndex, ao)) {
                    return state.DoS(100, false, REJECT_MALFORMED, "bad-anonin-unknown-i");
                }

                memcpy(&vM[(i + k * nCols) * 33], ao.pubkey.begin(), 33);
                vCommitments.push_back(ao.commitment);
                vpInCommits[i + k * nCols] = vCommitments.back().data;
            }
        }

        // checking for duplicate key image to prevent double spends
        uint256 txhashKI;
        for (size_t k = 0; k < nInputs; ++k) {
            const CCmpPubKey &ki = *((CCmpPubKey*)&vKeyImages[k*33]);

            if (!setHaveKI.insert(ki).second) {
                return state.DoS(100, false, REJECT_INVALID, "bad-anonin-dup-ki-tx-double");
            }

//            if (mempool.HaveKeyImage(ki, txhashKI) && txhashKI != txhash) {
//                return state.DoS(100, false, REJECT_INVALID, "bad-anonin-dup-ki-mempool");
//            }

            if (pblocktree->ReadRCTKeyImage(ki, txhashKI) && txhashKI != txhash) {
                LogPrintf("%s: Key image in tx %s\n", __func__, txhashKI.GetHex());
                return state.DoS(100, false, REJECT_INVALID, "bad-anonin-dup-keyimage");
            }
        }

        if (0 != (rv = secp256k1_prepare_mlsag(&vM[0], nullptr, vpOutCommits.size(), vpOutCommits.size(), nCols, nRows,
                &vpInCommits[0], &vpOutCommits[0], nullptr)))
            return state.DoS(100, error("%s: prepare-mlsag-failed %d", __func__, rv), REJECT_INVALID, "prepare-mlsag-failed");

        if (0 != (rv = secp256k1_verify_mlsag(secp256k1_ctx_blind, hashOutputs.begin(), nCols, nRows, &vM[0], &vKeyImages[0],
                &vDL[0], &vDL[32])))
            return state.DoS(100, error("%s: verify-mlsag-failed %d", __func__, rv), REJECT_INVALID, "verify-mlsag-failed");
    }

    // Verify commitment sums match
    if (fSplitCommitments) {
        std::vector<const uint8_t*> vpOutCommits;

        if (nPlainValueOut > 0)
            vpOutCommits.push_back(plainCommitment.data);

        secp256k1_pedersen_commitment *pc;
        for (const auto &txout : tx.vpout) {
            if ((pc = txout->GetPCommitment()))
                vpOutCommits.push_back(pc->data);
        }


        if (1 != (rv = secp256k1_pedersen_verify_tally(secp256k1_ctx_blind,
                (const secp256k1_pedersen_commitment* const*)vpInputSplitCommits.data(), vpInputSplitCommits.size(),
                (const secp256k1_pedersen_commitment* const*)vpOutCommits.data(), vpOutCommits.size())))
            return state.DoS(100, error("%s: verify-commit-tally-failed %d", __func__, rv), REJECT_INVALID, "verify-commit-tally-failed");
    }

    return true;
}

bool VerifyCoinbase(CAmount nExpStakeReward, const CTransaction &tx, CValidationState &state)
{
    std::vector<uint8_t> vBlindPlain(32);
    secp256k1_pedersen_commitment plainCommitment;

    std::vector<const uint8_t*> vpPlainCommits;
    std::vector<const uint8_t*> vpOutCommits;

    if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, &plainCommitment, &vBlindPlain[0],
                                    (uint64_t) nExpStakeReward, secp256k1_generator_h)) {
        return state.DoS(100, error("%s: %s", __func__, "Pedersen Commit failed for plain out."), REJECT_INTERNAL, "plain-pedersen-commit-failed");
    }

    vpPlainCommits.push_back(plainCommitment.data);

    secp256k1_pedersen_commitment *pc;
    for (const auto &txout : tx.vpout) {
        if ((pc = txout->GetPCommitment()))
            vpOutCommits.push_back(pc->data);
    }

    int rv;
    if (1 != (rv = secp256k1_pedersen_verify_tally(secp256k1_ctx_blind,
            (const secp256k1_pedersen_commitment* const*)vpPlainCommits.data(), vpPlainCommits.size(),
            (const secp256k1_pedersen_commitment* const*)vpOutCommits.data(), vpOutCommits.size())))
        return state.DoS(100, error("%s: verify-commit-tally-failed %d", __func__, rv), REJECT_INVALID, "verify-commit-tally-failed");

    return true;
}

bool AddKeyImagesToMempool(const CTransaction &tx, CTxMemPool &pool)
{
    for (const CTxIn &txin : tx.vin) {
        if (!txin.IsAnonInput())
            continue;

        uint256 txhash = tx.GetHash();
        LOCK(pool.cs);
        uint32_t nInputs, nRingSize;
        txin.GetAnonInfo(nInputs, nRingSize);

        const std::vector<uint8_t> &vKeyImages = txin.scriptData.stack[0];

        if (vKeyImages.size() != nInputs * 33)
            return false;

        for (size_t k = 0; k < nInputs; ++k) {
            const CCmpPubKey &ki = *((CCmpPubKey*)&vKeyImages[k*33]);
            pool.mapKeyImages[ki] = txhash;
        }
    }

    return true;
}

bool RemoveKeyImagesFromMempool(const uint256 &hash, const CTxIn &txin, CTxMemPool &pool)
{
    if (!txin.IsAnonInput())
        return false;

    LOCK(pool.cs);
    uint32_t nInputs, nRingSize;
    txin.GetAnonInfo(nInputs, nRingSize);

    const std::vector<uint8_t> &vKeyImages = txin.scriptData.stack[0];

    if (vKeyImages.size() != nInputs * 33)
        return false;

    for (size_t k = 0; k < nInputs; ++k) {
        const CCmpPubKey &ki = *((CCmpPubKey*)&vKeyImages[k*33]);
        pool.mapKeyImages.erase(ki);
    }

    return true;
}


bool AllAnonOutputsUnknown(const CTransaction &tx, CValidationState &state)
{
    state.fHasAnonOutput = false;
    for (unsigned int k = 0; k < tx.vpout.size(); k++) {
        if (!tx.vpout[k]->IsType(OUTPUT_RINGCT))
            continue;

        state.fHasAnonOutput = true;

        CTxOutRingCT *txout = (CTxOutRingCT*)tx.vpout[k].get();

        int64_t nTestExists;
        if (pblocktree->ReadRCTOutputLink(txout->pk, nTestExists)) {
            COutPoint op(tx.GetHash(), k);
            CAnonOutput ao;
            if (!pblocktree->ReadRCTOutput(nTestExists, ao) || ao.outpoint != op) {
                return state.DoS(100, error("%s: Duplicate anon-output %s, index %d - existing: %s,%d.", __func__,
                        HexStr(txout->pk.begin(), txout->pk.end()), nTestExists, ao.outpoint.hash.ToString(), ao.outpoint.n),
                                REJECT_INVALID, "duplicate-anon-output");
            } else {
                // Already in the blockchain, containing block could have been received before loose tx
                return false;
                /*
                return state.DoS(1,
                    error("%s: Duplicate anon-output %s, index %d - existing at same outpoint.",
                        __func__, HexStr(txout->pk.begin(), txout->pk.end()), nTestExists),
                    REJECT_INVALID, "duplicate-anon-output");
                */
            }
        }
    }

    return true;
}


bool RollBackRCTIndex(int64_t nLastValidRCTOutput, int64_t nExpectErase, std::set<CCmpPubKey> &setKi)
{
    // This happens when the client is shutdown before flushing indexes
    AssertLockHeld(cs_main);

    int64_t nRemRCTOutput = nLastValidRCTOutput;
    CAnonOutput ao;
    while (true) {
        nRemRCTOutput++;

        if (!pblocktree->ReadRCTOutput(nRemRCTOutput, ao))
            break;

        pblocktree->EraseRCTOutput(nRemRCTOutput);
        pblocktree->EraseRCTOutputLink(ao.pubkey);
    }

    if (nExpectErase > nRemRCTOutput) {
        nRemRCTOutput = nExpectErase;
        while (nRemRCTOutput > nLastValidRCTOutput) {
            if (!pblocktree->ReadRCTOutput(nRemRCTOutput, ao))
                break;

            pblocktree->EraseRCTOutput(nRemRCTOutput);
            pblocktree->EraseRCTOutputLink(ao.pubkey);
            nRemRCTOutput--;
        }

    }

    for (const auto &ki : setKi) {
        pblocktree->EraseRCTKeyImage(ki);
    }

    return true;
}

std::vector<std::vector<COutPoint>> GetTxRingCtInputs(const CTransactionRef ptx)
{
    std::vector<std::vector<COutPoint> > vTxRingCtInputs;
    for (const CTxIn& txin : ptx->vin) {
        if (txin.IsAnonInput()) {
            std::vector<COutPoint> vInputs = GetRingCtInputs(txin);
            vTxRingCtInputs.emplace_back(vInputs);
        }
    }
    return vTxRingCtInputs;
}

std::vector<COutPoint> GetRingCtInputs(const CTxIn& txin)
{
    std::vector<COutPoint> vInputs;
    uint32_t nInputs, nRingSize;
    txin.GetAnonInfo(nInputs, nRingSize);

    size_t nCols = nRingSize;
    size_t nRows = nInputs + 1;

    if (txin.scriptData.stack.size() != 1)
        return vInputs;

    if (txin.scriptWitness.stack.size() != 2)
        return vInputs;

    const std::vector<uint8_t> vKeyImages = txin.scriptData.stack[0];
    const std::vector<uint8_t> vMI = txin.scriptWitness.stack[0];
    const std::vector<uint8_t> vDL = txin.scriptWitness.stack[1];

    if (vKeyImages.size() != nInputs * 33)
        return vInputs;
    std::vector<uint8_t> vM(nCols * nRows * 33);
    std::set<CKey> setKeyCandidates;

    size_t ofs = 0, nB = 0;
    for (size_t k = 0; k < nInputs; ++k) {
        for (size_t i = 0; i < nCols; ++i) {
            int64_t nIndex = 0;

            if (0 != GetVarInt(vMI, ofs, (uint64_t&) nIndex, nB))
                continue;
            ofs += nB;

            CAnonOutput ao;
            if (!pblocktree->ReadRCTOutput(nIndex, ao)) {
                continue;
            }
            vInputs.emplace_back(ao.outpoint);
        }
    }
    return vInputs;
}

bool GetRingCtInputs(const CTxIn& txin, std::vector<std::vector<COutPoint> >& vInputs)
{
    vInputs.clear();
    uint32_t nInputs, nRingSize;
    txin.GetAnonInfo(nInputs, nRingSize);

    size_t nCols = nRingSize;
    size_t nRows = nInputs + 1;

    if (txin.scriptData.stack.size() != 1)
        return false;

    if (txin.scriptWitness.stack.size() != 2)
        return false;

    const std::vector<uint8_t> vKeyImages = txin.scriptData.stack[0];
    const std::vector<uint8_t> vMI = txin.scriptWitness.stack[0];
    const std::vector<uint8_t> vDL = txin.scriptWitness.stack[1];

    if (vKeyImages.size() != nInputs * 33)
        return false;
    std::vector<uint8_t> vM(nCols * nRows * 33);
    std::set<CKey> setKeyCandidates;

    size_t ofs = 0, nB = 0;
    for (size_t k = 0; k < nInputs; ++k) {
        std::vector<COutPoint> vOutpoints;
        for (size_t i = 0; i < nCols; ++i) {
            int64_t nIndex = 0;

            if (0 != GetVarInt(vMI, ofs, (uint64_t&) nIndex, nB))
                return false;
            ofs += nB;

            CAnonOutput ao;
            if (!pblocktree->ReadRCTOutput(nIndex, ao)) {
                return false;
            }
            vOutpoints.emplace_back(ao.outpoint);
        }
        vInputs.emplace_back(vOutpoints);
    }
    return true;
}

//bool RewindToCheckpoint(int nCheckPointHeight, int &nBlocks, std::string &sError)
//{
//    LogPrintf("%s: At height %d\n", __func__, nCheckPointHeight);
//    nBlocks = 0;
//    int64_t nLastRCTOutput = 0;
//
//    const CChainParams& chainparams = Params();
//    CCoinsViewCache view(pcoinsTip.get());
//    view.fForceDisconnect = true;
//    CValidationState state;
//
//    for (CBlockIndex *pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev) {
//        if (pindex->nHeight <= nCheckPointHeight) {
//            break;
//        }
//
//        nBlocks++;
//
//        std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
//        CBlock& block = *pblock;
//        if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus())) {
//            return errorN(false, sError, __func__, "ReadBlockFromDisk failed.");
//        }
//        if (DISCONNECT_OK != DisconnectBlock(block, pindex, view)) {
//            return errorN(false, sError, __func__, "DisconnectBlock failed.");
//        }
//        if (!FlushView(&view, state, true)) {
//            return errorN(false, sError, __func__, "FlushView failed.");
//        }
//
//        if (!FlushStateToDisk(Params(), state, FlushStateMode::IF_NEEDED)) {
//            return errorN(false, sError, __func__, "FlushStateToDisk failed.");
//        }
//
//        chainActive.SetTip(pindex->pprev);
//        UpdateTip(pindex->pprev, chainparams);
//        GetMainSignals().BlockDisconnected(pblock);
//    }
//    nLastRCTOutput = chainActive.Tip()->nAnonOutputs;
//
//    int nRemoveOutput = nLastRCTOutput+1;
//    CAnonOutput ao;
//    while (pblocktree->ReadRCTOutput(nRemoveOutput, ao)) {
//        pblocktree->EraseRCTOutput(nRemoveOutput);
//        pblocktree->EraseRCTOutputLink(ao.pubkey);
//        nRemoveOutput++;
//    }
//
//    return true;
//};
