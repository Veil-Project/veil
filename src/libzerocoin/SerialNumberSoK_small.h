/**
* @file       SerialNumberSoK_small.h
*
* @brief      SerialNumberSoK_small class for the Zerocoin library.
*
* @author     Mary Maller, Jonathan Bootle and Gian Piero Dionisio
* @date       April 2018
*
* @copyright  Copyright 2018 The PIVX Developers
* @license    This project is released under the MIT license.
**/

#ifndef SERIALNUMBERPROOFSMALL_H_
#define SERIALNUMBERPROOFSMALL_H_
#include <list>
#include <vector>
#include <bitset>
#include "Params.h"
#include "Coin.h"
#include "Commitment.h"
#include "bignum.h"
#include "serialize.h"
#include "Accumulator.h"
#include "hash.h"
#include "PolynomialCommitment.h"
#include "Bulletproofs.h"
#include <streams.h>

using namespace std;

namespace libzerocoin {

/**A Signature of knowledge on the hash of metadata attesting that the signer knows the values
 *  necessary to open a commitment which contains a coin(which it self is of course a commitment)
 * with a given serial number.
 */

class SerialNumberSoK_small {
public:
    /** Empty constructor */
    SerialNumberSoK_small(){};
    SerialNumberSoK_small(const ZerocoinParams* ZCp);

    /** Creates a Signature of knowledge object that a commitment to a coin contains a coin with serial number x
     *
     * @param p params
     * @param coin the coin we are going to prove the serial number of.
     * @param commitmentToCoin the commitment to the coin
     * @param msghash hash of meta data to create a signature of knowledge on.
     */
    SerialNumberSoK_small(const ZerocoinParams* ZCp, const PrivateCoin& coin, const Commitment& commitmentToCoin, uint256 msghash);

    /** Verifies the Signature of knowledge.
     *
     * @param msghash hash of meta data to create a signature of knowledge on.
     * @return
     */
    bool Verify(const CBigNum& coinSerialNumber, const CBigNum& valueOfCommitmentToCoin,const uint256 msghash) const;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>  inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(ComA);
        READWRITE(ComB);
        READWRITE(ComC);
        READWRITE(ComD);
        READWRITE(comRdash);
        READWRITE(polyComm.Tf);
        READWRITE(polyComm.Trho);
        READWRITE(polyComm.U);
        READWRITE(polyComm.tbar);
        READWRITE(polyComm.taubar);
        READWRITE(rho);
        READWRITE(innerProduct.pi);
        READWRITE(innerProduct.final_a);
        READWRITE(innerProduct.final_b);
    }

    friend class SerialNumberSoKProof;

private:
    const ZerocoinParams* params;

    // Commitments
    CBN_vector ComA;
    CBN_vector ComB;
    CBN_vector ComC;
    CBigNum ComD;
    CBigNum comRdash;

    // Blinder
    CBigNum rho;

    // Polynomial Commitment
    PolynomialCommitment polyComm;

    // Inner Product Proof
    Bulletproofs innerProduct;

};


struct fBE {
    fBE() {};
    CBigNum gamma;
    CBN_vector xlist;
    CBN_vector ymPowers;
    CBigNum a;
    CBigNum b;
};

struct SerialNumberSoKProof2 {
    SerialNumberSoKProof2(const SerialNumberSoK_small &sig, const CBigNum& coinSerial, const CBigNum& valueOfCommitment,
            const CBN_vector& xPowersPositive, const CBN_vector& xPowersNegative, const CBN_vector& YPowers, const CBN_vector& YDash, const CBN_vector& YmPowers ):
        signature(sig),
        coinSerialNumber(coinSerial),
        valueOfCommitmentToCoin(valueOfCommitment),
        xPowersPos(xPowersPositive),
        xPowersNeg(xPowersNegative),
        yPowers(YPowers),
        yDash(YDash),
        ymPowers(YmPowers),
        s_vec_2(ZKP_N, CBigNum(0))
{}
    SerialNumberSoK_small signature;
    CBigNum  coinSerialNumber;
    CBigNum valueOfCommitmentToCoin;
    CBN_vector xPowersPos, xPowersNeg;
    CBN_vector yPowers, yDash, ymPowers;
    // *** proof3 ***
    CBigNum z;
    // *** proof4 ***
    CBigNum ComR;
    // *** proof5 ***
    CBN_vector s_vec_2;
};

class SerialNumberSoKProof {
public:
    SerialNumberSoKProof(const ZerocoinParams* ZCp):
        signature(ZCp)
{};

    SerialNumberSoKProof(const SerialNumberSoK_small &sig, const CBigNum& coinSerial, const CBigNum& valueOfCommitment, const uint256 msg):
        signature(sig),
        coinSerialNumber(coinSerial),
        valueOfCommitmentToCoin(valueOfCommitment),
        msghash(msg)
{};
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>  inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(signature);
        READWRITE(coinSerialNumber);
        READWRITE(valueOfCommitmentToCoin);
        READWRITE(msghash);
    }
    SerialNumberSoK_small signature;
    CBigNum  coinSerialNumber;
    CBigNum valueOfCommitmentToCoin;
    uint256 msghash;

    static bool BatchVerify(std::vector<SerialNumberSoKProof> &proofs);
    static bool BatchBulletproofs(const CBN_matrix ck_inner_g, std::vector<SerialNumberSoKProof2> &proofs);
    static CBN_vector getFinal_gh(const ZerocoinParams* ZCp, CBN_vector gs, std::vector<fBE> forBigExpo);
};


} /* namespace libzerocoin */
#endif /* SERIALNUMBERPROOFSMALL_H_ */
