// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_veil.h>

#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <miner.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <streams.h>
#include <validation.h>
#include <version.h>

#include <boost/test/unit_test.hpp>

// The fProofOfStake byte in the block header is not part of the block hash. These tests pin down
// that consensus routes on the hashed nVersion algo bits and on the block content instead, so a
// peer that rewrites the byte in transit cannot change how a block gets validated.

static CBlockHeader MakePostUpdateHeader(int32_t nAlgoBits, uint8_t fPoSByte)
{
    CBlockHeader header;
    header.nVersion = (NEW_POW_BLOCK_VERSION << BITS_TO_BLOCK_VERSION) | nAlgoBits;
    header.nTime = nPowTimeStampActive + 60;
    header.nBits = 0x207fffff;
    header.fProofOfStake = fPoSByte;
    return header;
}

static CTransactionRef MakeCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << 1 << OP_0;
    tx.vpout.push_back(MAKE_OUTPUT<CTxOutStandard>(1 * COIN, CScript() << OP_TRUE));
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

BOOST_FIXTURE_TEST_SUITE(pos_header_type_tests, BasicTestingSetup)

// Root cause: flipping the byte does not change the hash, and the byte does travel on the wire.
BOOST_AUTO_TEST_CASE(pos_byte_is_not_part_of_the_hash)
{
    CBlockHeader stake = MakePostUpdateHeader(0, 1);
    CBlockHeader stakeFlipped = stake;
    stakeFlipped.fProofOfStake = 0;
    BOOST_CHECK(stake.GetHash() == stakeFlipped.GetHash());

    CBlockHeader work = MakePostUpdateHeader(CBlockHeader::PROGPOW_BLOCK, 0);
    CBlockHeader workFlipped = work;
    workFlipped.fProofOfStake = 1;
    BOOST_CHECK(work.GetHash() == workFlipped.GetHash());

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << workFlipped;
    CBlockHeader received;
    ss >> received;
    BOOST_CHECK_EQUAL((int)received.fProofOfStake, 1);
    BOOST_CHECK(received.GetHash() == work.GetHash());
}

// After the PoW update the type is read from the hashed version bits and the byte is ignored.
BOOST_AUTO_TEST_CASE(post_update_type_comes_from_the_hashed_version)
{
    for (int byte = 0; byte <= 1; byte++) {
        BOOST_CHECK(MakePostUpdateHeader(0, byte).IsProofOfStakeHeader());
        BOOST_CHECK(!MakePostUpdateHeader(CBlockHeader::PROGPOW_BLOCK, byte).IsProofOfStakeHeader());
        BOOST_CHECK(!MakePostUpdateHeader(CBlockHeader::RANDOMX_BLOCK, byte).IsProofOfStakeHeader());
        BOOST_CHECK(!MakePostUpdateHeader(CBlockHeader::SHA256D_BLOCK, byte).IsProofOfStakeHeader());
        // More than one algo bit is not a stake header either. It fails closed as work.
        BOOST_CHECK(!MakePostUpdateHeader(CBlockHeader::PROGPOW_BLOCK | CBlockHeader::SHA256D_BLOCK, byte).IsProofOfStakeHeader());
    }
}

// Before the update the byte was the only carrier of the type, so that era keeps reading it.
BOOST_AUTO_TEST_CASE(pre_update_type_still_comes_from_the_byte)
{
    CBlockHeader header = MakePostUpdateHeader(0, 1);
    header.nTime = nPowTimeStampActive - 1;
    BOOST_CHECK(header.IsProofOfStakeHeader());
    header.fProofOfStake = 0;
    BOOST_CHECK(!header.IsProofOfStakeHeader());
}

// A work block under a stake version is permanently bad, both sides are covered by the hash.
BOOST_AUTO_TEST_CASE(check_block_rejects_work_content_under_a_stake_version)
{
    CBlock block(MakePostUpdateHeader(0, 0));
    block.vtx.push_back(MakeCoinbase());
    BOOST_CHECK(!block.IsProofOfStake());

    CValidationState state;
    BOOST_CHECK(!CheckBlock(block, state, Params().GetConsensus(), /*fSkipComputation=*/true, /*fCheckPOW=*/false, /*fCheckMerkleRoot=*/false));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-pos-version");
    BOOST_CHECK(!state.CorruptionPossible());
}

// A stake block under a work version is rejected the same way, before any signature or kernel work.
BOOST_AUTO_TEST_CASE(check_block_rejects_stake_content_under_a_work_version)
{
    CBlock block(MakePostUpdateHeader(CBlockHeader::PROGPOW_BLOCK, 1));
    block.vtx.push_back(MakeCoinbase());
    block.vtx.push_back(MakeCoinstakeShape());
    BOOST_CHECK(block.IsProofOfStake());

    CValidationState state;
    BOOST_CHECK(!CheckBlock(block, state, Params().GetConsensus(), true, false, false));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-pos-version");
    BOOST_CHECK(!state.CorruptionPossible());
}

// A work block whose version and content agree is not touched by the new check.
BOOST_AUTO_TEST_CASE(check_block_leaves_a_matching_work_block_alone)
{
    CBlock block(MakePostUpdateHeader(CBlockHeader::PROGPOW_BLOCK, 0));
    block.vtx.push_back(MakeCoinbase());

    CValidationState state;
    CheckBlock(block, state, Params().GetConsensus(), true, false, false);
    BOOST_CHECK(state.GetRejectReason() != "bad-pos-version");
}

BOOST_AUTO_TEST_SUITE_END()

struct PosHeaderRegtestSetup : public TestingSetup {
    PosHeaderRegtestSetup() : TestingSetup(CBaseChainParams::REGTEST) {}
};

BOOST_FIXTURE_TEST_SUITE(pos_header_type_index_tests, PosHeaderRegtestSetup)

// Regtest skips the proof of work check in CheckBlockHeader, which is exactly what lets this test
// watch the routing decision on its own. The only thing deciding how each header lands in the
// block index is the type derivation, and the byte is set to lie in both directions.
BOOST_AUTO_TEST_CASE(index_flag_follows_the_hashed_header_not_the_byte)
{
    const CBlockIndex* pindexGenesis = chainActive.Genesis();
    BOOST_REQUIRE(pindexGenesis);
    const uint32_t nTime = std::max<uint32_t>(nPowTimeStampActive, pindexGenesis->nTime) + 60;

    // Stake version, byte claims work.
    CBlockHeader stake = MakePostUpdateHeader(0, 0);
    stake.hashPrevBlock = pindexGenesis->GetBlockHash();
    stake.nTime = nTime;
    stake.nNonce = 1;

    // Work version, byte claims stake.
    CBlockHeader work = MakePostUpdateHeader(CBlockHeader::PROGPOW_BLOCK, 1);
    work.hashPrevBlock = pindexGenesis->GetBlockHash();
    work.nTime = nTime;
    work.nHeight = 1;
    work.nNonce64 = 2;

    CValidationState state;
    const CBlockIndex* pindexStake = nullptr;
    BOOST_REQUIRE_MESSAGE(ProcessNewBlockHeaders({stake}, state, Params(), &pindexStake), FormatStateMessage(state));
    BOOST_REQUIRE(pindexStake);
    BOOST_CHECK(pindexStake->IsProofOfStake());
    BOOST_CHECK(!pindexStake->IsProofOfWork());

    const CBlockIndex* pindexWork = nullptr;
    BOOST_REQUIRE_MESSAGE(ProcessNewBlockHeaders({work}, state, Params(), &pindexWork), FormatStateMessage(state));
    BOOST_REQUIRE(pindexWork);
    BOOST_CHECK(pindexWork->IsProofOfWork());
    BOOST_CHECK(pindexWork->IsProgProofOfWork());
    BOOST_CHECK(!pindexWork->IsProofOfStake());
}

// Work templates from the block assembler always carry an algo bit, so re timing one across the
// PoW update boundary, the way validation_block_tests builds its chains, never turns it into a
// stake header and never trips the version versus content check.
BOOST_AUTO_TEST_CASE(work_templates_derive_as_work_on_both_sides_of_the_update)
{
    const CBlockIndex* pindexGenesis = chainActive.Genesis();
    BOOST_REQUIRE(pindexGenesis);

    std::unique_ptr<CBlockTemplate> ptemplate = BlockAssembler(Params()).CreateNewBlock(CScript() << OP_TRUE, false);
    BOOST_REQUIRE(ptemplate);
    CBlock block = ptemplate->block;
    BOOST_CHECK(block.IsProofOfWork());
    BOOST_CHECK(block.nVersion & (CBlockHeader::PROGPOW_BLOCK | CBlockHeader::RANDOMX_BLOCK | CBlockHeader::SHA256D_BLOCK));

    for (uint32_t nTime : {pindexGenesis->nTime + 1, nPowTimeStampActive - 1, nPowTimeStampActive, nPowTimeStampActive + 1000}) {
        block.nTime = nTime;
        block.fChecked = false;
        BOOST_CHECK(!block.IsProofOfStakeHeader());
        CValidationState state;
        CheckBlock(block, state, Params().GetConsensus(), true, false, false);
        BOOST_CHECK_MESSAGE(state.GetRejectReason() != "bad-pos-version", "nTime=" << nTime);
    }
}

BOOST_AUTO_TEST_SUITE_END()
