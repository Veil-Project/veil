// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_veil.h>

#include <uint256.h>
#include <veil/mnemonic/generateseed.h>
#include <veil/mnemonic/mnemonic.h>

#include <cstring>
#include <string>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(mnemonic_cleanse_tests, BasicTestingSetup)

// GenerateNewMnemonicSeed wipes its transient private key and intermediate seed after use. This
// checks the wipe did not change the result: the phrase it returns still re-derives exactly the
// master seed it returned, so a wallet created through this path derives identically to before.
BOOST_AUTO_TEST_CASE(generated_seed_round_trips_after_cleanse)
{
    std::string mnemonic;
    uint512 seed = veil::GenerateNewMnemonicSeed(mnemonic, "english");

    BOOST_CHECK(!mnemonic.empty());
    BOOST_CHECK(seed != uint512());

    // The wallet re-derives the master seed from the phrase; it must match what was returned.
    long_hash reDerived = decode_mnemonic(mnemonic);
    uint512 seedFromPhrase;
    memcpy(seedFromPhrase.begin(), reDerived.data(), reDerived.size());
    BOOST_CHECK(seed == seedFromPhrase);
}

// The wallet-open path wipes the phrase and the decoded seed after decode_mnemonic runs. The wipe
// is on caller-side copies, not inside the derivation, so decode_mnemonic must stay a pure,
// deterministic function. Confirm it still returns a stable, non-zero seed.
BOOST_AUTO_TEST_CASE(decode_mnemonic_is_deterministic_and_nonzero)
{
    const std::string phrase = "abandon abandon abandon abandon abandon abandon "
                               "abandon abandon abandon abandon abandon about";
    long_hash a = decode_mnemonic(phrase);
    long_hash b = decode_mnemonic(phrase);
    BOOST_CHECK(a == b);

    bool anyNonZero = false;
    for (uint8_t c : a)
        if (c != 0) { anyNonZero = true; break; }
    BOOST_CHECK(anyNonZero);
}

BOOST_AUTO_TEST_SUITE_END()
