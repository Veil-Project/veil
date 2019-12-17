// Copyright (c) 2019 Veil developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <tinyformat.h>
#include <boost/thread.hpp>

// ProgPow
#include <crypto/ethash/lib/ethash/endianness.hpp>
#include <crypto/ethash/include/ethash/progpow.hpp>
#include <crypto/ethash/helpers.hpp>

// RandomX
#include <crypto/randomx/randomx.h>
#include <miner.h>
#include <validation.h>


// TODO, build an class object that holds this data
// Used by CPU miner for randomx
static uint256 mining_key_block;
static randomx_cache *myMiningCache;
static randomx_dataset *myMiningDataset;
std::vector<randomx_vm*> vecRandomXVM;
std::vector<std::thread> vecRandomXThreads;
bool fKeyBlockedChanged = false;

// Used by Validator
static uint256 validation_key_block;
static randomx_cache *myCacheValidating;
static randomx_vm *myMachineValidating;
static bool fLightCacheInited = false;

bool IsRandomXLightInit()
{
    return fLightCacheInited;
}

void InitRandomXLightCache(const int32_t& height) {
    if (fLightCacheInited)
        return;

    validation_key_block = GetKeyBlock(height);

    global_randomx_flags = (int)randomx_get_flags();
    myCacheValidating = randomx_alloc_cache((randomx_flags)global_randomx_flags);
    randomx_init_cache(myCacheValidating, &validation_key_block, sizeof uint256());
    myMachineValidating = randomx_create_vm((randomx_flags)global_randomx_flags, myCacheValidating, NULL);
    fLightCacheInited = true;
}

void KeyBlockChanged(const uint256& new_block) {
    validation_key_block = new_block;

    DeallocateRandomXLightCache();
    myCacheValidating = randomx_alloc_cache((randomx_flags)global_randomx_flags);
    randomx_init_cache(myCacheValidating, &validation_key_block, sizeof uint256());
    myMachineValidating = randomx_create_vm((randomx_flags)global_randomx_flags, myCacheValidating, NULL);
    fLightCacheInited = true;
}

uint256 GetCurrentKeyBlock() {
    return validation_key_block;
}

randomx_vm* GetMyMachineValidating() {
    return myMachineValidating;
}

randomx_flags GetRandomXFlags() {
    return (randomx_flags)global_randomx_flags;
}

bool CheckIfMiningKeyShouldChange(const uint256& check_block)
{
    return check_block != mining_key_block;
}
void CheckIfValidationKeyShouldChangeAndUpdate(const uint256& check_block)
{
    if (check_block != validation_key_block)
        KeyBlockChanged(check_block);
}

void DeallocateRandomXLightCache() {
    if (!fLightCacheInited)
        return;

    randomx_destroy_vm(myMachineValidating);
    randomx_release_cache(myCacheValidating);
    fLightCacheInited = false;
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock,
                                    const Consensus::Params& params, bool fProofOfStake, int nPoWType)
{
    assert(pindexLast != nullptr);

    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    if (nPoWType & CBlock::RANDOMX_BLOCK) {
        nProofOfWorkLimit = UintToArith256(params.powLimitRandomX).GetCompact();
    }

    if (params.fPowNoRetargeting) // regtest only
        return nProofOfWorkLimit;

//    if (params.fPowAllowMinDifficultyBlocks) {
//        // Special difficulty rule for testnet:
//        // If the new block's timestamp is more than 10 minutes
//        // then allow mining of a min-difficulty block.
//        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 5)
//            return nProofOfWorkLimit;
//        else {
//            // Return the last non-special-min-difficulty-rules-block
//            const CBlockIndex *pindex = pindexLast;
//            while (pindex->pprev && (pindex->nBits == nProofOfWorkLimit || pindex->nProofOfStakeFlag != fProofOfStake))
//                pindex = pindex->pprev;
//            return pindex->nBits;
//        }
//    }

    // Retarget every block with DarkGravityWave
    return DarkGravityWave(pindexLast, params, fProofOfStake, nPoWType);
}

unsigned int DarkGravityWave(const CBlockIndex* pindexLast, const Consensus::Params& params, bool fProofOfStake, int nPoWType) {
    /* current difficulty formula, veil - DarkGravity v3, written by Evan Duffield - evan@dash.org */
    const arith_uint256 bnPowLimit = nPoWType & CBlock::RANDOMX_BLOCK ? UintToArith256(params.powLimitRandomX) : UintToArith256(params.powLimit);

    const CBlockIndex *pindex = pindexLast;
    const CBlockIndex* pindexLastMatchingProof = nullptr;
    arith_uint256 bnPastTargetAvg = 0;

    unsigned int nCountBlocks = 0;
    while (nCountBlocks < params.nDgwPastBlocks) {
        // Ran out of blocks, return pow limit
        if (!pindex)
            return bnPowLimit.GetCompact();

        // Only consider PoW or PoS blocks but not both
        if (pindex->IsProofOfStake() != fProofOfStake) {
            pindex = pindex->pprev;
            continue;
        } else if (pindex->IsProgProofOfWork() && !(nPoWType & CBlockHeader::PROGPOW_BLOCK)) {
            pindex = pindex->pprev;
            continue;
        } else if (pindex->IsRandomXProofOfWork() && !(nPoWType & CBlockHeader::RANDOMX_BLOCK)) {
            pindex = pindex->pprev;
            continue;
        } else if (pindex->IsSha256DProofOfWork() && !(nPoWType & CBlockHeader::SHA256D_BLOCK)) {
            pindex = pindex->pprev;
            continue;
        } else if (!pindexLastMatchingProof) {
            pindexLastMatchingProof = pindex;
        }

        arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
        bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);

        if (++nCountBlocks != params.nDgwPastBlocks)
            pindex = pindex->pprev;
    }

    arith_uint256 bnNew(bnPastTargetAvg);

    //Should only happen on the first PoS block
    if (pindexLastMatchingProof)
        pindexLastMatchingProof = pindexLast;

    int64_t nPowSpacing;
    if (nPoWType & CBlockHeader::PROGPOW_BLOCK) {
        nPowSpacing = params.nProgPowTargetSpacing;
    } else if (nPoWType & CBlockHeader::RANDOMX_BLOCK) {
        nPowSpacing = params.nRandomXTargetSpacing;
    } else if (nPoWType & CBlockHeader::SHA256D_BLOCK) {
        nPowSpacing = params.nSha256DTargetSpacing;
    } else {
        nPowSpacing = params.nPowTargetSpacing;
    }

    int64_t nActualTimespan = pindexLastMatchingProof->GetBlockTime() - pindex->GetBlockTime();
    int64_t nTargetTimespan = params.nDgwPastBlocks * nPowSpacing;

    if (nActualTimespan < nTargetTimespan/3)
        nActualTimespan = nTargetTimespan/3;
    if (nActualTimespan > nTargetTimespan*3)
        nActualTimespan = nTargetTimespan*3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit)) {
        //std::cout << fNegative << " " << (bnTarget == 0) << " " << fOverflow << " " << (bnTarget > UintToArith256(params.powLimit)) << "\n";
        return false;
    }

    // Check proof of work matches claimed amount
    return UintToArith256(hash) < bnTarget;
}

bool CheckProgProofOfWork(const CBlockHeader& block, unsigned int nBits, const Consensus::Params& params)
{
    // Create epoch context
    ethash::epoch_context_ptr context{nullptr, nullptr};

    // Create the eth_boundary from the nBits
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit)) {
        return false;
    }

    // Get the context from the block height
    const auto epoch_number = ethash::get_epoch_number(block.nHeight);
    if (!context || context->epoch_number != epoch_number)
        context = ethash::create_epoch_context(epoch_number);

    // Build the header_hash
    uint256 nHeaderHash = block.GetProgPowHeaderHash();
    const auto header_hash = to_hash256(nHeaderHash.GetHex());

    // ProgPow hash
    const auto result = progpow::hash(*context, block.nHeight, header_hash, block.nNonce64);

    // Get the eth boundary
    auto boundary = to_hash256(ArithToUint256(bnTarget).GetHex());

    return progpow::verify(
            *context, block.nHeight, header_hash, result.mix_hash, block.nNonce64, boundary);
}

#define KEY_CHANGE 2048
#define SWITCH_KEY 64

uint256 GetKeyBlock(const uint32_t& nHeight)
{
    static uint256 current_key_block;

    auto remainer = nHeight % KEY_CHANGE;

    auto first_check = nHeight - remainer;
    auto second_check = nHeight - KEY_CHANGE - remainer;

    if (nHeight > nHeight - remainer + SWITCH_KEY) {
        if (chainActive.Height() > first_check)
            current_key_block = chainActive[first_check]->GetBlockHash();
    } else {
        if (chainActive.Height() > second_check)
            current_key_block = chainActive[second_check]->GetBlockHash();
    }

    if (current_key_block == uint256())
        current_key_block = chainActive.Genesis()->GetBlockHash();

    return current_key_block;
}

bool CheckRandomXProofOfWork(const CBlockHeader& block, unsigned int nBits, const Consensus::Params& params)
{
    InitRandomXLightCache(block.nHeight);

    // This will check if the key block needs to change and will take down the cache and vm, and spin up the new ones
    CheckIfValidationKeyShouldChangeAndUpdate(GetKeyBlock(block.nHeight));

    // Create epoch context
    ethash::epoch_context_ptr context{nullptr, nullptr};

    // Create the eth_boundary from the nBits
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimitRandomX)) {
        //std::cout << fNegative << " " << (bnTarget == 0) << " " << fOverflow << " " << (bnTarget > UintToArith256(params.powLimit)) << "\n";
        return false;
    }

    uint256 hash_blob = block.GetRandomXHeaderHash();

    char hash[RANDOMX_HASH_SIZE];

    randomx_calculate_hash(GetMyMachineValidating(), &hash_blob, sizeof uint256(), hash);

    uint256 nHash = RandomXHashToUint256(hash);

    // Check proof of work matches claimed amount
    return UintToArith256(nHash) < bnTarget;

}

uint256 RandomXHashToUint256(const char* p_char)
{
    int i;
    std::vector<unsigned char> vec;
    for (i = 0; i < RANDOMX_HASH_SIZE; i++) {
        vec.push_back(p_char[i]);
    }

    std::string hexStr = HexStr(vec.begin(), vec.end());
    return uint256S(hexStr);
}

void StartRandomXMining(void* pPowThreadGroup, const int nThreads, std::shared_ptr<CReserveScript> pCoinbaseScript)
{
    bool fInitialized = false;
    auto threadGroup = (boost::thread_group *) pPowThreadGroup;
    while (true) {
        boost::this_thread::interruption_point();
        if (!fInitialized) {
            boost::this_thread::interruption_point();
            auto full_flags = RANDOMX_FLAG_FULL_MEM;

            myMiningCache = randomx_alloc_cache((randomx_flags)global_randomx_flags);

            /// Create the RandomX Dataset
            myMiningDataset = randomx_alloc_dataset((randomx_flags)global_randomx_flags);
            mining_key_block = GetKeyBlock(chainActive.Height());

            randomx_init_cache(myMiningCache, &mining_key_block, sizeof(mining_key_block));

            auto nTime1 = GetTimeMillis();
            LogPrintf(" %s: Starting dataset creation\n", __func__);

            CreateRandomXInitDataSet(nThreads, myMiningDataset, myMiningCache);
            boost::this_thread::interruption_point();

            auto nTime2 = GetTimeMillis();
            LogPrintf(" %s: Finished dataset creation %.2fms\n", __func__, nTime2 - nTime1);

            boost::this_thread::interruption_point();
            /// Create the RandomX Virtual Machines
            for (int i = 0; i < nThreads; ++i) {
                randomx_vm *vm = randomx_create_vm(full_flags, nullptr, myMiningDataset);
                if (vm == nullptr) {
                    LogPrintf("Cannot create VM\n");
                    return;
                }
                vecRandomXVM.push_back(vm);
            }
            boost::this_thread::interruption_point();
            auto nTime3 = GetTimeMillis();
            LogPrintf("%s: Finished vm creation %.2fms\n", __func__, nTime3 - nTime2);
            boost::this_thread::interruption_point();
            uint32_t startNonce = 0;
            for (int i = 0; i < nThreads; i++) {
                threadGroup->create_thread(boost::bind(&ThreadRandomXBitcoinMiner, pCoinbaseScript, i, startNonce));
                startNonce += 100000;
            }
            boost::this_thread::interruption_point();
            fInitialized = true;
        }

        if (fKeyBlockedChanged) {
            boost::this_thread::interruption_point();
            threadGroup->interrupt_all();
            threadGroup->join_all();
            DeallocateVMVector();
            DeallocateDataSet();
            fInitialized = false;
            fKeyBlockedChanged = false;
        }
    }
}

void CreateRandomXInitDataSet(int nThreads, randomx_dataset* dataset, randomx_cache* cache)
{
    uint32_t datasetItemCount = randomx_dataset_item_count();

    if (nThreads > 1) {
        uint32_t datasetItemCount = randomx_dataset_item_count();
        auto perThread = datasetItemCount / nThreads;
        auto remainder = datasetItemCount % nThreads;
        uint32_t startItem = 0;
        for (int i = 0; i < nThreads; ++i) {
            auto count = perThread + (i == nThreads - 1 ? remainder : 0);
            vecRandomXThreads.push_back(std::thread(&randomx_init_dataset, dataset, cache, startItem, count));
            startItem += count;
        }

        for (unsigned i = 0; i < vecRandomXThreads.size(); ++i) {
            vecRandomXThreads[i].join();
        }
    } else {
        randomx_init_dataset(dataset, cache, 0, datasetItemCount);
    }

    randomx_release_cache(cache);
    cache = nullptr;
    vecRandomXThreads.clear();
}

void DeallocateVMVector()
{
    if (vecRandomXVM.size()) {
        for (unsigned i = 0; i < vecRandomXVM.size(); ++i)
            randomx_destroy_vm(vecRandomXVM[i]);
    }

    vecRandomXVM.clear();
}

void DeallocateDataSet()
{
    if (myMiningDataset == nullptr)
        return;

    randomx_release_dataset(myMiningDataset);
}

void DeallocateCache()
{
    if (myMiningCache == nullptr)
        return;

    randomx_release_cache(myMiningCache);
}
