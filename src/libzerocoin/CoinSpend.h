/**
 * @file       CoinSpend.h
 *
 * @brief      CoinSpend class for the Zerocoin library.
 *
 * @author     Ian Miers, Christina Garman and Matthew Green
 * @date       June 2013
 *
 * @copyright  Copyright 2013 Ian Miers, Christina Garman and Matthew Green
 * @license    This project is released under the MIT license.
 **/
// Copyright (c) 2017-2018 The PIVX developers

#ifndef COINSPEND_H_
#define COINSPEND_H_

#include "Accumulator.h"
#include "AccumulatorProofOfKnowledge.h"
#include "Coin.h"
#include "Commitment.h"
#include "Params.h"
#include "PubcoinSignature.h"
#include "SerialNumberSignatureOfKnowledge.h"
#include "SpendType.h"

#include "bignum.h"
#include "pubkey.h"
#include "serialize.h"
#include "SerialNumberSoK_small.h"

namespace libzerocoin
{

/** The complete proof needed to spend a zerocoin.
 * Composes together a proof that a coin is accumulated
 * and that it has a given serial number.
 */
class CoinSpend
{
public:

    static int const V3_SMALL_SOK = 3;
    static int const V4_LIMP = 4;

    //! \param paramsV1 - if this is a V1 zerocoin, then use params that existed with initial modulus, ignored otherwise
    //! \param paramsV2 - params that begin when V2 zerocoins begin on the VEIL network
    //! \param strm - a serialized CoinSpend
    template <typename Stream>
    CoinSpend(const ZerocoinParams* params, Stream& strm) :
        accumulatorPoK(&params->accumulatorParams),
        smallSoK(params),
        commitmentPoK(&params->serialNumberSoKCommitmentGroup, &params->accumulatorParams.accumulatorPoKCommitmentGroup)
    {
        strm >> *this;
    }

    /**Generates a proof spending a zerocoin.
	 *
	 * To use this, provide an unspent PrivateCoin, the latest Accumulator
	 * (e.g from the most recent Bitcoin block) containing the public part
	 * of the coin, a witness to that, and whatever medeta data is needed.
	 *
	 * Once constructed, this proof can be serialized and sent.
	 * It is validated simply be calling validate.
	 * @warning Validation only checks that the proof is correct
	 * @warning for the specified values in this class. These values must be validated
	 *  Clients ought to check that
	 * 1) params is the right params
	 * 2) the accumulator actually is in some block
	 * 3) that the serial number is unspent
	 * 4) that the transaction
	 *
	 * @param p cryptographic parameters
	 * @param coin The coin to be spend
	 * @param a The current accumulator containing the coin
	 * @param witness The witness showing that the accumulator contains the coin
	 * @param a hash of the partial transaction that contains this coin spend
	 * @throw ZerocoinException if the process fails
	 */
    CoinSpend(const ZerocoinParams* params, const PrivateCoin& coin, Accumulator& a, const uint256& checksum,
              const AccumulatorWitness& witness, const uint256& ptxHash, const SpendType& spendType, const uint8_t version = (uint8_t) V3_SMALL_SOK);

    bool operator<(const CoinSpend& rhs) const { return this->getCoinSerialNumber() < rhs.getCoinSerialNumber(); }

    /** Returns the serial number of the coin spend by this proof.
	 *
	 * @return the coin's serial number
	 */
    const CBigNum& getCoinSerialNumber() const { return this->coinSerialNumber; }

    /**Gets the denomination of the coin spent in this proof.
	 *
	 * @return the denomination
	 */
    CoinDenomination getDenomination() const { return this->denomination; }

    /**Gets the checksum of the accumulator used in this proof.
	 *
	 * @return the checksum
	 */
    uint256 getAccumulatorChecksum() const { return this->accChecksum; }

    /**Gets the txout hash used in this proof.
	 *
	 * @return the txout hash
	 */
    uint256 getTxOutHash() const { return ptxHash; }
    uint256 getS1Size() const { return commitmentPoK.GetS1Size(); }
    CBigNum getAccCommitment() const { return accCommitmentToCoinValue; }
    CBigNum getSerialComm() const { return serialCommitmentToCoinValue; }
    SerialNumberSoK_small getSmallSoK() const { return smallSoK; }
    uint8_t getVersion() const { return version; }
    CBigNum getPubcoinValue() const;
    CPubKey getPubKey() const { return pubkey; }
    SpendType getSpendType() const { return spendType; }
    std::vector<unsigned char> getSignature() const { return vchSig; }
    uint256 getHashSig() {
        if (hashSig.IsNull()){
            hashSig = signatureHash();
        }
        return hashSig;
    }

    bool Verify(const Accumulator& a, std::string& strError, bool verifySoK = true, bool verifyPubcoin = false) const;
    bool HasValidSerial(ZerocoinParams* params) const;
    bool HasValidSignature() const;
    std::string ToString() const;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(version);
        READWRITE(spendType);
        READWRITE(pubkey);
        READWRITE(vchSig);
        READWRITE(denomination);
        READWRITE(ptxHash);
        READWRITE(accChecksum);
        READWRITE(coinSerialNumber);
        READWRITE(accCommitmentToCoinValue);
        READWRITE(serialCommitmentToCoinValue);
        READWRITE(accumulatorPoK);
        READWRITE(smallSoK);
        READWRITE(commitmentPoK);
        if (version == V4_LIMP) {
            READWRITE(pubcoinSig);
        }
    }

private:
    const uint256 signatureHash() const;
    CoinDenomination denomination;
    uint256 accChecksum;
    uint256 ptxHash;
    CBigNum accCommitmentToCoinValue;
    CBigNum serialCommitmentToCoinValue;
    CBigNum coinSerialNumber;
    AccumulatorProofOfKnowledge accumulatorPoK;
    SerialNumberSoK_small smallSoK;
    CommitmentProofOfKnowledge commitmentPoK;
    uint8_t version;

    //As of version 2
    CPubKey pubkey;
    std::vector<unsigned char> vchSig;
    SpendType spendType;

    // Cached hashSig
    uint256 hashSig;

    // Version 4 "Limp Mode"
    PubcoinSignature pubcoinSig;
};

} /* namespace libzerocoin */
#endif /* COINSPEND_H_ */
