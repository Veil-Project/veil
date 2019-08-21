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
    CBigNum m_bnRandomness; //randomness for v1 is c1 randomness. For v2 it is coin randomness.

    //Version 2
    uint256 m_hashTxFrom;
    unsigned int m_nOutpointPos;

public:
    static const uint8_t C1_VERSION = 1; //Still uses other ZKPs as dependencies to validity
    static const uint8_t PUBCOIN_VERSION = 2; //Directly checks pubcoin value for validity
    static const uint8_t CURRENT_VERSION = PUBCOIN_VERSION;

    PubcoinSignature(const ZerocoinParams* params)
    {
        SetNull();
        m_params = params;
    }
    void SetNull()
    {
        m_version = 0;
        m_bnPubcoin = CBigNum(0);
        m_bnRandomness = CBigNum(0);
        m_hashTxFrom = uint256();
        m_nOutpointPos = 0;
    }


    PubcoinSignature(const ZerocoinParams* params, const CBigNum& bnPubcoin, const Commitment& C1);
    PubcoinSignature(const ZerocoinParams* params, const CBigNum& bnPubcoin, const CBigNum& bnRandomness, const uint256& txidFrom, int vout);
    bool VerifyV1(const CBigNum& bnC1, std::string& strError) const;
    bool VerifyV2(const CBigNum& bnSerial, const CBigNum& bnPubcoin, std::string strError) const;
    CBigNum GetPubcoinValue() const { return m_bnPubcoin; }
    CBigNum GetRandomness() const { return m_bnRandomness; }
    bool GetMintOutpoint(uint256& txid, int& n) const;
    uint8_t GetVersion() const { return m_version; }
    void SetParams(const ZerocoinParams* params) { m_params = params; }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(m_version);
        READWRITE(m_bnPubcoin);
        READWRITE(m_bnRandomness);
        if (m_version >= PUBCOIN_VERSION) {
            READWRITE(m_hashTxFrom);
            READWRITE(m_nOutpointPos);
        }
    }
};

}

#endif //VEIL_PUBCOINSIGNATURE_H
