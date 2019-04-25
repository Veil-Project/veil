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
        m_params = params;
        m_version = CURRENT_VERSION;
        m_bnPubcoin = bnPubcoin;
        m_bnC1Randomness = C1.getRandomness();
    }

    /**
     * Validate signature by revealing C1's (a commitment to pubcoin) randomness, building a new commitment
     * C(pubcoin, C1.randomness) and checking equality between C and C1.
     *
     * @param bnC1 - This C1 value must be the same value as the serialCommitmentToCoinValue in the coinspend object
     * @return true if bnPubcoin matches the value that was committed to in C1
     */
    bool PubcoinSignature::Verify(const CBigNum& bnC1, std::string& strError) const
    {
        // Check that given member vars are as expected
        if (m_version == 0 || m_bnC1Randomness <= CBigNum(0) || m_bnC1Randomness >= m_params->serialNumberSoKCommitmentGroup.groupOrder) {
            strError = "member var sanity check failed.";
            return false;
        }

        // Check that the pubcoin is valid according to libzerocoin::PublicCoin standards (denom does not matter here)
        try {
            PublicCoin coin(m_params, m_bnPubcoin, CoinDenomination::ZQ_TEN);
            if (!coin.validate()) {
                strError = "pubcoin did not validate";
                return false;
            }
        } catch (...) {
            strError = "pubcoin threw an error";
            return false;
        }

        // Check that C1, the commitment to the pubcoin under serial params, uses the same pubcoin
        Commitment commitmentCheck(&m_params->serialNumberSoKCommitmentGroup, m_bnPubcoin, m_bnC1Randomness);
        return commitmentCheck.getCommitmentValue() == bnC1;
    }
}
