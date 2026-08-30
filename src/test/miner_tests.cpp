// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <validation.h>
#include <miner.h>
#include <policy/policy.h>
#include <pubkey.h>
#include <script/standard.h>
#include <txmempool.h>
#include <uint256.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <pow.h>
#include <index/txindex.h>
#include <libzerocoin/Coin.h>
#include <primitives/zerocoin.h>
#include <txdb.h>
#include <util/memory.h>
#include <util/time.h>
#include <veil/zerocoin/zchain.h>

#include <test/test_veil.h>

#include <memory>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(miner_tests, TestingSetup)

// BOOST_CHECK_EXCEPTION predicates to check the specific validation error
class HasReason {
public:
    HasReason(const std::string& reason) : m_reason(reason) {}
    bool operator() (const std::runtime_error& e) const {
        return std::string(e.what()).find(m_reason) != std::string::npos;
    };
private:
    const std::string m_reason;
};

static CFeeRate blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);

static BlockAssembler AssemblerForTest(const CChainParams& params) {
    BlockAssembler::Options options;

    options.nBlockMaxWeight = MAX_BLOCK_WEIGHT;
    options.blockMinFeeRate = blockMinFeeRate;
    return BlockAssembler(params, options);
}

static
struct {
    unsigned char extranonce;
    unsigned int nonce;
} blockinfo[] = {
    {4, 2762203683}, {2, 365113247}, {1, 58045770}, {1, 1879353509},
    {2, 3460563608}, {2, 1389355416}, {1, 2007444688}, {2, 3143462788},
    {2, 2213743661}, {1, 1218919775}, {1, 4017999106}, {2, 1745471175},
    {2, 142846781}, {1, 142326757}, {2, 553953300}, {2, 3757796778},
    {1, 4002023944}, {2, 3124901826}, {1, 2802010588}, {1, 877077179},
    {3, 3593029427}, {2, 3969866900}, {2, 3392125992}, {1, 1805972488},
    {2, 22889778}, {1, 1846647676}, {2, 1139340052}, {2, 664153912},
    {2, 3044128027}, {2, 3010194983}, {2, 3514377385}, {2, 1807880927},
    {1, 1661815113}, {2, 2588692158}, {2, 1431037239}, {1, 3594379207},
    {2, 2701839377}, {1, 1973060451}, {2, 4218620173}, {1, 3895224885},
    {1, 3812528855}, {3, 1703867853}, {2, 3123904292}, {5, 36151566},
    {1, 2846294357}, {5, 3499546633}, {1, 924279156}, {1, 2198572305},
    {1, 3336802574}, {2, 2185182377}, {1, 2575632457}, {1, 1965161345},
    {1, 2865408939}, {1, 3594887916}, {5, 2062161794}, {5, 2641251195},
    {1, 1290342360}, {1, 2883355437}, {6, 1244984106}, {2, 4065479708},
    {2, 465482400}, {1, 2907174250}, {1, 2668119617}, {1, 363418432},
    {2, 3520059103}, {2, 4165715942}, {1, 262701229}, {1, 3764360746},
    {1, 2027337848}, {5, 1043870112}, {5, 1937510025}, {1, 1664600435},
    {1, 1835070335}, {2, 2288073289}, {2, 3915808407}, {1, 3086668046},
    {2, 431164525}, {1, 1519443927}, {2, 1540918490}, {2, 2496662638},
    {1, 2847536504}, {1, 977330052}, {1, 1458105173}, {5, 2241824422},
    {1, 4254010326}, {1, 722249656}, {1, 3127968622}, {1, 1903212114},
    {1, 1240076798}, {1, 1762708584}, {1, 1681745075}, {2, 1411797252},
    {0, 2548738154}, {1, 1427604842}, {2, 65602055}, {2, 2566089199},
    {2, 3229392033}, {1, 3756169963}, {1, 826496712}, {1, 3015031264},
    {1, 1935361125}, {1, 1648108884}, {1, 3545375322}, {5, 4106905061},
    {2, 4107039329}, {1, 1528565221}, {1, 2707745393}, {1, 3149881008},
    {2, 4263295470}, {2, 4263284471},
};

static CBlockIndex CreateBlockIndex(int nHeight)
{
    CBlockIndex index;
    index.nHeight = nHeight;
    index.pprev = chainActive.Tip();
    return index;
}

static bool TestSequenceLocks(const CTransaction &tx, int flags)
{
    LOCK(mempool.cs);
    return CheckSequenceLocks(tx, flags);
}

// Test suite for ancestor feerate transaction selection.
// Implemented as an additional function, rather than a separate test case,
// to allow reusing the blockchain created in CreateNewBlock_validity.
static void TestPackageSelection(const CChainParams& chainparams, const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst) EXCLUSIVE_LOCKS_REQUIRED(::mempool.cs)
{
    // Test the ancestor feerate transaction selection.
    TestMemPoolEntryHelper entry;

    // Test that a medium fee transaction will be selected after a higher fee
    // rate package with a low fee rate parent.
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vpout.resize(1);
    tx.vpout[0]->SetValue(5000000000LL - 1000);
    // This tx has a low fee: 1000 satoshis
    uint256 hashParentTx = tx.GetHash(); // save this txid for later use
    mempool.addUnchecked(hashParentTx, entry.Fee(1000).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a medium fee: 10000 satoshis
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vpout[0]->SetValue(5000000000LL - 10000);
    uint256 hashMediumFeeTx = tx.GetHash();
    mempool.addUnchecked(hashMediumFeeTx, entry.Fee(10000).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a high fee, but depends on the first transaction
    tx.vin[0].prevout.hash = hashParentTx;
    tx.vpout[0]->SetValue(5000000000LL - 1000 - 50000); // 50k satoshi fee
    uint256 hashHighFeeTx = tx.GetHash();
    mempool.addUnchecked(hashHighFeeTx, entry.Fee(50000).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));

    std::unique_ptr<CBlockTemplate> pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate->block.vtx[1]->GetHash() == hashParentTx);
    BOOST_CHECK(pblocktemplate->block.vtx[2]->GetHash() == hashHighFeeTx);
    BOOST_CHECK(pblocktemplate->block.vtx[3]->GetHash() == hashMediumFeeTx);

    // Test that a package below the block min tx fee doesn't get included
    tx.vin[0].prevout.hash = hashHighFeeTx;
    tx.vpout[0]->SetValue(5000000000LL - 1000 - 50000); // 0 fee
    uint256 hashFreeTx = tx.GetHash();
    mempool.addUnchecked(hashFreeTx, entry.Fee(0).FromTx(tx));
    size_t freeTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);

    // Calculate a fee on child transaction that will put the package just
    // below the block min tx fee (assuming 1 child tx of the same size).
    CAmount feeToUse = blockMinFeeRate.GetFee(2*freeTxSize) - 1;

    tx.vin[0].prevout.hash = hashFreeTx;
    tx.vpout[0]->SetValue(5000000000LL - 1000 - 50000 - feeToUse);
    uint256 hashLowFeeTx = tx.GetHash();
    mempool.addUnchecked(hashLowFeeTx, entry.Fee(feeToUse).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    // Verify that the free tx and the low fee tx didn't get selected
    for (size_t i=0; i<pblocktemplate->block.vtx.size(); ++i) {
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashFreeTx);
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashLowFeeTx);
    }

    // Test that packages above the min relay fee do get included, even if one
    // of the transactions is below the min relay fee
    // Remove the low fee transaction and replace with a higher fee transaction
    mempool.removeRecursive(tx);
    tx.vpout[0]->AddToValue(-2); // Now we should be just over the min relay fee
    hashLowFeeTx = tx.GetHash();
    mempool.addUnchecked(hashLowFeeTx, entry.Fee(feeToUse+2).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate->block.vtx[4]->GetHash() == hashFreeTx);
    BOOST_CHECK(pblocktemplate->block.vtx[5]->GetHash() == hashLowFeeTx);

    // Test that transaction selection properly updates ancestor fee
    // calculations as ancestor transactions get included in a block.
    // Add a 0-fee transaction that has 2 outputs.
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vpout.resize(2);
    tx.vpout[0]->SetValue(5000000000LL - 100000000);
    tx.vpout[1]->SetValue(100000000); // 1BTC output
    uint256 hashFreeTx2 = tx.GetHash();
    mempool.addUnchecked(hashFreeTx2, entry.Fee(0).SpendsCoinbase(true).FromTx(tx));

    // This tx can't be mined by itself
    tx.vin[0].prevout.hash = hashFreeTx2;
    tx.vpout.resize(1);
    feeToUse = blockMinFeeRate.GetFee(freeTxSize);
    tx.vpout[0]->SetValue(5000000000LL - 100000000 - feeToUse);
    uint256 hashLowFeeTx2 = tx.GetHash();
    mempool.addUnchecked(hashLowFeeTx2, entry.Fee(feeToUse).SpendsCoinbase(false).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);

    // Verify that this tx isn't selected.
    for (size_t i=0; i<pblocktemplate->block.vtx.size(); ++i) {
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashFreeTx2);
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashLowFeeTx2);
    }

    // This tx will be mineable, and should cause hashLowFeeTx2 to be selected
    // as well.
    tx.vin[0].prevout.n = 1;
    tx.vpout[0]->SetValue(100000000 - 10000); // 10k satoshi fee
    mempool.addUnchecked(tx.GetHash(), entry.Fee(10000).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate->block.vtx[8]->GetHash() == hashLowFeeTx2);
}

// TODO Check to see if we can get this test case working
/*
// NOTE: These tests rely on CreateNewBlock doing its own self-validation!
BOOST_AUTO_TEST_CASE(CreateNewBlock_validity)
{
    // Note that by default, these tests run with size accounting enabled.
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    const CChainParams& chainparams = *chainParams;
    CScript scriptPubKey = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    CMutableTransaction tx;
    CScript script;
    uint256 hash;
    TestMemPoolEntryHelper entry;
    entry.nFee = 11;
    entry.nHeight = 11;

    fCheckpointsEnabled = false;

    // Simple block creation, nothing special yet:
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    // We can't make transactions until we have inputs
    // Therefore, load 100 blocks :)
    int baseheight = 0;
    std::vector<CTransactionRef> txFirst;
    for (unsigned int i = 0; i < sizeof(blockinfo)/sizeof(*blockinfo); ++i)
    {
        CBlock *pblock = &pblocktemplate->block; // pointer for convenience
        {
            LOCK(cs_main);
            pblock->nVersion = 1;
            pblock->nTime = chainActive.Tip()->GetMedianTimePast()+1;
            CMutableTransaction txCoinbase(*pblock->vtx[0]);
            txCoinbase.nVersion = 1;
            txCoinbase.vin[0].scriptSig = CScript();
            txCoinbase.vin[0].scriptSig.push_back(blockinfo[i].extranonce);
            txCoinbase.vin[0].scriptSig.push_back(chainActive.Height());
            txCoinbase.vout.resize(2); // Ignore the (optional) segwit commitment added by CreateNewBlock (as the hardcoded nonces don't account for this)
            txCoinbase.vout[0].scriptPubKey = CScript();
            pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
            if (txFirst.size() == 0)
                baseheight = chainActive.Height();
            if (txFirst.size() < 4)
                txFirst.push_back(pblock->vtx[0]);
            pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
            pblock->nNonce = blockinfo[i].nonce;
            pblock->nBits = GetNextWorkRequired(chainActive.Tip(), pblock, chainparams.GetConsensus());
            if (pblock->IsProofOfWork()) {
                while (!CheckProofOfWork(pblock->GetX16RTPoWHash(), pblock->nBits, chainparams.GetConsensus())) {
                    pblock->nNonce++;
                }
            }
        }
        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        BOOST_CHECK(ProcessNewBlock(chainparams, shared_pblock, true, nullptr));
        pblock->hashPrevBlock = pblock->GetHash();
    }

    LOCK(cs_main);
    LOCK(::mempool.cs);

    // Just to make sure we can still make simple blocks
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    const CAmount BLOCKSUBSIDY = 50*COIN;
    const CAmount LOWFEE = CENT;
    const CAmount HIGHFEE = COIN;
    const CAmount HIGHERFEE = 4*COIN;

    // block sigops > limit: 1000 CHECKMULTISIG + 1
    tx.vin.resize(1);
    // NOTE: OP_NOP is used to force 20 SigOps for the CHECKMULTISIG
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_0 << OP_0 << OP_NOP << OP_CHECKMULTISIG << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vpout[0]->SetValue(BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 1001; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        // If we don't set the # of sig ops in the CTxMemPoolEntry, template creation fails
        mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }

    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-blk-sigops"));
    mempool.clear();

    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vpout[0]->SetValue(BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 1001; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        // If we do set the # of sig ops in the CTxMemPoolEntry, template creation passes
        mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).SigOpsCost(80).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    mempool.clear();

    // block size > limit
    tx.vin[0].scriptSig = CScript();
    // 18 * (520char + DROP) + OP_1 = 9433 bytes
    std::vector<unsigned char> vchData(520);
    for (unsigned int i = 0; i < 18; ++i)
        tx.vin[0].scriptSig << vchData << OP_DROP;
    tx.vin[0].scriptSig << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vpout[0]->SetValue(BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 128; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    mempool.clear();

    // orphan in mempool, template creation fails
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).FromTx(tx));
    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    mempool.clear();

    // child with higher feerate than parent
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vpout[0]->SetValue(BLOCKSUBSIDY-HIGHFEE;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin.resize(2);
    tx.vin[1].scriptSig = CScript() << OP_1;
    tx.vin[1].prevout.hash = txFirst[0]->GetHash();
    tx.vin[1].prevout.n = 0;
    tx.vpout[0]->SetValue(tx.vout[0].nValue+BLOCKSUBSIDY-HIGHERFEE; //First txn output + fresh coinbase - new txn fee
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHERFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    mempool.clear();

    // coinbase in mempool, template creation fails
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_1;
    tx.vpout[0]->SetValue(0;
    hash = tx.GetHash();
    // give it a fee so it'll get mined
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    // Should throw bad-cb-multiple
    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-cb-multiple"));
    mempool.clear();

    // double spend txn pair in mempool, template creation fails
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vpout[0]->SetValue(BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vout[0].scriptPubKey = CScript() << OP_2;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    mempool.clear();

    // subsidy changing
    int nHeight = chainActive.Height();
    // Create an actual 209999-long block chain (without valid blocks).
    while (chainActive.Tip()->nHeight < 209999) {
        CBlockIndex* prev = chainActive.Tip();
        CBlockIndex* next = new CBlockIndex();
        next->phashBlock = new uint256(InsecureRand256());
        pcoinsTip->SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        chainActive.SetTip(next);
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    // Extend to a 210000-long block chain.
    while (chainActive.Tip()->nHeight < 210000) {
        CBlockIndex* prev = chainActive.Tip();
        CBlockIndex* next = new CBlockIndex();
        next->phashBlock = new uint256(InsecureRand256());
        pcoinsTip->SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        chainActive.SetTip(next);
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    // invalid p2sh txn in mempool, template creation fails
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vpout[0]->SetValue(BLOCKSUBSIDY-LOWFEE;
    script = CScript() << OP_0;
    tx.vout[0].scriptPubKey = GetScriptForDestination(CScriptID(script));
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(script.begin(), script.end());
    tx.vout[0].nValue -= LOWFEE;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    // Should throw block-validation-failed
    BOOST_CHECK_EXCEPTION(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("block-validation-failed"));
    mempool.clear();

    // Delete the dummy blocks again.
    while (chainActive.Tip()->nHeight > nHeight) {
        CBlockIndex* del = chainActive.Tip();
        chainActive.SetTip(del->pprev);
        pcoinsTip->SetBestBlock(del->pprev->GetBlockHash());
        delete del->phashBlock;
        delete del;
    }

    // non-final txs in mempool
    SetMockTime(chainActive.Tip()->GetMedianTimePast()+1);
    int flags = LOCKTIME_VERIFY_SEQUENCE|LOCKTIME_MEDIAN_TIME_PAST;
    // height map
    std::vector<int> prevheights;

    // relative height locked
    tx.nVersion = 2;
    tx.vin.resize(1);
    prevheights.resize(1);
    tx.vin[0].prevout.hash = txFirst[0]->GetHash(); // only 1 transaction
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].nSequence = chainActive.Tip()->nHeight + 1; // txFirst[0] is the 2nd block
    prevheights[0] = baseheight + 1;
    tx.vout.resize(1);
    tx.vpout[0]->SetValue(BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    tx.nLockTime = 0;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail
    BOOST_CHECK(SequenceLocks(tx, flags, &prevheights, CreateBlockIndex(chainActive.Tip()->nHeight + 2))); // Sequence locks pass on 2nd block

    // relative time locked
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | (((chainActive.Tip()->GetMedianTimePast()+1-chainActive[1]->GetMedianTimePast()) >> CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) + 1); // txFirst[1] is the 3rd block
    prevheights[0] = baseheight + 2;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail

    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast
    BOOST_CHECK(SequenceLocks(tx, flags, &prevheights, CreateBlockIndex(chainActive.Tip()->nHeight + 1))); // Sequence locks pass 512 seconds later
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime -= 512; //undo tricked MTP

    // absolute height locked
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL - 1;
    prevheights[0] = baseheight + 3;
    tx.nLockTime = chainActive.Tip()->nHeight + 1;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(tx, flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(tx, chainActive.Tip()->nHeight + 2, chainActive.Tip()->GetMedianTimePast())); // Locktime passes on 2nd block

    // absolute time locked
    tx.vin[0].prevout.hash = txFirst[3]->GetHash();
    tx.nLockTime = chainActive.Tip()->GetMedianTimePast();
    prevheights.resize(1);
    prevheights[0] = baseheight + 4;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(tx, flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(tx, chainActive.Tip()->nHeight + 2, chainActive.Tip()->GetMedianTimePast() + 1)); // Locktime passes 1 second later

    // mempool-dependent transactions (not added)
    tx.vin[0].prevout.hash = hash;
    prevheights[0] = chainActive.Tip()->nHeight + 1;
    tx.nLockTime = 0;
    tx.vin[0].nSequence = 0;
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    tx.vin[0].nSequence = 1;
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail

    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    // None of the of the absolute height/time locked tx should have made
    // it into the template because we still check IsFinalTx in CreateNewBlock,
    // but relative locked txs will if inconsistently added to mempool.
    // For now these will still generate a valid template until BIP68 soft fork
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 3U);
    // However if we advance height by 1 and time by 512, all of them should be mined
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast
    chainActive.Tip()->nHeight++;
    SetMockTime(chainActive.Tip()->GetMedianTimePast() + 1);

    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 5U);

    chainActive.Tip()->nHeight--;
    SetMockTime(0);
    mempool.clear();

    TestPackageSelection(chainparams, scriptPubKey, txFirst);

    fCheckpointsEnabled = true;
}
*/

// A transaction that is already confirmed on chain must never be mineable again, and
// must never be re-admitted to the mempool. If one ever lands in the mempool it is stuck
// forever: ConnectBlock rejects any block containing it with bad-txns-BIP30, so the node
// stops producing blocks entirely until the mempool is cleared. This happened on testnet
// when a zerocoin spend was accepted into the mempool in the same instant its block
// connected, and took the node's block production down for three and a half days.
BOOST_AUTO_TEST_CASE(CreateNewBlock_skips_already_confirmed)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::REGTEST);
    const CChainParams& chainparams = *chainParams;
    CScript scriptPubKey = CScript() << OP_TRUE;
    TestMemPoolEntryHelper entry;

    // Build a transaction, then pretend its output is already in the UTXO set. That is
    // exactly the state a confirmed-but-still-in-mempool transaction leaves behind, and
    // exactly what the BIP30 check in ConnectBlock keys on.
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(uint256S("0000000000000000000000000000000000000000000000000000000000000001"), 0);
    tx.vin[0].scriptSig = CScript() << OP_11;
    tx.vpout.resize(1);
    tx.vpout[0] = MAKE_OUTPUT<CTxOutStandard>();
    tx.vpout[0]->SetScriptPubKey(CScript() << OP_11 << OP_EQUAL);
    tx.vpout[0]->SetValue(5000000000LL);
    const uint256 txid = tx.GetHash();

    {
        LOCK(cs_main);
        CTxOut out(5000000000LL, CScript() << OP_11 << OP_EQUAL);
        pcoinsTip->AddCoin(COutPoint(txid, 0), Coin(std::move(out), 1, false), false);
        BOOST_CHECK(pcoinsTip->HaveCoin(COutPoint(txid, 0)));
    }

    // Put it straight into the mempool, the way it got there in production: a tx accepted
    // in the same instant its block connected, so removeForBlock had already run. This is
    // the state the fix has to survive. addUnchecked bypasses acceptance so the coins-view
    // state above is the only thing that matters.
    {
        LOCK(cs_main);
        LOCK(mempool.cs);
        mempool.addUnchecked(txid, entry.Fee(10000).FromTx(tx));
        BOOST_CHECK(mempool.exists(txid));
    }

    // Before the fix, CreateNewBlock happily selected it and the resulting block was
    // rejected by ConnectBlock as bad-txns-BIP30, so the node produced nothing. Now the
    // assembler must skip it and sweep it out.
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    for (const auto& btx : pblocktemplate->block.vtx)
        BOOST_CHECK(btx->GetHash() != txid);   // not selected into the block
    BOOST_CHECK(!mempool.exists(txid));         // and removed from the mempool
}

// The zerocoin sibling of the case above: the mempool transaction itself never
// confirmed, but its pubcoin was already accumulated by a DIFFERENT transaction, the
// state deterministic mints produce when two wallets derive from one seed. The dedup
// pass in CreateNewBlock has always checked for this, but it asked "is the accumulating
// tx in the chain" with the tip as the reference index, and a reference index excludes
// its own block, so a pubcoin accumulated by the tip block was invisible. ConnectBlock
// checks from the new block's index and does see it, so TestBlockValidity failed and no
// block was produced. A staker that is the chain's only block producer can never
// advance the tip past the accumulating block that way, and bricks permanently.
BOOST_FIXTURE_TEST_CASE(CreateNewBlock_sweeps_mint_accumulated_in_tip, TestChain100Setup)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::REGTEST);
    const CChainParams& chainparams = *chainParams;
    CScript scriptPubKey = CScript() << OP_TRUE;
    TestMemPoolEntryHelper entry;

    // TestingSetup does not create a zerocoin db; use an in-memory one.
    pzerocoinDB.reset(new CZerocoinDB(1 << 23, true /* fMemory */));

    // A real pubcoin, recorded as accumulated by the coinbase of the TIP block.
    libzerocoin::PrivateCoin priv(Params().Zerocoin_Params(), libzerocoin::CoinDenomination::ZQ_TEN, true);
    const libzerocoin::PublicCoin pub = priv.getPublicCoin();
    const uint256 txidAccumulated = m_coinbase_txns.back()->GetHash();
    {
        std::map<libzerocoin::PublicCoin, uint256> mintInfo;
        mintInfo.emplace(pub, txidAccumulated);
        BOOST_CHECK(pzerocoinDB->WriteCoinMintBatch(mintInfo));
    }

    // The trap itself, spelled out: a reference index cannot see its own block, only
    // the plain active-chain lookup can. This is why the mempool side must never pass
    // the tip as a reference.
    {
        LOCK(cs_main);
        int nHeightDummy = 0;
        const uint256 hashTip = chainActive.Tip()->GetBlockHash();
        BOOST_CHECK(!IsBlockHashInChain(hashTip, nHeightDummy, chainActive.Tip()));
        BOOST_CHECK(IsBlockHashInChain(hashTip, nHeightDummy, nullptr));
    }

    // A different transaction minting the same pubcoin, stuck in the mempool. Real
    // input coin so nothing else disqualifies it from selection.
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(uint256S("0000000000000000000000000000000000000000000000000000000000000002"), 0);
    tx.vin[0].scriptSig = CScript() << OP_11;
    CScript scriptMint = CScript() << OP_ZEROCOINMINT << pub.getValue().getvch().size() << pub.getValue().getvch();
    tx.vpout.resize(1);
    tx.vpout[0] = CTxOut(libzerocoin::ZerocoinDenominationToAmount(pub.getDenomination()), scriptMint).GetSharedPtr();
    const uint256 txidZombie = tx.GetHash();

    // The crafted mint output parses back to the exact pubcoin hash the db record was
    // written under, so the dedup pass is looking at the right key.
    {
        const CTransaction ctx(tx);
        std::set<uint256> setHashes;
        BOOST_CHECK(TxToPubcoinHashSet(&ctx, setHashes));
        BOOST_CHECK(setHashes.count(GetPubCoinHash(pub.getValue())));
    }

    {
        LOCK(cs_main);
        CTxOut in(libzerocoin::ZerocoinDenominationToAmount(pub.getDenomination()) + 100000, CScript() << OP_11 << OP_EQUAL);
        pcoinsTip->AddCoin(tx.vin[0].prevout, Coin(std::move(in), 1, false), false);
        BOOST_CHECK(pcoinsTip->HaveCoin(tx.vin[0].prevout));
    }
    {
        LOCK(cs_main);
        LOCK(mempool.cs);
        mempool.addUnchecked(txidZombie, entry.Fee(10000).FromTx(tx));
        BOOST_CHECK(mempool.exists(txidZombie));
    }

    // Before the fix the template carried the duplicate mint and TestBlockValidity
    // failed on it, so this returned null on every attempt. Now the dedup pass sees
    // the tip-block accumulation, skips the transaction and sweeps it out.
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    if (pblocktemplate) {
        for (const auto& btx : pblocktemplate->block.vtx)
            BOOST_CHECK(btx->GetHash() != txidZombie);
    }
    BOOST_CHECK(!mempool.exists(txidZombie));

    pzerocoinDB.reset();
}

// IsTransactionInChain answered "not in chain" for any transaction sitting in the
// mempool, even one that was also confirmed: GetTransaction prefers the mempool and
// returns a null block hash on a hit there. A confirmed transaction stuck in the
// mempool is exactly the zombie state above, so the one moment the dedup pass most
// needed the truth was the one moment the lookup lied. With a txindex available the
// lookup must see through the mempool.
BOOST_FIXTURE_TEST_CASE(transaction_in_chain_seen_through_mempool, TestChain100Setup)
{
    TestMemPoolEntryHelper entry;

    // Put a long-confirmed transaction (block 1's coinbase) back into the mempool.
    // addUnchecked bypasses acceptance on purpose, mirroring the race that creates
    // the stuck state in production.
    const CTransactionRef txConfirmed = m_coinbase_txns.front();
    const uint256 txid = txConfirmed->GetHash();
    {
        LOCK(cs_main);
        LOCK(mempool.cs);
        mempool.addUnchecked(txid, entry.Fee(10000).FromTx(CMutableTransaction(*txConfirmed)));
        BOOST_CHECK(mempool.exists(txid));
    }

    // Bring up the global txindex the fixed lookup consults when the mempool masks
    // the answer.
    g_txindex = MakeUnique<TxIndex>(1 << 20, true);
    g_txindex->Start();
    constexpr int64_t timeout_ms = 10 * 1000;
    const int64_t time_start = GetTimeMillis();
    while (!g_txindex->BlockUntilSyncedToCurrentChain()) {
        BOOST_REQUIRE(time_start + timeout_ms > GetTimeMillis());
        UninterruptibleSleep(std::chrono::milliseconds{100});
    }

    int nHeightTx = 0;
    // In the chain and in the mempool at once: still in the chain.
    BOOST_CHECK(IsTransactionInChain(txid, nHeightTx, Params().GetConsensus()));
    BOOST_CHECK_EQUAL(nHeightTx, 1);

    // A transaction that only exists in the mempool must still count as unconfirmed.
    CMutableTransaction txLoose;
    txLoose.vin.resize(1);
    txLoose.vin[0].prevout = COutPoint(uint256S("0000000000000000000000000000000000000000000000000000000000000003"), 0);
    txLoose.vin[0].scriptSig = CScript() << OP_11;
    txLoose.vpout.resize(1);
    txLoose.vpout[0] = CTxOut(1 * COIN, CScript() << OP_11 << OP_EQUAL).GetSharedPtr();
    const uint256 txidLoose = txLoose.GetHash();
    {
        LOCK(cs_main);
        LOCK(mempool.cs);
        mempool.addUnchecked(txidLoose, entry.Fee(10000).FromTx(txLoose));
    }
    BOOST_CHECK(!IsTransactionInChain(txidLoose, nHeightTx, Params().GetConsensus()));

    g_txindex->Stop(); // Stop thread before calling destructor
    g_txindex.reset();
}

BOOST_AUTO_TEST_SUITE_END()
