// Copyright (c) 2017-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_BLOCKCHAIN_H
#define BITCOIN_RPC_BLOCKCHAIN_H

#include <vector>
#include <stdint.h>
#include <amount.h>
#include <map>
#include <arith_uint256.h>

class CBlock;
class CBlockIndex;
class UniValue;
class uint256;

static constexpr int NUM_GETBLOCKSTATS_PERCENTILES = 5;

static const unsigned int DEFAULT_CHECK_DIFFICULTY_BLOCK_COUNT = 1440; // ~1 day

static const unsigned int ALGO_RATIO_LOOK_BACK_BLOCK_COUNT = 1440; // ~1 day

extern std::map<std::string, CBlock> mapProgPowTemplates;

/**
 * Get the difficulty of the net wrt to the given block index, or the chain tip if
 * not provided.
 *
 * @return A floating point number that is a multiple of the main net minimum
 * difficulty (4295032833 hashes).
 */
double GetDifficulty(const CBlockIndex* blockindex);
double GetDifficulty(const uint32_t nBits);
double GetDifficulty(const arith_uint256 bn);

/** Callback for when block tip changed. */
void RPCNotifyBlockChange(bool ibd, const CBlockIndex *);

/** Block description to JSON */
UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false);

/** Mempool information to JSON */
UniValue mempoolInfoToJSON();

/** Mempool to JSON */
UniValue mempoolToJSON(bool fVerbose = false);

/** Block header to JSON */
UniValue blockheaderToJSON(const CBlockIndex* blockindex);

/** Used by getblockstats to get feerates at different percentiles by weight  */
void CalculatePercentilesByWeight(CAmount result[NUM_GETBLOCKSTATS_PERCENTILES], std::vector<std::pair<CAmount, int64_t>>& scores, int64_t total_weight);

#endif
