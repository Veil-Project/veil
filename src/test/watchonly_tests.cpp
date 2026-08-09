// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for the watch-only transaction index layout and erasure.
//
// Historically the non-cached write path stored a fresh key's first
// transaction at index 0 with the stored count not including it (the count
// only covers indices 1..count), permanently under-counting, while the
// cached/bulk path was 1-based. The fixes make fresh writes 1-based and make
// erasure probe index 0 so legacy layouts are swept completely and the
// removed-count is truthful.

#include <test/test_veil.h>

#include <key.h>
#include <veil/ringct/watchonly.h>
#include <veil/ringct/watchonlydb.h>

#include <boost/test/unit_test.hpp>

namespace {

struct WatchOnlyTestingSetup : public BasicTestingSetup {
    WatchOnlyTestingSetup()
    {
        pwatchonlyDB.reset(new CWatchOnlyDB(1 << 20, true /* fMemory */));
    }
    ~WatchOnlyTestingSetup()
    {
        pwatchonlyDB.reset();
    }

    static CKey NewScanKey()
    {
        CKey key;
        key.MakeNewKey(true);
        return key;
    }

    static CWatchOnlyTx MakeTx(const CKey& scan_secret)
    {
        CWatchOnlyTx tx(scan_secret, InsecureRand256());
        tx.type = CWatchOnlyTx::STEALTH; // serializes the (default) ctout
        tx.tx_index = 0;
        return tx;
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(watchonly_tests, WatchOnlyTestingSetup)

// A fresh key's first transaction must land at index 1 with count 1 — the
// same 1-based layout the cached/bulk path uses — and nothing at index 0.
BOOST_AUTO_TEST_CASE(fresh_key_writes_one_based)
{
    const CKey key = NewScanKey();

    int nCount = 0;
    BOOST_REQUIRE(!GetWatchOnlyKeyCount(key, nCount));

    const CWatchOnlyTx tx1 = MakeTx(key);
    BOOST_REQUIRE(AddWatchOnlyTransaction(key, tx1));

    BOOST_REQUIRE(GetWatchOnlyKeyCount(key, nCount));
    BOOST_CHECK_EQUAL(nCount, 1);

    CWatchOnlyTx txRead;
    BOOST_CHECK(ReadWatchOnlyTransaction(key, 1, txRead));
    BOOST_CHECK(txRead.tx_hash == tx1.tx_hash);

    // The pre-fix path wrote the first transaction at index 0; nothing may
    // live there now.
    BOOST_CHECK(!ReadWatchOnlyTransaction(key, 0, txRead));

    // Appending stays contiguous and the count stays truthful.
    const CWatchOnlyTx tx2 = MakeTx(key);
    BOOST_REQUIRE(AddWatchOnlyTransaction(key, tx2));
    BOOST_REQUIRE(GetWatchOnlyKeyCount(key, nCount));
    BOOST_CHECK_EQUAL(nCount, 2);
    BOOST_CHECK(ReadWatchOnlyTransaction(key, 2, txRead));
    BOOST_CHECK(txRead.tx_hash == tx2.tx_hash);
}

// Erasure must sweep a legacy layout completely: first transaction at index
// 0, count not including it. The probe loop covers 0..count and the removed
// count reports what was actually erased.
BOOST_AUTO_TEST_CASE(erase_sweeps_legacy_index0_layout)
{
    const CKey key = NewScanKey();
    const CKeyID keyID = key.GetPubKey().GetID();

    // Reproduce the legacy layout at the db layer exactly as the old write
    // path created it: WriteWatchOnlyTx(key, -1, ...) stored the first tx at
    // index 0 with count 0, the next at index 1 with count 1.
    const CWatchOnlyTx txLegacy0 = MakeTx(key);
    const CWatchOnlyTx txLegacy1 = MakeTx(key);
    BOOST_REQUIRE(pwatchonlyDB->WriteWatchOnlyTx(key, -1, txLegacy0));
    BOOST_REQUIRE(pwatchonlyDB->WriteWatchOnlyTx(key, 0, txLegacy1));

    int nCount = 0;
    BOOST_REQUIRE(GetWatchOnlyKeyCount(key, nCount));
    BOOST_CHECK_EQUAL(nCount, 1); // legacy under-count: 2 txs on disk

    CWatchOnlyTx txRead;
    BOOST_REQUIRE(ReadWatchOnlyTransaction(key, 0, txRead));
    BOOST_REQUIRE(ReadWatchOnlyTransaction(key, 1, txRead));

    int nTxesRemoved = 0;
    BOOST_REQUIRE(pwatchonlyDB->EraseWatchOnlyAddressData(keyID, key, nTxesRemoved));

    // Both records gone — including the index-0 one the pre-fix loop
    // (1..count) leaked forever — and the count reflects reality.
    BOOST_CHECK_EQUAL(nTxesRemoved, 2);
    BOOST_CHECK(!ReadWatchOnlyTransaction(key, 0, txRead));
    BOOST_CHECK(!ReadWatchOnlyTransaction(key, 1, txRead));
    BOOST_CHECK(!GetWatchOnlyKeyCount(key, nCount));
}

// Erasing a fixed-era (1-based) layout is exact as well.
BOOST_AUTO_TEST_CASE(erase_one_based_layout)
{
    const CKey key = NewScanKey();
    const CKeyID keyID = key.GetPubKey().GetID();

    BOOST_REQUIRE(AddWatchOnlyTransaction(key, MakeTx(key)));
    BOOST_REQUIRE(AddWatchOnlyTransaction(key, MakeTx(key)));
    BOOST_REQUIRE(AddWatchOnlyTransaction(key, MakeTx(key)));

    int nTxesRemoved = 0;
    BOOST_REQUIRE(pwatchonlyDB->EraseWatchOnlyAddressData(keyID, key, nTxesRemoved));
    BOOST_CHECK_EQUAL(nTxesRemoved, 3);

    int nCount = 0;
    BOOST_CHECK(!GetWatchOnlyKeyCount(key, nCount));
}

BOOST_AUTO_TEST_SUITE_END()
