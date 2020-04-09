// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ringctstakeinput.h"
#include <veil/ringct/anon.h>
#include <veil/ringct/blind.h>
#include <secp256k1/include/secp256k1_mlsag.h>

#ifdef ENABLE_WALLET
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>
#include <veil/ringct/anonwallet.h>
#endif

/**
 * @brief Constructor
 *
 * @throws std::runtime_error: If any of the loading of the data associated with the RingCtStakeCandidate fails, a runtime
 * error is thrown.
 *
 * @note Since this is an object that only occurs within the local instance of Veil, throwing runtime errors
 * is not an issue, but they should be caught and handled.
 *
 * @param[in] pwallet: The CWallet that holds the RingCt output that is a stake candidate.
 * @param[in] ptx: The CTransactionRef that the RingCt output is from.
 * @param[in] outpoint: The COutPoint that identifies the location of the RingCt output within the ptx->vpout vector.
 * @param[in] pout: The COutputRecord* that is held by the AnonWallet and represents the RingCt output being staked.
 *
 * @return RingCtStakeCandidate
 */
RingCtStakeCandidate::RingCtStakeCandidate(CWallet* pwallet, CTransactionRef ptx, const COutPoint& outpoint, const COutputRecord* pout) : m_ptx(ptx), m_outpoint(outpoint), m_pout(pout)
{
    AnonWallet* panonwallet = pwallet->GetAnonWallet();
    AnonWalletDB wdb(pwallet->GetDBHandle());

    //Get key image
    CCmpPubKey keyimage;
    if (!wdb.GetKeyImageFromOutpoint(m_outpoint, keyimage)) {
        //Manually get the key image if possible.
        CKeyID idStealth;
        if (!m_pout->GetStealthID(idStealth)) {
            error("%s:%d FAILED TO GET STEALTH ID FOR RCTCANDIDATE\n", __func__, __LINE__);
            throw std::runtime_error("rct candidate failed.");
        }

        CKey keyStealth;
        if (!panonwallet->GetKey(idStealth, keyStealth))
            throw std::runtime_error("RingCtStakeCandidate failed to get stealth key");

        CTxOutRingCT* txout = (CTxOutRingCT*)m_ptx->vpout[m_outpoint.n].get();
        if (secp256k1_get_keyimage(secp256k1_ctx_blind, keyimage.ncbegin(), txout->pk.begin(), keyStealth.begin()) != 0)
            throw std::runtime_error("Unable to get key image");

        if (!wdb.WriteKeyImageFromOutpoint(m_outpoint, keyimage))
            error("%s: failed to write keyimage to disk.");
    }

    m_hashPubKey = keyimage.GetHash();
    m_nAmount = m_pout->GetAmount();
}

/**
 * @brief Get the deterministic uniqueness of a RingCT output.
 * The uniqueness of a RingCT stake is a hash of the key image.
 *
 * @return CDataStream : uniqueness is serialized into a datastream object.
 */
CDataStream RingCtStakeCandidate::GetUniqueness()
{
    CDataStream ss(0,0);
    ss << m_hashPubKey;
    return ss;
}

/**
 * @brief Get the CBlockIndex that the staked input is from.
 * With a RingCT stake, there is no reliable way to know which input is the input being spent. This means the input
 * that the stake is "from" is the input that is an ancestor of the current chain tip with a depth of the required
 * stake depth.
 *
 * @note The RingCtStakeCandidate implementation of GetIndexFrom() relies on chainActive and its height. Since a stake
 * candidate is going to be the <i>next</i> block in the chain, the index from has to have 1 added to the height.
 *
 * @return CBlockIndex: The index the stake is "from".
 */
CBlockIndex* RingCtStakeCandidate::GetIndexFrom()
{
    int nCurrentHeight = chainActive.Height();
    return chainActive.Tip()->GetAncestor(nCurrentHeight + 1 - Params().RequiredStakeDepth());
}

/**
 * @brief Create a coinstake transaction from the stake candidate.
 *
 * @note Call CreateCoinStake() after finding a valid stake kernel. A kernel can be found without needing to create the CTxIn.
 *
 * @param[in] pwallet: The CWallet that holds the AnonWallet that holds the RingCT output that is being staked.
 * @param[out] txIn: The resulting CTxIn that will be used in the coinstake transaction.
 * @return <b>true</b> upon success.
 *         <b>false</b> if the AnonWallet fails to find the StakeAddress or if AddAnonInputs() fails.
 */
bool RingCtStakeCandidate::CreateCoinStake(CWallet* pwallet, const CAmount& nBlockReward, CMutableTransaction& txCoinStake)
{
    AnonWallet* panonWallet = pwallet->GetAnonWallet();
    CTransactionRef ptx = MakeTransactionRef();
    CWalletTx wtx(pwallet, ptx);

    //Add the input to coincontrol so that addanoninputs knows what to use
    CCoinControl coinControl;
    coinControl.Select(m_outpoint, m_nAmount);
    coinControl.nCoinType = OUTPUT_RINGCT;
    coinControl.fProofOfStake = true;
    coinControl.nValueBlockReward = nBlockReward;

    //Tell the rct code who the recipient is
    std::vector<CTempRecipient> vecSend;
    CTempRecipient tempRecipient;
    tempRecipient.nType = OUTPUT_RINGCT;
    tempRecipient.SetAmount(m_nAmount);
    CStealthAddress address;
    if (!panonWallet->GetStakeAddress(address))
        return error("%s: failed to get the staking address");

    tempRecipient.address = address;
    tempRecipient.fSubtractFeeFromAmount = false;
    tempRecipient.fExemptFeeSub = true;
    vecSend.emplace_back(tempRecipient);

    std::string strError;
    CTransactionRecord rtx;
    CAmount nFeeRet = 0;
    if (panonWallet->AddAnonInputs(wtx, rtx, vecSend, /*fSign*/false, /*nRingSize*/Params().DefaultRingSize(), /*nInputsPerSig*/32, nFeeRet, &coinControl, strError) != 0)
        return error("%s: AddAnonInputs failed with error %s", __func__, strError);

    return true;
}

/**
 * @brief Create the CTxOut(s) that will be added to the coinstake transaction.
 * These outputs should:
 * <ol>
 *   <li> Pay the staker the original value of the RingCt output (the value of RingCtStakeCandidate::m_nAmount)
 *   <li> Pay the staker the \link veil::BudgetParams::GetBlockRewards() block reward \endlink
 *   <li> Have a rangeproof that proves that it did not create more value than the blockreward.
 * </ol>
 * @param[in] pwallet: The wallet that holds the AnonWallet that owns the RingCt output being staked.
 * @param[out] vout: The resulting std::vector of CTxOut that should be placed in the coinstake transaction's vpout vector.
 * @return <b>true</b> upon success.
 *         <b>false</b> on fail.
 */
// bool RingCtStakeCandidate::CreateTxOuts(CWallet* pwallet, std::vector<CTxOut>& vout, CAmount nTotal)
// {
// #ifdef ENABLE_WALLET
//     if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
// #endif
//         return error("%s: wallet disabled", __func__);
// #ifdef ENABLE_WALLET
//     }

//     //Create an output returning the RingCT VEIL that was staked
//     AnonWallet* panonWallet = pwallet->GetAnonWallet();

//     CTxOut outStaked;
//     panonWallet->AddToSpends(m_outpoint,m_ptx->GetHash());
//     vout.emplace_back(outStaked);

//     //Create the block reward output
//     CTxOut outReward;
//     if (!pwallet->CreateZOutPut(denomReward, out, dMintReward))
//         return error("%s: failed to create RingCT output", __func__);
//     vout.emplace_back(out);

//     return true;
// #endif
// }

/**
 * @brief Constructor
 * @param[in] txStake: The coinstake transaction.
 * @return <b>true</b> upon success.
 *         <b>false</b> if the output type is not OUTPUT_RINGCT or if GetRangeProofInfo() fails.
 */
PublicRingCtStake::PublicRingCtStake(const CTransactionRef& txStake)
{
    m_ptx = txStake;
    m_type = STAKE_RINGCT;
}

/**
 * @brief Get the lowest possible value of the inputs used in this RingCT transaction.
 * @param[out] nValue: The returned minimum value of output 0.
 * @return <b>true</b> upon success.
 *         <b>false</b> if the output type is not OUTPUT_RINGCT or if GetRangeProofInfo() fails.
 */
bool PublicRingCtStake::GetMinimumInputValue(CAmount& nValue) const
{
    int nExp = 0;
    int nMantissa = 0;
    CAmount nMinValue = 0;
    CAmount nMaxValue = 0;
    if (m_ptx->vpout[0]->GetType() != OUTPUT_RINGCT)
        return error("%s: Output type is not RingCT.", __func__);

    auto txout = (CTxOutRingCT*)m_ptx->vpout[0].get();
    if (!GetRangeProofInfo(txout->vRangeproof, nExp, nMantissa, nMinValue, nMaxValue))
        return error("%s: Failed to get range proof info.", __func__);

    nValue = nMinValue;
    return true;
}

/**
 * @brief Get the theoretical value of the RingCT stake.
 * @return CAmount: the minimum value of the PublicRingCtStake. Returns 0 on fail.
 * @see GetMinimumInputValue()
 */
CAmount PublicRingCtStake::GetValue()
{
    CAmount nValue = 0;
    if (!GetMinimumInputValue(nValue))
        return 0;

    return nValue;
}

/**
 * @brief Get a hash of the the key image for output 0.
 * @param[out] hashPubKey: resulting hash of the key image
 * @return bool: true upon success. false if there is a casting error when attempting to extract the key.
 */
bool PublicRingCtStake::GetPubkeyHash(uint256& hashPubKey) const
{
    //Extract the pubkeyhash from the keyimage
    try {
        const CTxIn &txin = m_ptx->vin[0];
        const std::vector<uint8_t> vKeyImages = txin.scriptData.stack[0];
        uint32_t nInputs, nRingSize;
        txin.GetAnonInfo(nInputs, nRingSize);
        const CCmpPubKey &ki = *((CCmpPubKey *) &vKeyImages[0]);
        hashPubKey = ki.GetHash();
    } catch (...) {
        return error("%s: Deserialization of compressed pubkey failed.", __func__);
    }

    return true;
}

/**
 * @brief Get the inputs used for the RingCT transaction. This includes all inputs, including decoy inputs.
 * @return std::vector<COutPoint>: A vector of the outpoints that are inputs in the transaction.
 */
std::vector<COutPoint> PublicRingCtStake::GetTxInputs() const
{
    return GetRingCtInputs(m_ptx->vin[0]);
}

/**
 * @brief Get the deterministic uniqueness of the RingCT output that is spent in this transaction.
 * The uniqueness of a RingCT stake is a hash of the key image.
 *
 * @return CDataStream : uniqueness is serialized into a datastream object.
 */
CDataStream PublicRingCtStake::GetUniqueness()
{
    uint256 hashPubKey;
    GetPubkeyHash(hashPubKey);
    CDataStream ss(0,0);
    ss << hashPubKey;
    return ss;
}

/**
 * @brief Create a coinstake transaction from the stake candidate.
 *
 * @note Call CreateCoinStake() after finding a valid stake kernel. A kernel can be found without needing to create the CTxIn.
 *
 * @param[in] pwallet: The CWallet that holds the AnonWallet that holds the RingCT output that is being staked.
 * @param[out] txIn: The resulting CTxIn that will be used in the coinstake transaction.
 * @return <b>true</b> upon success.
 *         <b>false</b> if the AnonWallet fails to find the StakeAddress or if AddAnonInputs() fails.
 */
bool PublicRingCtStake::CreateCoinStake(CWallet* pwallet, const CAmount& nBlockReward, CMutableTransaction& txCoinStake)
{
    // AnonWallet* panonWallet = pwallet->GetAnonWallet();
    // CTransactionRef ptx = MakeTransactionRef();
    // CWalletTx wtx(pwallet, ptx);

    // //Add the input to coincontrol so that addanoninputs knows what to use
    // CCoinControl coinControl;
    // coinControl.Select(m_outpoint, m_nAmount);
    // coinControl.nCoinType = OUTPUT_RINGCT;
    // coinControl.fProofOfStake = true;
    // coinControl.nValueBlockReward = nBlockReward;

    // //Tell the rct code who the recipient is
    // std::vector<CTempRecipient> vecSend;
    // CTempRecipient tempRecipient;
    // tempRecipient.nType = OUTPUT_RINGCT;
    // tempRecipient.SetAmount(m_nAmount);
    // CStealthAddress address;
    // if (!panonWallet->GetStakeAddress(address))
    //     return error("%s: failed to get the staking address");

    // tempRecipient.address = address;
    // tempRecipient.fSubtractFeeFromAmount = false;
    // tempRecipient.fExemptFeeSub = true;
    // vecSend.emplace_back(tempRecipient);

    // std::string strError;
    // CTransactionRecord rtx;
    // CAmount nFeeRet = 0;
    // if (panonWallet->AddAnonInputs(wtx, rtx, vecSend, /*fSign*/false, /*nRingSize*/Params().DefaultRingSize(), /*nInputsPerSig*/32, nFeeRet, &coinControl, strError) != 0)
    //     return error("%s: AddAnonInputs failed with error %s", __func__, strError);

    // return true;
}

