// Copyright (c) 2015-2019 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <random.h>
#include <util/system.h>
#include <test/test_veil.h>

#include <boost/test/unit_test.hpp>

namespace {
struct RegtestingSetup : public BasicTestingSetup {
    RegtestingSetup() : BasicTestingSetup(CBaseChainParams::REGTEST) {}
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    int64_t nLastRetargetTime = 1261130161; // Block #30240
    CBlockIndex pindexLast;
    pindexLast.nHeight = 32255;
    pindexLast.nTime = 1262152739;  // Block #32255
    pindexLast.nBits = 0x1d00ffff;
    // Add test for DarkGravityWave
    //BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), 0x1d00d86aU);
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    int64_t nLastRetargetTime = 1231006505; // Block #0
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2015;
    pindexLast.nTime = 1233061996;  // Block #2015
    pindexLast.nBits = 0x1d00ffff;
    // Add test for DarkGravityWave
    //BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), 0x1d00ffffU);
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    int64_t nLastRetargetTime = 1279008237; // Block #66528
    CBlockIndex pindexLast;
    pindexLast.nHeight = 68543;
    pindexLast.nTime = 1279297671;  // Block #68543
    pindexLast.nBits = 0x1c05a3f4;
    // Add test for DarkGravityWave
    //BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), 0x1c0168fdU);
}

/* Test the constraint on the upper bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1269211443;  // Block #46367
    pindexLast.nBits = 0x1c387f6f;
    // Add test for DarkGravityWave
    //BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), 0x1d00e1fdU);
}

/* The DGW running-average step is computed at 512-bit width: with 256-bit
 * arithmetic the intermediate sum wraps 2^256 once ~3 near-pow-limit (regtest)
 * targets are accumulated, so the "average" of three limit targets came out as
 * ~limit/3 (compact 0x202aaaa9 instead of 0x207fffff). */
BOOST_AUTO_TEST_CASE(dgw_target_avg_step)
{
    const arith_uint256 bnLimit("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    // The former 256-bit evaluation wraps for near-limit targets...
    BOOST_CHECK((bnLimit * 2u + bnLimit) / 3 != bnLimit);

    // ...while the average of identical targets must be that target, at every count
    arith_uint256 bnAvg = 0;
    for (unsigned int nCount = 0; nCount < 60; nCount++) {
        bnAvg = DgwTargetAvgStep(bnAvg, nCount, bnLimit);
        BOOST_CHECK(bnAvg == bnLimit);
    }

    // Bit-identical to the former 256-bit evaluation whenever that does not wrap
    arith_uint256 bnTarget = arith_uint256().SetCompact(0x1b0404cb);
    bnAvg = 0;
    for (unsigned int nCount = 0; nCount < 60; nCount++) {
        const arith_uint256 bnExpected = (bnAvg * nCount + bnTarget) / (nCount + 1);
        bnAvg = DgwTargetAvgStep(bnAvg, nCount, bnTarget);
        BOOST_CHECK(bnAvg == bnExpected);
        bnTarget += bnTarget / 5 + nCount; // drift upward, staying far below the wrap range
    }
}

/* A regtest sha256d chain mined at the pow limit must keep requiring the pow
 * limit; the wrapped 256-bit average used to demand ~3x the work instead. */
BOOST_FIXTURE_TEST_CASE(dgw_at_regtest_pow_limit, RegtestingSetup)
{
    const Consensus::Params& params = Params().GetConsensus();
    const unsigned int nLimitBits = UintToArith256(params.powLimitSha256).GetCompact();
    const int64_t nSpacing = params.nSha256DTargetSpacing;
    const int64_t nStart = Params().PowUpdateTimestamp();

    // One more block than DGW averages over, so the whole window is sha256d
    // blocks at the pow limit. Blocks arrive slower than the target spacing
    // (except the newest, which is on time, so sha256d is not overdue): DGW
    // then wants difficulty to drop, which the pow limit caps, so anything
    // below the limit exposes a corrupted average.
    std::vector<CBlockIndex> blocks(params.nDgwPastBlocks + 1);
    for (size_t i = 0; i < blocks.size(); i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nVersion = CBlockHeader::SHA256D_BLOCK;
        blocks[i].nBits = nLimitBits;
        blocks[i].nTime = nStart + i * 2 * nSpacing;
    }
    blocks.back().nTime = blocks[blocks.size() - 2].nTime + nSpacing;

    BOOST_CHECK_EQUAL(DarkGravityWave(&blocks.back(), params, false, CBlockHeader::SHA256D_BLOCK), nLimitBits);
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p2 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p3 = &blocks[InsecureRandRange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

BOOST_AUTO_TEST_SUITE_END()
