// Copyright (c) 2015-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CORE_MEMUSAGE_H
#define BITCOIN_CORE_MEMUSAGE_H

#include <primitives/transaction.h>
#include <primitives/block.h>
#include <memusage.h>

static inline size_t RecursiveDynamicUsage(const CScript& script) {
    return memusage::DynamicUsage(script);
}

static inline size_t RecursiveDynamicUsage(const COutPoint& out) {
    return 0;
}

static inline size_t RecursiveDynamicUsage(const CTxIn& in) {
    size_t mem = RecursiveDynamicUsage(in.scriptSig) + RecursiveDynamicUsage(in.prevout) + memusage::DynamicUsage(in.scriptWitness.stack);
    for (std::vector<std::vector<unsigned char> >::const_iterator it = in.scriptWitness.stack.begin(); it != in.scriptWitness.stack.end(); it++) {
         mem += memusage::DynamicUsage(*it);
    }
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CTxOut& out) {
    return RecursiveDynamicUsage(out.scriptPubKey);
}

static inline size_t RecursiveDynamicUsage(const CTxOutBaseRef& pOut) {
    auto type = pOut->GetType();
    size_t mem = 0;
    switch (type) {
        case OutputTypes::OUTPUT_STANDARD:
            mem += RecursiveDynamicUsage(*pOut->GetPScriptPubKey());
            break;
        case OutputTypes::OUTPUT_RINGCT:
        {
            auto* outRingCT = dynamic_cast<CTxOutRingCT*>(pOut.get());
            mem += memusage::DynamicUsage(outRingCT->vData) + memusage::DynamicUsage(outRingCT->vRangeproof);
            break;
        }
        case OutputTypes::OUTPUT_CT :
        {
            auto outCT = dynamic_cast<CTxOutCT*>(pOut.get());
            mem += RecursiveDynamicUsage(outCT->scriptPubKey) + memusage::DynamicUsage(outCT->vRangeproof) + memusage::DynamicUsage(outCT->vData);
            break;
        }
        case OutputTypes::OUTPUT_DATA :
        {
            auto outData = dynamic_cast<CTxOutData*>(pOut.get());
            mem += memusage::DynamicUsage(outData->vData);
            break;
        }
        default:
            break;
    }
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CTransaction& tx) {
    size_t mem = memusage::DynamicUsage(tx.vin) + memusage::DynamicUsage(tx.vpout);
    for (std::vector<CTxIn>::const_iterator it = tx.vin.begin(); it != tx.vin.end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    for (const auto& pout : tx.vpout) {
        mem += RecursiveDynamicUsage(pout);
    }
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CMutableTransaction& tx) {
    size_t mem = memusage::DynamicUsage(tx.vin) + memusage::DynamicUsage(tx.vpout);
    for (std::vector<CTxIn>::const_iterator it = tx.vin.begin(); it != tx.vin.end(); it++) {
        mem += RecursiveDynamicUsage(*it);
    }
    for (const auto& pout : tx.vpout) {
        mem += RecursiveDynamicUsage(pout);
    }
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CBlock& block) {
    size_t mem = memusage::DynamicUsage(block.vtx);
    for (const auto& tx : block.vtx) {
        mem += memusage::DynamicUsage(tx) + RecursiveDynamicUsage(*tx);
    }
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CBlockLocator& locator) {
    return memusage::DynamicUsage(locator.vHave);
}

template<typename X>
static inline size_t RecursiveDynamicUsage(const std::shared_ptr<X>& p) {
    return p ? memusage::DynamicUsage(p) + RecursiveDynamicUsage(*p) : 0;
}

#endif // BITCOIN_CORE_MEMUSAGE_H
