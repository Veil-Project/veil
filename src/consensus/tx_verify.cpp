// Copyright (c) 2017-2019 The Bitcoin Core developers
// Copyright (c) 2015-2019 The PIVX developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_verify.h>

#include <consensus/consensus.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <consensus/validation.h>

// TODO remove the following dependencies
#include <chain.h>
#include <coins.h>
#include <utilmoneystr.h>
#include <chainparams.h>
#include <pubkey.h>
#include <script/standard.h>
#include <key_io.h>
#include <veil/ringct/blind.h>
#include <validation.h>
#include <tinyformat.h>
#include <libzerocoin/CoinSpend.h>
#include <veil/zerocoin/zchain.h>

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const auto& txin : tx.vin) {
        if (!(txin.nSequence == CTxIn::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = static_cast<uint32_t>(tx.nVersion) >= 2
                      && flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn& txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.IsAnonInput() || txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight-1, 0))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair)
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    for (const auto &txout : tx.vpout) {
        const CScript *pScriptPubKey = txout->GetPScriptPubKey();
        if (pScriptPubKey)
            nSigOps += pScriptPubKey->GetSigOpCount(false);
    }

    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        if (tx.vin[i].IsAnonInput())
            continue;
        if (tx.vin[i].scriptSig.IsZerocoinSpend())
            continue;
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

int64_t GetTransactionSigOpCost(const CTransaction& tx, const CCoinsViewCache& inputs, int flags)
{
    int64_t nSigOps = GetLegacySigOpCount(tx) * WITNESS_SCALE_FACTOR;

    if (tx.IsCoinBase())
        return nSigOps;

    if (flags & SCRIPT_VERIFY_P2SH) {
        nSigOps += GetP2SHSigOpCount(tx, inputs) * WITNESS_SCALE_FACTOR;
    }

    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        if (tx.vin[i].IsAnonInput())
            continue;
        if (tx.vin[i].scriptSig.IsZerocoinSpend())
            continue;
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        nSigOps += CountWitnessSigOps(tx.vin[i].scriptSig, prevout.scriptPubKey, &tx.vin[i].scriptWitness, flags);
    }
    return nSigOps;
}

bool CheckZerocoinSpend(const CTransaction& tx, CValidationState& state)
{
    //max needed non-mint outputs should be 2 - one for redemption address and a possible 2nd for change
    if (tx.vpout.size() > 2) {
        int outs = 0;
        for (const auto& pout : tx.vpout) {
            if (pout->IsZerocoinMint())
                continue;
            outs++;
        }
        //if (outs > 2 && !tx.IsCoinStake())
        //    return state.DoS(100, error("CheckZerocoinSpend(): over two non-mint outputs in a zerocoinspend transaction"));
    }

    //compute the txout hash that is used for the zerocoinspend signatures
    CMutableTransaction txTemp;
    for (const auto& out : tx.vpout)
        txTemp.vpout.emplace_back(out);

    uint256 hashTxOut = txTemp.GetOutputsHash();

    bool fValidated = false;
    std::set<CBigNum> setSerials;
    CAmount nTotalRedeemed = 0;
    for (const CTxIn& txin : tx.vin) {
        //only check txin that is a zcspend
        if (!txin.scriptSig.IsZerocoinSpend())
            continue;

        auto newSpend = TxInToZerocoinSpend(txin);
        if (!newSpend)
            return state.DoS(100, error("%s: failed to convert TxIn to zerocoinspend", __func__));

        //check that the denomination is valid
        if (newSpend->getDenomination() == libzerocoin::ZQ_ERROR)
            return state.DoS(100, error("Zerocoinspend does not have the correct denomination"));

        //check that denomination is what it claims to be in nSequence
        if (newSpend->getDenomination()*COIN != txin.GetZerocoinSpent())
            return state.DoS(100, error("Zerocoinspend nSequence denomination does not match CoinSpend"));

        //make sure the txout has not changed
        if (newSpend->getTxOutHash() != hashTxOut)
            return state.DoS(100, error("Zerocoinspend does not use the same txout that was used in the SoK"));

        if (setSerials.count(newSpend->getCoinSerialNumber()))
            return state.DoS(100, error("Zerocoinspend serial is used twice in the same tx"));
        setSerials.emplace(newSpend->getCoinSerialNumber());

        //make sure that there is no over redemption of coins
        nTotalRedeemed += libzerocoin::ZerocoinDenominationToAmount(newSpend->getDenomination());
        fValidated = true;
    }

    if (!tx.IsCoinStake() && nTotalRedeemed < tx.GetValueOut()) {
        LogPrintf("redeemed = %s , spend = %s \n", FormatMoney(nTotalRedeemed), FormatMoney(tx.GetValueOut()));
        return state.DoS(100, error("Transaction spend more than was redeemed in zerocoins"));
    }

    return fValidated;
}

bool CheckZerocoinMint(const CTxOut& txout, CBigNum& bnValue, CValidationState& state, bool fSkipZerocoinMintIsPrime)
{
    libzerocoin::PublicCoin pubCoin(Params().Zerocoin_Params());
    if (!TxOutToPublicCoin(txout, pubCoin))
        return state.DoS(100, error("CheckZerocoinMint(): TxOutToPublicCoin() failed"));

    if (!fSkipZerocoinMintIsPrime && !pubCoin.validate())
        return state.DoS(100, error("CheckZerocoinMint() : PubCoin does not validate"));

    bnValue = pubCoin.getValue();
    return true;
}

bool CheckValue(CValidationState &state, CAmount nValue, CAmount &nValueOut)
{
    if (nValue < 0)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");
    if (nValue > MAX_MONEY)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");
    nValueOut += nValue;

    return true;
}

bool CheckStandardOutput(CValidationState &state, const Consensus::Params& consensusParams, const CTxOutStandard *p, CAmount &nValueOut, CBigNum& bnPubcoin, bool fSkipZerocoinMintIsPrime)
{
    if (p->IsZerocoinMint()) {
        CTxOut txout;
        if (!p->GetTxOut(txout))
            return error("%s: failed to extract zerocoinmint from output", __func__);
        if (!CheckZerocoinMint(txout, bnPubcoin, state, fSkipZerocoinMintIsPrime))
            return error("%s: zerocoin mint check failed", __func__);
    }
    return CheckValue(state, p->nValue, nValueOut);
}

bool CheckBlindOutput(CValidationState &state, const CTxOutCT *p)
{
    if (p->vData.size() < 33 || p->vData.size() > 33 + 5)
        return state.DoS(100, false, REJECT_INVALID, "bad-ctout-ephem-size");

    size_t nRangeProofLen = 5134;
    if (p->vRangeproof.size() > nRangeProofLen)
        return state.DoS(100, false, REJECT_INVALID, "bad-ctout-rangeproof-size");


    if (/*todo: fBusyImporting && */ fSkipRangeproof)
        return true;

    uint64_t min_value, max_value;
    int rv = secp256k1_rangeproof_verify(secp256k1_ctx_blind, &min_value, &max_value, &p->commitment, p->vRangeproof.data(),
            p->vRangeproof.size(), nullptr, 0, secp256k1_generator_h);

    if (rv != 1)
        return state.DoS(100, false, REJECT_INVALID, "bad-ctout-rangeproof-verify");

    return true;
}

bool CheckAnonOutput(CValidationState &state, const CTxOutRingCT *p)
{
    if (p->vData.size() < 33 || p->vData.size() > 33 + 5)
        return state.DoS(100, false, REJECT_INVALID, "bad-rctout-ephem-size");

    size_t nRangeProofLen = 5134;
    if (p->vRangeproof.size() > nRangeProofLen)
        return state.DoS(100, false, REJECT_INVALID, "bad-rctout-rangeproof-size");

    if (/* todo: fBusyImporting && */ fSkipRangeproof)
        return true;

    uint64_t min_value, max_value;
    int rv = secp256k1_rangeproof_verify(secp256k1_ctx_blind, &min_value, &max_value, &p->commitment, p->vRangeproof.data(),
            p->vRangeproof.size(), nullptr, 0, secp256k1_generator_h);

    if (rv != 1)
        return state.DoS(100, false, REJECT_INVALID, "bad-rctout-rangeproof-verify");

    return true;
}

bool CheckDataOutput(CValidationState &state, const CTxOutData *p)
{
    if (p->vData.size() < 1)
        return state.DoS(100, false, REJECT_INVALID, "bad-output-data-size");

    const size_t MAX_DATA_OUTPUT_SIZE = 34 + 5 + 34; // DO_STEALTH 33, DO_STEALTH_PREFIX 4, DO_NARR_CRYPT (max 32 bytes)
    if (p->vData.size() > MAX_DATA_OUTPUT_SIZE)
        return state.DoS(100, false, REJECT_INVALID, "bad-output-data-size");

    return true;
}

bool CheckTransaction(const CTransaction& tx, CValidationState &state, bool fSkipZerocoinMintIsPrime)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");

    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    const Consensus::Params& consensusParams = Params().GetConsensus();
    if (tx.vpout.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vpout-empty");

    size_t nStandardOutputs = 0;
    CAmount nValueOut = 0;
    size_t nDataOutputs = 0;
    std::set<CBigNum> setPubCoin;
    int nZerocoinMints = 0;
    int nRingCTOut = 0;
    int nCTOut = 0;
    for (const auto &txout : tx.vpout) {
        switch (txout->nVersion) {
            case OUTPUT_STANDARD: {
                CBigNum bnPubCoin = 0;
                if (!CheckStandardOutput(state, consensusParams, (CTxOutStandard*) txout.get(), nValueOut, bnPubCoin, fSkipZerocoinMintIsPrime))
                    return false;
                if (txout->IsZerocoinMint())
                    nZerocoinMints++;

                //Keep track of pubcoins so they can be checked for the same value being included twice in one tx
                if (bnPubCoin != 0) {
                    if (setPubCoin.count(bnPubCoin))
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-duplicate-zerocoinmints");
                    setPubCoin.emplace(bnPubCoin);
                }
                nStandardOutputs++;
                break;
            }
            case OUTPUT_CT:
                if (!CheckBlindOutput(state, (CTxOutCT*) txout.get()))
                    return false;
                nCTOut++;
                break;
            case OUTPUT_RINGCT:
                if (!CheckAnonOutput(state, (CTxOutRingCT*) txout.get()))
                    return false;
                nRingCTOut++;
                break;
            case OUTPUT_DATA:
                if (!CheckDataOutput(state, (CTxOutData*) txout.get()))
                    return false;
                nDataOutputs++;
                break;
            default:
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-unknown-output-version");
        }

        if (!MoneyRange(nValueOut))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    if (nDataOutputs > 1 + nStandardOutputs) // extra 1 for ct fee output
        return state.DoS(100, false, REJECT_INVALID, "too-many-data-outputs");

    // Explicitly ban using both ringct and ct outputs at the same time.
    if (nRingCTOut && nCTOut)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-both-ringctout-and-ctout");

    std::set<COutPoint> vInOutPoints;
    std::set<uint256> setZerocoinSpendHashes;
    int nAnonIn = 0;
    for (const auto& txin : tx.vin) {
        if (txin.scriptSig.IsZerocoinSpend()) {
            //Veil: Cheap check here by hashing the entire script. This could be worked around, so still needs
            // a final full check in ConnectBlock()
            auto hashSpend = Hash(txin.scriptSig.begin(), txin.scriptSig.end());
            if (setZerocoinSpendHashes.count(hashSpend))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate-zcspend");

            setZerocoinSpendHashes.emplace(hashSpend);
            continue;
        }

        if (!txin.IsAnonInput() && !vInOutPoints.insert(txin.prevout).second)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
        if (txin.IsAnonInput())
            nAnonIn++;
    }

    if (tx.IsCoinBase()) {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
    } else if (tx.IsZerocoinSpend()) {
        if (tx.vin.size() < 1 || static_cast<int>(tx.vin.size()) > Params().Zerocoin_MaxSpendsPerTransaction())
            return state.DoS(10, error("CheckTransaction() : Zerocoin Spend has more than allowed txin's"),
                             REJECT_INVALID, "bad-zerocoinspend");
    } else {
        for (const auto& txin : tx.vin)
            if (!txin.IsAnonInput() && txin.prevout.IsNull())
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null");
    }

    if (tx.IsZerocoinSpend())
        return CheckZerocoinSpend(tx, state);

    return true;
}

bool Consensus::CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs,
                              int nSpendHeight, CAmount& txfee, CAmount& nValueIn, CAmount& nValueOut)
{
    // reset per tx
    state.fHasAnonOutput = false;
    state.fHasAnonInput = false;

    if (tx.vin.size() < 1) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txn-no-inputs", false, strprintf("%s: no inputs", __func__));
    }

    // are the actual inputs available?
    if (!inputs.HaveInputs(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-missingorspent", false,
                         strprintf("%s: inputs missing/spent", __func__));
    }

    std::vector<const secp256k1_pedersen_commitment*> vpCommitsIn, vpCommitsOut;
    size_t nBasecoin = 0, nCt = 0, nRingCT = 0, nZerocoin = 0;
    nValueIn = 0;
    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        if (tx.vin[i].IsAnonInput()) {
            state.fHasAnonInput = true;
            nRingCT++;
            continue;
        }

        if (tx.vin[i].scriptSig.IsZerocoinSpend()) {
            //Zerocoinspend uses nSequence as an easy reference to denomination
            CAmount nValue = tx.vin[i].nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK;
            nValue *= COIN;
            if (!MoneyRange(nValue))
                return false;
            nValueIn += nValue;
            nZerocoin++;
            continue;
        }
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        // If prev is coinbase, check that it's matured
        if (coin.IsCoinBase() && nSpendHeight - coin.nHeight < ::Params().CoinbaseMaturity()) {
            return state.Invalid(false, REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                strprintf("tried to spend coinbase at depth %d", nSpendHeight - coin.nHeight));
        }

        // Check for negative or overflow input values
        if (coin.nType == OUTPUT_STANDARD) {
            nValueIn += coin.out.nValue;
            if (!MoneyRange(coin.out.nValue) || !MoneyRange(nValueIn))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");

            nBasecoin++;
        } else if (coin.nType == OUTPUT_CT) {
            vpCommitsIn.push_back(&coin.commitment);
            nCt++;
        } else {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-input-type");
        }
    }

    if ((nBasecoin > 0) + (nCt > 0) + (nRingCT > 0) + (nZerocoin > 0) > 1)
        return state.DoS(100, false, REJECT_INVALID, "mixed-input-types");

    size_t nRingCTInputs = nRingCT;
    size_t nCTInputs = nCt;
    // GetPlainValueOut adds to nStandard, nCt, nRingCT
    CAmount nPlainValueOut = tx.GetPlainValueOut(nBasecoin, nCt, nRingCT);
    state.fHasAnonOutput = nRingCT > nRingCTInputs;

    //Only allow ringct outputs if there are anon inputs or ct inputs
    if (nRingCT && !nRingCTInputs && !nCTInputs)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-ringct-output-no-anonin");

    txfee = 0;
    CAmount nFees = 0;
    if (!tx.IsCoinStake()) {
        // Tally transaction fees
        if (nCt > 0 || nRingCT > 0) {
            if (!tx.GetCTFee(txfee))
                return state.DoS(100, error("%s: bad-fee-output", __func__), REJECT_INVALID, "bad-fee-output");
        } else {
            txfee = nValueIn - nPlainValueOut;

            if (nValueIn < nPlainValueOut)
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
                                 strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(nPlainValueOut)));
        }

        if (txfee < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-negative");

        nFees += txfee;
        if (!MoneyRange(nFees))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-outofrange");
    } else {
        // Return stake reward in nTxFee
        txfee = nPlainValueOut - nValueIn;
        if (nCt > 0 || nRingCT > 0) { // counters track both outputs and inputs
            return state.DoS(100, error("ConnectBlock(): non-standard elements in coinstake"),
                             REJECT_INVALID, "bad-coinstake-outputs");
        }
    }

    if (nCt > 0) {
        nPlainValueOut += txfee;

        if (!MoneyRange(nPlainValueOut))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-out-outofrange");

        if (!MoneyRange(nValueIn))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-outofrange");

        // commitments must sum to 0
        secp256k1_pedersen_commitment plainInCommitment, plainOutCommitment;
        uint8_t blindPlain[32];
        memset(blindPlain, 0, 32);
        if (nValueIn > 0) {
            if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, &plainInCommitment, blindPlain, (uint64_t) nValueIn, secp256k1_generator_h))
                return state.Invalid(false, REJECT_INVALID, "commit-failed");

            vpCommitsIn.push_back(&plainInCommitment);
        }

        if (nPlainValueOut > 0) {
            if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, &plainOutCommitment, blindPlain, (uint64_t) nPlainValueOut, secp256k1_generator_h))
                return state.Invalid(false, REJECT_INVALID, "commit-failed");

            vpCommitsOut.push_back(&plainOutCommitment);
        }

        secp256k1_pedersen_commitment *pc;
        for (auto &txout : tx.vpout) {
            if ((pc = txout->GetPCommitment()))
                vpCommitsOut.push_back(pc);
        }

        int rv = secp256k1_pedersen_verify_tally(secp256k1_ctx_blind, vpCommitsIn.data(), vpCommitsIn.size(),
                vpCommitsOut.data(), vpCommitsOut.size());

        if (rv != 1)
            return state.DoS(100, false, REJECT_INVALID, "bad-commitment-sum");
    }

    int nZerocoinMints = 0;
    for (const auto &txout : tx.vpout) {
        if (txout->IsZerocoinMint())
            nZerocoinMints++;
    }

    CAmount nFeeRequired = nZerocoinMints * ::Params().Zerocoin_MintFee();
    if (txfee < nFeeRequired)
        return state.DoS(100, error("%s: Low fee: required=%s paid=%s", __func__, FormatMoney(nFeeRequired), FormatMoney(txfee)), REJECT_INVALID, "low-zerocoinmint-fee");

    nValueOut = nPlainValueOut;
    return true;
}
