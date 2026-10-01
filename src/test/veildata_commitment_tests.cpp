// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_veil.h>

#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <libzerocoin/Denominations.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <streams.h>
#include <validation.h>
#include <version.h>

#include <boost/test/unit_test.hpp>

// Before the PoW update the merkle roots, the accumulator map and hashPoFN are body fields, outside
// the header hash, and hashVeilData is the one header field that commits to them. Nothing ever checked
// it. These tests pin down that a stake block of that era whose body does not reproduce its committed
// hashVeilData is refused before storage as a possible corruption, and that work blocks of that era,
// whose stored hashVeilData never matched their body, are left alone.

static CTransactionRef MakeCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << 1 << OP_0;
    tx.vpout.push_back(MAKE_OUTPUT<CTxOutStandard>());
    return MakeTransactionRef(std::move(tx));
}

// The shape CTransaction::IsCoinStake looks for: one zerocoin spend input and an empty first output.
static CTransactionRef MakeCoinstakeShape()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_ZEROCOINSPEND;
    tx.vpout.push_back(MAKE_OUTPUT<CTxOutStandard>());
    tx.vpout.push_back(MAKE_OUTPUT<CTxOutStandard>(1 * COIN, CScript() << OP_TRUE));
    return MakeTransactionRef(std::move(tx));
}

// A stake block from the era before the PoW update, with its body committed the way the staker did it.
static CBlock MakeLegacyStakeBlock()
{
    CBlock block;
    block.nVersion = OLD_POW_BLOCK_VERSION << BITS_TO_BLOCK_VERSION;
    block.nTime = nPowTimeStampActive - 1000;
    block.nBits = 0x1e0fffff;
    block.fProofOfStake = 1;
    block.vtx.push_back(MakeCoinbase());
    block.vtx.push_back(MakeCoinstakeShape());
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.hashWitnessMerkleRoot = BlockWitnessMerkleRoot(block);
    block.mapAccumulatorHashes[libzerocoin::ZQ_TEN] = uint256S("01");
    block.mapAccumulatorHashes[libzerocoin::ZQ_ONE_HUNDRED] = uint256S("02");
    block.hashPoFN = uint256S("03");
    block.hashVeilData = block.GetVeilDataHash();
    return block;
}

BOOST_FIXTURE_TEST_SUITE(veildata_commitment_tests, BasicTestingSetup)

// Root cause: in this era the body fields do not move the header hash, only hashVeilData does.
BOOST_AUTO_TEST_CASE(legacy_body_fields_are_outside_the_header_hash)
{
    CBlock block = MakeLegacyStakeBlock();
    const uint256 hash = block.GetHash();
    block.hashMerkleRoot = uint256S("ee");
    block.mapAccumulatorHashes[libzerocoin::ZQ_TEN] = uint256S("ff");
    block.hashPoFN = uint256S("dd");
    BOOST_CHECK(block.GetHash() == hash);
    block.hashVeilData = uint256S("cc");
    BOOST_CHECK(block.GetHash() != hash);
}

BOOST_AUTO_TEST_CASE(committed_stake_body_passes)
{
    CBlock block = MakeLegacyStakeBlock();
    CValidationState state;
    BOOST_CHECK(CheckVeilDataCommitment(block, state));
    BOOST_CHECK(state.IsValid());
}

// Each committed body field, mutated on its own, is refused without the hash being held against it.
BOOST_AUTO_TEST_CASE(mutated_stake_body_is_a_possible_corruption)
{
    const CBlock honest = MakeLegacyStakeBlock();

    CBlock mutatedMerkle = honest;
    mutatedMerkle.hashMerkleRoot = uint256S("ee");
    CBlock mutatedWitness = honest;
    mutatedWitness.hashWitnessMerkleRoot = uint256S("ed");
    CBlock mutatedMap = honest;
    mutatedMap.mapAccumulatorHashes[libzerocoin::ZQ_TEN] = uint256S("ff");
    CBlock mutatedProof = honest;
    mutatedProof.hashPoFN = uint256S("dd");

    for (const CBlock* pblock : {&mutatedMerkle, &mutatedWitness, &mutatedMap, &mutatedProof}) {
        CValidationState state;
        BOOST_CHECK(!CheckVeilDataCommitment(*pblock, state));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-veildata-mutated");
        BOOST_CHECK(state.CorruptionPossible());
        BOOST_CHECK(pblock->GetHash() == honest.GetHash());
    }
}

// The committed values travel with the block, so a clean copy still reproduces the commitment.
BOOST_AUTO_TEST_CASE(committed_stake_body_survives_the_wire)
{
    CBlock block = MakeLegacyStakeBlock();
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << block;
    CBlock received;
    ss >> received;
    BOOST_CHECK(received.GetHash() == block.GetHash());
    CValidationState state;
    BOOST_CHECK(CheckVeilDataCommitment(received, state));
}

// hashVeilData is only serialized under the old version bits. A stake block dated before the update
// but carrying the new bits arrives with no commitment at all and is refused by the same check.
BOOST_AUTO_TEST_CASE(legacy_stake_block_without_a_commitment_is_refused)
{
    CBlock block = MakeLegacyStakeBlock();
    block.nVersion = NEW_POW_BLOCK_VERSION << BITS_TO_BLOCK_VERSION;
    block.hashVeilData = uint256();
    CValidationState state;
    BOOST_CHECK(!CheckVeilDataCommitment(block, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-veildata-mutated");
    BOOST_CHECK(state.CorruptionPossible());
}

// Work blocks of that era carry a hashVeilData computed before mining changed the coinbase, so the
// commitment cannot be enforced for them and they are left alone.
BOOST_AUTO_TEST_CASE(legacy_work_blocks_are_not_checked)
{
    CBlock block = MakeLegacyStakeBlock();
    block.vtx.pop_back();
    block.fProofOfStake = 0;
    block.hashVeilData = uint256S("bb");
    BOOST_CHECK(!block.IsProofOfStake());
    CValidationState state;
    BOOST_CHECK(CheckVeilDataCommitment(block, state));
}

// After the PoW update the header commits to the body directly and hashVeilData is gone.
BOOST_AUTO_TEST_CASE(post_update_blocks_are_not_checked)
{
    CBlock block = MakeLegacyStakeBlock();
    block.nTime = nPowTimeStampActive;
    block.hashVeilData = uint256S("bb");
    CValidationState state;
    BOOST_CHECK(CheckVeilDataCommitment(block, state));
}

BOOST_AUTO_TEST_SUITE_END()
