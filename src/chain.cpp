// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2019-2020 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <validation.h>
#include <hash.h>
#include <libzerocoin/Denominations.h>

#include <crypto/randomx/randomx.h>
#include <pow.h>

/**
 * CChain implementation
 */
void CChain::SetTip(CBlockIndex *pindex) {
    LOCK(cs_vchain);
    if (pindex == nullptr) {
        vChain.clear();
        return;
    }
    vChain.resize(pindex->nHeight + 1);
    while (pindex && vChain[pindex->nHeight] != pindex) {
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;
    }
}

CBlockLocator CChain::GetLocator(const CBlockIndex *pindex) const {
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

const CBlockIndex *CChain::FindFork(const CBlockIndex *pindex) const {
    if (pindex == nullptr) {
        return nullptr;
    }
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex;
}

CBlockIndex* CChain::FindEarliestAtLeast(int64_t nTime) const
{
    LOCK(cs_vchain);
    std::vector<CBlockIndex*>::const_iterator lower = std::lower_bound(vChain.begin(), vChain.end(), nTime,
        [](CBlockIndex* pBlock, const int64_t& time) -> bool { return pBlock->GetBlockTimeMax() < time; });
    return (lower == vChain.end() ? nullptr : *lower);
}

/** Turn the lowest '1' bit in the binary representation of a number into a '0'. */
int static inline InvertLowestOne(int n) { return n & (n - 1); }

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
int static inline GetSkipHeight(int height) {
    if (height < 2)
        return 0;

    // Determine which height to jump back to. Any number strictly lower than height is acceptable,
    // but the following expression seems to perform well in simulations (max 110 steps to go back
    // up to 2**18 blocks).
    return (height & 1) ? InvertLowestOne(InvertLowestOne(height - 1)) + 1 : InvertLowestOne(height);
}

const CBlockIndex* CBlockIndex::GetAncestor(int height) const
{
    if (height > nHeight || height < 0) {
        return nullptr;
    }

    const CBlockIndex* pindexWalk = this;
    int heightWalk = nHeight;
    while (heightWalk > height) {
        int heightSkip = GetSkipHeight(heightWalk);
        int heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (pindexWalk->pskip != nullptr &&
            (heightSkip == height ||
             (heightSkip > height && !(heightSkipPrev < heightSkip - 2 &&
                                       heightSkipPrev >= height)))) {
            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        } else {
            if (!pindexWalk->pprev) {
                LogPrintf("%s: pindexWalk:  Hash: %s, Looking for Height: %d, Current Height: %d, Working Height: %d, pindexWalk height: %d\n",
                          __func__, pindexWalk->phashBlock->GetHex(), height, heightWalk, nHeight, pindexWalk->nHeight);
            }
            assert(pindexWalk->pprev);
            pindexWalk = pindexWalk->pprev;
            heightWalk--;
        }
    }
    return pindexWalk;
}

CBlockIndex* CBlockIndex::GetAncestor(int height)
{
    return const_cast<CBlockIndex*>(static_cast<const CBlockIndex*>(this)->GetAncestor(height));
}

const CBlockIndex* CBlockIndex::GetBestPoWAncestor() const
{
    const CBlockIndex* pindexBestPoW = nullptr;
    if (this->IsProofOfWork()) {
        pindexBestPoW = this;
    } else {
        const CBlockIndex* pprev = this;
        while (pprev->pprev) {
            pprev = pprev->pprev;
            if (pprev->IsProofOfWork()) {
                pindexBestPoW = pprev;
                break;
            }
        }
    }
    return pindexBestPoW;
}

void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
}

int64_t CBlockIndex::GetBlockWork() const
{
    int64_t nTimeSpan = 0;
    if (pprev && pprev->pprev)
        nTimeSpan = pprev->GetBlockTime() - pprev->pprev->GetBlockTime();
    int64_t nBlockWork = 1000 - nTimeSpan;

    //PoS blocks have the final decision on consensus, if it is between a PoW block and PoS block
    // PoS blocks are also the only type of block that has the scoring of the block directly based on the elapsed time
    // of this block and the prev. PoW is not bound by timestamps the same way PoS is, so it is not as safe to do with PoW
    if (IsProofOfStake()) {
        nTimeSpan = GetBlockTime() - pprev->GetBlockTime();
        if (nTimeSpan < 0)
            nTimeSpan = 1;
        nBlockWork += (1000 - nTimeSpan);
    }
    if (nBlockWork <= 0)
        nBlockWork = 1;
    return nBlockWork;
}

int64_t CBlockIndex::GetBlockPoW() const
{
    if (!IsProofOfWork())
        return 0;

    int64_t nBlockPoW = 0;
    if (pprev) {
        int64_t nTimeSpan = 0;
        const CBlockIndex* pindexWalk = pprev;
        while (pindexWalk->pprev) {
            if (pindexWalk->IsProofOfWork()) {
                nTimeSpan = GetBlockTime() - pindexWalk->GetBlockTime();
                break;
            }
            pindexWalk = pindexWalk->pprev;
        }
        nBlockPoW = 1000 - nTimeSpan;
    }
    if (nBlockPoW < 1)
        nBlockPoW = 1;
    return nBlockPoW;
}

arith_uint256 CBlockIndex::GetChainPoW() const
{
    if (!pprev)
        return 0;
    return pprev->nChainPoW + (IsProofOfWork() ? GetBlockPoW() : 0);
}

arith_uint256 GetBlockProof(const CBlockIndex& block)
{
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for an arith_uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (bnTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}

int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params& params)
{
    arith_uint256 r;
    int sign = 1;
    if (to.nChainWork > from.nChainWork) {
        r = to.nChainWork - from.nChainWork;
    } else {
        r = from.nChainWork - to.nChainWork;
        sign = -1;
    }
    r = r * arith_uint256(params.nPowTargetSpacing) / GetBlockProof(tip);
    if (r.bits() > 63) {
        return sign * std::numeric_limits<int64_t>::max();
    }
    return sign * r.GetLow64();
}

/** Find the last common ancestor two blocks have.
 *  Both pa and pb must be non-nullptr. */
const CBlockIndex* LastCommonAncestor(const CBlockIndex* pa, const CBlockIndex* pb) {
    if (pa->nHeight > pb->nHeight) {
        pa = pa->GetAncestor(pb->nHeight);
    } else if (pb->nHeight > pa->nHeight) {
        pb = pb->GetAncestor(pa->nHeight);
    }

    while (pa != pb && pa && pb) {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}

void CBlockIndex::AddAccumulator(libzerocoin::CoinDenomination denom, CBigNum bnAccumulator)
{
    mapAccumulatorHashes[denom] = SerializeHash(bnAccumulator);
    hashAccumulators = SerializeHash(mapAccumulatorHashes);
}

void CBlockIndex::AddAccumulator(AccumulatorMap mapAccumulator)
{
    for(libzerocoin::CoinDenomination denom : libzerocoin::zerocoinDenomList) {
        CBigNum bnAccumulator = mapAccumulator.GetValue(denom);
        mapAccumulatorHashes[denom] = SerializeHash(bnAccumulator);
    }
    hashAccumulators = SerializeHash(mapAccumulatorHashes);
}

uint256 CBlockIndex::GetRandomXPoWHash() const
{
    return GetRandomXBlockHash(GetBlockHeader().nHeight, GetBlockHeader().GetRandomXHeaderHash());
}

uint256 CBlockIndex::GetProgPowHash(uint256& mix_hash) const
{
    CBlockHeader header = GetBlockHeader();
    return ProgPowHash(header, mix_hash);
}

void CBlockIndex::CopyBlockHashIntoIndex()
{
    if (phashBlock) {
        _phashBlock.reset(new uint256(*phashBlock));
        phashBlock = _phashBlock.get();
    }
}

// We are going to be performing a block hash for RandomX. To see if we need to spin up a new
// cache, we can first check to see if we can use the current validation cache
uint256 GetRandomXBlockHash(const int32_t& height, const uint256& hash_blob ) {

    char hash[RANDOMX_HASH_SIZE];

    // Get the keyblock for the height
    auto temp_keyblock = GetKeyBlock(height);

    // The hash we are calculating is in the same realm at the current validation caches
    // We don't need to spin up a new cache, as we can use the one already allocated
    if (temp_keyblock == GetCurrentKeyBlock()) {
        if (!IsRandomXLightInit()) {
            InitRandomXLightCache(height);
        }

        randomx_calculate_hash(GetMyMachineValidating(), &hash_blob, sizeof uint256(), hash);
        return RandomXHashToUint256(hash);
    } else {
        // Create a new temp cache, and machine
        auto temp_cache = randomx_alloc_cache((randomx_flags)global_randomx_flags);
        randomx_init_cache(temp_cache, &temp_keyblock, sizeof uint256());
        auto tempMachine = randomx_create_vm((randomx_flags)global_randomx_flags, temp_cache, NULL);

        // calculate the hash
        randomx_calculate_hash(tempMachine, &hash_blob, sizeof uint256(), hash);

        // Destroy the vm and cache
        randomx_destroy_vm(tempMachine);
        randomx_release_cache(temp_cache);

        // Return the hash
        return RandomXHashToUint256(hash);
    }
}

