// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_veil.h>

#include <uint256.h>
#include <util/time.h>
#include <veil/dandelioninventory.h>

#include <algorithm>
#include <vector>

#include <boost/test/unit_test.hpp>

using veil::DandelionInventory;
using veil::SelectDandelionNextHop;

BOOST_FIXTURE_TEST_SUITE(dandelion_tests, BasicTestingSetup)

// IsInStemPhase must be true while the stem timer is in the future and false once it has passed.
// The predicate was inverted (end < now), which made it always false, so stem routing was silently
// off and every transaction fluffed from its origin.
BOOST_AUTO_TEST_CASE(is_in_stem_phase_tracks_future_end_time)
{
    const int64_t t0 = 1600000000;
    SetMockTime(t0);

    DandelionInventory dinv;
    const uint256 hash = uint256S("01");
    dinv.Add(hash, t0 + 60, /*nNodeIDFrom=*/2);   // clamped into [t0, t0+120]; stays in the future

    BOOST_CHECK(dinv.IsInStemPhase(hash));         // future end time -> in the stem phase

    SetMockTime(t0 + 1000);                        // advance well past the end time
    BOOST_CHECK(!dinv.IsInStemPhase(hash));        // past end time -> no longer in the stem phase

    SetMockTime(0);
}

// The peer-supplied stem end time is clamped to the honest window, so a peer cannot pin an entry in
// the stem phase forever: even an end time far in the future expires at now + nDefaultStemTime (120s).
BOOST_AUTO_TEST_CASE(add_clamps_peer_supplied_end_time)
{
    const int64_t t0 = 1600000000;
    SetMockTime(t0);

    DandelionInventory dinv;
    const uint256 hash = uint256S("02");
    dinv.Add(hash, t0 + 10LL * 365 * 24 * 3600, /*nNodeIDFrom=*/2); // ~10 years in the future

    BOOST_CHECK(dinv.IsInStemPhase(hash));
    SetMockTime(t0 + 121);                         // just past now + nDefaultStemTime
    BOOST_CHECK(!dinv.IsInStemPhase(hash));         // the clamp expired it; it was not pinned for years

    SetMockTime(0);
}

// No eligible peer -> skip (return false) instead of the old behaviour: a single origin-only peer
// looped forever in the do/while, and an empty list underflowed size()-1 and indexed out of range.
BOOST_AUTO_TEST_CASE(next_hop_zero_eligible_is_skipped)
{
    int64_t chosen = -999;
    BOOST_CHECK(!SelectDandelionNextHop({}, /*origin=*/7, chosen));   // empty peer list
    BOOST_CHECK(!SelectDandelionNextHop({7}, /*origin=*/7, chosen));  // only the origin is connected
}

// Exactly one eligible peer -> terminates and picks it (the case that used to loop forever).
BOOST_AUTO_TEST_CASE(next_hop_single_eligible_terminates_and_picks_it)
{
    int64_t chosen = -999;
    BOOST_CHECK(SelectDandelionNextHop({4}, /*origin=*/7, chosen));
    BOOST_CHECK_EQUAL(chosen, 4);

    chosen = -999;
    BOOST_CHECK(SelectDandelionNextHop({7, 4}, /*origin=*/7, chosen)); // origin present, one eligible
    BOOST_CHECK_EQUAL(chosen, 4);
}

// Larger list -> always an in-range peer that is never the origin, across many random draws.
BOOST_AUTO_TEST_CASE(next_hop_many_peers_excludes_origin_in_range)
{
    const std::vector<int64_t> peers = {1, 2, 3, 7, 4, 5};
    const int64_t origin = 7;
    for (int i = 0; i < 1000; ++i) {
        int64_t chosen = -999;
        BOOST_CHECK(SelectDandelionNextHop(peers, origin, chosen));
        BOOST_CHECK(chosen != origin);
        BOOST_CHECK(std::find(peers.begin(), peers.end(), chosen) != peers.end());
    }
}

BOOST_AUTO_TEST_SUITE_END()
