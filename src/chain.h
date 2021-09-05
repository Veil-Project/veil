// Copyright (c) 2019 Veil developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAIN_H
#define BITCOIN_CHAIN_H

#include <veil/zerocoin/accumulatormap.h>
#include <arith_uint256.h>
#include <consensus/params.h>
#include <primitives/block.h>
#include <tinyformat.h>
#include <uint256.h>
#include <libzerocoin/bignum.h>

#include <map>
#include <memory>
#include <vector>

/**
 * Maximum amount of time that a block timestamp is allowed to exceed the
 * current network-adjusted time before the block will be accepted.
 */
static const int64_t MAX_FUTURE_BLOCK_TIME = 75;
static const int64_t MAX_PAST_BLOCK_TIME = 15;

/**
 * Timestamp window used as a grace period by code that compares external
 * timestamps (such as timestamps passed to RPCs, or wallet key creation times)
 * to block timestamps. This should be set at least as high as
 * MAX_FUTURE_BLOCK_TIME.
 */
static const int64_t TIMESTAMP_WINDOW = MAX_FUTURE_BLOCK_TIME;

class CBlockFileInfo
{
public:
    unsigned int nBlocks;      //!< number of blocks stored in file
    unsigned int nSize;        //!< number of used bytes of block file
    unsigned int nUndoSize;    //!< number of used bytes in the undo file
    unsigned int nHeightFirst; //!< lowest height of block in file
    unsigned int nHeightLast;  //!< highest height of block in file
    uint64_t nTimeFirst;       //!< earliest time of block in file
    uint64_t nTimeLast;        //!< latest time of block in file

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT(nBlocks));
        READWRITE(VARINT(nSize));
        READWRITE(VARINT(nUndoSize));
        READWRITE(VARINT(nHeightFirst));
        READWRITE(VARINT(nHeightLast));
        READWRITE(VARINT(nTimeFirst));
        READWRITE(VARINT(nTimeLast));
    }

     void SetNull() {
         nBlocks = 0;
         nSize = 0;
         nUndoSize = 0;
         nHeightFirst = 0;
         nHeightLast = 0;
         nTimeFirst = 0;
         nTimeLast = 0;
     }

     CBlockFileInfo() {
         SetNull();
     }

     std::string ToString() const;

     /** update statistics (does not update nSize) */
     void AddBlock(unsigned int nHeightIn, uint64_t nTimeIn) {
         if (nBlocks==0 || nHeightFirst > nHeightIn)
             nHeightFirst = nHeightIn;
         if (nBlocks==0 || nTimeFirst > nTimeIn)
             nTimeFirst = nTimeIn;
         nBlocks++;
         if (nHeightIn > nHeightLast)
             nHeightLast = nHeightIn;
         if (nTimeIn > nTimeLast)
             nTimeLast = nTimeIn;
     }
};

struct CDiskBlockPos
{
    int nFile;
    unsigned int nPos;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT_MODE(nFile, VarIntMode::NONNEGATIVE_SIGNED));
        READWRITE(VARINT(nPos));
    }

    CDiskBlockPos() {
        SetNull();
    }

    CDiskBlockPos(int nFileIn, unsigned int nPosIn) {
        nFile = nFileIn;
        nPos = nPosIn;
    }

    friend bool operator==(const CDiskBlockPos &a, const CDiskBlockPos &b) {
        return (a.nFile == b.nFile && a.nPos == b.nPos);
    }

    friend bool operator!=(const CDiskBlockPos &a, const CDiskBlockPos &b) {
        return !(a == b);
    }

    void SetNull() { nFile = -1; nPos = 0; }
    bool IsNull() const { return (nFile == -1); }

    std::string ToString() const
    {
        return strprintf("CBlockDiskPos(nFile=%i, nPos=%i)", nFile, nPos);
    }

};

enum BlockStatus: uint32_t {
    //! Unused.
    BLOCK_VALID_UNKNOWN      =    0,

    //! Parsed, version ok, hash satisfies claimed PoW, 1 <= vtx count <= max, timestamp not in future
    BLOCK_VALID_HEADER       =    1,

    //! All parent headers found, difficulty matches, timestamp >= median previous, checkpoint. Implies all parents
    //! are also at least TREE.
    BLOCK_VALID_TREE         =    2,

    /**
     * Only first tx is coinbase, 2 <= coinbase input script length <= 100, transactions valid, no duplicate txids,
     * sigops, size, merkle root. Implies all parents are at least TREE but not necessarily TRANSACTIONS. When all
     * parent blocks also have TRANSACTIONS, CBlockIndex::nChainTx will be set.
     */
    BLOCK_VALID_TRANSACTIONS =    3,

    //! Outputs do not overspend inputs, no double spends, coinbase output ok, no immature coinbase spends, BIP30.
    //! Implies all parents are also at least CHAIN.
    BLOCK_VALID_CHAIN        =    4,

    //! Scripts & signatures ok. Implies all parents are also at least SCRIPTS.
    BLOCK_VALID_SCRIPTS      =    5,

    //! All validity bits.
    BLOCK_VALID_MASK         =   BLOCK_VALID_HEADER | BLOCK_VALID_TREE | BLOCK_VALID_TRANSACTIONS |
                                 BLOCK_VALID_CHAIN | BLOCK_VALID_SCRIPTS,

    BLOCK_HAVE_DATA          =    8, //!< full block available in blk*.dat
    BLOCK_HAVE_UNDO          =   16, //!< undo data available in rev*.dat
    BLOCK_HAVE_MASK          =   BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO,

    BLOCK_FAILED_VALID       =   32, //!< stage after last reached validness failed
    BLOCK_FAILED_CHILD       =   64, //!< descends from failed block
    BLOCK_FAILED_MASK        =   BLOCK_FAILED_VALID | BLOCK_FAILED_CHILD,

    BLOCK_OPT_WITNESS       =   128, //!< block data in blk*.data was received with a witness-enforcing client
};

enum BlockMemFlags: uint32_t {
    POS_BADWEIGHT = (1 << 0),
};

/** The block chain is a tree shaped structure starting with the
 * genesis block at the root, with each block potentially having multiple
 * candidates to be the next block. A blockindex may have multiple pprev pointing
 * to it, but at most one of them can be part of the currently active branch.
 */
class CBlockIndex
{
private:
    //! owned pointer to the hash of the block, for use when erased from mapBlockIndex.
    std::shared_ptr<const uint256> _phashBlock;
public:
    //! pointer to the hash of the block, if any. Memory is owned by mapBlockIndex, if this index is in mapBlockIndex
    const uint256* phashBlock{nullptr};

    //! pointer to the index of the predecessor of this block
    CBlockIndex* pprev{nullptr};

    //! pointer to the index of some further predecessor of this block
    CBlockIndex* pskip{nullptr};

    //! height of the entry in the chain. The genesis block has height 0
    int nHeight{0};

    //! money supply tracking
    int64_t nMoneySupply{0};

    //! zerocoin mint supply tracking
    int64_t nMint{0};

    //! Which # file this block is stored in (blk?????.dat)
    int nFile{0};

    //! Byte offset within blk?????.dat where this block's data is stored
    unsigned int nDataPos{0};

    //! Byte offset within rev?????.dat where this block's undo data is stored
    unsigned int nUndoPos{0};

    //! (memory only) Total amount of work (expected number of hashes) in the chain up to and including this block
    arith_uint256 nChainWork{};

    //! (memory only) Total amount of work (only looking at PoW) in the chain up to and including this block
    arith_uint256 nChainPoW{};

    //! Number of transactions in this block.
    //! Note: in a potential headers-first mode, this number cannot be relied upon
    unsigned int nTx{0};

    //! (memory only) Number of transactions in the chain up to and including this block.
    //! This value will be non-zero only if and only if transactions for this block and all its parents are available.
    //! Change to 64-bit type when necessary; won't happen before 2030
    unsigned int nChainTx{0};

    int64_t nAnonOutputs{0}; // last index

    //! Verification status of this block. See enum BlockStatus
    uint32_t nStatus{0};

    bool fProofOfStake{false};
    bool fProofOfFullNode{false};

    //! Funds sent into the network to serve as an additional reward to stakers and miners
    CAmount nNetworkRewardReserve{0};

    //! block header
    int32_t nVersion{0};
    uint256 hashVeilData{};
    uint32_t nTime{0};
    uint32_t nBits{0};
    uint32_t nNonce{0};

    //! ProgPow Header items
    // Height was already in the CBlockIndex
    uint64_t nNonce64{0};
    uint256 mixHash{};

    //! (memory only) Sequential id assigned to distinguish order in which blocks are received.
    int32_t nSequenceId{0};

    //! zerocoin specific fields
    std::map<libzerocoin::CoinDenomination, int64_t> mapZerocoinSupply;
    std::vector<libzerocoin::CoinDenomination> vMintDenominationsInBlock;

    //! (memory only) Maximum nTime in the chain up to and including this block.
    unsigned int nTimeMax{0};

    //! Hash value for the accumulator. Can be used to access the zerocoindb for the accumulator value
    std::map<libzerocoin::CoinDenomination ,uint256> mapAccumulatorHashes;

    uint256 hashMerkleRoot{};
    uint256 hashWitnessMerkleRoot{};
    uint256 hashPoFN{};
    uint256 hashAccumulators{};

    //! vector that holds a proof of stake proof hash if the block has one. If not, its empty and has less memory
    //! overhead than an empty uint256
    std::vector<unsigned char> vHashProof;

    void ResetMaps()
    {
        for (auto& denom : libzerocoin::zerocoinDenomList) {
            mapAccumulatorHashes[denom] = uint256();
        }

        // Start supply of each denomination with 0s
        for (auto& denom : libzerocoin::zerocoinDenomList) {
            mapZerocoinSupply.insert(std::make_pair(denom, 0));
        }
    }

    void SetNull()
    {
        phashBlock = nullptr;
        pprev = nullptr;
        pskip = nullptr;
        nHeight = 0;
        nMoneySupply = 0;
        nMint = 0;
        nFile = 0;
        nDataPos = 0;
        nUndoPos = 0;
        nChainWork = arith_uint256();
        nChainPoW = arith_uint256();
        nTx = 0;
        nChainTx = 0;
        nStatus = 0;
        nSequenceId = 0;
        nTimeMax = 0;
        nNetworkRewardReserve = 0;

        //Proof of stake
        fProofOfStake = false;
        vHashProof = {};

        //Proof of Full Node
        fProofOfFullNode = false;
        hashPoFN = uint256();

        nAnonOutputs = 0;

        mapAccumulatorHashes.clear();
        hashMerkleRoot = uint256();
        hashWitnessMerkleRoot = uint256();
        hashAccumulators = uint256();

        ResetMaps();

        vMintDenominationsInBlock.clear();

        nVersion       = 0;
        hashVeilData   = uint256();
        hashMerkleRoot = uint256();
        hashWitnessMerkleRoot = uint256();
        nTime          = 0;
        nBits          = 0;
        nNonce         = 0;

        //ProgPow
        nNonce64       = 0;
        mixHash        = uint256();
    }

    CBlockIndex()
    {
        ResetMaps();
    }

    explicit CBlockIndex(const CBlockHeader& block)
        : nHeight{static_cast<int>(block.nHeight)},
          nVersion{block.nVersion},
          hashVeilData{block.hashVeilData},
          nTime{block.nTime},
          nBits{block.nBits},
          nNonce{block.nNonce},
          nNonce64{block.nNonce64},
          mixHash{block.mixHash},
          hashMerkleRoot{block.hashMerkleRoot},
          hashWitnessMerkleRoot{block.hashWitnessMerkleRoot},
          hashAccumulators{block.hashAccumulators}
    {
        ResetMaps();
    }

    CDiskBlockPos GetBlockPos() const {
        CDiskBlockPos ret;
        if (nStatus & BLOCK_HAVE_DATA) {
            ret.nFile = nFile;
            ret.nPos  = nDataPos;
        }
        return ret;
    }

    CDiskBlockPos GetUndoPos() const {
        CDiskBlockPos ret;
        if (nStatus & BLOCK_HAVE_UNDO) {
            ret.nFile = nFile;
            ret.nPos  = nUndoPos;
        }
        return ret;
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion       = nVersion;
        if (pprev)
            block.hashPrevBlock = pprev->GetBlockHash();
        block.hashVeilData   = hashVeilData;
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        block.fProofOfStake = IsProofOfStake();
        block.fProofOfFullNode = fProofOfFullNode;

        block.hashMerkleRoot = hashMerkleRoot;
        block.hashWitnessMerkleRoot = hashWitnessMerkleRoot;
        block.hashAccumulators = hashAccumulators;
        //ProgPow
        block.nNonce64       = nNonce64;
        block.mixHash        = mixHash;
        block.nHeight        = nHeight;
        return block;
    }

    uint256 GetBlockHash() const
    {
        return *phashBlock;
    }

    uint256 GetX16RTPoWHash(bool fSetVeilDataHashNull = false) const
    {
        return GetBlockHeader().GetX16RTPoWHash(fSetVeilDataHashNull);
    }

    uint256 GetProgPowHash(uint256& mix_hash) const;

    uint256 GetRandomXPoWHash() const;

    uint256 GetSha256DPowHash() const
    {
        return GetBlockHeader().GetSha256DPoWHash();
    }

    uint256 GetBlockPoSHash() const
    {
        uint256 hash;
        if (vHashProof.empty())
            return hash;
        memcpy(hash.begin(), vHashProof.data(), vHashProof.size());
        return hash;
    }

    void SetPoSHash(const uint256& proofHash)
    {
        vHashProof.clear();
        vHashProof.insert(vHashProof.begin(), proofHash.begin(), proofHash.end());
    }

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }

    int64_t GetBlockTimeMax() const
    {
        return (int64_t)nTimeMax;
    }

    int64_t GetBlockWork() const;
    int64_t GetBlockPoW() const;
    arith_uint256 GetChainPoW() const;

    /** Returns the hash of the accumulator for the specified denomination. If it doesn't exist then a new uint256 is returned*/
    uint256 GetAccumulatorHash(libzerocoin::CoinDenomination denom) const
    {
        if(mapAccumulatorHashes.find(denom) != mapAccumulatorHashes.end()) {
            return mapAccumulatorHashes.find(denom)->second;
        }
        else {
            return uint256();
        }
    }

    static constexpr int nMedianTimeSpan = 11;

    int64_t GetMedianTimePast() const
    {
        int64_t pmedian[nMedianTimeSpan];
        int64_t* pbegin = &pmedian[nMedianTimeSpan];
        int64_t* pend = &pmedian[nMedianTimeSpan];

        const CBlockIndex* pindex = this;
        for (int i = 0; i < nMedianTimeSpan && pindex; i++, pindex = pindex->pprev)
            *(--pbegin) = pindex->GetBlockTime();

        std::sort(pbegin, pend);
        return pbegin[(pend - pbegin)/2];
    }

    std::string GetType() const {
        if (fProofOfStake) return "PoS";
        if (IsX16RTProofOfWork()) return "PoW";
        if (IsSha256DProofOfWork()) return "Sha256d";
        if (IsRandomXProofOfWork()) return "RandomX";
        if (IsProgProofOfWork()) return "ProgPow";
        return "Unknown";
    }

    bool IsProofOfWork() const
    {
        return !fProofOfStake;
    }

    bool IsProgProofOfWork() const
    {
        return !fProofOfStake && (nVersion & CBlockHeader::PROGPOW_BLOCK) && nTime >= nPowTimeStampActive;
    }

    bool IsRandomXProofOfWork() const
    {
        return !fProofOfStake && (nVersion & CBlockHeader::RANDOMX_BLOCK) && nTime >= nPowTimeStampActive;
    }

    bool IsSha256DProofOfWork() const
    {
        return !fProofOfStake && (nVersion & CBlockHeader::SHA256D_BLOCK) && nTime >= nPowTimeStampActive;
    }

    bool IsX16RTProofOfWork() const
    {
        return !fProofOfStake && nTime < nPowTimeStampActive;
    }

    bool IsProofOfStake() const
    {
        return fProofOfStake;
    }

    void SetProofOfStake()
    {
        fProofOfStake = true;
    }

    int64_t GetZerocoinSupply() const
    {
        int64_t nTotal = 0;
        for (auto& denom : libzerocoin::zerocoinDenomList) {
            nTotal += libzerocoin::ZerocoinDenominationToAmount(denom) * mapZerocoinSupply.at(denom);
        }

        return nTotal;
    }

    bool MintedDenomination(libzerocoin::CoinDenomination denom) const
    {
        return std::find(vMintDenominationsInBlock.begin(), vMintDenominationsInBlock.end(), denom)
                        != vMintDenominationsInBlock.end();
    }

    std::string ToString() const
    {
        return strprintf("CBlockIndex(pprev=%p, nHeight=%d, merkle=%s, hashBlock=%s)",
            pprev, nHeight,
            hashMerkleRoot.ToString(),
            GetBlockHash().ToString());
    }

    //! Check whether this block index entry is valid up to the passed validity level.
    bool IsValid(enum BlockStatus nUpTo = BLOCK_VALID_TRANSACTIONS) const
    {
        assert(!(nUpTo & ~BLOCK_VALID_MASK)); // Only validity flags allowed.
        if (nStatus & BLOCK_FAILED_MASK)
            return false;
        return ((nStatus & BLOCK_VALID_MASK) >= nUpTo);
    }

    //! Raise the validity level of this block index entry.
    //! Returns true if the validity was changed.
    bool RaiseValidity(enum BlockStatus nUpTo)
    {
        assert(!(nUpTo & ~BLOCK_VALID_MASK)); // Only validity flags allowed.
        if (nStatus & BLOCK_FAILED_MASK)
            return false;
        if ((nStatus & BLOCK_VALID_MASK) < nUpTo) {
            nStatus = (nStatus & ~BLOCK_VALID_MASK) | nUpTo;
            return true;
        }
        return false;
    }

    //! Build the skiplist pointer for this entry.
    void BuildSkip();

    //! Efficiently find an ancestor of this block.
    CBlockIndex* GetAncestor(int height);
    const CBlockIndex* GetAncestor(int height) const;
    const CBlockIndex* GetBestPoWAncestor() const;

    //!Add an accumulator to the CBlockIndex
    void AddAccumulator(libzerocoin::CoinDenomination denom,CBigNum bnAccumulator);

    //! Adds all accumulators to the block idnex given an accumulator map
    void AddAccumulator(AccumulatorMap mapAccumulator);

    //! Copies the value of phashBlock (if any) into a new uint256 owned by the CBlockIndex.
    void CopyBlockHashIntoIndex();
};

arith_uint256 GetBlockProof(const CBlockIndex& block);
/** Return the time it would take to redo the work difference between from and to, assuming the current hashrate corresponds to the difficulty at tip, in seconds. */
int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params&);
/** Find the forking point between two chain tips. */
const CBlockIndex* LastCommonAncestor(const CBlockIndex* pa, const CBlockIndex* pb);


/** Used to marshal pointers into hashes for db storage. */
class CDiskBlockIndex : public CBlockIndex
{
public:
    uint256 hashPrev;

    CDiskBlockIndex() {
        hashPrev = uint256();
    }

    explicit CDiskBlockIndex(const CBlockIndex* pindex) : CBlockIndex(*pindex) {
        hashPrev = (pprev ? pprev->GetBlockHash() : uint256());
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int _nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(VARINT_MODE(_nVersion, VarIntMode::NONNEGATIVE_SIGNED));

        READWRITE(VARINT_MODE(nHeight, VarIntMode::NONNEGATIVE_SIGNED));
        READWRITE(nMoneySupply);
        READWRITE(VARINT(nStatus));
        READWRITE(VARINT(nTx));
        if (nStatus & (BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO))
            READWRITE(VARINT_MODE(nFile, VarIntMode::NONNEGATIVE_SIGNED));
        if (nStatus & BLOCK_HAVE_DATA)
            READWRITE(VARINT(nDataPos));
        if (nStatus & BLOCK_HAVE_UNDO)
            READWRITE(VARINT(nUndoPos));
        READWRITE(nNetworkRewardReserve);

        // block header
        READWRITE(this->nVersion);
        READWRITE(hashPrev);
        bool fCheckVeilDataHash = false;
        // When Veil started the block version in hex it looks is the following 0x20000000
        // We only want to write the hashVeilData to disk when first 4 bits of the version == OLD_POW_MAIN_VERSION or lower, e.g -> 2
        if (nVersion >> BITS_TO_BLOCK_VERSION <= OLD_POW_BLOCK_VERSION) {
            fCheckVeilDataHash = true;
            // This is the version that the new PoW started at. This is when we stop using hashVeilData.
            // We would normally use the nTime of the block. However, that is serialized after the hashVeilData
            // So we don't have access to it
            READWRITE(hashVeilData);
        }
        READWRITE(hashMerkleRoot);
        // NOTE: Careful matching the version, qa tests use different versions
        READWRITE(hashWitnessMerkleRoot);
        READWRITE(hashPoFN);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
        READWRITE(mapAccumulatorHashes);
        READWRITE(mapZerocoinSupply);
        READWRITE(vMintDenominationsInBlock);
        READWRITE(fProofOfFullNode);

        //Proof of stake
        READWRITE(fProofOfStake);

        //Ring CT
        READWRITE(nAnonOutputs);

        if (fProofOfStake) {
            try {
                READWRITE(vHashProof);
            } catch (...) {
                //Could fail since this was added without requiring a reindex
            }
        }

        if (nTime >= nPowTimeStampActive) {
            if (fCheckVeilDataHash) {
                // We want to fail to accept this block. setting it NUll should fail all checks done on it
                SetNull();
                return;
            }

            int nPowType = (nVersion &
                            (CBlockHeader::PROGPOW_BLOCK | CBlockHeader::RANDOMX_BLOCK | CBlockHeader::SHA256D_BLOCK));
            switch (nPowType) {
                case CBlockHeader::PROGPOW_BLOCK:
                    READWRITE(nNonce64);
                    READWRITE(mixHash);
                    break;
                case CBlockHeader::RANDOMX_BLOCK:
                    break;
                case CBlockHeader::SHA256D_BLOCK:
                    READWRITE(nNonce64);
                    break;
                default:
                    // Is POS
                    break;
            }

            READWRITE(hashAccumulators);
        } else {
            // Check for an early new block
            if (!fCheckVeilDataHash) {
                // We want to fail to accept this block. setting it NUll should fail all checks done on it
                SetNull();
                return;
            }
        }
    }

    uint256 GetBlockHash() const
    {
        CBlockHeader block;
        block.nVersion        = nVersion;
        block.hashPrevBlock   = hashPrev;
        block.hashVeilData    = hashVeilData;
        block.nTime           = nTime;
        block.nBits           = nBits;
        block.nNonce          = nNonce;

        // ProgPow
        block.nHeight         = nHeight;
        block.mixHash         = mixHash;

        // New block header items to help remove hashVeilData
        block.nNonce64        = nNonce64;
        block.hashAccumulators = hashAccumulators;
        block.hashMerkleRoot = hashMerkleRoot;
        block.hashWitnessMerkleRoot = hashWitnessMerkleRoot;
        return block.GetHash();
    }


    std::string ToString() const
    {
        std::string str = "CDiskBlockIndex(";
        str += CBlockIndex::ToString();
        str += strprintf("\n                hashBlock=%s, hashPrev=%s)",
            GetBlockHash().ToString(),
            hashPrev.ToString());
        return str;
    }
};

/** An in-memory indexed chain of blocks. */
class CChain {
private:
    mutable CCriticalSection cs_vchain;
    std::vector<CBlockIndex*> vChain GUARDED_BY(cs_vchain);

public:
    /** Returns the index entry for the genesis block of this chain, or nullptr if none. */
    CBlockIndex *Genesis() const {
        LOCK(cs_vchain);
        return vChain.size() > 0 ? vChain[0] : nullptr;
    }

    /** Returns the index entry for the tip of this chain, or nullptr if none. */
    CBlockIndex *Tip() const {
        LOCK(cs_vchain);
        return vChain.size() > 0 ? vChain[vChain.size() - 1] : nullptr;
    }

    /** Returns the index entry at a particular height in this chain, or nullptr if no such height exists. */
    CBlockIndex *operator[](int nHeight) const {
        LOCK(cs_vchain);
        if (nHeight < 0 || nHeight >= (int)vChain.size())
            return nullptr;
        return vChain[nHeight];
    }

    /** Compare two chains efficiently. */
    friend bool operator==(const CChain &a, const CChain &b) {
        // Maintain consistent lock order.
        if (&b < &a) return b == a;

        LOCK2(a.cs_vchain, b.cs_vchain);
        return a.vChain.size() == b.vChain.size() &&
               a.vChain[a.vChain.size() - 1] == b.vChain[b.vChain.size() - 1];
    }

    /** Efficiently check whether a block is present in this chain. */
    bool Contains(const CBlockIndex *pindex) const {
        return (*this)[pindex->nHeight] == pindex;
    }

    /** Find the successor of a block in this chain, or nullptr if the given index is not found or is the tip. */
    CBlockIndex *Next(const CBlockIndex *pindex) const {
        LOCK(cs_vchain);
        if (Contains(pindex))
            return (*this)[pindex->nHeight + 1];
        else
            return nullptr;
    }

    /** Return the maximal height in the chain. Is equal to chain.Tip() ? chain.Tip()->nHeight : -1. */
    int Height() const {
        LOCK(cs_vchain);
        return vChain.size() - 1;
    }

    /** Set/initialize a chain with a given tip. */
    void SetTip(CBlockIndex *pindex);

    /** Return a CBlockLocator that refers to a block in this chain (by default the tip). */
    CBlockLocator GetLocator(const CBlockIndex *pindex = nullptr) const;

    /** Find the last common block between this chain and a block index entry. */
    const CBlockIndex *FindFork(const CBlockIndex *pindex) const;

    /** Find the earliest block with timestamp equal or greater than the given. */
    CBlockIndex* FindEarliestAtLeast(int64_t nTime) const;
};

uint256 GetRandomXBlockHash(const int32_t& height, const uint256& hash_blob);

#endif // BITCOIN_CHAIN_H
