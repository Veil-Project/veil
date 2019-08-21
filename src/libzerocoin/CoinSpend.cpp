/**
 * @file       CoinSpend.cpp
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

#include "CoinSpend.h"
#include <iostream>
#include <sstream>

namespace libzerocoin
{
    CoinSpend::CoinSpend(const ZerocoinParams* params, const PrivateCoin& coin, Accumulator& a, const uint256& checksum,
                     const AccumulatorWitness& witness, const uint256& ptxHash, const SpendType& spendType, const uint8_t v,
                     bool fLightZerocoin, const uint256& txidMintFrom, int nOutputPos) : accChecksum(checksum),
                                                                                  ptxHash(ptxHash),
                                                                                  coinSerialNumber(coin.getSerialNumber()),
                                                                                  accumulatorPoK(&params->accumulatorParams),
                                                                                  commitmentPoK(&params->serialNumberSoKCommitmentGroup,
                                                                                                &params->accumulatorParams.accumulatorPoKCommitmentGroup),
                                                                                  version(v),
                                                                                  spendType(spendType),
                                                                                  pubcoinSig(params)
{
    denomination = coin.getPublicCoin().getDenomination();

    if (!fLightZerocoin) {
        // Sanity check: let's verify that the Witness is valid with respect to
        // the coin and Accumulator provided.
        if (!(witness.VerifyWitness(a, coin.getPublicCoin()))) {
            //std::cout << "CoinSpend: Accumulator witness does not verify\n";
            throw std::runtime_error("Accumulator witness does not verify");
        }
        // 1: Generate two separate commitments to the public coin (C), each under
        // a different set of public parameters. We do this because the RSA accumulator
        // has specific requirements for the commitment parameters that are not
        // compatible with the group we use for the serial number proof.
        // Specifically, our serial number proof requires the order of the commitment group
        // to be the same as the modulus of the upper group. The Accumulator proof requires a
        // group with a significantly larger order.
        const Commitment fullCommitmentToCoinUnderSerialParams(&params->serialNumberSoKCommitmentGroup,
                                                               coin.getPublicCoin().getValue());
        this->serialCommitmentToCoinValue = fullCommitmentToCoinUnderSerialParams.getCommitmentValue();

        const Commitment fullCommitmentToCoinUnderAccParams(&params->accumulatorParams.accumulatorPoKCommitmentGroup,
                                                            coin.getPublicCoin().getValue());
        this->accCommitmentToCoinValue = fullCommitmentToCoinUnderAccParams.getCommitmentValue();

        // 2. Generate a ZK proof that the two commitments contain the same public coin.
        this->commitmentPoK = CommitmentProofOfKnowledge(&params->serialNumberSoKCommitmentGroup,
                                                         &params->accumulatorParams.accumulatorPoKCommitmentGroup,
                                                         fullCommitmentToCoinUnderSerialParams,
                                                         fullCommitmentToCoinUnderAccParams);

        // Now generate the two core ZK proofs:
        // 3. Proves that the committed public coin is in the Accumulator (PoK of "witness")
        this->accumulatorPoK = AccumulatorProofOfKnowledge(&params->accumulatorParams,
                                                           fullCommitmentToCoinUnderAccParams, witness, a);

        // 4. Proves that the coin is correct w.r.t. serial number and hidden coin secret
        // (This proof is bound to the coin 'metadata', i.e., transaction hash)
        hashSig = signatureHash();
        this->smallSoK = SerialNumberSoK_small(params, coin, fullCommitmentToCoinUnderSerialParams, hashSig);
        this->pubkey = coin.getPubKey();
        if (!coin.sign(hashSig, this->vchSig))
            throw std::runtime_error("Coinspend failed to sign signature hash");
        if (version == V4_LIMP)
            pubcoinSig = PubcoinSignature(params, coin.getPublicCoin().getValue(), fullCommitmentToCoinUnderSerialParams);
    } else {
        hashSig = signatureHash();
        this->pubkey = coin.getPubKey();
        if (!coin.sign(hashSig, this->vchSig))
            throw std::runtime_error("Coinspend failed to sign signature hash");
        pubcoinSig = PubcoinSignature(params, coin.getPublicCoin().getValue(), coin.getRandomness(), txidMintFrom, nOutputPos);
    }
}

bool CoinSpend::Verify(const Accumulator& a, std::string& strError, const CBigNum& bnPubcoin, bool verifySoK, bool verifyPubcoin, bool verifyZKP) const
{
    // Double check that the version is the same as marked in the serial
    //! Only should verify ZKP before zerocoin light mode!!
    if (verifyZKP) {
        if (a.getDenomination() != this->denomination) {
            strError = "CoinsSpend::Verify: failed, denominations do not match";
            return false;
        }

        // Verify both of the sub-proofs using the given meta-data
        if (!commitmentPoK.Verify(serialCommitmentToCoinValue, accCommitmentToCoinValue)) {
            strError = "CoinsSpend::Verify: commitmentPoK failed";
            return false;
        }

        if (!accumulatorPoK.Verify(a, accCommitmentToCoinValue)) {
            strError = "CoinsSpend::Verify: accumulatorPoK failed";
            return false;
        }

        if (verifySoK) {
            if (!smallSoK.Verify(coinSerialNumber, serialCommitmentToCoinValue, signatureHash())) {
                strError = "CoinsSpend::Verify: serialNumberSoK failed. sighash:";
                strError += signatureHash().GetHex();
                return false;
            }
        }
    }

    if (verifyPubcoin) {
        if (version != V4_LIMP) {
            strError = "CoinSpend::Verify version is not V4_LIMP";
            return false;
        }
        std::string err;
        if (verifyZKP) {
            if (pubcoinSig.GetVersion() != PubcoinSignature::C1_VERSION) {
                strError = "expected pubcoinsig V1, but is the wrong version";
                return false;
            }

            if (!pubcoinSig.VerifyV1(serialCommitmentToCoinValue, err)) {
                strError = std::string("CoinSpend::VerifyV1 pubcoin signature is invalid: ") + err;
                return false;
            }
        } else {
            //! Light Mode validation
            if (pubcoinSig.GetVersion() != PubcoinSignature::PUBCOIN_VERSION) {
                strError = "expected pubcoinsig V2, but is the wrong version";
                return false;
            }

            if (!pubcoinSig.VerifyV2(coinSerialNumber, bnPubcoin, strError)) {
                strError = std::string("CoinSpend::VerifyV2 pubcoin signature is invalid: ") + err;
                return false;
            }
        }
    }

    return true;
}

const uint256 CoinSpend::signatureHash() const
{
    CHashWriter h(0, 0);
    h << serialCommitmentToCoinValue << accCommitmentToCoinValue << commitmentPoK << accumulatorPoK << ptxHash
      << coinSerialNumber << accChecksum << denomination << spendType;

    return h.GetHash();
}

std::string CoinSpend::ToString() const
{
    std::stringstream ss;
    ss << "CoinSpend:\n "
          "  version=" << (int)version <<
          "  signatureHash=" << signatureHash().GetHex() <<
          "  spendtype=" << spendType <<
          "  serial=" << coinSerialNumber.GetHex() << "\n";
    return ss.str();
}

bool CoinSpend::HasValidSerial(ZerocoinParams* params) const
{
    if (coinSerialNumber.bitSize() > 256)
        return false;

    return IsValidSerial(params, coinSerialNumber);
}

//Additional verification layer that requires the spend be signed by the private key associated with the serial
bool CoinSpend::HasValidSignature() const
{
    //V2 serial requires that the signature hash be signed by the public key associated with the serial
    arith_uint256 hashedPubkey = UintToArith256(Hash(pubkey.begin(), pubkey.end())) >> PrivateCoin::V2_BITSHIFT;
    if (hashedPubkey != GetAdjustedSerial(coinSerialNumber).getarith_uint256())
        return false;

    return pubkey.Verify(signatureHash(), vchSig);
}

CBigNum CoinSpend::getPubcoinValue() const
{
    return pubcoinSig.GetPubcoinValue();
}

} /* namespace libzerocoin */
