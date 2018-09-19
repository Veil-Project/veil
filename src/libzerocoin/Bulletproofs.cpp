/**
* @file       Bulletproofs.cpp
*
* @brief      InnerProductArgument class for the Zerocoin library.
*
* @author     Mary Maller, Jonathan Bootle and Gian Piero Dionisio
* @date       May 2018
*
* @copyright  Copyright 2018 The PIVX Developers
* @license    This project is released under the MIT license.
**/
#include "hash.h"
#include "Bulletproofs.h"
#include <algorithm>

using namespace libzerocoin;

void Bulletproofs::Prove(const CBN_matrix ck_inner_g,
        const CBigNum P_inner_prod, const CBigNum z,
        const CBN_matrix a_sets, const CBN_matrix b_sets, const CBigNum y)
{
    // ----------------------------- **** INNER-PRODUCT PROVE **** ------------------------------
    // ------------------------------------------------------------------------------------------
    // Algorithm used by the signer to prove the inner product argument for two vector polynomials
    // @param   P_inner_prod, z
    // @param   y                       :  value used to create commitment keys ck_inner_{g,h}
    // @param   a_sets, b_sets          :  The two length 1x(N+PADS) matrices that are committed to
    // @init    final_a, final_b        :  final witness
    // @init    pi                      :  final shifted commitments

    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const CBigNum p = params->serialNumberSoKCommitmentGroup.modulus;
    const CBigNum u_inner_prod = params->serialNumberSoKCommitmentGroup.u_inner_prod;

    int s = 2;

    // Inserting the z into u
    CHashWriter1024 hasher(0,0);
    hasher << u_inner_prod.ToString() << P_inner_prod.ToString() << z.ToString();
    CBigNum pre = CBigNum(hasher.GetHash()) % q;

    CBigNum u_inner = u_inner_prod.pow_mod(pre,p);

    // Starting the actual protocol
    CBN_matrix a_sets2 = splitIntoSets(a_sets, s);
    CBN_matrix b_sets2 = splitIntoSets(b_sets, s);
    CBN_matrix g_sets = splitIntoSets(ck_inner_g, s);
    CBN_matrix h_sets;

    CBigNum Ak, Bk;

    bool first = true;

    int N1 = a_sets[0].size();
    CBigNum x;
    while (N1 > 1) {
        hasher = CHashWriter1024(0,0);
        pair<CBigNum, CBigNum> cLcR = findcLcR(a_sets2, b_sets2);
        CBigNum cL = cLcR.first;
        CBigNum cR = cLcR.second;

        if (first) {
            CBigNum ny = y.pow_mod(-ZKP_M, q);
            CBN_vector ymPowers(1, CBigNum(1));
            CBigNum temp = CBigNum(1);

            for(unsigned int i=0; i<2*g_sets[0].size(); i++) {
                temp = temp.mul_mod(ny, q);
                ymPowers.push_back(temp);
            }
            pair<CBigNum, CBigNum> AkBk = firstPreChallengeShifts(
                    g_sets, u_inner, a_sets2, b_sets2, cL, cR, ymPowers);

            Ak = AkBk.first;
            Bk = AkBk.second;

            hasher << Ak.ToString() << Bk.ToString();
            x = CBigNum(hasher.GetHash()) % q;

            // DEFINE h_sets first here
            h_sets = first_get_new_hs(g_sets, x, s, ymPowers);
            g_sets = get_new_gs_hs(g_sets, -1, x, s);

        } else {
            pair<CBigNum, CBigNum> AkBk = preChallengeShifts(
                    g_sets, h_sets,  u_inner, a_sets2, b_sets2, cL, cR);

            Ak = AkBk.first;
            Bk = AkBk.second;

            hasher << Ak.ToString() << Bk.ToString();
            x = CBigNum(hasher.GetHash()) % q;

            if (N1 == 2) s = 1;

            g_sets = get_new_gs_hs(g_sets, -1, x, s);
            h_sets = get_new_gs_hs(h_sets, 1, x, s);
        }

        a_sets2 = get_new_as_bs(a_sets2, 1, x, s);
        b_sets2 = get_new_as_bs(b_sets2, -1, x, s);

        first = false;

        pi[0].push_back(Ak);
        pi[1].push_back(Bk);

        N1 = N1 / 2;
    }

    final_a = a_sets2;
    final_b = b_sets2;
}



bool Bulletproofs::Verify(const ZerocoinParams* ZCp,
        const CBN_matrix ck_inner_g, const CBN_matrix ck_inner_h,
        CBigNum A, CBigNum B, CBigNum z)
{
    // ---------------------------- **** INNER-PRODUCT VERIFY **** ------------------------------
    // ------------------------------------------------------------------------------------------
    // Algorithm used to verify the inner product proof
    // @param   ck_inner_g, ck_inner_h, u_inner_prod ?
    // @param   A, B    :  commitments (to a_sets and b_sets)
    // @param   z       :  target value
    // @return  bool    :  result of the verification

    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const CBigNum p = params->serialNumberSoKCommitmentGroup.modulus;
    const CBigNum u_inner_prod = params->serialNumberSoKCommitmentGroup.u_inner_prod;

    CBigNum P_inner_prod = A.mul_mod(B, p);

    // Inserting the z into u
    CHashWriter1024 hasher(0,0);
    hasher << u_inner_prod.ToString() << P_inner_prod.ToString() << z.ToString();
    CBigNum x1 = CBigNum(hasher.GetHash()) % q;

    CBigNum u_inner = u_inner_prod.pow_mod(x1,p);
    CBigNum P_inner = P_inner_prod.mul_mod(u_inner.pow_mod(z,p),p);

    // Starting the actual protocol
    int N1 = pi[0].size();
    CBigNum x, Ak, Bk;
    CBN_vector xlist;

    for(int i=0; i<N1; i++) {
        Ak = pi[0][i];
        Bk = pi[1][i];

        hasher << Ak.ToString() << Bk.ToString();
        x = CBigNum(hasher.GetHash()) % q;

        xlist.push_back(x);

        P_inner = P_inner.mul_mod(
                (Ak.pow_mod(x.pow_mod(2,q),p)).mul_mod(Bk.pow_mod(x.pow_mod(-2,q),p),p),p);
    }

    CBigNum z2 = final_a[0][0].mul_mod(final_b[0][0],q);
    CBN_vector gh_final = getFinal_gh(ck_inner_g[0], ck_inner_h[0], xlist);

    return testComsCorrect(gh_final, u_inner, P_inner, final_a, final_b, z2);

}


// Split set of 1 set (ck_inner_g) into set of many sets (g_sets)
CBN_matrix Bulletproofs::splitIntoSets(const CBN_matrix ck_inner_g, const int s)
{
    const int N1 = ck_inner_g[0].size() / 2;

    if (s==1) {
        // allocate g_sets
        CBN_matrix g_sets(ck_inner_g);
        return g_sets;
    }

    else if (s==2) {
        // allocate g_sets
        CBN_matrix g_sets(2, CBN_vector());

        for(int i=0; i<N1; i++) {
            g_sets[0].push_back(ck_inner_g[0][i]);
            g_sets[1].push_back(ck_inner_g[0][N1+i]);
        }
        return g_sets;
    }

    else throw std::runtime_error("wrong s inside splitIntoSets: " + s);
}

// Function for reduction when mu > 1
pair<CBigNum, CBigNum> Bulletproofs::findcLcR(const CBN_matrix a_sets, const CBN_matrix b_sets)
{
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;

    CBigNum cL = dotProduct(a_sets[0], b_sets[1], q);
    CBigNum cR = dotProduct(a_sets[1], b_sets[0], q);

    return make_pair(cL, cR);
}



// reduction used in innerProductProve when mu > 1
pair<CBigNum, CBigNum> Bulletproofs::firstPreChallengeShifts(
        const CBN_matrix g_sets, const CBigNum u_inner,
        const CBN_matrix a_sets, const CBN_matrix b_sets, CBigNum cL, CBigNum cR,
        const CBN_vector ymPowers)
{
    const CBigNum p = params->serialNumberSoKCommitmentGroup.modulus;
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;

    CBigNum Ak = u_inner.pow_mod(cL, p);
    const int n1 = g_sets[0].size();
    for(int i=0; i<n1; i++) {
        Ak = Ak.mul_mod(g_sets[1][i].pow_mod(a_sets[0][i],p),p);
        Ak = Ak.mul_mod(g_sets[0][i].pow_mod(b_sets[1][i].mul_mod(ymPowers[i+1],q),p),p);
    }

    CBigNum Bk = u_inner.pow_mod(cR, p);
    for(int i=0; i<n1; i++) {
        Bk = Bk.mul_mod(g_sets[0][i].pow_mod(a_sets[1][i],p),p);
        Bk = Bk.mul_mod(g_sets[1][i].pow_mod(b_sets[0][i].mul_mod(ymPowers[n1+i+1],q),p),p);
    }

    return make_pair(Ak, Bk);
}

pair<CBigNum, CBigNum> Bulletproofs::preChallengeShifts(
        const CBN_matrix g_sets, const CBN_matrix h_sets, const CBigNum u_inner,
        const CBN_matrix a_sets, const CBN_matrix b_sets, CBigNum cL, CBigNum cR)
{
    const CBigNum p = params->serialNumberSoKCommitmentGroup.modulus;
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;

    CBigNum Ak = u_inner.pow_mod(cL, p);
    const int n1 = g_sets[0].size();
    for(int i=0; i<n1; i++) {
        Ak = Ak.mul_mod(g_sets[1][i].pow_mod(a_sets[0][i],p),p);
        Ak = Ak.mul_mod(h_sets[0][i].pow_mod(b_sets[1][i],p),p);
    }

    CBigNum Bk = u_inner.pow_mod(cR, p);
    for(int i=0; i<n1; i++) {
        Bk = Bk.mul_mod(g_sets[0][i].pow_mod(a_sets[1][i],p),p);
        Bk = Bk.mul_mod(h_sets[1][i].pow_mod(b_sets[0][i],p),p);
    }

    return make_pair(Ak, Bk);
}



// Find the new gs and hs in the inner product reduction
CBN_matrix Bulletproofs::first_get_new_hs(const CBN_matrix g_sets,
        const CBigNum x, const int m2, const CBN_vector ymPowers)
{
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const CBigNum p = params->serialNumberSoKCommitmentGroup.modulus;
    const int N1 = g_sets[0].size();
    CBN_matrix new_gs(1, CBN_vector());
    CBigNum new_g, x1, x2;  // temp

    const CBigNum xn = x.pow_mod(-1,q);

    for(int j=0; j<N1; j++) {
        x1 = ymPowers[j+1].mul_mod(x,q);
        x2 = ymPowers[N1+j+1].mul_mod(xn, q);

        new_g = (g_sets[0][j].pow_mod(x1,p)).mul_mod(g_sets[1][j].pow_mod(x2,p),p);

        new_gs[0].push_back(new_g);
    }

    return splitIntoSets(new_gs, m2);
}

CBN_matrix Bulletproofs::get_new_gs_hs(const CBN_matrix g_sets, const int sign,
        const CBigNum x, const int m2)
{
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const CBigNum p = params->serialNumberSoKCommitmentGroup.modulus;
    const int N1 = g_sets[0].size();
    CBN_matrix new_gs(1, CBN_vector());
    CBigNum new_g;

    const CBigNum xn = x.pow_mod(-1,q);

    for(int j=0; j<N1; j++) {
        if (sign > 0)
            new_g = (g_sets[0][j].pow_mod(x,p)).mul_mod(
                    g_sets[1][j].pow_mod(xn,p),p);
        else if (sign < 0)
            new_g = (g_sets[0][j].pow_mod(xn,p)).mul_mod(
                    g_sets[1][j].pow_mod(x,p),p);
        else
            throw std::runtime_error("wrong sign inside get_new_gs_hs: " + sign);

        new_gs[0].push_back(new_g);
    }

    return splitIntoSets(new_gs, m2);
}


// Get new as and bs in inner product reduction
CBN_matrix Bulletproofs::get_new_as_bs(const CBN_matrix a_sets, const int sign,
        const CBigNum x, const int m2)
{
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const int N1 = a_sets[0].size();
    CBN_matrix a2(1, CBN_vector(N1));
    CBigNum aj;

    const CBigNum xn = x.pow_mod(-1,q);

    for(int j=0; j<N1; j++) {
        if (sign > 0)
            aj = (a_sets[0][j].mul_mod(x,q) + a_sets[1][j].mul_mod(xn, q)) % q;
        else if (sign < 0)
            aj = (a_sets[0][j].mul_mod(xn,q) + a_sets[1][j].mul_mod(x, q)) % q;
        else
            throw std::runtime_error("wrong sign inside get_new_as_bs: " + sign);

        a2[0][j] = aj;
    }

    return splitIntoSets(a2, m2);
}



// Verify Commitments
bool Bulletproofs::testComsCorrect(const CBN_vector gh_sets,
        const CBigNum u_inner, const CBigNum P_inner,
        const CBN_matrix final_a, const CBN_matrix final_b, const CBigNum z)
{
    const CBigNum p = params->serialNumberSoKCommitmentGroup.modulus;
    CBigNum Ptest = gh_sets[0].pow_mod(final_a[0][0],p);
    Ptest = Ptest.mul_mod(gh_sets[1].pow_mod(final_b[0][0],p),p);
    Ptest = Ptest.mul_mod(u_inner.pow_mod(z,p),p);
    return (Ptest == P_inner);
}


// Optimizations
CBN_vector Bulletproofs::getFinal_gh(const CBN_vector gs, const CBN_vector hs, CBN_vector xlist)
{
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const CBigNum p = params->serialNumberSoKCommitmentGroup.modulus;

    const int logn = xlist.size();
    const int n = gs.size();
    CBN_vector xnlist;
    CBigNum sg_i, sh_i, xg, xh;
    CBN_vector sg_expo, sh_expo;
    std::vector<int> bi;

    std::reverse(xlist.begin(), xlist.end());

    for(int i=0; i<logn; i++)
        xnlist.push_back(xlist[i].pow_mod(-1,q));

    std::vector< std::vector<int>> binary_lookup = findBinaryLookup(logn);

    for(int i=0; i<n; i++) {
        sg_i = CBigNum(1);
        sh_i = CBigNum(1);
        bi = binary_lookup[i];

        for(int j=0; j<logn; j++) {

            if (bi[j] == 1) {
                xg = xlist[j];
                xh = xnlist[j];
            } else {
                xg = xnlist[j];
                xh = xlist[j];
            }

            sg_i = sg_i.mul_mod(xg,q);
            sh_i = sh_i.mul_mod(xh,q);
        }

        sg_expo.push_back(sg_i);
        sh_expo.push_back(sh_i);
    }

    CBN_vector ghfinal(2, CBigNum(1));

    for(int i=0; i<n; i++) {
        ghfinal[0] = ghfinal[0].mul_mod(gs[i].pow_mod(sg_expo[i],p),p);
        ghfinal[1] = ghfinal[1].mul_mod(hs[i].pow_mod(sh_expo[i],p),p);
    }

    return ghfinal;
}


std::vector< std::vector<int>> Bulletproofs::findBinaryLookup(const int range)
{
    std::vector< std::vector<int>> binary_lookup(2);
    binary_lookup[0] = {0};
    binary_lookup[1] = {1};

    for(int i=0; i<range; i++)
        binary_lookup = binaryBigger(binary_lookup);

    return binary_lookup;
}

std::vector< std::vector<int>> Bulletproofs::binaryBigger(const std::vector< std::vector<int>> bin_lookup)
{
    std::vector< std::vector<int>> bigger;
    std::vector<int> lookup;

    for(int i=0; i<2; i++)
        for(int j=0; j<(int)bin_lookup.size(); j++) {
            lookup = bin_lookup[j];
            lookup.push_back(i);
            bigger.push_back(lookup);
        }

    return bigger;
}

