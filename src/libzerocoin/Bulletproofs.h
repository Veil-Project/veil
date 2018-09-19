/**
* @file       Bulletproofs.h
*
* @brief      InnerProductArgument class for the Zerocoin library.
*
* @author     Mary Maller, Jonathan Bootle and Gian Piero Dionisio
* @date       May 2018
*
* @copyright  Copyright 2018 The PIVX Developers
* @license    This project is released under the MIT license.
**/

#pragma once
#include "Coin.h"
#include "zkplib.h"

using namespace std;

namespace libzerocoin {

class Bulletproofs {
public:
    Bulletproofs(){};
    Bulletproofs(const ZerocoinParams* ZCp): pi(2, CBN_vector()), params(ZCp) {};

    void Prove(const CBN_matrix ck_inner_g, const CBigNum P_inner_prod, const CBigNum z, const CBN_matrix a_sets, const CBN_matrix b_sets, const CBigNum y);
    bool Verify(const ZerocoinParams* ZCp, const CBN_matrix ck_inner_g, const CBN_matrix ck_inner_h, CBigNum A, CBigNum B, CBigNum z);

    CBN_matrix pi;                      // shifted commitments to a_sets and b_sets
    CBN_matrix final_a, final_b;        // final witness

    static std::vector< std::vector<int>> findBinaryLookup(const int range);
    static std::vector< std::vector<int>> binaryBigger(const std::vector< std::vector<int>> bin_lookup);


private:
    const ZerocoinParams* params;
    CBN_matrix splitIntoSets(const CBN_matrix ck_inner_g, const int s);
    pair<CBigNum, CBigNum> findcLcR(const CBN_matrix a_sets, const CBN_matrix b_sets);
    pair<CBigNum, CBigNum> firstPreChallengeShifts(const CBN_matrix g_sets, const CBigNum u_inner, const CBN_matrix a_sets, const CBN_matrix b_sets, CBigNum cL, CBigNum cR, const CBN_vector ymPowers);
    pair<CBigNum, CBigNum> preChallengeShifts(const CBN_matrix g_sets, const CBN_matrix h_sets, const CBigNum u_inner, const CBN_matrix a_sets, const CBN_matrix b_sets, CBigNum cL, CBigNum cR);
    CBN_matrix first_get_new_hs(const CBN_matrix g_sets, const CBigNum x, const int m2, const CBN_vector ymPowers);
    CBN_matrix get_new_gs_hs(const CBN_matrix g_sets, const int sign, const CBigNum x, const int m2);
    CBN_matrix get_new_as_bs(const CBN_matrix a_sets, const int sign, const CBigNum x, const int m2);
    bool testComsCorrect(const CBN_vector gh_sets, const CBigNum u_inner, const CBigNum P_inner, const CBN_matrix final_a, const CBN_matrix final_b, const CBigNum z);
    CBN_vector getFinal_gh(const CBN_vector gs, const CBN_vector hs, CBN_vector xlist);
};

} /* namespace libzerocoin */
