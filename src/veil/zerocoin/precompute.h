// Copyright (c) 2019 The Veil Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_PRECOMPUTE_H
#define VEIL_PRECOMPUTE_H

#include "lrucache.h"
#include "boost/thread.hpp"

static const int DEFAULT_PRECOMPUTE_BPC = 100; // BPC = Blocks Per Cycle
static const int MIN_PRECOMPUTE_BPC = 100;
static const int MAX_PRECOMPUTE_BPC = 2000;

class Precompute
{
private:
    int nBlocksPerCycle;
    boost::thread_group* pthreadGroupPrecompute;

public:

    LRUCache lru;

    Precompute();
    void SetNull();
    boost::thread_group* GetThreadGroupPointer();
    void SetThreadGroupPointer(void* threadGroup);
    void SetThreadPointer();
    std::string StartPrecomputing();
    void StopPrecomputing();
    void SetBlocksPerCycle(const int& nNewBlockPerCycle);
    int GetBlocksPerCycle();
};

void ThreadPrecomputeSpends();
void LinkPrecomputeThreadGroup(void* pthreadgroup);
void DumpPrecomputes();


#endif //VEIL_PRECOMPUTE_H
