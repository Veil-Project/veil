// Copyright (c) 2018-2019 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_RINGCT_TRANSACTIONSIGCONTEXT_H
#define VEIL_RINGCT_TRANSACTIONSIGCONTEXT_H

#include <cstdint>
#include <string>
#include <vector>

#include <key.h>
#include <secp256k1_mlsag.h>
#include "veil/ringct/types.h"

namespace veil_ringct {

// A wrapper to hold vectors for ringct txn signing, for the inputs.
struct TransactionInputsSigContext {
    TransactionInputsSigContext(size_t nCols, size_t nRows)
        : vsk(nRows - 1), vpsk(nRows), vm(nCols * nRows * 33),
          vpInCommits(nCols * (nRows - 1))
    {
        vCommitments.reserve(nCols * (nRows - 1));
    }

    size_t secretColumn;

    std::vector<std::vector<int64_t>> vMI;
    std::vector<uint8_t> vPubkeyMatrixIndices;
    // SetBlinds
    std::vector<CKey> vsk;
    std::vector<const uint8_t*> vpsk;
    std::vector<uint8_t> vm;
    std::vector<secp256k1_pedersen_commitment> vCommitments;
    std::vector<const uint8_t*> vpInCommits;
    ec_point vInputBlinds;
    std::vector<const uint8_t*> vpBlinds;
};

// A wrapper to hold vectors for ringct txn signing, for the outputs.
struct TransactionOutputsSigContext {
    TransactionOutputsSigContext() : vBlindPlain(32) {}

    // ArrangeOutBlinds
    std::vector<const uint8_t*> vpOutCommits;
    std::vector<const uint8_t*> vpOutBlinds;
    std::vector<uint8_t> vBlindPlain;
    secp256k1_pedersen_commitment plainCommitment;
};

}  // namespace veil_ringct

#endif  // VEIL_RINGCT_TRANSACTIONSIGCONTEXT_H