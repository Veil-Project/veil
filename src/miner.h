// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2019-2020 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MINER_H
#define BITCOIN_MINER_H

#include <primitives/block.h>
#include <txmempool.h>
#include <validation.h>

#include <stdint.h>
#include <memory>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>

class CBlockIndex;
class CChainParams;
class CScript;


// Used for determining which PoW mining algorithm to use
extern const char * PROGPOW_STRING;
extern const char * SHA256D_STRING;
extern const char * RANDOMX_STRING;

extern int nMiningAlgorithm;

enum {
    MINE_RANDOMX = 0,
    MINE_PROGPOW = 1,
    MINE_SHA256D = 2
};

static std::string GetMiningType(int nPoWType, bool fProofOfStake = false, bool block=true) {
    if (block) {
        if (fProofOfStake) return "PoS";
        if (!nPoWType) return "X16RT";
        if (nPoWType & CBlockHeader::SHA256D_BLOCK) return "Sha256d";
        if (nPoWType & CBlockHeader::RANDOMX_BLOCK) return "RandomX";
        if (nPoWType & CBlockHeader::PROGPOW_BLOCK) return "ProgPow";
    } else {
        if (MINE_RANDOMX == nPoWType) return RANDOMX_STRING;
        if (MINE_PROGPOW == nPoWType) return PROGPOW_STRING;
        if (MINE_SHA256D == nPoWType) return SHA256D_STRING;
    }
    return "Unknown";
}

double GetHashSpeed();
void ClearHashSpeed();
double GetRecentHashSpeed();
int GetMiningAlgorithm();
bool SetMiningAlgorithm(const std::string& algo, bool fSet = true);

// End Pow algorithm to use

namespace Consensus { struct Params; };

static const bool DEFAULT_PRINTPRIORITY = false;

// This is used for both the nonce separation and the loop count, so
// make it a constant used by both to maintain their synchronization
static const uint32_t RANDOMX_INNER_LOOP_COUNT = 100000;

enum TemplateFlags
{
    TF_FAIL = 0,
    TF_SUCCESS = (1 << 0),
    TF_STAILTIP = (1 << 1),
    TF_MEMPOOLFAIL = (1 << 2),
};

struct CBlockTemplate
{
    CBlock block;
    std::vector<CAmount> vTxFees;
    std::vector<int64_t> vTxSigOpsCost;
    std::vector<unsigned char> vchCoinbaseCommitment;
    uint8_t nFlags;
};

// Container for tracking updates to ancestor feerate as we include (parent)
// transactions in a block
struct CTxMemPoolModifiedEntry {
    explicit CTxMemPoolModifiedEntry(CTxMemPool::txiter entry)
    {
        iter = entry;
        nSizeWithAncestors = entry->GetSizeWithAncestors();
        nModFeesWithAncestors = entry->GetModFeesWithAncestors();
        nSigOpCostWithAncestors = entry->GetSigOpCostWithAncestors();
    }

    int64_t GetModifiedFee() const { return iter->GetModifiedFee(); }
    uint64_t GetSizeWithAncestors() const { return nSizeWithAncestors; }
    CAmount GetModFeesWithAncestors() const { return nModFeesWithAncestors; }
    size_t GetTxSize() const { return iter->GetTxSize(); }
    const CTransaction& GetTx() const { return iter->GetTx(); }

    CTxMemPool::txiter iter;
    uint64_t nSizeWithAncestors;
    CAmount nModFeesWithAncestors;
    int64_t nSigOpCostWithAncestors;
};

/** Comparator for CTxMemPool::txiter objects.
 *  It simply compares the internal memory address of the CTxMemPoolEntry object
 *  pointed to. This means it has no meaning, and is only useful for using them
 *  as key in other indexes.
 */
struct CompareCTxMemPoolIter {
    bool operator()(const CTxMemPool::txiter& a, const CTxMemPool::txiter& b) const
    {
        return &(*a) < &(*b);
    }
};

struct modifiedentry_iter {
    typedef CTxMemPool::txiter result_type;
    result_type operator() (const CTxMemPoolModifiedEntry &entry) const
    {
        return entry.iter;
    }
};

// A comparator that sorts transactions based on number of ancestors.
// This is sufficient to sort an ancestor package in an order that is valid
// to appear in a block.
struct CompareTxIterByAncestorCount {
    bool operator()(const CTxMemPool::txiter &a, const CTxMemPool::txiter &b) const
    {
        if (a->GetCountWithAncestors() != b->GetCountWithAncestors())
            return a->GetCountWithAncestors() < b->GetCountWithAncestors();
        return CTxMemPool::CompareIteratorByHash()(a, b);
    }
};

typedef boost::multi_index_container<
    CTxMemPoolModifiedEntry,
    boost::multi_index::indexed_by<
        boost::multi_index::ordered_unique<
            modifiedentry_iter,
            CompareCTxMemPoolIter
        >,
        // sorted by modified ancestor fee rate
        boost::multi_index::ordered_non_unique<
            // Reuse same tag from CTxMemPool's similar index
            boost::multi_index::tag<ancestor_score>,
            boost::multi_index::identity<CTxMemPoolModifiedEntry>,
            CompareTxMemPoolEntryByAncestorFee
        >
    >
> indexed_modified_transaction_set;

typedef indexed_modified_transaction_set::nth_index<0>::type::iterator modtxiter;
typedef indexed_modified_transaction_set::index<ancestor_score>::type::iterator modtxscoreiter;

struct update_for_parent_inclusion
{
    explicit update_for_parent_inclusion(CTxMemPool::txiter it) : iter(it) {}

    void operator() (CTxMemPoolModifiedEntry &e)
    {
        e.nModFeesWithAncestors -= iter->GetFee();
        e.nSizeWithAncestors -= iter->GetTxSize();
        e.nSigOpCostWithAncestors -= iter->GetSigOpCost();
    }

    CTxMemPool::txiter iter;
};

/** Generate a new block, without valid proof-of-work */
class BlockAssembler
{
private:
    // The constructed block template
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    // A convenience pointer that always refers to the CBlock in pblocktemplate
    CBlock* pblock;

    // Configuration parameters for the block size
    bool fIncludeWitness;
    unsigned int nBlockMaxWeight;
    CFeeRate blockMinFeeRate;

    // Information on the current status of the block
    uint64_t nBlockWeight;
    uint64_t nBlockTx;
    uint64_t nBlockSigOpsCost;
    CAmount nFees;
    CTxMemPool::setEntries inBlock;

    // Chain context for the block
    int nHeight;
    int64_t nLockTimeCutoff;
    const CChainParams& chainparams;

public:
    struct Options {
        Options();
        size_t nBlockMaxWeight;
        CFeeRate blockMinFeeRate;
    };

    explicit BlockAssembler(const CChainParams& params);
    BlockAssembler(const CChainParams& params, const Options& options);

    /** Construct a new block template with coinbase to scriptPubKeyIn */
    std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript& scriptPubKeyIn, bool fMineWitnessTx=true, bool fProofOfStake=false, bool fProofOfFullNode = false);

private:
    // utility functions
    /** Clear the block's state and prepare for assembling a new block */
    void resetBlock();
    /** Add a tx to the block */
    void AddToBlock(CTxMemPool::txiter iter);

    // Methods for how to add transactions to a block.
    /** Add transactions based on feerate including unconfirmed ancestors
      * Increments nPackagesSelected / nDescendantsUpdated with corresponding
      * statistics from the package selection (for logging statistics). */
    void addPackageTxs(int &nPackagesSelected, int &nDescendantsUpdated) EXCLUSIVE_LOCKS_REQUIRED(mempool.cs);

    // helper functions for addPackageTxs()
    /** Remove confirmed (inBlock) entries from given set */
    void onlyUnconfirmed(CTxMemPool::setEntries& testSet);
    /** Test if a new package would "fit" in the block */
    bool TestPackage(uint64_t packageSize, int64_t packageSigOpsCost) const;
    /** Perform checks on each transaction in a package:
      * locktime, premature-witness, serialized size (if necessary)
      * These checks should always succeed, and they're here
      * only as an extra check in case of suboptimal node configuration */
    bool TestPackageTransactions(const CTxMemPool::setEntries& package);
    /** Return true if given transaction from mapTx has already been evaluated,
      * or if the transaction's cached data in mapTx is incorrect. */
    bool SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx) EXCLUSIVE_LOCKS_REQUIRED(mempool.cs);
    /** Sort the package in an order that is valid to appear in a block */
    void SortForBlock(const CTxMemPool::setEntries& package, std::vector<CTxMemPool::txiter>& sortedEntries);
    /** Add descendants of given transactions to mapModifiedTx with ancestor
      * state updated assuming given transactions are inBlock. Returns number
      * of updated descendants. */
    int UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded, indexed_modified_transaction_set &mapModifiedTx) EXCLUSIVE_LOCKS_REQUIRED(mempool.cs);
};

bool GenerateActive();
void setGenerate(bool fGenerate);

/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock* pblock, unsigned int nHeight, unsigned int& nExtraNonce);
int64_t UpdateTime(CBlock* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev);
void GenerateBitcoins(bool fGenerate, int nThreads, std::shared_ptr<CReserveScript> coinbaseScript);
void ThreadStakeMiner();
void LinkPoWThreadGroup(void* pthreadgroup);
void LinkRandomXThreadGroup(void* pthreadgroup);
void ThreadRandomXBitcoinMiner(std::shared_ptr<CReserveScript> coinbaseScript, const int vm_index, const uint32_t startNonce);

#endif // BITCOIN_MINER_H
