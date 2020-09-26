// Copyright (c) 2019 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <test/test_veil.h>

#include <boost/test/unit_test.hpp>

#include <crypto/ethash/lib/ethash/endianness.hpp>
#include <crypto/ethash/include/ethash/progpow.hpp>

#include "crypto/ethash/helpers.hpp"
#include "crypto/ethash/progpow_test_vectors.hpp"

#include <array>

BOOST_FIXTURE_TEST_SUITE(progpow_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(progpow_l1_cache)
{
    auto& context = get_ethash_epoch_context_0();

    constexpr auto test_size = 20;
    std::array<uint32_t, test_size> cache_slice;
    for (size_t i = 0; i < cache_slice.size(); ++i)
    cache_slice[i] = ethash::le::uint32(context.l1_cache[i]);

    const std::array<uint32_t, test_size> expected{
            {2492749011, 430724829, 2029256771, 3095580433, 3583790154, 3025086503,
                    805985885, 4121693337, 2320382801, 3763444918, 1006127899, 1480743010,
                    2592936015, 2598973744, 3038068233, 2754267228, 2867798800, 2342573634,
                    467767296, 246004123}};
    int i = 0;
    for (auto item : cache_slice) {
        BOOST_CHECK(item == expected[i]);
        i++;
    }
}

BOOST_AUTO_TEST_CASE(progpow_hash_empty)
{
    auto& context = get_ethash_epoch_context_0();

    const auto result = progpow::hash(context, 0, {}, 0);
    const auto mix_hex = "b1388e12e9898029a487f5534225c2ea8bd79c6ef6754db0405809f53d83c497";
    const auto final_hex = "3d2f6484ee555362e9d8e2ca54fb99741e755e849f9900ef84ad65aa3c935bd1";
    BOOST_CHECK_EQUAL(to_hex(result.mix_hash), mix_hex);
    BOOST_CHECK_EQUAL(to_hex(result.final_hash), final_hex);
}

BOOST_AUTO_TEST_CASE(progpow_hash_30000)
{
        const int block_number = 30000;
        const auto header =
        to_hash256("ffeeddccbbaa9988776655443322110000112233445566778899aabbccddeeff");
        const uint64_t nonce = 0x123456789abcdef0;

        auto context = ethash::create_epoch_context(ethash::get_epoch_number(block_number));

        const auto result = progpow::hash(*context, block_number, header, nonce);
        const auto mix_hex = "aa15c8b7b34bfc7da13e6d734f63dd0517da65072f4b833c52d586cc3089a990";
        const auto final_hex = "d8f9786659a08e0ae792bb39725d3dc1e2cc0fb36c228a0ec3101bf17a7cb44b";
        BOOST_CHECK_EQUAL(to_hex(result.mix_hash), mix_hex);
        BOOST_CHECK_EQUAL(to_hex(result.final_hash), final_hex);
}

BOOST_AUTO_TEST_CASE(progpow_hash_and_verify)
{
    ethash::epoch_context_ptr context{nullptr, nullptr};

    for (auto& t : progpow_hash_test_cases)
    {
        const auto epoch_number = ethash::get_epoch_number(t.block_number);
        if (!context || context->epoch_number != epoch_number)
            context = ethash::create_epoch_context(epoch_number);

        const auto header_hash = to_hash256(t.header_hash_hex);
        const auto nonce = std::stoull(t.nonce_hex, nullptr, 16);
        const auto result = progpow::hash(*context, t.block_number, header_hash, nonce);

        BOOST_CHECK_EQUAL(to_hex(result.mix_hash), t.mix_hash_hex);
        BOOST_CHECK_EQUAL(to_hex(result.final_hash), t.final_hash_hex);

        auto success = progpow::verify(
                *context, t.block_number, header_hash, result.mix_hash, nonce, result.final_hash);
        BOOST_CHECK(success);

        auto lower_boundary = result.final_hash;
        --lower_boundary.bytes[31];
        auto final_failure = progpow::verify(
                *context, t.block_number, header_hash, result.mix_hash, nonce, lower_boundary);
        BOOST_CHECK(!final_failure);

        auto different_mix = result.mix_hash;
        ++different_mix.bytes[7];
        auto mix_failure = progpow::verify(
                *context, t.block_number, header_hash, different_mix, nonce, result.final_hash);
        BOOST_CHECK(!mix_failure);
    }
}

BOOST_AUTO_TEST_CASE(progpow_search)
{
    auto ctxp = ethash::create_epoch_context_full(0);
    auto& ctx = *ctxp;
    auto& ctxl = reinterpret_cast<const ethash::epoch_context&>(ctx);

    auto boundary = to_hash256("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    auto sr = progpow::search(ctx, 0, {}, boundary, 100, 100);
    auto srl = progpow::search_light(ctxl, 0, {}, boundary, 100, 100);

    BOOST_CHECK(sr.mix_hash == ethash::hash256{});
    BOOST_CHECK(sr.final_hash == ethash::hash256{});
    BOOST_CHECK(sr.nonce == 0x0);
    BOOST_CHECK(sr.mix_hash == srl.mix_hash);
    BOOST_CHECK(sr.final_hash == srl.final_hash);
    BOOST_CHECK(sr.nonce == srl.nonce);

    // Switch it to a different starting nonce and find another solution
    sr = progpow::search(ctx, 0, {}, boundary, 0, 100);
    srl = progpow::search_light(ctxl, 0, {}, boundary, 0, 100);

    BOOST_CHECK(sr.mix_hash != ethash::hash256{});
    BOOST_CHECK(sr.final_hash != ethash::hash256{});
    BOOST_CHECK(sr.nonce == 9);
    BOOST_CHECK(sr.mix_hash == srl.mix_hash);
    BOOST_CHECK(sr.final_hash == srl.final_hash);
    BOOST_CHECK(sr.nonce == srl.nonce);

    auto r = progpow::hash(ctx, 0, {}, 9);
    BOOST_CHECK(sr.final_hash == r.final_hash);
    BOOST_CHECK(sr.mix_hash == r.mix_hash);
}


BOOST_AUTO_TEST_CASE(progpow_veil_header)
{
    CBlockHeader header;
    header.SetNull();
    header.nVersion = 0x20000000UL;
    header.nVersion |= CBlockHeader::PROGPOW_BLOCK;
    header.hashPrevBlock = uint256S("aabbcceeffaabbcceeffaabbcceeffaabbcceeffaabbcceeffaabbcceeffaabb");
    header.hashVeilData = uint256S("0011223344556677889900112233445566778899001122334455667788990011");
    header.nTime = 1571415021;
    header.nNonce64 = 4745;
    header.nBits = 0x1e008eb5;
    header.nHeight = 25000;

    const auto epoch_number = ethash::get_epoch_number(header.nHeight);
    auto ctxp = ethash::create_epoch_context_full(epoch_number);
    auto& ctx = *ctxp;
    auto& ctxl = reinterpret_cast<const ethash::epoch_context&>(ctx);

    uint256 nHeaderHash = header.GetProgPowHeaderHash();

    const auto header_hash = to_hash256(nHeaderHash.GetHex());
    const auto result = progpow::hash(ctx, header.nHeight, header_hash, header.nNonce64);

    BOOST_CHECK(result.mix_hash == to_hash256("5c4388c54a7c703f451578f4111a5e5afa06df8fd974db4e06ef8d27e029ac8e"));
    BOOST_CHECK(result.final_hash == to_hash256("0006b1d02a83eabb24be6388ed8d6bc05030f2bdc50bc34c4220dc7acbd8ec44"));

    auto boundary = to_hash256("000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    auto sr = progpow::search(ctx, header.nHeight, header_hash, boundary, 4700, 100);

    BOOST_CHECK(sr.solution_found);

    auto success = progpow::verify(ctxl, header.nHeight, header_hash, result.mix_hash, sr.nonce, result.final_hash);
    BOOST_CHECK(success);

    BOOST_CHECK(result.mix_hash == sr.mix_hash);
    BOOST_CHECK(result.final_hash == sr.final_hash);
}



BOOST_AUTO_TEST_SUITE_END()