// Copyright (c) 2017-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_MINING_H
#define BITCOIN_RPC_MINING_H

#include <script/script.h>

#include <univalue.h>

class CTempRecipient;

/** Generate blocks (mine) */
UniValue generateBlocks(std::shared_ptr<CReserveScript> coinbaseScript, int nGenerate, uint64_t nMaxTries, bool keepScript);

UniValue generateBlocks(bool fGenerate, int nThreads, std::shared_ptr<CReserveScript> coinbaseScript);

UniValue generateBlocks(std::shared_ptr<CTempRecipient> recipient, int nGenerate, uint64_t nMaxTries, bool keepScript, std::shared_ptr<CStealthAddress> address = nullptr);

UniValue generateBlocks(bool fGenerate, int nThreads, std::shared_ptr<CTempRecipient> recipient);

/** Check bounds on a command line confirm target */
unsigned int ParseConfirmTarget(const UniValue& value);

#endif
