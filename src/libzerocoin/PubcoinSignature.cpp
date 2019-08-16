/**
* @file       PubcoinSignature.cpp
*
* @brief      De-anonymize zerocoins for times that the integrity of the accumulators or related libzerocoin zkp's are broken.
*
* @author     presstab https://github.com/presstab, random-zebra https://github.com/random-zebra
* @date       April 2019
*
* @copyright  Copyright 2019 The Veil Developers, Copyright 2019 The PIVX Developers
* @license    This project is released under the MIT license.
**/

#include "PubcoinSignature.h"

namespace libzerocoin {

    /**
     * Prove ownership of a pubcoin and prove that it links to the C1 commitment
     *
     * @param params: zerocoin params
     * @param bnPubcoin: pubcoin value that is being spent
     * @param C1: serialCommitmentToCoinValue
     * @return PubcoinSignature object containing C1 randomness and pubcoin value
     */
    PubcoinSignature::PubcoinSignature(const ZerocoinParams* params, const CBigNum& bnPubcoin, const Commitment& C1)
    {
        SetNull();
        m_params = params;
        m_version = C1_VERSION;
        m_bnPubcoin = bnPubcoin;
        m_bnRandomness = C1.getRandomness();
    }

    /**
     * Reveal a pubcoin's secrets
     *
     * @param params: zerocoin params
     * @param bnPubcoin: pubcoin value that is being spent
     * @param bnRandomness: coin randomness
     * @param bnSerial: coin serial
     * @return PubcoinSignature object containing pubcoin randomness
     */
    PubcoinSignature::PubcoinSignature(const ZerocoinParams* params, const CBigNum& bnPubcoin, const CBigNum& bnRandomness, const uint256& txidFrom, int vout)
    {
        SetNull();
        m_params = params;
        m_version = CURRENT_VERSION;
        m_bnPubcoin = bnPubcoin;
        m_bnRandomness = bnRandomness; //Randomness is the coin's randomness
        m_hashTxFrom = txidFrom;
        m_nOutpointPos = vout;
    }

    /**
     * Validate signature by revealing C1's (a commitment to pubcoin) randomness, building a new commitment
     * C(pubcoin, C1.randomness) and checking equality between C and C1.
     *
     * @param bnC1 - This C1 value must be the same value as the serialCommitmentToCoinValue in the coinspend object
     * @return true if bnPubcoin matches the value that was committed to in C1
     */
    bool PubcoinSignature::VerifyV1(const CBigNum& bnC1, std::string& strError) const
    {
        // Check that given member vars are as expected
        if (m_version == 0) {
            strError = "version is 0";
            return false;
        }

        if (m_bnRandomness <= CBigNum(0)) {
            strError = strprintf("randomness is equal to or less than 0 %s", m_bnRandomness.GetHex());
            return false;
        }

        if (m_bnRandomness >= m_params->serialNumberSoKCommitmentGroup.groupOrder) {
            strError = strprintf("randomness greater than max allowed amount %s", m_bnRandomness.GetHex());
            return false;
        }

        // Check that the pubcoin is valid according to libzerocoin::PublicCoin standards (denom does not matter here)
        try {
            PublicCoin pubcoin(m_params, m_bnPubcoin, CoinDenomination::ZQ_TEN);
            if (!pubcoin.validate()) {
                strError = "pubcoin did not validate";
                return false;
            }
        } catch (...) {
            strError = "pubcoin threw an error";
            return false;
        }

        // Check that C1, the commitment to the pubcoin under serial params, uses the same pubcoin
        Commitment commitmentCheck(&m_params->serialNumberSoKCommitmentGroup, m_bnPubcoin, m_bnRandomness);
        return commitmentCheck.getCommitmentValue() == bnC1;
    }

    /**
     * Validate signature by revealing a pubcoin's randomness and importing its serial from the coinspend. Check that
     * the pubcoin opens to a commitment of the randomness and the serial.
     *
     * @param bnSerial - The serial of the coinspend object
     * @param bnPubcoin - The pubcoin value that is taken directly from the blockchain
     * @return true if the commitment is equal to the provided pubcoin value
     */
    bool PubcoinSignature::VerifyV2(const CBigNum& bnSerial, const CBigNum& bnPubcoin, std::string strError) const
    {
        if (m_version != 2 || m_bnRandomness < CBigNum(0) || m_bnRandomness >= m_params->coinCommitmentGroup.groupOrder || m_hashTxFrom.IsNull()) {
            strError = "member var sanity check failed";
            return false;
        }

        Commitment commitment(&m_params->coinCommitmentGroup, bnSerial, m_bnRandomness);
        if (commitment.getCommitmentValue() != bnPubcoin) {
            strError = "pubcoin value does not open to serial and randomness combination";
            return false;
        }

        if (m_bnPubcoin != bnPubcoin) {
            strError = "mismatched pubcoin value";
            return false;
        }

        return true;
    }

    bool PubcoinSignature::GetMintOutpoint(uint256& txid, int& n) const
    {
        if (m_version < 2)
            return false;

        txid = m_hashTxFrom;
        n = m_nOutpointPos;
        return true;
    }
}
