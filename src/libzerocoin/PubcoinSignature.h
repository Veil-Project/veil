/**
* @file       PubcoinSignature.h
*
* @brief      De-anonymize zerocoins for times that the integrity of the accumulators or related libzerocoin zkp's are broken.
*
* @author     presstab https://github.com/presstab, random-zebra https://github.com/random-zebra
* @date       April 2019
*
* @copyright  Copyright 2019 The Veil Developers, Copyright 2019 The PIVX Developers
* @license    This project is released under the MIT license.
**/

#ifndef VEIL_PUBCOINSIGNATURE_H
#define VEIL_PUBCOINSIGNATURE_H

#include "bignum.h"
#include "Coin.h"
#include "Commitment.h"

namespace libzerocoin {

class PubcoinSignature
{
private:
    uint8_t m_version;
    const ZerocoinParams* m_params;
    CBigNum m_bnPubcoin;
    CBigNum m_bnC1Randomness;

public:
    static const uint8_t CURRENT_VERSION = 1;

    PubcoinSignature()
    {
        m_version = 0;
        m_bnPubcoin = CBigNum(0);
        m_bnC1Randomness = CBigNum(0);
    }

    PubcoinSignature(const ZerocoinParams* params, const CBigNum& bnPubcoin, const Commitment& C1);
    bool Verify(const CBigNum& bnC2, std::string& strError) const;
    CBigNum GetPubcoinValue() const { return m_bnPubcoin; }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(m_version);
        READWRITE(m_bnPubcoin);
        READWRITE(m_bnC1Randomness);
    }
};

}

#endif //VEIL_PUBCOINSIGNATURE_H
