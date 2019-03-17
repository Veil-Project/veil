// Copyright (c) 2019 The Veil Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>
#include "precompute.h"

Precompute precomputer = Precompute();

Precompute::Precompute()
{
    SetNull();
}

void Precompute::SetNull()
{
    nBlocksPerCycle = DEFAULT_PRECOMPUTE_BPC;
    pthreadGroupPrecompute = nullptr;
    lru.Clear();
}

boost::thread_group* Precompute::GetThreadGroupPointer()
{
    return pthreadGroupPrecompute;
}

void Precompute::SetThreadGroupPointer(void* threadGroup)
{
    pthreadGroupPrecompute = (boost::thread_group*)threadGroup;
}

std::string Precompute::StartPrecomputing()
{
    if (!pthreadGroupPrecompute) {
        error("%s: pthreadGroupPrecompute is null! Cannot precompute.", __func__);
        return "pthreadGroupPrecompute is null! Cannot precompute.";
    }

    // Close any active precomputing threads before starting new threads
    if (pthreadGroupPrecompute->size() > 0) {
        StopPrecomputing();
    }

    pthreadGroupPrecompute->create_thread(boost::bind(&ThreadPrecomputeSpends));

    return "Start precomputing";
}

void Precompute::StopPrecomputing()
{
    DumpPrecomputes();

    if (!pthreadGroupPrecompute) {
        error("%s: pthreadGroupPrecompute is null! Cannot stop precomputing.", __func__);
        return;
    }

    if (pthreadGroupPrecompute->size() > 0) {
        pthreadGroupPrecompute->interrupt_all();
        pthreadGroupPrecompute->join_all();
    }
}


void Precompute::SetBlocksPerCycle(const int& nNewBlockPerCycle)
{
    nBlocksPerCycle = nNewBlockPerCycle;

    // Force the cache length to be divisible by 10
    if (nBlocksPerCycle % 10)
        nBlocksPerCycle -= nBlocksPerCycle % 10;

    // Enforce the mimimum number of blocks per cycle
    if (nBlocksPerCycle < MIN_PRECOMPUTE_BPC)
        nBlocksPerCycle = MIN_PRECOMPUTE_BPC;

    // Enforce the maximum number of blocks per cycle
    if (nBlocksPerCycle > MAX_PRECOMPUTE_BPC)
        nBlocksPerCycle = MAX_PRECOMPUTE_BPC;
}

int Precompute::GetBlocksPerCycle()
{
    return nBlocksPerCycle;
}

void ThreadPrecomputeSpends()
{
    boost::this_thread::interruption_point();
    LogPrintf("ThreadPrecomputeSpends started\n");
    auto pwallet = GetMainWallet();

    if (!pwallet) {
        LogPrintf("%s: pwallet is null cannot precompute\n", __func__);
        return;
    }

    try {
        pwallet->PrecomputeSpends();
        boost::this_thread::interruption_point();
    }  catch (std::exception& e) {
        LogPrintf("ThreadPrecomputeSpends() exception\n");
    } catch (boost::thread_interrupted) {
        LogPrintf("ThreadPrecomputeSpends() interrupted\n");
    }

    LogPrintf("ThreadPrecomputeSpends exiting,\n");
}

void LinkPrecomputeThreadGroup(void* pthreadgroup)
{
    pprecompute->SetThreadGroupPointer(pthreadgroup);
}

void DumpPrecomputes() {

    if (!pprecomputeDB) {
        LogPrintf("Dump Precomputes: Database pointer not found\n");
        return;
    }

    if (!pprecompute) {
        LogPrintf("Dump Precomputes: Precompute pointer not found\n");
        return;
    }

    int64_t start = GetTimeMicros();
    pprecompute->lru.FlushToDisk(pprecomputeDB.get());
    int64_t end = GetTimeMicros();
    LogPrintf("Dump Precomputes: %gs to dump\n", (end-start)*0.000001);
}

