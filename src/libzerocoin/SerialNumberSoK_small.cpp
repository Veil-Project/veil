/**
* @file       SerialNumberSoK_small.cpp
*
* @brief      SerialNumberSoK_small class for the Zerocoin library.
*
* @author     Mary Maller, Jonathan Bootle and Gian Piero Dionisio
* @date       April 2018
*
* @copyright  Copyright 2018 The PIVX Developers
* @license    This project is released under the MIT license.
**/
#include <streams.h>
#include "ArithmeticCircuit.h"
#include "SerialNumberSoK_small.h"
//#include <time.h>

namespace libzerocoin {

SerialNumberSoK_small::SerialNumberSoK_small(const ZerocoinParams* ZCp) :
                params(ZCp),
                ComA(ZKP_M),
                ComB(ZKP_M),
                ComC(ZKP_M),
                polyComm(ZCp),
                innerProduct(ZCp)
{ }


SerialNumberSoK_small::SerialNumberSoK_small(const ZerocoinParams* ZCp, const PrivateCoin& coin,
        const Commitment& commitmentToCoin, uint256 msghash) :
                params(ZCp),
                ComA(ZKP_M),
                ComB(ZKP_M),
                ComC(ZKP_M),
                polyComm(ZCp),
                innerProduct(ZCp)

{
    // ---------------------------------- **** SoK PROVE **** -----------------------------------
    // ------------------------------------------------------------------------------------------
    // Specifies how a spender should produce the signature of knowledge on a message msghash that
    // he knows v such that commitmentToCoin is a commitment to a^S b^v.
    // @param   coin                :  The PrivateCoin ww are committing to
    // @param   commitmentToCoin    :  commitment (y1)
    // @param   msghash             :  a message hash to sign
    // @init    SoK

    const CBigNum a = params->coinCommitmentGroup.g;
    const CBigNum b = params->coinCommitmentGroup.h;
    const CBigNum g = params->serialNumberSoKCommitmentGroup.g;
    const CBigNum h = params->serialNumberSoKCommitmentGroup.h;
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const CBigNum p = params->serialNumberSoKCommitmentGroup.modulus;
    const CBigNum y1 = commitmentToCoin.getCommitmentValue();
    const CBigNum S = coin.getSerialNumber();
    const CBigNum v = coin.getRandomness();
    const int m = ZKP_M;
    const int n = ZKP_N;
    const int N = ZKP_SERIALSIZE;
    const int m1dash = ZKP_M1DASH;
    const int m2dash = ZKP_M2DASH;
    const int ndash = ZKP_NDASH;
    const int pads = ZKP_PADS;

    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_a1(params->S_POLY_A1);
    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_a2(params->S_POLY_A2);
    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_b1(params->S_POLY_B1);
    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_b2(params->S_POLY_B2);
    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_c1(params->S_POLY_C1);
    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_c2(params->S_POLY_C2);


    // ****************************************************************************
    // ********************** STEP 1: Generate Commitments ************************
    // ****************************************************************************

    // Select blinding vectors alpha, beta, gamma, D, delta
    CBN_vector f_alpha(m);
    CBN_vector f_beta(m);
    CBN_vector f_gamma(m);
    CBN_vector D(n);
    CBigNum f_delta = CBigNum::randBignum(q);

    random_vector_mod(f_alpha, q);
    random_vector_mod(f_beta, q);
    random_vector_mod(f_gamma, q);
    random_vector_mod(D, q);

    // set arithmetic circuit wire values and constraints
    ArithmeticCircuit circuit(params);
    circuit.setWireValues(coin);

    // Commit to the assignment of the circuit: ComA[i] = pedersenCommitment(params, A[i], f_alpha[i]);
    transform(circuit.A.begin(), circuit.A.end(), f_alpha.begin(), ComA.begin(),
            [=] (CBN_vector A, CBigNum alpha) {
        return pedersenCommitment(params, A, alpha);} );

    transform(circuit.B.begin(), circuit.B.end(), f_beta.begin(), ComB.begin(),
            [=] (CBN_vector B, CBigNum beta) {
        return pedersenCommitment(params, B, beta);} );

    transform(circuit.C.begin(), circuit.C.end(), f_gamma.begin(), ComC.begin(),
            [=] (CBN_vector C, CBigNum gamma) {
        return pedersenCommitment(params, C, gamma);} );

    ComD = pedersenCommitment(params, D, f_delta);

    // replace commitment y1 and blind value r
    ComC[m-1] = y1;
    f_gamma[m-1] = commitmentToCoin.getRandomness();


    // ****************************************************************************
    // ************* STEP 2: Challenge component + eval w-polynomials *************
    // ****************************************************************************

    CHashWriter1024 hasher(0,0);
    hasher << msghash << ComD.ToString();

    for(unsigned int i=0; i<m; i++)
        hasher << ComA[i].ToString() << ComB[i].ToString() << ComC[i].ToString();

    // get the challenge component y
    CBigNum y = CBigNum(hasher.GetHash() )% q;

    // set circuit w-Polynomials
    circuit.setYPoly(y);

    // verify correct assignment of circuit values
    // !TODO: skip this for efficiency?
    //circuit.check();


    // ****************************************************************************
    // ************************ STEP 3: Laurent polynomial ************************
    // ****************************************************************************

    // rPoly
    CBN_matrix rPolyPositive(1, CBN_vector(n, CBigNum(0)));
    CBN_matrix rPolyNegative(1, CBN_vector(n, CBigNum(0)));

    for(int i=0; i<m; i++) {
        rPolyPositive.push_back( vectorTimesConstant(circuit.A[i], y.pow_mod(i+1,q), q) );
        rPolyNegative.push_back( circuit.B[i] );
    }

    for(int i=0; i<m; i++) {
        rPolyPositive.push_back( circuit.C[i] );
        rPolyNegative.push_back( CBN_vector(n, CBigNum(0)) );
    }

    rPolyPositive.push_back( D );
    rPolyNegative.push_back( CBN_vector(n, CBigNum(0)) );


    // sPoly
    CBN_matrix sPolyPositive(1, CBN_vector(n, CBigNum(0)));
    CBN_matrix sPolyNegative(1, CBN_vector(n, CBigNum(0)));
    CBN_vector temp1, temp2;
    CBigNum coef1, coef2;
    CBigNum duo1;
    int duo0;

    for(unsigned int i=0; i<s_poly_b1.size(); i++) {
        coef1 = CBigNum(0);
        coef2 = CBigNum(0);

        for(unsigned int j=0; j<s_poly_b1[i].size(); j++) {
            duo0 = s_poly_b1[i][j].first;
            duo1 = s_poly_b1[i][j].second;
            coef1 = (coef1 + duo1.mul_mod(circuit.YPowers[duo0+1+4*N+m],q)) % q;
        }

        for(unsigned int j=0; j<s_poly_b2[i].size(); j++) {
            duo0 = s_poly_b2[i][j].first;
            duo1 = s_poly_b2[i][j].second;
            coef2 = (coef2 + duo1.mul_mod(circuit.YPowers[duo0+1+4*N+m],q)) % q;
        }

        temp1.push_back(coef1);
        temp2.push_back(coef2);
    }

    sPolyPositive.push_back(temp1);
    sPolyPositive.push_back(temp2);
    sPolyPositive.push_back(CBN_vector(n, CBigNum(0)));
    sPolyPositive.push_back(CBN_vector(n, CBigNum(0)));


    temp1.clear();
    temp2.clear();
    for(unsigned int i=0; i<s_poly_a1.size(); i++) {
        coef1 = CBigNum(0);
        coef2 = CBigNum(0);

        for(unsigned int j=0; j<s_poly_a1[i].size(); j++) {
            duo0 = s_poly_a1[i][j].first;
            duo1 = s_poly_a1[i][j].second;
            coef1 = (coef1 + duo1.mul_mod(circuit.YPowers[duo0+4*N+m],q)) % q;
        }

        for(unsigned int j=0; j<s_poly_a2[i].size(); j++) {
            duo0 = s_poly_a2[i][j].first;
            duo1 = s_poly_a2[i][j].second;
            coef2 = (coef2 + duo1.mul_mod(circuit.YPowers[duo0-1+4*N+m],q)) % q;
        }

        temp1.push_back(coef1);
        temp2.push_back(coef2);
    }

    sPolyNegative.push_back(temp1);
    sPolyNegative.push_back(temp2);


    temp1.clear();
    temp2.clear();
    for(unsigned int i=0; i<s_poly_c1.size(); i++) {
        coef1 = (- circuit.YPowers[2*(i+1)+1]) % q;
        coef2 = (- circuit.YPowers[2*(i+1)+2]) % q;

        for(unsigned int j=0; j<s_poly_c1[i].size(); j++) {
            duo0 = s_poly_c1[i][j].first;
            duo1 = s_poly_c1[i][j].second;
            coef1 = (coef1 + duo1.mul_mod(circuit.YPowers[duo0+1+4*N+m],q)) % q;
        }

        for(unsigned int j=0; j<s_poly_c2[i].size(); j++) {
            duo0 = s_poly_c2[i][j].first;
            duo1 = s_poly_c2[i][j].second;
            coef2 = (coef2 + duo1.mul_mod(circuit.YPowers[duo0+1+4*N+m],q)) % q;
        }

        temp1.push_back(coef1);
        temp2.push_back(coef2);
    }

    sPolyNegative.push_back(temp1);
    sPolyNegative.push_back(temp2);


    // rDashPoly
    CBN_matrix rDashPolyPositive(2*m+2, CBN_vector(n));
    CBN_matrix rDashPolyNegative(2*m+2, CBN_vector(n));

    fill(rDashPolyPositive[0].begin(), rDashPolyPositive[0].end(), CBigNum(0));
    fill(rDashPolyNegative[0].begin(), rDashPolyNegative[0].end(), CBigNum(0));


    for(int i=1; i<2*m+1; i++)
        for(int j=0; j<n; j++) {
            rDashPolyPositive[i][j] = rPolyPositive[i][j].mul_mod(circuit.YDash[j],q);
            rDashPolyPositive[i][j] = (rDashPolyPositive[i][j] + 2 * sPolyPositive[i][j]) % q;
            rDashPolyNegative[i][j] = rPolyNegative[i][j].mul_mod(circuit.YDash[j],q);
            rDashPolyNegative[i][j] = (rDashPolyNegative[i][j] + 2 * sPolyNegative[i][j]) % q;
        }

    for(int j=0; j<n; j++)
            rDashPolyPositive[2*m+1][j] = D[j].mul_mod(circuit.YDash[j],q);

    fill(rDashPolyNegative[2*m+1].begin(), rDashPolyNegative[2*m+1].end(), CBigNum(0));


    // tPoly
    CBN_vector tPoly(7*m+3);
    CBigNum tcoef;
    CBN_vector *oper1, *oper2;

    for(int k=0; k<7*m+3; k++) {
        tcoef = CBigNum(0);
        for(int i=max(k-5*m-1,-m); i<min(k-m,2*m+1)+1; i++) {
            int j = k - 3*m - i;
            oper1 = i > 0 ? &rPolyPositive[i] : &rPolyNegative[-i];
            oper2 = j > 0 ? &rDashPolyPositive[j] : &rDashPolyNegative[-j];
            tcoef += dotProduct(*oper1, *oper2, q);
            tcoef %=q;
        }
        tPoly[k] = tcoef;
    }

    // sanity check
    if (tPoly[3*m] != (2*circuit.Kconst)%q)
        throw std::runtime_error("SerialNumberSoK_small - error: sanity check failed");


    tPoly[3*m] = CBigNum(0);

    // commit to the polynomial
    polyComm.Commit(tPoly);


    // ****************************************************************************
    // *********************** STEP 4: Challenge Component ************************
    // ****************************************************************************

    CHashWriter1024 hasher2(0,0);
    hasher2 << polyComm.U.ToString();
    for(unsigned int i=0; i<m1dash; i++) hasher2 << polyComm.Tf[i].ToString();
    for(unsigned int i=0; i<m1dash; i++) hasher2 << polyComm.Trho[i].ToString();

    // get the challenge component x
    CBigNum x = CBigNum(hasher2.GetHash()) % q;


    // precomputation of x powers
    CBN_vector xPowersPos(m2dash*ndash+1);
    CBN_vector xPowersNeg(m1dash*ndash+1);
    xPowersPos[0] = xPowersNeg[0] = CBigNum(1);
    xPowersPos[1] = x;
    xPowersNeg[1] = x.pow_mod(-1,q);
    for(int i=2; i<m2dash*ndash+1; i++)
        xPowersPos[i] = xPowersPos[i-1].mul_mod(x,q);
    for(int i=2; i<m1dash*ndash+1; i++)
        xPowersNeg[i] = xPowersNeg[i-1].mul_mod(xPowersNeg[1],q);


    // ****************************************************************************
    // **************************** STEP 5: Poly Eval *****************************
    // ****************************************************************************

    // evaluate the polynomial at x
    polyComm.Eval(xPowersPos, xPowersNeg);

    CBN_vector r_vec(n+pads, CBigNum(0));

    for(unsigned int j=0; j<n; j++){
        for(unsigned int rcoef=0; rcoef<rPolyNegative.size(); rcoef++) {
            r_vec[j] +=
                    rPolyPositive[rcoef][j].mul_mod(xPowersPos[rcoef],q) +
                    rPolyNegative[rcoef][j].mul_mod(xPowersNeg[rcoef],q);
            r_vec[j] %= q;
        }

    }

    CBN_vector s_vec(n+pads, CBigNum(0));

    for(unsigned int j=0; j<n; j++){
        for(unsigned int scoef=0; scoef<sPolyNegative.size(); scoef++) {
            s_vec[j] +=
                    sPolyPositive[scoef][j].mul_mod(xPowersPos[scoef],q) +
                    sPolyNegative[scoef][j].mul_mod(xPowersNeg[scoef],q);
            s_vec[j] %= q;
        }

    }

    rho = f_delta.mul_mod(xPowersPos[2*m+1],q);


    for(unsigned int i=1; i<m+1; i++) {
        rho +=
                f_alpha[i-1].mul_mod(xPowersPos[i].mul_mod(circuit.YPowers[i],q),q) +
                f_beta[i-1].mul_mod(xPowersNeg[i],q) +
                f_gamma[i-1].mul_mod(xPowersPos[m+i],q);
        rho %= q;
    }


    // ****************************************************************************
    // ********************** STEP 6: Inner Product Argument **********************
    // ****************************************************************************

    CBN_vector temp_vec;
    hadamard(temp_vec, circuit.YDash, r_vec, q);
    CBN_vector temp_vec2;
    for(unsigned j=0; j<s_vec.size(); j++)
        temp_vec2.push_back(s_vec[j].mul_mod(CBigNum(2), q));

    CBN_vector rdash_vec1;
    addVectors_mod(rdash_vec1, temp_vec, temp_vec2, q);

    temp_vec.clear();
    hadamard(temp_vec, circuit.y_vec_neg, s_vec, q);

    CBN_vector rdash_vec2;
    addVectors_mod(rdash_vec2, r_vec, temp_vec, q);


    // Inner-product PROVE
    CBigNum ComR = pedersenCommitment(params, r_vec, CBigNum(0));
    comRdash = pedersenCommitment(params, rdash_vec2, CBigNum(0));

    CBigNum Pinner = ComR.mul_mod(comRdash, p);

    CBigNum z = dotProduct(r_vec, rdash_vec1, q);


    CBN_matrix ck_inner_g = ck_inner_gen(params);

    CBN_matrix r1(1, CBN_vector(r_vec));
    CBN_matrix r2(1, CBN_vector(rdash_vec1));
    innerProduct.Prove(ck_inner_g, Pinner, z, r1, r2, y);

    // Remove y1 from ComC
    ComC.pop_back();
}


bool SerialNumberSoK_small::Verify(const CBigNum& coinSerialNumber,
        const CBigNum& valueOfCommitmentToCoin, const uint256 msghash) const
{
    auto proof = SerialNumberSoKProof(*this, coinSerialNumber, valueOfCommitmentToCoin, msghash);
    std::vector<const SerialNumberSoKProof*> vproof{&proof};

    uint8_t nReturn;
    return SerialNumberSoKProof::BatchVerify(vproof);

}

bool SerialNumberSoKProof::BatchVerify(std::vector<const SerialNumberSoKProof*> &proofs, uint8_t* nReturn)
{
    if (!BatchVerify(proofs)) {
        *nReturn = 0;
        return false;
    }

    *nReturn = 1;
    return true;
}

bool SerialNumberSoKProof::BatchVerify(std::vector<const SerialNumberSoKProof*> &proofs) {
    const CBigNum q = proofs[0]->signature.params->serialNumberSoKCommitmentGroup.groupOrder;
    const CBigNum p = proofs[0]->signature.params->serialNumberSoKCommitmentGroup.modulus;
    const int m =  ZKP_M;
    const int n =  ZKP_N;
    const int N = ZKP_SERIALSIZE;
    const int m1dash =  ZKP_M1DASH;
    const int m2dash =  ZKP_M2DASH;
    const int ndash =  ZKP_NDASH;
    const int pads = ZKP_PADS;
    const CBigNum bnZero(0);
    const CBigNum bnOne(1);

    // ****************************************************************************
    // **************************** STEP 1: Parsing *******************************
    // ****************************************************************************

    CBigNum ny;
    CBigNum temp;
    CBigNum y1;

    std::vector<SerialNumberSoKProof2> proofs2;

    for(unsigned int w=0; w<proofs.size(); w++)
    {
        const uint256& msghash = proofs[w]->msghash;
        const CBigNum& S = proofs[w]->coinSerialNumber;
        y1 = proofs[w]->valueOfCommitmentToCoin;
        const auto& ComA = proofs[w]->signature.ComA;
        const CBN_vector& ComB = proofs[w]->signature.ComB;
        const CBN_vector& ComC = proofs[w]->signature.ComC;
        const CBigNum& ComD = proofs[w]->signature.ComD;
        const CBigNum& comRdash  = proofs[w]->signature.comRdash;
        const CBigNum& rho = proofs[w]->signature.rho;
        const PolynomialCommitment* polyComm = &proofs[w]->signature.polyComm;
        const Bulletproofs* innerProduct = &proofs[w]->signature.innerProduct;

        // Restore y1 in ComC
        CBN_vector ComC_(ComC);
        ComC_.push_back(y1);

        // Assert inputs in correct groups
        if( S < bnZero || S > CBigNum(2).pow(256))
            return error("wrong value for S");

        if( ComD < bnZero || ComD > p )
            return error("wrong value for ComD");

        for(int i=0; i<m; i++) {
            if( ComA[i] < bnZero || ComA[i] > p )
                return error("wrong value for ComA at %d", i);
            if( ComB[i] < bnZero || ComB[i] > p )
                return error("wrong value for ComB at %d", i);
            if( ComC_[i] < bnZero || ComC_[i] > p )
                return error("wrong value for ComC at %d", i);
        }

        if( comRdash < bnZero || comRdash > p )
            return error("wrong value for comRdash");

        for(int i=0; i<m1dash; i++)
            if( polyComm->Tf[i] < bnZero || polyComm->Tf[i] > p )
                return error("wrong value for Tf at %d", i);

        for(int i=0; i<m2dash; i++)
            if( polyComm->Trho[i] < bnZero || polyComm->Trho[i] > p )
                return error("wrong value for Trho at %d", i);

        if( polyComm->U < bnZero || polyComm->U > p )
            return error("wrong value for U");

        for(int i=0; i<ndash; i++)
            if( polyComm->tbar[i] < bnZero || polyComm->tbar[i] > q )
                return error("wrong value for tbar at %d", i);

        if( polyComm->taubar < bnZero || polyComm->taubar > q )
            return error("wrong value for taubar");

        for(int j=0; j<(int)innerProduct->pi[0].size(); j++)
            if( innerProduct->pi[0][j] < bnZero || innerProduct->pi[0][j] > p )
                return error("wrong value for pi[0] at j=%d", j);

        for(int j=0; j<(int)innerProduct->pi[1].size(); j++)
            if( innerProduct->pi[1][j] < bnZero || innerProduct->pi[1][j] > p )
                return error("wrong value for pi[1] at j=%d", j);

        const int M1 = innerProduct->final_a.size();
        const int N1 = innerProduct->final_a[0].size();

        for(int i=0; i<M1; i++)
            for(int j=0; j<N1; j++) {
                if( innerProduct->final_a[i][j] < bnZero || innerProduct->final_a[i][j] > q )
                    return error("wrong value for final_a at [%d, %d]", i, j);
                if( innerProduct->final_b[i][j] < bnZero || innerProduct->final_b[i][j] > q )
                    return error("wrong value for final_b at [%d, %d]", i, j);
            }




        // ****************************************************************************
        // *********************** STEP 2: Compute Challenges *************************
        // ****************************************************************************

        CHashWriter1024 hasher(0,0);
        hasher << msghash << ComD.ToString();

        for(int i=0; i<m; i++)
            hasher << ComA[i].ToString() << ComB[i].ToString() << ComC_[i].ToString();

        // get the challenge component y
        CBigNum y = CBigNum(hasher.GetHash()) % q;

        CHashWriter1024 hasher2(0,0);

        hasher2 << polyComm->U.ToString();
        for(int i=0; i<m1dash; i++) hasher2 << polyComm->Tf[i].ToString();
        for(int i=0; i<m1dash; i++) hasher2 << polyComm->Trho[i].ToString();

        // get the challenge component x
        CBigNum x = CBigNum(hasher2.GetHash()) % q;

        // precomputation of x powers
        CBN_vector xPowersPos(m2dash*ndash+1);
        CBN_vector xPowersNeg(m1dash*ndash+1);
        xPowersPos[0] = xPowersNeg[0] = bnOne;
        xPowersPos[1] = x;
        xPowersNeg[1] = x.pow_mod(-1,q);
        for(int i=2; i<m2dash*ndash+1; i++)
            xPowersPos[i] = xPowersPos[i-1].mul_mod(x,q);
        for(int i=2; i<m1dash*ndash+1; i++)
            xPowersNeg[i] = xPowersNeg[i-1].mul_mod(xPowersNeg[1],q);


        // set ymPowers
        ny = y.pow_mod(-ZKP_M, q);
        CBN_vector ymPowers(1, bnOne);
        temp = bnOne;

        for(unsigned int i=0; i<n+pads; i++) {
            temp = temp.mul_mod(ny, q);
            ymPowers.push_back(temp);
        }

        // set yPowers
        CBN_vector yPowers(1, bnOne);
        temp = bnOne;

        for(unsigned int i=0; i<8*N+m+1; i++) {
            temp = temp.mul_mod(y, q);
            yPowers.push_back(temp);
        }

        // set yDash
        CBN_vector yDash;
        for(unsigned int i=1; i<n+1; i++)
            yDash.push_back(yPowers[m*i]);

        // append the proof
        SerialNumberSoKProof2 newProof(proofs[w]->signature, S, y1, xPowersPos, xPowersNeg, yPowers, yDash, ymPowers);
        proofs2.push_back(newProof);

    }


    // ****************************************************************************
    // ************************* STEP 3: Check PolyVerify *************************
    // ****************************************************************************

    const ZerocoinParams *params;

    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_a1;
    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_a2;
    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_b1;
    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_b2;
    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_c1;
    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_c2;

    int duo0;
    CBigNum duo1;
    CBN_vector xPowersPositive, xPowersNegative, yPowers;

    CBN_vector test_vec(n, CBigNum(0));
    CBigNum comTest = CBigNum(1);
    CBigNum gamma;

    for(unsigned int w=0; w<proofs2.size(); w++)
    {
        const auto& S = proofs2[w].coinSerialNumber;
        y1 = proofs2[w].valueOfCommitmentToCoin;
        const auto& ComA = proofs2[w].signature.ComA;
        const auto& ComB = proofs2[w].signature.ComB;
        const auto& ComC = proofs2[w].signature.ComC;
        const auto& ComD = proofs2[w].signature.ComD;
        const auto& comRdash  = proofs2[w].signature.comRdash;
        const auto& rho = proofs2[w].signature.rho;
        const auto* polyComm = &proofs2[w].signature.polyComm;
        const auto* innerProduct = &proofs2[w].signature.innerProduct;

        // Restore y1 in ComC
        CBN_vector ComC_(ComC);
        ComC_.push_back(y1);
        params = proofs2[w].signature.params;
        polyComm = &proofs2[w].signature.polyComm;

        // set arithmetic circuit
        ArithmeticCircuit circuit(proofs2[0].signature.params);
        circuit.set_Kconst(proofs2[w].yPowers, S);

        // restore PolynomialCommitment object from commitments
        PolynomialCommitment polyCommitment(params, polyComm->Tf, polyComm->Trho, polyComm->U,
                polyComm->tbar, polyComm->taubar, proofs2[w].xPowersPos, proofs2[w].xPowersNeg);

        // verify the polynomial commitment and save the evaluation in z
        CBigNum z;
        if(!polyCommitment.Verify(z)) {
            std::cout << "Polynomial Commitment Verification failed for proof n. " << w << std::endl;
            return false;
        }

        // verify the inner product argument
        z = (z + 2*circuit.Kconst) % q;

        // append proof3
        proofs2[w].z = z;



        // ****************************************************************************
        // ***************************** STEP 4: Find ComR ****************************
        // ****************************************************************************
        CBigNum ComR = pedersenCommitment(params, CBN_vector(1, bnZero), -rho);
        ComR = ComR.mul_mod(ComD.pow_mod(proofs2[w].xPowersPos[2*m+1],p),p);

        for(int i=1; i<m+1; i++) {
            ComR = ComR.mul_mod(ComA[i-1].pow_mod(proofs2[w].xPowersPos[i].mul_mod(proofs2[w].yPowers[i],q),p),p);
            ComR = ComR.mul_mod(ComB[i-1].pow_mod(proofs2[w].xPowersNeg[i],p),p);
            ComR = ComR.mul_mod(ComC_[i-1].pow_mod(proofs2[w].xPowersPos[m+i],p),p);
        }

        // append proof4
        proofs2[w].ComR = ComR;


        // ****************************************************************************
        // *************************** STEP 5: Find s_vec_2 ***************************
        // ****************************************************************************

        s_poly_a1 = params->S_POLY_A1;
        s_poly_a2 = params->S_POLY_A2;
        s_poly_b1 = params->S_POLY_B1;
        s_poly_b2 = params->S_POLY_B2;
        s_poly_c1 = params->S_POLY_C1;
        s_poly_c2 = params->S_POLY_C2;
        xPowersPositive = proofs2[w].xPowersPos;
        xPowersNegative = proofs2[w].xPowersNeg;
        yPowers = proofs2[w].yPowers;

        for(int i=0; i<(int)s_poly_b1.size(); i++) {

            for(int j=0; j<(int)s_poly_b1[i].size(); j++) {
                duo0 = s_poly_b1[i][j].first;
                duo1 = s_poly_b1[i][j].second;
                proofs2[w].s_vec_2[i] = (proofs2[w].s_vec_2[i] + duo1.mul_mod(yPowers[duo0+1+4*N+m],q).mul_mod(xPowersPositive[1],q)) % q;
            }

            for(int j=0; j<(int)s_poly_b2[i].size(); j++) {
                duo0 = s_poly_b2[i][j].first;
                duo1 = s_poly_b2[i][j].second;
                proofs2[w].s_vec_2[i] = (proofs2[w].s_vec_2[i] + duo1.mul_mod(yPowers[duo0+1+4*N+m],q).mul_mod(xPowersPositive[2],q)) % q;
            }

            for(int j=0; j<(int)s_poly_a1[i].size(); j++) {
                duo0 = s_poly_a1[i][j].first;
                duo1 = s_poly_a1[i][j].second;
                proofs2[w].s_vec_2[i] = (proofs2[w].s_vec_2[i] + duo1.mul_mod(yPowers[duo0+4*N+m],q).mul_mod(xPowersNegative[1],q)) % q;
            }

            for(int j=0; j<(int)s_poly_a2[i].size(); j++) {
                duo0 = s_poly_a2[i][j].first;
                duo1 = s_poly_a2[i][j].second;
                proofs2[w].s_vec_2[i] = (proofs2[w].s_vec_2[i] + duo1.mul_mod(yPowers[duo0-1+4*N+m],q).mul_mod(xPowersNegative[2],q)) % q;
            }

            proofs2[w].s_vec_2[i] = (proofs2[w].s_vec_2[i] - yPowers[2*(i+1)+1].mul_mod(xPowersNegative[3],q)) % q;
            proofs2[w].s_vec_2[i] = (proofs2[w].s_vec_2[i] - yPowers[2*(i+1)+2].mul_mod(xPowersNegative[4],q)) % q;

            for(int j=0; j<(int)s_poly_c1[i].size(); j++) {
                duo0 = s_poly_c1[i][j].first;
                duo1 = s_poly_c1[i][j].second;
                proofs2[w].s_vec_2[i] = (proofs2[w].s_vec_2[i] + duo1.mul_mod(yPowers[duo0+1+4*N+m],q).mul_mod(xPowersNegative[3],q)) % q;
            }

            for(int j=0; j<(int)s_poly_c2[i].size(); j++) {
                duo0 = s_poly_c2[i][j].first;
                duo1 = s_poly_c2[i][j].second;
                proofs2[w].s_vec_2[i] = (proofs2[w].s_vec_2[i] + duo1.mul_mod(yPowers[duo0+1+4*N+m],q).mul_mod(xPowersNegative[4],q)) % q;
            }

            // append proof5
            proofs2[w].s_vec_2[i] = proofs2[w].s_vec_2[i].mul_mod(2,q);
        }



        // ****************************************************************************
        // ************************** STEP 6: check ComRdash **************************
        // ****************************************************************************

        gamma = CBigNum::randBignum(q);
        ComR = proofs2[w].ComR;

        CBN_vector temp_v;
        for(int i=0; i<(int)proofs2[w].s_vec_2.size(); i++) {
            temp_v.push_back(gamma.mul_mod(proofs2[w].s_vec_2[i],q).mul_mod(proofs2[w].ymPowers[i+1],q));
        }

        addVectors_mod(test_vec, temp_v, test_vec, q);

        comTest = comTest.mul_mod(
                        ((ComR.pow_mod(-1,p)).mul_mod(comRdash,p)).pow_mod(gamma,p),p);


    }


    CBigNum test = pedersenCommitment(proofs2[0].signature.params, test_vec, bnZero);

    if(test != comTest) {
        LogPrintf("BatchVerify failed: different test and comTest\n");
        return false;
    }


    // ****************************************************************************
    // ******************************* FINAL STEP *********************************
    // ****************************************************************************

    CBN_matrix ck_inner_g = ck_inner_gen(proofs2[0].signature.params);
    bool valid = BatchBulletproofs(ck_inner_g, proofs2);

    return valid;
}

bool SerialNumberSoKProof::BatchBulletproofs(const CBN_matrix ck_inner_g, std::vector<SerialNumberSoKProof2> &proofs)
{
    // Initialize
    const SerialNumberSoKProof2& dp_outter = proofs[0];
    const ZerocoinParams* params = dp_outter.signature.params;
    const int N1 = dp_outter.signature.innerProduct.pi[0].size();

    const CBigNum& q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const CBigNum& p = params->serialNumberSoKCommitmentGroup.modulus;
    const CBigNum& u_inner_prod = params->serialNumberSoKCommitmentGroup.u_inner_prod;

    CBigNum Ptest = CBigNum(1);

    std::vector<fBE> forBigExpo;
    CBigNum gamma, x1, u_inner, P_inner;
    CBigNum x, Ak, Bk;
    CBN_vector xlist;
    CBigNum pt1, pt2;
    CBigNum z;
    for(unsigned int w=0; w<proofs.size(); w++)
    {
        const auto& dp = proofs[w];
        const auto& A = dp.ComR;
        const auto& B = dp.signature.comRdash;
        z = dp.z;

        gamma = CBigNum::randBignum(q);

        CBigNum P_inner_prod = A.mul_mod(B, p);

        // Inserting the z into u
        CHashWriter1024 hasher(0,0);
        hasher << u_inner_prod.ToString() << P_inner_prod.ToString() << z.ToString();
        x1 = CBigNum(hasher.GetHash()) % q;

        u_inner = u_inner_prod.pow_mod(x1,p);
        P_inner = P_inner_prod.mul_mod(u_inner.pow_mod(z,p),p);

        // Starting the actual protocol
        xlist.clear();

        for(int i=0; i<N1; i++) {
            Ak = dp.signature.innerProduct.pi[0][i];
            Bk = dp.signature.innerProduct.pi[1][i];

            hasher = CHashWriter1024(0,0);
            hasher << Ak.ToString() << Bk.ToString();
            x = CBigNum(hasher.GetHash()) % q;

            xlist.push_back(x);

            P_inner = P_inner.mul_mod(
                    (Ak.pow_mod(x.pow_mod(2,q),p)).mul_mod(Bk.pow_mod(x.pow_mod(-2,q),p),p),p);
        }

        z = dp.signature.innerProduct.final_a[0][0].mul_mod(dp.signature.innerProduct.final_b[0][0],q);

        pt1 = P_inner.pow_mod(gamma,p);
        pt2 = u_inner.pow_mod(z.mul_mod(-gamma,q),p);

        Ptest = Ptest.mul_mod( pt1.mul_mod(pt2,p) ,p);

        fBE new_element;
        new_element.gamma = gamma;
        new_element.xlist = xlist;
        new_element.ymPowers = dp.ymPowers;
        new_element.a = dp.signature.innerProduct.final_a[0][0];
        new_element.b = dp.signature.innerProduct.final_b[0][0];


        forBigExpo.push_back(new_element);
    }

    CBN_vector gh_final = getFinal_gh(params, ck_inner_g[0], forBigExpo);

    return (gh_final[0].mul_mod(gh_final[1],p) == Ptest);
}


CBN_vector SerialNumberSoKProof::getFinal_gh(const ZerocoinParams* ZCp, CBN_vector gs, std::vector<fBE> forBigExpo)
{
    const CBigNum q = ZCp->serialNumberSoKCommitmentGroup.groupOrder;
    const CBigNum p = ZCp->serialNumberSoKCommitmentGroup.modulus;

    int logn = forBigExpo[0].xlist.size();
    int n = gs.size();
    CBN_vector sg_expo(n, CBigNum(0));
    CBN_vector sh_expo(n, CBigNum(0));
    CBN_vector xnlist;

    for(int k=0; k<(int)forBigExpo.size(); k++) {
        fBE comp = forBigExpo[k];

        std::reverse(comp.xlist.begin(),comp.xlist.end());

        xnlist.clear();
        for(int i=0; i<logn; i++)
            xnlist.push_back(comp.xlist[i].pow_mod(-1,q));

        std::vector< std::vector<int>> binary_lookup = Bulletproofs::findBinaryLookup(logn);

        CBN_vector temp_g, temp_h;
        CBigNum sg_i, sh_i;
        std::vector<int> bi;
        for(int i=0; i<n; i++) {
            sg_i = (comp.gamma).mul_mod(comp.a,q);
            sh_i = (comp.gamma).mul_mod((comp.b).mul_mod(comp.ymPowers[i+1],q),q);
            bi = binary_lookup[i];

            for(int j=0; j<logn; j++) {
                if (bi[j] == 1) {
                    sg_i = sg_i.mul_mod(comp.xlist[j],q);
                    sh_i = sh_i.mul_mod(xnlist[j],q);
                } else {
                    sg_i = sg_i.mul_mod(xnlist[j],q);
                    sh_i = sh_i.mul_mod(comp.xlist[j],q);
                }
            }

            temp_g.push_back(sg_i);
            temp_h.push_back(sh_i);

            sg_expo[i] = (sg_expo[i] + sg_i) % q;
            sh_expo[i] = (sh_expo[i] + sh_i) % q;
        }
    }

    CBN_vector gh_final(2, CBigNum(1));

    for(int i=0; i<n; i++) {
        gh_final[0] = gh_final[0].mul_mod(gs[i].pow_mod(sg_expo[i],p),p);
        gh_final[1] = gh_final[1].mul_mod(gs[i].pow_mod(sh_expo[i],p),p);
    }

    return gh_final;
}

} /* namespace libzerocoin */


