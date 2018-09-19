/**
* @file       ArithmeticCircuit.cpp
*
* @brief      ArithmeticCircuit class for the Zerocoin library.
*
* @author     Mary Maller, Jonathan Bootle and Gian Piero Dionisio
* @date       April 2018
*
* @copyright  Copyright 2018 The PIVX Developers
* @license    This project is released under the MIT license.
**/

#include "ArithmeticCircuit.h"

using namespace libzerocoin;

ArithmeticCircuit::ArithmeticCircuit(const ZerocoinParams* p):
            A(ZKP_M, CBN_vector(ZKP_N)),
            B(ZKP_M, CBN_vector(ZKP_N)),
            C(ZKP_M, CBN_vector(ZKP_N)),
            K(p->ZKP_K),
            YPowers(0),
            YDash(ZKP_N),
            y_vec_neg(0),
            params(p),
            r_bits(ZKP_SERIALSIZE, 0)
{}

void ArithmeticCircuit::setWireValues(const PrivateCoin& coin)
{
    const CBigNum a = params->coinCommitmentGroup.g;
    const CBigNum b = params->coinCommitmentGroup.h;
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;

    /* ---------------------------------- **** WIRE VALUES **** ----------------------------------
     * -------------------------------------------------------------------------------------------
     * Sets wire values (in M*N matrices A, B and C) correctly for a circuit
     * with serial number 'serialNumber' and randomness bits 'r_bits'.
     * Here we have a and b are the group elements used to mint coins.
     */
    serialNumber = coin.getSerialNumber();
    randomness = coin.getRandomness();
    coin.getRandomnessBits(r_bits);

    unsigned int row=0, col=0;
    for(unsigned int i=1; i<ZKP_SERIALSIZE+1; i++) {
        row = i / ZKP_N;
        col = i % ZKP_N;
        A[row][col] = r_bits[i-1] % q;
        B[row][col] = (r_bits[i-1] - CBigNum(1)) % q;
        C[row][col] = CBigNum(0);
    }

    int k=0;
    CBigNum x, product = CBigNum(1);    // efficiency registers
    x = r_bits[0] * (b - CBigNum(1)) + CBigNum(1);
    CBigNum Cpenultimate;
    for(unsigned int i=ZKP_SERIALSIZE+1; i<2*ZKP_SERIALSIZE; i++) {
        row = i / ZKP_N;
        col = i % ZKP_N;
        product = product.mul_mod(x, q);
        A[row][col] = product;
        x = r_bits[k+1] * (b.pow_mod(CBigNum(2).pow(k+1), q) - CBigNum(1)) + CBigNum(1);
        B[row][col] = x % q;
        C[row][col] = A[row][col].mul_mod(B[row][col], q);

        if (i == 2*ZKP_SERIALSIZE-1)
            Cpenultimate = C[row][col];

        k++;
    }
    A[2*ZKP_SERIALSIZE / ZKP_N][2*ZKP_SERIALSIZE % ZKP_N] = Cpenultimate;
    B[2*ZKP_SERIALSIZE / ZKP_N][2*ZKP_SERIALSIZE % ZKP_N] = a.pow_mod(serialNumber, q);
    C[2*ZKP_SERIALSIZE / ZKP_N][2*ZKP_SERIALSIZE % ZKP_N] = A[2*ZKP_SERIALSIZE / ZKP_N][2*ZKP_SERIALSIZE % ZKP_N].mul_mod(
            B[2*ZKP_SERIALSIZE / ZKP_N][2*ZKP_SERIALSIZE % ZKP_N],q);
}

void ArithmeticCircuit::setPreConstraints(const ZerocoinParams* params,
        vector<CBN_matrix>& wA, vector<CBN_matrix>& wB, vector<CBN_matrix>& wC, CBN_vector& K)
{
    const CBigNum a = params->coinCommitmentGroup.g;
    const CBigNum b = params->coinCommitmentGroup.h;
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const int N = ZKP_SERIALSIZE;
    const int n = ZKP_N;
    const int m = ZKP_M;

    /* ---------------------------------- **** CONSTRAINTS **** ----------------------------------
     * -------------------------------------------------------------------------------------------
     * Matrices wA, wB, wC and vector K, specifying constraints that ensure that the circuit
     * is satisfied if and only if Cfinal = a^S b^v.
     *
     *     wA      constraints for the left input wires
     *     wB      constraints for the right input wires
     *     wC      constraints for the output wires
     *     K       constraints vector
     *
     */

    wA.resize(4*N, CBN_matrix(m, CBN_vector(n, CBigNum(0))));
    wB.resize(4*N, CBN_matrix(m, CBN_vector(n, CBigNum(0))));
    wC.resize(4*N, CBN_matrix(m, CBN_vector(n, CBigNum(0))));
    K.resize(4*N, CBigNum(0));
    unsigned int i_div_n = 0;
    unsigned int k_div_n = 0, k_mod_n = 0;
    unsigned int ell_div_n = 0, ell_mod_n = 0;
    unsigned int ell=0;
    int k = 0;
    CBigNum x;             // efficiency reg
    CBN_vector U(n);   // Unit vector

    /* Constraints to ensure A[k][l] - B[k][l] = 1 */
    k = 1;
    for(unsigned int i=0; i<N; i++) {
        i_div_n = i / n;
        k_div_n = k / n;
        k_mod_n = k % n;
        unit_vector(wA[i][i_div_n], k_mod_n);
        unit_vector(U, k_mod_n);
        vectorTimesConstant(wB[i][i_div_n], U, CBigNum(-1), q);
        K[i] = CBigNum(1);
        k = k+1;
    }

    /* Constraints to ensure C[i] = 0 */
    k = 1;
    for(unsigned int i=N; i<2*N; i++) {
        k_div_n = k / ZKP_N;
        k_mod_n = k % ZKP_N;
        unit_vector(wC[i][k_div_n], k_mod_n);
        K[i] = CBigNum(0);
        k++;
    }

    /* Constraints to ensure that B[N+1+k] = A[k+2] * (b** (2**(k+1)) - 1) +1    */
    k = 2;   ell = N+1;
    for(unsigned int i=2*N; i<3*N-1; i++) {
        k_div_n = k / ZKP_N;
        k_mod_n = k % ZKP_N;
        ell_div_n = ell / ZKP_N;
        ell_mod_n = ell % ZKP_N;
        unit_vector(U, k_mod_n);
        x = b.pow_mod( CBigNum(2).pow(k-1), q) - CBigNum(1);
        vectorTimesConstant(wA[i][k_div_n], U, x, q);
        unit_vector(U, ell_mod_n);
        vectorTimesConstant(wB[i][ell_div_n], U, CBigNum(-1), q);
        K[i] = CBigNum(-1);
        k++; ell++;
    }

    /* Constraints to ensure A[N+1] = A[1] * (b-1) + 1     */
    unit_vector(wA[3*N-1][(N+1)/n], (N+1)%n);
    unit_vector(U, 1);
    vectorTimesConstant(wB[3*N-1][0], U, CBigNum(1)-b, q);
    K[3*N-1] = b;

    /* Constraints to ensure A[N+k+1] = C[N+k]     */
    k = N+2; ell = N+1;
    for(unsigned int i=3*N; i<4*N-2; i++) {
        k_div_n = k/n;
        k_mod_n = k%n;
        ell_div_n = ell/n;
        ell_mod_n = ell%n;
        unit_vector(wA[i][k_div_n], k_mod_n);
        unit_vector(U, ell_mod_n);
        vectorTimesConstant(wC[i][ell_div_n], U, CBigNum(-1), q);
        K[i] = 0;
        k++; ell++;
    }

    /* Constraints to ensure B[final] = (a**S)   */
    k_div_n = (2*N) / n;
    k_mod_n = (2*N) % n;
    unit_vector(wB[4*N-2][k_div_n], k_mod_n);

}

void ArithmeticCircuit::set_s_poly(const ZerocoinParams* params,
            std::vector< std::vector< std::pair<int, CBigNum> > >& s_a1, std::vector< std::vector< std::pair<int, CBigNum> > >& s_a2,
            std::vector< std::vector< std::pair<int, CBigNum> > >& s_b1, std::vector< std::vector< std::pair<int, CBigNum> > >& s_b2,
            std::vector< std::vector< std::pair<int, CBigNum> > >& s_c1, std::vector< std::vector< std::pair<int, CBigNum> > >& s_c2)
{
    const int n = ZKP_N;

    s_a1.clear();
    s_a2.clear();
    for(int k=0; k<n; k++) {
        std::vector< std::pair<int, CBigNum> > temp1, temp2;
        for(int i=0; i<(int)params->ZKP_wA.size(); i++) {
            if (params->ZKP_wA[i][0][k] != CBigNum(0))
                temp1.push_back( std::make_pair(i, params->ZKP_wA[i][0][k]) );
            if (params->ZKP_wA[i][1][k] != CBigNum(0))
                temp2.push_back( std::make_pair(i, params->ZKP_wA[i][1][k]) );
        }
        s_a1.push_back(temp1);
        s_a2.push_back(temp2);
    }

    s_b1.clear();
    s_b2.clear();
    for(int k=0; k<n; k++) {
        std::vector< std::pair<int, CBigNum> > temp1, temp2;
        for(int i=0; i<(int)params->ZKP_wB.size(); i++) {
            if (params->ZKP_wB[i][0][k] != CBigNum(0))
                temp1.push_back( std::make_pair(i, params->ZKP_wB[i][0][k]) );
            if (params->ZKP_wB[i][1][k] != CBigNum(0))
                temp2.push_back( std::make_pair(i, params->ZKP_wB[i][1][k]) );
        }
        s_b1.push_back(temp1);
        s_b2.push_back(temp2);
    }

    s_c1.clear();
    s_c2.clear();
    for(int k=0; k<n; k++) {
        std::vector< std::pair<int, CBigNum> > temp1, temp2;
        for(int i=0; i<(int)params->ZKP_wC.size(); i++) {
            if (params->ZKP_wC[i][0][k] != CBigNum(0))
                temp1.push_back( std::make_pair(i, params->ZKP_wC[i][0][k]) );
            if (params->ZKP_wC[i][1][k] != CBigNum(0))
                temp2.push_back( std::make_pair(i, params->ZKP_wC[i][1][k]) );
        }
        s_c1.push_back(temp1);
        s_c2.push_back(temp2);
    }

}

void ArithmeticCircuit::setYPoly(const CBigNum& y)
{
    /* --------------------------------- **** w-POLYNOMIALS **** ---------------------------------
     * -------------------------------------------------------------------------------------------
     */
    this->y = y;
    set_YPowers();
    set_YDash();
    set_Kconst(YPowers, serialNumber);
}

CBigNum ArithmeticCircuit::sumWiresDotWs(const int i)
{
    CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    CBigNum sum = CBigNum(0);
    for(unsigned int j=0; j<ZKP_M; j++) {
        sum = (sum + dotProduct(A[j], params->ZKP_wA[i][j], q)) % q;
        sum = (sum + dotProduct(B[j], params->ZKP_wB[i][j], q)) % q;
        sum = (sum + dotProduct(C[j], params->ZKP_wC[i][j], q)) % q;
    }
    return sum;
}

CBigNum ArithmeticCircuit::AiDotBiYDash(const int i)
{
    CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    CBigNum res = CBigNum(0);
    for(unsigned int j=0; j<ZKP_N; j++)
        res = (res + A[i][j] * B[i][j] * YDash[j]) % q;
    return res;
}

void ArithmeticCircuit::set_YPowers()
{
    CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;

    YPowers.clear();
    YPowers.push_back(CBigNum(1));
    CBigNum temp = CBigNum(1);
    for(unsigned int i=0; i<8*ZKP_SERIALSIZE+ZKP_M+1; i++) {
        temp = temp.mul_mod(y, q);
        YPowers.push_back(temp);
    }

    y_vec_neg.clear();
    CBigNum ym = y.pow_mod(-ZKP_M,q);
    temp = CBigNum(1);
    for(unsigned int i=0; i<ZKP_N+ZKP_PADS; i++) {
        temp = temp.mul_mod(ym, q);
        y_vec_neg.push_back(temp.mul_mod(2,q));
    }
}

void ArithmeticCircuit::set_YDash()
{
    CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    for(unsigned int i=0; i<ZKP_N; i++)
        YDash[i] = YPowers[ZKP_M*(i+1)];
}


void ArithmeticCircuit::set_Kconst(CBN_vector& YPowers, const CBigNum serial)
{
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const CBigNum a = params->coinCommitmentGroup.g;

    K[4*ZKP_SERIALSIZE-2] = a.pow_mod(serial, q);

    Kconst = CBigNum(0);
    for(unsigned int i=0; i<K.size(); i++)
        Kconst = (Kconst + K[i].mul_mod(YPowers[4*ZKP_SERIALSIZE+ZKP_M+1+i], q)) %  q;
}

// verifies correct assignment
void ArithmeticCircuit::check()
{
    const CBigNum a = params->coinCommitmentGroup.g;
    const CBigNum b = params->coinCommitmentGroup.h;
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;

    for(unsigned int i=0; i<ZKP_M; i++)
        for(unsigned int j=0; j<ZKP_N; j++)
            if (A[i][j].mul_mod(B[i][j], q) != C[i][j])
                throw std::runtime_error("ArithmeticCircuit::check() error: code 1");
;
    CBigNum logarithm = (a.pow_mod(serialNumber,q)).mul_mod(b.pow_mod(randomness,q),q);
    if (C[ZKP_M-1][0] != logarithm)
        throw std::runtime_error("ArithmeticCircuit::check() error: code 2");

    for(unsigned int i=0; i<4*ZKP_SERIALSIZE-2; i++)
        if(K[i] != sumWiresDotWs(i)) {
            throw std::runtime_error("ArithmeticCircuit::check() error: code 3");
        }

    //if (sumWiresDotWPoly() != Kconst)
    //    throw std::runtime_error("ArithmeticCircuit::check() error: code 4");
}

