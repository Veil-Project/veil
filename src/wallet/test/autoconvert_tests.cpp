// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for the -autoconvert failure handling (wallet.cpp AutoConvertToRingCT):
// a conversion transaction that is rejected by the mempool must leave no
// trace in the wallet — no transaction record, no inputs stuck in the
// pending-spend state, no balance change — and the backoff must delay the
// next attempt. This is the one part of the feature that mainnet happy-path
// testing cannot exercise.

#include <test/test_veil.h>

#include <consensus/validation.h>
#include <policy/policy.h>
#include <script/interpreter.h>
#include <txdb.h>
#include <txmempool.h>
#include <util/time.h>
#include <validation.h>
#include <veil/ringct/anonwallet.h>
#include <veil/ringct/blind.h>
#include <veil/ringct/stealth.h>
#include <veil/zerocoin/zwallet.h>
#include <wallet/wallet.h>

#include <boost/test/unit_test.hpp>

namespace {

//! Depth the autoconvert selector requires (AUTOCONVERT_MIN_DEPTH in
//! wallet.cpp) plus margin, on top of the CoinbaseMaturity() blocks the
//! TestChain100Setup fixture already mined.
constexpr int EXTRA_BLOCKS = 15;

struct AutoConvertTestingSetup : public TestChain100Setup {
    std::shared_ptr<CWallet> wallet;
    AnonWallet* panonwallet{nullptr};
    CScript coinbaseScript;

    AutoConvertTestingSetup()
    {
        // The anon wallet needs the stealth and blinding secp256k1 contexts
        // that AppInitMain normally starts; BasicTestingSetup only starts the
        // base ECC context.
        ECC_Start_Stealth();
        ECC_Start_Blinding();

        // ConnectBlock writes zerocoin accumulator checksums every ten blocks
        // (first at height 20), so a chain extended past the fixture's default
        // needs the zerocoin db that AppInitMain normally creates. In-memory,
        // like the fixture's pblocktree/pcoinsdbview.
        pzerocoinDB.reset(new CZerocoinDB(1 << 23, true /* fMemory */));

        // Extend the chain so the early coinbases clear both coinbase
        // maturity and the 12-confirmation floor the converter requires.
        // These are X16RT proof-of-work blocks; after the PowUpdateTimestamp
        // GetNextWorkRequired always returns the pow limit for them, so any
        // number of blocks can be mined at constant difficulty.
        coinbaseScript = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
        for (int i = 0; i < EXTRA_BLOCKS; i++) {
            CBlock b = CreateAndProcessBlock({}, coinbaseScript);
            m_coinbase_txns.push_back(b.vtx[0]);
        }

        // Wallet wired the same way CWallet::CreateWalletFromFile does it:
        // HD seed first (the zerocoin and anon wallet master keys derive from
        // it), then the zerocoin wallet, then the anon wallet on its own mock
        // database.
        wallet = std::make_shared<CWallet>("autoconvert-mock", WalletDatabase::CreateMock());
        bool fFirstRun;
        wallet->LoadWallet(fFirstRun);
        wallet->SetHDSeed(wallet->GenerateNewSeed());

        CzWallet* zwallet = new CzWallet(wallet.get());
        wallet->setZWallet(zwallet);

        std::shared_ptr<WalletDatabase> anonDatabase = WalletDatabase::CreateMock();
        {
            // Batches only create the backing (in-memory) db file when opened
            // with 'c' in the mode; AnonWallet's own batches open "r+".
            WalletBatch batch(*anonDatabase, "cr+");
        }
        panonwallet = new AnonWallet(wallet, "anonwallet", anonDatabase);
        CExtKey extMasterAnon;
        BOOST_REQUIRE(wallet->GetAnonWalletSeed(extMasterAnon));
        BOOST_REQUIRE(panonwallet->Initialise(&extMasterAnon));
        wallet->SetAnonWallet(panonwallet);
        wallet->SetBroadcastTransactions(true);

        // Hand the fixture's coinbase key to the wallet and pick up all the
        // mined coinbases.
        {
            LOCK(wallet->cs_wallet);
            wallet->AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());
        }
        WalletRescanReserver reserver(wallet.get());
        BOOST_REQUIRE(reserver.reserve());
        wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver, true /* fUpdate */);
    }

    ~AutoConvertTestingSetup()
    {
        SetMockTime(0);
        mempool.clear();
        wallet->SetAnonWallet(nullptr);
        delete panonwallet;
        pzerocoinDB.reset();
        ECC_Stop_Stealth();
        ECC_Stop_Blinding();
    }

    //! Find the coinbase output paying the fixture's script (the miner
    //! payment; Veil coinbases can carry additional budget outputs).
    bool FindMinerOutput(const CTransactionRef& coinbase, int& nOut, CAmount& nValue) const
    {
        for (size_t i = 0; i < coinbase->vpout.size(); i++) {
            const CTxOutBase* pout = coinbase->vpout[i].get();
            if (!pout->IsStandardOutput())
                continue;
            if (*pout->GetPScriptPubKey() == coinbaseScript && pout->GetValue() > 0) {
                nOut = (int)i;
                nValue = pout->GetValue();
                return true;
            }
        }
        return false;
    }

    //! Build and sign a transaction spending a fixture coinbase directly with
    //! the coinbase key, without going through the wallet. Submitted to the
    //! mempool, it conflicts with any wallet transaction that later selects
    //! the same coinbase — the wallet does not see foreign mempool spends, so
    //! its next conversion attempt is guaranteed to fail mempool acceptance.
    CMutableTransaction BuildForeignSpend(const CTransactionRef& coinbase) const
    {
        int nOut = -1;
        CAmount nValue = 0;
        BOOST_REQUIRE(FindMinerOutput(coinbase, nOut, nValue));

        CMutableTransaction tx;
        tx.nVersion = 1;
        tx.vin.resize(1);
        tx.vin[0].prevout = COutPoint(coinbase->GetHash(), nOut);

        OUTPUT_PTR<CTxOutStandard> out = MAKE_OUTPUT<CTxOutStandard>();
        out->nValue = nValue - CENT; // 0.01 VEIL fee
        out->scriptPubKey = coinbaseScript;
        tx.vpout.push_back(out);

        std::vector<unsigned char> vchSig;
        std::vector<uint8_t> vchAmount(8);
        memcpy(vchAmount.data(), &nValue, 8);
        uint256 hash = SignatureHash(coinbaseScript, tx, 0, SIGHASH_ALL, vchAmount, SigVersion::BASE);
        BOOST_REQUIRE(coinbaseKey.Sign(hash, vchSig));
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        tx.vin[0].scriptSig << vchSig;
        return tx;
    }

    size_t CountAvailableCoins() const
    {
        LOCK2(cs_main, wallet->cs_wallet);
        std::vector<COutput> vCoins;
        wallet->AvailableCoins(vCoins);
        return vCoins.size();
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(autoconvert_tests, AutoConvertTestingSetup)

// End-to-end: a mempool-rejected conversion unwinds completely, the failure
// backoff delays the next attempt, and a later attempt converts for real.
BOOST_AUTO_TEST_CASE(autoconvert_mempool_reject_unwinds)
{
    const size_t nRecordsBefore = panonwallet->mapRecords.size();
    const size_t nWalletTxsBefore = wallet->mapWallet.size();
    const size_t nCoinsBefore = CountAvailableCoins();
    const CAmount nBalanceBefore = wallet->GetAvailableBalance();
    BOOST_REQUIRE(nCoinsBefore > 0);
    BOOST_REQUIRE(nBalanceBefore > 0);

    // Occupy one of the wallet's mature coinbases in the mempool from the
    // outside. The converter selects every eligible output (equal values,
    // well under the 32-input cap), so its transaction must conflict.
    CMutableTransaction foreignSpend = BuildForeignSpend(m_coinbase_txns[0]);
    {
        LOCK(cs_main);
        CValidationState state;
        BOOST_REQUIRE_MESSAGE(
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(foreignSpend), nullptr, nullptr, false, 0),
            "foreign conflict spend rejected: " + FormatStateMessage(state));
    }
    BOOST_REQUIRE_EQUAL(mempool.size(), 1U);

    // First call after startup only arms the jittered timer (60..600s);
    // advance mock time past the maximum jitter to reach the attempt.
    const int64_t nStart = GetTime();
    SetMockTime(nStart);
    wallet->AutoConvertToRingCT();
    SetMockTime(nStart + 700);
    wallet->AutoConvertToRingCT();

    // The build succeeded (record saved, inputs marked pending-spend), the
    // mempool test-accept failed on the conflict, and the unwind must have
    // erased every trace: no record, no wallet transaction, nothing in the
    // mempool beyond the foreign spend, and the full balance spendable.
    BOOST_CHECK_EQUAL(mempool.size(), 1U);
    BOOST_CHECK_EQUAL(panonwallet->mapRecords.size(), nRecordsBefore);
    BOOST_CHECK_EQUAL(wallet->mapWallet.size(), nWalletTxsBefore);
    BOOST_CHECK_EQUAL(CountAvailableCoins(), nCoinsBefore);
    BOOST_CHECK_EQUAL(wallet->GetAvailableBalance(), nBalanceBefore);

    // Backoff — and proof the failure path really executed. A failed attempt
    // schedules the next one exactly 10 minutes out (5min << 1), while a
    // NOTHING_TO_CONVERT pass reschedules within at most 9 minutes. Probing
    // in between (at +570s) with the conflict cleared discriminates the two:
    // a genuine failure is still backed off (nothing converts), whereas an
    // attempt that never really happened would convert right here and fail
    // the mempool-empty check below.
    mempool.clear();
    SetMockTime(nStart + 700 + 570);
    wallet->AutoConvertToRingCT();
    BOOST_CHECK_EQUAL(mempool.size(), 0U);
    BOOST_CHECK_EQUAL(panonwallet->mapRecords.size(), nRecordsBefore);

    // Past the backoff window the conversion must go through: one
    // transaction in the mempool, committed to the wallet, with the anon
    // record keyed by the same txid, and basecoin balance reduced.
    SetMockTime(nStart + 700 + 15 * 60);
    wallet->AutoConvertToRingCT();

    BOOST_REQUIRE_EQUAL(mempool.size(), 1U);
    std::vector<uint256> vtxid;
    mempool.queryHashes(vtxid);
    const uint256& txidConversion = vtxid[0];

    BOOST_CHECK_EQUAL(panonwallet->mapRecords.size(), nRecordsBefore + 1);
    BOOST_CHECK(panonwallet->mapRecords.count(txidConversion));
    BOOST_REQUIRE(wallet->mapWallet.count(txidConversion));
    BOOST_CHECK(wallet->GetAvailableBalance() < nBalanceBefore);

    // The conversion is a genuine batch (every mature coinbase selected, well
    // over a handful of inputs) and its outputs are CT, not basecoin.
    const CTransactionRef& txConversion = wallet->mapWallet.at(txidConversion).tx;
    BOOST_CHECK(txConversion->vin.size() >= 10);
    bool fHasCTOutput = false;
    for (const auto& pout : txConversion->vpout) {
        if (pout->GetType() == OUTPUT_CT)
            fHasCTOutput = true;
    }
    BOOST_CHECK(fHasCTOutput);
}

// The do/undo symmetry of the wallet-state side effect itself:
// MarkInputsAsPendingSpend + SaveRecord (what AddStandardInputs and
// AddBlindedInputs do at build time) followed by UnwindPendingTransaction
// must restore the source outputs exactly.
BOOST_AUTO_TEST_CASE(unwind_restores_pending_spend)
{
    LOCK(wallet->cs_wallet);

    const uint256 txidSource = InsecureRand256();
    const uint256 txidSpend = InsecureRand256();

    CTransactionRecord rtxSource;
    COutputRecord rec;
    rec.n = 0;
    rec.nType = OUTPUT_CT;
    rec.nFlags = ORF_OWNED;
    rec.SetValue(5 * COIN);
    rtxSource.InsertOutput(rec);
    panonwallet->mapRecords[txidSource] = rtxSource;
    BOOST_REQUIRE(panonwallet->SaveRecord(txidSource, rtxSource));

    CTransactionRecord rtxSpend;
    rtxSpend.vin.push_back(COutPoint(txidSource, 0));
    panonwallet->mapRecords[txidSpend] = rtxSpend;
    BOOST_REQUIRE(panonwallet->SaveRecord(txidSpend, rtxSpend));
    panonwallet->MarkInputsAsPendingSpend(rtxSpend.vin);

    // Build-time state: the source output is unusable.
    {
        const COutputRecord* pout = panonwallet->mapRecords.at(txidSource).GetOutput(0);
        BOOST_REQUIRE(pout);
        BOOST_CHECK(pout->nFlags & ORF_PENDING_SPEND);
        BOOST_CHECK(pout->IsSpent(true /* fIncludePendingSpend */));
    }

    panonwallet->UnwindPendingTransaction(txidSpend);

    // The failed spend's record is gone and the source output is spendable
    // again, with no residual flags.
    BOOST_CHECK_EQUAL(panonwallet->mapRecords.count(txidSpend), 0U);
    {
        const COutputRecord* pout = panonwallet->mapRecords.at(txidSource).GetOutput(0);
        BOOST_REQUIRE(pout);
        BOOST_CHECK(!(pout->nFlags & ORF_PENDING_SPEND));
        BOOST_CHECK(!pout->IsSpent(true /* fIncludePendingSpend */));
    }
}

BOOST_AUTO_TEST_SUITE_END()
