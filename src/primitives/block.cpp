// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>
#include <utilstrencodings.h>
#include <crypto/common.h>
#include <streams.h>
#include <crypto/ethash/helpers.hpp>
#include <crypto/ethash/include/ethash/progpow.hpp>
#include <pow.h>
#include <crypto/randomx/randomx.h>

uint32_t nPowTimeStampActive = 0;

uint256 CBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

#define TIME_MASK 0xffffff80
uint256 CBlockHeader::GetX16RTPoWHash() const
{
    //Only change every 128 seconds
    int32_t nTimeX16r = nTime&TIME_MASK;
    uint256 hashTime = Hash(BEGIN(nTimeX16r), END(nTimeX16r));
    return HashX16R(BEGIN(nVersion), END(nNonce), hashTime);
}

uint256 CBlockHeader::GetSha256DPoWHash() const
{
    return Hash(BEGIN(nVersion), END(nNonce));
}

uint256 CBlockHeader::GetProgPowHash() const
{
    // Build the header_hash
    uint256 nHeaderHash = GetProgPowHeaderHash();
    const auto header_hash = to_hash256(nHeaderHash.GetHex());

    ethash::epoch_context_ptr context{nullptr, nullptr};
    // Get the context from the block height
    const auto epoch_number = ethash::get_epoch_number(nHeight);
    if (!context || context->epoch_number != epoch_number)
        context = ethash::create_epoch_context(epoch_number);

    // ProgPow hash
    const auto result = progpow::hash(*context, nHeight, header_hash, nNonce64);

    return uint256S(to_hex(result.final_hash));
}

/**
 * @brief This takes a block header, removes the nNonce and the mixHash. Then performs a serialized hash of it SHA256D.
 * This will be used as the input to the ProgPow hashing function
 * @note Only to be called and used on ProgPow block headers
 */
uint256 CBlockHeader::GetProgPowHeaderHash() const
{
    CProgPowInput input{*this};

    return SerializeHash(input);
}

/**
 * @brief This takes a block header, removes the nNonce and the mixHash. Then performs a serialized hash of it SHA256D.
 * This will be used as the input to the ProgPow hashing function
 * @note Only to be called and used on ProgPow block headers
 */
uint256 CBlockHeader::GetRandomXHeaderHash() const
{
    CRandomXInput input{*this};

    return SerializeHash(input);
}

uint256 CBlock::GetVeilDataHash() const
{
    CVeilBlockData veilBlockData(hashMerkleRoot, hashWitnessMerkleRoot, mapAccumulatorHashes, hashPoFN);

    return SerializeHash(veilBlockData);
}

std::string CBlock::DataHashElementsToString() const
{
    return strprintf("%s:\n   HashMerkleRoot=%s\n   WitnessMerkleRoot=%s\n   hashPoFN=%s\n   mapAccumulatorHashes=%s\n",
            __func__, hashMerkleRoot.GetHex(), hashWitnessMerkleRoot.GetHex(), hashPoFN.GetHex(),
            SerializeHash(mapAccumulatorHashes).GetHex());
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashVeilData=%s, hashMerkleRoot=%s, hashWitnessMerkleRoot=%s, hashProofOfFullNode=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashVeilData.ToString(),
        hashMerkleRoot.ToString(),
        hashWitnessMerkleRoot.ToString(),
        hashPoFN.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}
