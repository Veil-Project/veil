// Copyright (c) 2019-2020 Veil developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <streams.h>
#include <crypto/ethash/include/ethash/ethash.hpp>
#include <crypto/ethash/helpers.hpp>
#include <crypto/ethash/include/ethash/progpow.hpp>

uint32_t nPowTimeStampActive = 0;

uint256 CBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

#define TIME_MASK 0xffffff80
uint256 CBlockHeader::GetX16RTPoWHash(bool fSetVeilDataHashNull) const
{
    //Only change every 128 seconds
    int32_t nTimeX16r = nTime&TIME_MASK;
    uint256 hashTime = Hash(BEGIN(nTimeX16r), END(nTimeX16r));

    // Because of the changes to the block header that removes veil data hash
    // When a PoS block looks back 100 blocks to get the blocks hash
    // If the block is a PoW block, sometimes the block data in the pointer is
    // not in the correct order, so when the Hash starts at the BEGIN(nVersion), END(nNonce)
    // the veildatahash is not found to be zero e.g 00000000xxxxxxx.
    // This is a bug only when the wallet is being mined to by ProgPow locally.
    // By allowing the code to pass a flag when computing the PoS block index PoW hash, we can
    // bypass the bug in the code and set veildatahash to all zeros before computing the hash.
    if (fSetVeilDataHashNull) {
        CBlockHeader temp(*this);
        temp.hashVeilData = uint256();
        return HashX16R(BEGIN(temp.nVersion), END(temp.nNonce), hashTime);
    }

    return HashX16R(BEGIN(nVersion), END(nNonce), hashTime);
}

uint256 CBlockHeader::GetSha256DPoWHash() const
{
    CSha256dDataInput input(*this);
    uint256 dataHash = SerializeHash(input);
    CSha256dInput sha256Final(*this, dataHash);
    return SerializeHash(sha256Final);
}

uint256 CBlockHeader::GetSha256dMidstate() const
{
    CSha256dDataInput input(*this);
    return SerializeHash(input);
}

uint256 CBlockHeader::GetSha256D(uint256& midState) const
{
    CSha256dInput sha256Final(*this, midState);
    return SerializeHash(sha256Final);
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
