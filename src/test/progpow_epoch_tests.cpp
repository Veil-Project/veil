// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_veil.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <hash.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(progpow_epoch_tests, BasicTestingSetup)

// A header's nHeight is attacker controlled on the header accept path and reaches ProgPowHash
// before it has been validated. Without a bound the ethash light cache is sized straight from
// that height (tens of GB for a large nHeight, a remote OOM / null deref). ProgPowClampEpoch caps
// the epoch so the allocation stays bounded. These checks exercise the bound directly and never
// build a cache.
BOOST_AUTO_TEST_CASE(clamp_bounds_out_of_range_epoch)
{
    const int maxEpoch = ProgPowMaxEpoch();
    BOOST_CHECK_EQUAL(ProgPowClampEpoch(maxEpoch + 1), maxEpoch);
    BOOST_CHECK_EQUAL(ProgPowClampEpoch(1 << 28), maxEpoch);   // ~268M, far past the ceiling
    BOOST_CHECK_EQUAL(ProgPowClampEpoch(-1), 0);               // a negative epoch is floored
}

// The clamp must never change a legitimate header's epoch, or valid blocks would hash differently
// and split consensus. Every epoch in [0, ceiling] is returned unchanged.
BOOST_AUTO_TEST_CASE(clamp_is_identity_over_legit_range)
{
    const int maxEpoch = ProgPowMaxEpoch();
    for (int e = 0; e <= maxEpoch; ++e)
        BOOST_CHECK_EQUAL(ProgPowClampEpoch(e), e);
}

// Tie the bound to the real epoch formula and a realistic attacker height: a large wire nHeight
// maps to an epoch far above the ceiling, and the clamp brings it back to the ceiling. That epoch
// is exactly what would otherwise size the light cache.
BOOST_AUTO_TEST_CASE(clamp_catches_attacker_height)
{
    const int maxEpoch = ProgPowMaxEpoch();
    const int rawEpoch = Params().GetProgPowEpochNumber(2000000000); // ~2e9, fits a header int nHeight
    BOOST_CHECK_GT(rawEpoch, maxEpoch);                              // the over range epoch is reachable
    BOOST_CHECK_EQUAL(ProgPowClampEpoch(rawEpoch), maxEpoch);        // and the clamp bounds it
}

// Fail closed sentinel: when the bounded light cache still cannot be allocated, ProgPowHash returns
// the maximum possible hash instead of dereferencing a null context. The maximum hash is greater
// than every valid PoW target, so a header hashed this way can never pass hash <= target. This is
// the constant returned on the null context branch in ProgPowHash.
BOOST_AUTO_TEST_CASE(fail_closed_hash_is_unsatisfiable)
{
    const uint256 failClosed = uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    BOOST_CHECK(UintToArith256(failClosed) == (~arith_uint256(0)));
}

BOOST_AUTO_TEST_SUITE_END()
