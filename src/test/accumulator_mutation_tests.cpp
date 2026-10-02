// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_veil.h>

#include <consensus/validation.h>
#include <hash.h>
#include <libzerocoin/Denominations.h>
#include <primitives/block.h>
#include <uint256.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(accumulator_mutation_tests, BasicTestingSetup)

// A post PoW update block whose header commits to its accumulator body.
static CBlock MakeCommittedBlock()
{
    CBlock block;
    block.nTime = nPowTimeStampActive;
    block.mapAccumulatorHashes[libzerocoin::ZQ_TEN] = uint256S("01");
    block.mapAccumulatorHashes[libzerocoin::ZQ_ONE_HUNDRED] = uint256S("02");
    block.hashAccumulators = SerializeHash(block.mapAccumulatorHashes);
    return block;
}

// A body that matches its header commitment passes.
BOOST_AUTO_TEST_CASE(committed_body_passes)
{
    CBlock block = MakeCommittedBlock();
    CValidationState state;
    BOOST_CHECK(CheckAccumulatorBodyCommitment(block, state));
    BOOST_CHECK(state.IsValid());
}

// A mutated body is rejected and flagged as a possible corruption. That flag is what stops
// InvalidBlockFound from permanently marking the shared header hash BLOCK_FAILED_VALID, so the
// canonical block with the same hash stays acceptable after a mutated copy has been seen.
BOOST_AUTO_TEST_CASE(mutated_body_is_rejected_as_possible_corruption)
{
    CBlock block = MakeCommittedBlock();
    block.mapAccumulatorHashes[libzerocoin::ZQ_TEN] = uint256S("ff"); // body no longer matches the header
    CValidationState state;
    BOOST_CHECK(!CheckAccumulatorBodyCommitment(block, state));
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK(state.CorruptionPossible());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-accumulators-mutated");
}

// Before the PoW update the body is committed through hashVeilData and checked in ConnectBlock,
// so this check is a no-op there and can never reject a legacy block.
BOOST_AUTO_TEST_CASE(legacy_blocks_are_not_checked_here)
{
    CBlock block = MakeCommittedBlock();
    block.nTime = nPowTimeStampActive - 1;
    block.hashAccumulators = uint256S("deadbeef"); // would mismatch if the check ran
    CValidationState state;
    BOOST_CHECK(CheckAccumulatorBodyCommitment(block, state));
    BOOST_CHECK(state.IsValid());
}

BOOST_AUTO_TEST_SUITE_END()
