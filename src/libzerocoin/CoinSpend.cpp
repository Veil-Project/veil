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
    CoinSpend::CoinSpend(const ZerocoinParams* paramsCoin, const ZerocoinParams* paramsAcc, const PrivateCoin& coin, Accumulator& a, const uint256& checksum,
                     const AccumulatorWitness& witness, const uint256& ptxHash, const SpendType& spendType, const uint8_t v) : accChecksum(checksum),
                                                                                  ptxHash(ptxHash),
                                                                                  coinSerialNumber(coin.getSerialNumber()),
                                                                                  accumulatorPoK(&paramsAcc->accumulatorParams),
                                                                                  commitmentPoK(&paramsCoin->serialNumberSoKCommitmentGroup,
                                                                                                &paramsAcc->accumulatorParams.accumulatorPoKCommitmentGroup),
                                                                                  version(v),
                                                                                  spendType(spendType)
{
    denomination = coin.getPublicCoin().getDenomination();

    if(version > V3_SMALL_SOK){
        throw std::runtime_error("Invalid coinSpend version");
    }

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
    const Commitment fullCommitmentToCoinUnderSerialParams(&paramsCoin->serialNumberSoKCommitmentGroup, coin.getPublicCoin().getValue());
    this->serialCommitmentToCoinValue = fullCommitmentToCoinUnderSerialParams.getCommitmentValue();

    const Commitment fullCommitmentToCoinUnderAccParams(&paramsAcc->accumulatorParams.accumulatorPoKCommitmentGroup, coin.getPublicCoin().getValue());
    this->accCommitmentToCoinValue = fullCommitmentToCoinUnderAccParams.getCommitmentValue();

    // 2. Generate a ZK proof that the two commitments contain the same public coin.
    this->commitmentPoK = CommitmentProofOfKnowledge(&paramsCoin->serialNumberSoKCommitmentGroup, &paramsAcc->accumulatorParams.accumulatorPoKCommitmentGroup, fullCommitmentToCoinUnderSerialParams, fullCommitmentToCoinUnderAccParams);

    // Now generate the two core ZK proofs:
    // 3. Proves that the committed public coin is in the Accumulator (PoK of "witness")
    this->accumulatorPoK = AccumulatorProofOfKnowledge(&paramsAcc->accumulatorParams, fullCommitmentToCoinUnderAccParams, witness, a);

    // 4. Proves that the coin is correct w.r.t. serial number and hidden coin secret
    // (This proof is bound to the coin 'metadata', i.e., transaction hash)
    hashSig = signatureHash();
    if (version < V3_SMALL_SOK) {
        this->serialNumberSoK =
                SerialNumberSignatureOfKnowledge(paramsCoin, coin, fullCommitmentToCoinUnderSerialParams, hashSig);
    } else {
        this->smallSoK =
                SerialNumberSoK_small(paramsCoin, coin, fullCommitmentToCoinUnderSerialParams, hashSig);
    }

    // 5. Sign the transaction using the private key associated with the serial number
    if (version >= PrivateCoin::PUBKEY_VERSION) {
        this->pubkey = coin.getPubKey();
        if (!coin.sign(hashSig, this->vchSig))
            throw std::runtime_error("Coinspend failed to sign signature hash");
    }
}

bool CoinSpend::Verify(const Accumulator& a, bool verifySoK) const
{
    // Double check that the version is the same as marked in the serial
    int serialVersion = ExtractVersionFromSerial(coinSerialNumber);
    if (serialVersion != version && (serialVersion == 1 || version == 1)) {
        //cout << "CoinSpend::Verify: version does not match serial=" << (int)ExtractVersionFromSerial(coinSerialNumber) << " actual=" << (int)version << endl;
        return false;
    }

    if (a.getDenomination() != this->denomination) {
        //std::cout << "CoinsSpend::Verify: failed, denominations do not match\n";
        return false;
    }

    // Verify both of the sub-proofs using the given meta-data
    if (!commitmentPoK.Verify(serialCommitmentToCoinValue, accCommitmentToCoinValue)) {
        //std::cout << "CoinsSpend::Verify: commitmentPoK failed\n";
        return false;
    }

    if (!accumulatorPoK.Verify(a, accCommitmentToCoinValue)) {
        //std::cout << "CoinsSpend::Verify: accumulatorPoK failed\n";
        return false;
    }

    if (verifySoK) {
        if (version < V3_SMALL_SOK) {
            if (!serialNumberSoK.Verify(coinSerialNumber, serialCommitmentToCoinValue, signatureHash())) {
                std::cout << "CoinsSpend::Verify: serialNumberSoK failed. sighash:" << signatureHash().GetHex() << "\n";
                return false;
            }
        } else {
            if (!smallSoK.Verify(coinSerialNumber, serialCommitmentToCoinValue, signatureHash())) {
                std::cout << "CoinsSpend::Verify: serialNumberSoK failed. sighash:" << signatureHash().GetHex() << "\n";
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
      << coinSerialNumber << accChecksum << denomination;

    if (version >= PrivateCoin::PUBKEY_VERSION)
        h << spendType;

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
    return IsValidSerial(params, coinSerialNumber);
}

//Additional verification layer that requires the spend be signed by the private key associated with the serial
bool CoinSpend::HasValidSignature() const
{
    //No private key for V1
    if (version < PrivateCoin::PUBKEY_VERSION)
        return true;

    //V2 serial requires that the signature hash be signed by the public key associated with the serial
    arith_uint256 hashedPubkey = UintToArith256(Hash(pubkey.begin(), pubkey.end())) >> PrivateCoin::V2_BITSHIFT;
    if (hashedPubkey != GetAdjustedSerial(coinSerialNumber).getarith_uint256()) {
        //cout << "CoinSpend::HasValidSignature() hashedpubkey is not equal to the serial!\n";
        return false;
    }

    return pubkey.Verify(signatureHash(), vchSig);
}

CBigNum CoinSpend::CalculateValidSerial(ZerocoinParams* params)
{
    CBigNum bnSerial = coinSerialNumber;
    bnSerial = bnSerial.mul_mod(CBigNum(1),params->coinCommitmentGroup.groupOrder);
    return bnSerial;
}

} /* namespace libzerocoin */
