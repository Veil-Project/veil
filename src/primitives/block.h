// Copyright (c) 2019 Veil developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include <primitives/transaction.h>
#include <serialize.h>
#include <uint256.h>

#include <unordered_map>
#include <libzerocoin/Denominations.h>
#include <iostream>


static const int32_t BITS_TO_BLOCK_VERSION = 28;
static const int OLD_POW_BLOCK_VERSION = 2;
static const int NEW_POW_BLOCK_VERSION = 3;

extern uint32_t nPowTimeStampActive;

struct CVeilBlockData
{
    uint256 hashMerkleRoot;
    uint256 hashWitnessMerkleRoot;
    uint256 hashPoFN;

    std::map<libzerocoin::CoinDenomination , uint256> mapAccumulatorHashes;

    CVeilBlockData(){
        SetNull();
    }

    CVeilBlockData(const uint256& hashMerkleRootTmp, const uint256& hashWitnessMerkleRootTmp, const std::map<libzerocoin::CoinDenomination , uint256> mapAccumulatorHashesTmp, const uint256& hashPoFNTmp)
    {
        SetNull();
        this->hashMerkleRoot= hashMerkleRootTmp;
        this->hashWitnessMerkleRoot = hashWitnessMerkleRootTmp;
        this->mapAccumulatorHashes = mapAccumulatorHashesTmp;
        this->hashPoFN = hashPoFNTmp;
    }

    void SetNull()
    {
        hashMerkleRoot = uint256();
        hashWitnessMerkleRoot = uint256();
        hashPoFN = uint256();
        mapAccumulatorHashes.clear();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(hashMerkleRoot);
        READWRITE(hashWitnessMerkleRoot);
        READWRITE(mapAccumulatorHashes);
        READWRITE(hashPoFN);
    }
};

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:

    enum {
        PROGPOW_BLOCK = ( 1 << 26),
        RANDOMX_BLOCK = ( 1 << 25),
        SHA256D_BLOCK = ( 1 << 24)
    };

    // header
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashVeilData; // Serialzie hash of CVeilBlockData(hashMerkleRoot, hashAccumulators, hashWitnessMerkleRoot, hashPoFN)
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;
    uint8_t fProofOfStake;
    uint8_t fProofOfFullNode;

    // Removal of the hashVeilData once PoW 3 algo goes live
    uint256 hashMerkleRoot;
    uint256 hashWitnessMerkleRoot;
    uint256 hashAccumulators;

    //ProgPow
    uint64_t nNonce64;
    uint32_t nHeight;
    uint256 mixHash;

    CBlockHeader()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        bool fCheckVeilDataHash = false;
        READWRITE(nVersion);
        READWRITE(hashPrevBlock);
        // When Veil started the block version in hex it looks is the following 0x20000000
        // We only want to write the hashVeilData to disk when first 4 bits of the version == OLD_POW_MAIN_VERSION or lower, e.g -> 2
        if (nVersion >> BITS_TO_BLOCK_VERSION <= OLD_POW_BLOCK_VERSION) {
            fCheckVeilDataHash = true;
            // This is the version that the new PoW started at. This is when we stop using hashVeilData.
            // We would normally use the nTime of the block. However, that is serialized after the hashVeilData
            // So we don't have access to it
            READWRITE(hashVeilData);
        }

        READWRITE(nTime);
        READWRITE(nBits);

        if (nTime >= nPowTimeStampActive) {
            if (fCheckVeilDataHash) {
                // We want to fail to accept this block. setting it NUll should fail all checks done on it
                SetNull();
                return;
            }

            READWRITE(hashMerkleRoot);
            READWRITE(hashWitnessMerkleRoot);
            READWRITE(hashAccumulators);

            int32_t nPowType = (nVersion & (PROGPOW_BLOCK | RANDOMX_BLOCK | SHA256D_BLOCK));
            std::cout << nPowType;
            switch(nPowType) {
                case PROGPOW_BLOCK:
                    READWRITE(nHeight);
                    READWRITE(nNonce64);
                    READWRITE(mixHash);
                    break;
                case RANDOMX_BLOCK:
                    READWRITE(nHeight);
                    READWRITE(nNonce);
                    break;
                case SHA256D_BLOCK:
                    READWRITE(nNonce64);
                    break;
                default:
                    SetNull();
                    return;
            }
        } else {
            READWRITE(nNonce);
        }

        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(fProofOfStake);
            if (nTime < nPowTimeStampActive) {
                READWRITE(fProofOfFullNode);
            } else {
                if (fProofOfStake) {
                    READWRITE(fProofOfFullNode);
                }
            }
        }
    }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashVeilData.SetNull();
        hashMerkleRoot.SetNull();
        hashWitnessMerkleRoot.SetNull();
        hashAccumulators.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
        fProofOfStake = 0;
        fProofOfFullNode = 0;

        //ProgPow
        nHeight = 0;
        nNonce64 = 0;
        mixHash.SetNull();
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetHash() const;
    uint256 GetX16RTPoWHash() const;
    uint256 GetSha256DPoWHash() const;

    uint256 GetProgPowHeaderHash() const;
    uint256 GetRandomXHeaderHash() const;

    bool IsProgPow() const {
        return nVersion & PROGPOW_BLOCK && nTime >= nPowTimeStampActive;
    }

    bool IsRandomX() const {
        return nVersion & RANDOMX_BLOCK && nTime >= nPowTimeStampActive;
    }

    bool IsSha256D() const {
        return nVersion & SHA256D_BLOCK && nTime >= nPowTimeStampActive;
    }

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }

    virtual ~CBlockHeader(){};
};

class CBlock : public CBlockHeader
{
public:
    // network and disk
    std::vector<CTransactionRef> vtx;

    // zerocoin
    std::map<libzerocoin::CoinDenomination , uint256> mapAccumulatorHashes;

    uint256 hashPoFN;

    // Proof of Stake: block signature - signed by one of the coin base txout[N]'s owner
    std::vector<unsigned char> vchBlockSig;

    // memory only
    mutable bool fChecked;
    mutable bool fSignaturesVerified;

    CBlock()
    {
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *(static_cast<CBlockHeader*>(this)) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CBlockHeader, *this);
        READWRITE(vtx);
        READWRITE(mapAccumulatorHashes);
        if (nTime < nPowTimeStampActive) {
            READWRITE(hashMerkleRoot);
            READWRITE(hashWitnessMerkleRoot);
        }
        if (IsProofOfStake()) {
            READWRITE(hashPoFN);
            READWRITE(vchBlockSig);
        }
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
        vchBlockSig.clear();
        fChecked = false;
        for (unsigned int i = 0; i < libzerocoin::zerocoinDenomList.size(); i++) {
            uint256 zero;
            mapAccumulatorHashes[libzerocoin::zerocoinDenomList[i]] = zero;
        }

        hashMerkleRoot = uint256();
        hashWitnessMerkleRoot = uint256();
        hashPoFN = uint256();
        fSignaturesVerified = false;
    }

    CBlockHeader GetBlockHeader() const
    {
        CVeilBlockData veilBlockData(hashMerkleRoot, hashWitnessMerkleRoot, mapAccumulatorHashes, hashPoFN);

        CBlockHeader block;
        block.nVersion       = nVersion;
        block.hashPrevBlock  = hashPrevBlock;
        block.hashMerkleRoot = hashMerkleRoot;
        block.hashWitnessMerkleRoot = hashWitnessMerkleRoot;
        block.hashAccumulators = SerializeHash(mapAccumulatorHashes);
        block.hashVeilData   = SerializeHash(veilBlockData);
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        block.fProofOfStake = IsProofOfStake();
        block.fProofOfFullNode = fProofOfFullNode;

        //ProgPow
        block.nHeight        = nHeight;
        block.nNonce64       = nNonce64;
        block.mixHash        = mixHash;
        return block;
    }

    bool IsProgPow() const {
        return IsProofOfWork() && (nVersion & PROGPOW_BLOCK) && nTime >= nPowTimeStampActive;
    }

    bool IsRandomX() const {
        return IsProofOfWork() && (nVersion & RANDOMX_BLOCK) && nTime >= nPowTimeStampActive;
    }

    bool IsSha256D() const {
        return IsProofOfWork() && (nVersion & SHA256D_BLOCK) && nTime >= nPowTimeStampActive;
    }

    int PowType() const {
        if (IsSha256D())
            return SHA256D_BLOCK;
        else if (IsRandomX())
            return RANDOMX_BLOCK;
        else if (IsProgPow())
            return PROGPOW_BLOCK;
        else return 0;
    }

    // two types of block: proof-of-work or proof-of-stake
    bool IsProofOfStake() const
    {
        return (vtx.size() > 1 && vtx[1]->IsCoinStake());
    }

    bool IsProofOfWork() const
    {
        return !IsProofOfStake();
    }

    uint256 GetVeilDataHash() const;

    std::string DataHashElementsToString() const;

    std::string ToString() const;
};

/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    std::vector<uint256> vHave;

    CBlockLocator() {}

    explicit CBlockLocator(const std::vector<uint256>& vHaveIn) : vHave(vHaveIn) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};


/**
 * Custom serializer for CBlockHeader that omits the nNonce and mixHash, for use
 * as input to ProgPow.
 */
class CProgPowInput : private CBlockHeader
{
public:
    CProgPowInput(const CBlockHeader &header)
    {
        CBlockHeader::SetNull();
        *((CBlockHeader*)this) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(this->nVersion);
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(hashWitnessMerkleRoot);
        READWRITE(hashAccumulators);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nHeight);
    }
};

class CRandomXInput : private CBlockHeader
{
public:
    CRandomXInput(const CBlockHeader &header)
    {
        CBlockHeader::SetNull();
        *((CBlockHeader*)this) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(this->nVersion);
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(hashWitnessMerkleRoot);
        READWRITE(hashAccumulators);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
        READWRITE(nHeight);
    }
};

#endif // BITCOIN_PRIMITIVES_BLOCK_H
