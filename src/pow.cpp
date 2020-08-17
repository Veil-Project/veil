// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2019-2020 Veil developers
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
CCriticalSection cs_randomx_mining;
static uint256 mining_key_block;
static randomx_cache *myMiningCache;
static randomx_dataset *myMiningDataset;
std::vector<randomx_vm*> vecRandomXVM;
std::vector<std::thread> vecRandomXThreads;
bool fKeyBlockedChanged = false;

// Used by Validator
CCriticalSection cs_randomx_validator;
static uint256 validation_key_block;
static randomx_cache *myCacheValidating;
static randomx_vm *myMachineValidating;
static bool fLightCacheInited = false;

bool IsRandomXLightInit()
{
    LOCK(cs_randomx_validator);
    return fLightCacheInited;
}

void InitRandomXLightCache(const int32_t& height) {
    LOCK(cs_randomx_validator);
    if (fLightCacheInited)
        return;

    validation_key_block = GetKeyBlock(height);

    global_randomx_flags = (int)randomx_get_flags();
    myCacheValidating = randomx_alloc_cache((randomx_flags)global_randomx_flags);
    randomx_init_cache(myCacheValidating, &validation_key_block, sizeof uint256());
    LogPrintf("%s : Spinning up a new vm  at new block height: %d\n", __func__, height);
    myMachineValidating = randomx_create_vm_timed((randomx_flags)global_randomx_flags, myCacheValidating, NULL, true);
    fLightCacheInited = true;
}

void KeyBlockChanged(const uint256& new_block) {
    LOCK(cs_randomx_validator);
    validation_key_block = new_block;

    DeallocateRandomXLightCache();
    myCacheValidating = randomx_alloc_cache((randomx_flags)global_randomx_flags);
    randomx_init_cache(myCacheValidating, &validation_key_block, sizeof uint256());
    LogPrintf("%s : Spinning up a new vm  at new block: %s\n", __func__, new_block.GetHex());
    myMachineValidating = randomx_create_vm_timed((randomx_flags)global_randomx_flags, myCacheValidating, NULL, true);
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
    LOCK(cs_randomx_validator);
    return check_block != mining_key_block;
}
void CheckIfValidationKeyShouldChangeAndUpdate(const uint256& check_block)
{
    LOCK(cs_randomx_validator);
    if (check_block != validation_key_block)
        KeyBlockChanged(check_block);
}

void DeallocateRandomXLightCache() {
    LOCK(cs_randomx_validator);
    if (!fLightCacheInited) {
        LogPrintf("%s Return because light cache isn't inited\n", __func__);
        return;
    }

    if (myMachineValidating) {
        LogPrintf("%s releasing the vm\n",__func__);
        randomx_destroy_vm(myMachineValidating);
        myMachineValidating = nullptr;
    }

    if (myCacheValidating) {
        LogPrintf("%s releasing the validating cache\n",__func__);
        randomx_release_cache(myCacheValidating);
        myCacheValidating = nullptr;
    }

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

    // Retarget every block with DarkGravityWave
    return DarkGravityWave(pindexLast, params, fProofOfStake, nPoWType);
}

unsigned int DarkGravityWave(const CBlockIndex* pindexLast, const Consensus::Params& params, bool fProofOfStake, int nPoWType) {
    /* current difficulty formula, veil - DarkGravity v3, written by Evan Duffield - evan@dash.org */
    arith_uint256 bnPowLimit;

    // Select the correct pow limit
    if (nPoWType & CBlockHeader::PROGPOW_BLOCK) {
        bnPowLimit = UintToArith256(params.powLimitProgPow);
    } else if (nPoWType & CBlock::RANDOMX_BLOCK) {
        bnPowLimit = UintToArith256(params.powLimitRandomX);
    } else if (nPoWType & CBlock::SHA256D_BLOCK) {
        bnPowLimit = UintToArith256(params.powLimitSha256);
    } else {
        bnPowLimit = UintToArith256(params.powLimit);
    }

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
        } else if ((nPoWType & CBlockHeader::PROGPOW_BLOCK) && !pindex->IsProgProofOfWork()) {
            pindex = pindex->pprev;
            continue;
        } else if ((nPoWType & CBlockHeader::RANDOMX_BLOCK) && !pindex->IsRandomXProofOfWork()) {
            pindex = pindex->pprev;
            continue;
        } else if ((nPoWType & CBlockHeader::SHA256D_BLOCK) && !pindex->IsSha256DProofOfWork()) {
            pindex = pindex->pprev;
            continue;
        } else if (pindex->IsX16RTProofOfWork() && !fProofOfStake && nPoWType != 0) {
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
    if (pindexLastMatchingProof && !pindex->IsProgProofOfWork() && !pindex->IsRandomXProofOfWork() && !pindex->IsSha256DProofOfWork()) {
        pindexLastMatchingProof = pindexLast;
    }

    bool fNewPoW = false;
    int64_t nPowSpacing;
    if (nPoWType & CBlockHeader::PROGPOW_BLOCK) {
        nPowSpacing = params.nProgPowTargetSpacing;
        fNewPoW = true;
    } else if (nPoWType & CBlockHeader::RANDOMX_BLOCK) {
        nPowSpacing = params.nRandomXTargetSpacing;
        fNewPoW = true;
    } else if (nPoWType & CBlockHeader::SHA256D_BLOCK) {
        nPowSpacing = params.nSha256DTargetSpacing;
        fNewPoW = true;
    } else {
        nPowSpacing = params.nPowTargetSpacing;
    }

    int64_t nActualTimespan = pindexLastMatchingProof->GetBlockTime() - pindex->GetBlockTime();
    int64_t nTargetTimespan = params.nDgwPastBlocks * nPowSpacing;

    // If we are calculating the diff for RandomX, Sha256, or ProgPow
    // We want to take into account the latest block tip time into the calculation
    int64_t nTipToLastMatchingProof = pindexLast->GetBlockTime() - pindexLastMatchingProof->GetBlockTime();
    if (fNewPoW && nTipToLastMatchingProof > nPowSpacing) {
        nActualTimespan += nTipToLastMatchingProof - nPowSpacing;
    }

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
        return false;
    }

    // Check proof of work matches claimed amount
    return UintToArith256(hash) < bnTarget;
}

#define KEY_CHANGE 2048
#define SWITCH_KEY 64

uint256 GetKeyBlock(const uint32_t& nHeight)
{
    static uint256 current_key_block = uint256();

    uint32_t checkMultiplier = 0;

    // We don't want to go negative
    if (nHeight >= SWITCH_KEY)
        checkMultiplier = (nHeight - SWITCH_KEY) / KEY_CHANGE;

    int checkHeight = checkMultiplier * KEY_CHANGE;

    if (chainActive.Height() >= checkHeight) {
	    current_key_block = chainActive[checkHeight]->GetBlockHash();
    } else {
	    LogPrintf("%s: Not synched for KeyBlock(%d) @ %d, Synched Height: %d\n", __func__,
                  checkHeight, nHeight, chainActive.Height());
    }

    if (current_key_block == uint256())
        current_key_block = chainActive.Genesis()->GetBlockHash();

    return current_key_block;
}

bool CheckRandomXProofOfWork(const CBlockHeader& block, unsigned int nBits, const Consensus::Params& params)
{
    LOCK(cs_randomx_validator);
    InitRandomXLightCache(block.nHeight);

    // This will check if the key block needs to change and will take down the cache and vm, and spin up the new ones
    CheckIfValidationKeyShouldChangeAndUpdate(GetKeyBlock(block.nHeight));

    // Create the eth_boundary from the nBits
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimitRandomX)) {
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
            LogPrintf("%s: Starting dataset creation\n", __func__);

            CreateRandomXInitDataSet(nThreads, myMiningDataset, myMiningCache);
            boost::this_thread::interruption_point();

            auto nTime2 = GetTimeMillis();
            LogPrintf("%s: Finished dataset creation %.2fms\n", __func__, nTime2 - nTime1);

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
    myMiningDataset = nullptr;
}

void DeallocateCache()
{
    LOCK(cs_randomx_validator);
    if (myMiningCache == nullptr)
        return;

    randomx_release_cache(myMiningCache);
    myMiningCache = nullptr;
}

