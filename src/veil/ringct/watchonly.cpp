// Copyright (c) 2021 The Flux Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "veil/ringct/watchonly.h"
#include "veil/ringct/watchonlydb.h"

#include <rpc/util.h>
#include <key_io.h>
#include <core_io.h>
#include <univalue.h>
#include <veil/ringct/anonwallet.h>

#include <veil/ringct/blind.h>
#include <secp256k1_mlsag.h>
#include <rpc/protocol.h>
#include <validation.h>
#include <util/time.h>
#include <util/system.h>

#include <boost/thread.hpp>

/** Map of watchonly keys */
std::map<CKeyID, CWatchOnlyAddress> mapWatchOnlyAddresses;

/** Global transaction cache instance */
CWatchOnlyTxCache watchonlyTxCache;

/** Global block cache instance */
CWatchOnlyBlockCache watchonlyBlockCache;

boost::thread_group* pthreadGroupWatchOnly;
bool fScanning = false;

void LinkWatchOnlyThreadGroup(void* pthreadgroup)
{
    pthreadGroupWatchOnly = (boost::thread_group*)pthreadgroup;
}

void StopWatchonlyScanningThread()
{
    if (!pthreadGroupWatchOnly) {
        error("%s: pthreadGroupWatchOnly is null! Cannot stop scanning.", __func__);
        return;
    }

    if (pthreadGroupWatchOnly->size() > 0) {
        pthreadGroupWatchOnly->interrupt_all();
        pthreadGroupWatchOnly->join_all();
    }

    fScanning = false;

    LogPrintf("Watchonly Scanning stopped\n");
}

bool StartWatchonlyScanningThread()
{
    if (!pthreadGroupWatchOnly) {
        error("%s: pthreadGroupWatchOnly is null! Cannot scan.", __func__);
        return false;
    }

    // Close any active auto spend threads before starting new threads
    // Do we want to stop active threads, or have the ability to start a new one
    if (pthreadGroupWatchOnly->size() > 0) {
        StopWatchonlyScanningThread();
    }

    pthreadGroupWatchOnly->create_thread(std::bind(&ScanWatchOnlyAddresses));
    return true;
}

bool StartWatchonlyScanningIfNotStarted()
{
    if (!pthreadGroupWatchOnly) {
        error("%s: pthreadGroupWatchOnly is null! Cannot scan.", __func__);
        return false;
    }

    if (fScanning) {
        LogPrintf("Scanning thread already running! Doesn't need to be run.\n");
        return false;
    }

    return StartWatchonlyScanningThread();
}

void ScanWatchOnlyAddresses()
{
    boost::this_thread::interruption_point();
    try {
        int64_t nMilliSeconds = 3000;
        int count = 0;
        int64_t nTotalTxsFound = 0;
        int64_t nTotalBlocksScanned = 0;
        int64_t nOverallStartTime = GetTimeMicros();

        LogPrint(BCLog::WATCHONLYIMPORT, "%s: Starting watchonly address scanning thread\n", __func__);

        while (true) {
            boost::this_thread::interruption_point();
            int nNumberOfBlockPerScan = 500;
            fScanning = true;

            //Find the smallest block height to start scanning at.
            int64_t nStartBlockHeight = 0;
            {
                LOCK(cs_watchonly);
                bool fSet = false;
                for (const auto &items : mapWatchOnlyAddresses) {
                    boost::this_thread::interruption_point();
                    if (items.second.nCurrentScannedHeight != items.second.nImportedHeight) {
                        if (!fSet) {
                            nStartBlockHeight = items.second.nCurrentScannedHeight;
                            fSet = true;
                        }
                        if (items.second.nCurrentScannedHeight < nStartBlockHeight) {
                            nStartBlockHeight = items.second.nCurrentScannedHeight;
                        }
                    }
                }

                if (!fSet) {
                    LogPrintf("%s: Nothing to watchonly scan stopping thread - \n", __func__);
                    break;
                }
            }

            int64_t currentTipHeight = chainActive.Height();

            if (nStartBlockHeight >= currentTipHeight) {
                break;
            }

            if (nStartBlockHeight + nNumberOfBlockPerScan > currentTipHeight) {
                // Don't go above the tip
                nNumberOfBlockPerScan = currentTipHeight - nStartBlockHeight + 1;
                LogPrintf("Changing blocks per scan to %d\n", nNumberOfBlockPerScan);
            }

            int64_t nBatchStartTime = GetTimeMicros();
            int64_t nBlocksRemaining = currentTipHeight - nStartBlockHeight;
            LogPrint(BCLog::WATCHONLYIMPORT, "%s: Scanning batch starting at height %d to %d (%d blocks, %d remaining to tip %d)\n",
                     __func__, nStartBlockHeight, nStartBlockHeight + nNumberOfBlockPerScan - 1,
                     nNumberOfBlockPerScan, nBlocksRemaining, currentTipHeight);

            std::vector<std::vector<CTxOutWatchonly>> vecNewRingCTTransactions(nNumberOfBlockPerScan, std::vector<CTxOutWatchonly>());
            std::vector<std::vector<CTxOutWatchonly>> vecRingCTTranasctionToScan(nNumberOfBlockPerScan, std::vector<CTxOutWatchonly>());
            for (int i = 0; i < nNumberOfBlockPerScan; i++) {
                boost::this_thread::interruption_point();
                int nIndexHeight = nStartBlockHeight + i;

                std::vector<CTxOutWatchonly> vTransactions;

                if (pwatchonlyDB->ReadBlockTransactions(nIndexHeight, vTransactions)) {
                    vecRingCTTranasctionToScan[i] = vTransactions;
                } else {
                    const CBlockIndex *pblockindex = chainActive[nIndexHeight];

                    if (!pblockindex) {
                        LogPrintf("%s: Block not found at height %d - \n", __func__, nIndexHeight);
                        continue;
                    }

                    CBlock block;
                    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) {
                        // Block not found on disk. This could be because we have the block
                        // header in our index but don't have the block (for example if a
                        // non-whitelisted node sends us an unrequested long chain of valid
                        // blocks, we add the headers to our index, but don't accept the
                        // block).
                        LogPrintf("%s: Block not found on disk at height %d - \n", __func__, nIndexHeight);
                        continue;
                    }

                    for (const auto &tx : block.vtx) {
                        int index = 0;
                        for (const auto &txout : tx->vpout) {
                            if (txout->IsType(OUTPUT_RINGCT)) {
                                const CTxOutRingCT *rctout = (CTxOutRingCT *) txout.get();
                                CTxOutWatchonly out;
                                out.type = CTxOutWatchonly::ANON;
                                out.tx_hash = tx->GetHash();
                                out.nIndex = index;
                                out.ringctOut = *rctout;
                                vecNewRingCTTransactions[i].push_back(out);
                            } else if (txout->IsType(OUTPUT_CT)) {
                                const CTxOutCT *ctout = (CTxOutCT *) txout.get();
                                CTxOutWatchonly out;
                                out.type = CTxOutWatchonly::STEALTH;
                                out.tx_hash = tx->GetHash();
                                out.nIndex = index;
                                out.ctOut = *ctout;
                                vecNewRingCTTransactions[i].push_back(out);
                            }
                            index++;
                        }
                    }
                }
            }

            // Cache block transactions (write to DB in batches for performance)
            bool fUseBlockCache = gArgs.GetBoolArg("-watchonlycache", false);
            for (int i = 0; i < nNumberOfBlockPerScan; i++) {
                boost::this_thread::interruption_point();
                int nIndexHeight = nStartBlockHeight + i;
                if (vecNewRingCTTransactions[i].size()) {
                    if (fUseBlockCache) {
                        // Use cache for better performance during initial scan
                        watchonlyBlockCache.AddBlock(nIndexHeight, vecNewRingCTTransactions[i]);

                        // Flush if cache is full
                        if (watchonlyBlockCache.ShouldFlush()) {
                            watchonlyBlockCache.FlushAll();
                        }
                    } else {
                        // Direct write (backward compatible)
                        pwatchonlyDB->WriteBlockTransactions(nIndexHeight, vecNewRingCTTransactions[i]);
                    }
                    vecRingCTTranasctionToScan[i] = vecNewRingCTTransactions[i];
                }
            }

            int64_t nBatchTxsFound = 0;

            for (int i = 0; i < nNumberOfBlockPerScan; i++) {
                boost::this_thread::interruption_point();

                // Get block height and timestamp for this batch
                int64_t nCurrentBlockHeight = nStartBlockHeight + i;
                int64_t nCurrentBlockTime = 0;
                const CBlockIndex* pCurrentBlockIndex = chainActive[nCurrentBlockHeight];
                if (pCurrentBlockIndex) {
                    nCurrentBlockTime = pCurrentBlockIndex->GetBlockTime();
                }

                // Get a list of CWatchOnlyAddress to scan with, pre-computing elliptic curve keys
                std::vector<CWatchOnlyAddressPrecomputed> scanThese;
                for (const auto &addr : mapWatchOnlyAddresses) {
                    if (addr.second.nCurrentScannedHeight != addr.second.nImportedHeight) {
                        if (nStartBlockHeight + i > addr.second.nCurrentScannedHeight &&
                            nStartBlockHeight + i <= addr.second.nImportedHeight) {
                            // addr.first is now CKeyID
                            scanThese.emplace_back(CWatchOnlyAddressPrecomputed(addr.first, addr.second));
                        }
                    }
                }

                // Skip expensive crypto operations if no addresses need scanning at this height
                if (scanThese.empty()) {
                    continue;
                }

                for (const auto& watchonlyout : vecRingCTTranasctionToScan[i]) {

                    CKey sShared;
                    CWatchOnlyTx watchonlyTx;
                    std::vector<uint8_t> vchEphemPK;
                    CKeyID idk;

                    if (watchonlyout.type == CTxOutWatchonly::ANON) {
                        auto rctout = watchonlyout.ringctOut;
                        /// Scan through transactions for txes that are owned.
                        idk = rctout.pk.GetID();

                        // Uncover stealth
                        uint32_t prefix = 0;
                        bool fHavePrefix = false;
                        if (rctout.vData.size() != 33) {
                            if (rctout.vData.size() == 38 // Have prefix
                                && rctout.vData[33] == DO_STEALTH_PREFIX) {
                                fHavePrefix = true;
                                memcpy(&prefix, &rctout.vData[34], 4);
                            } else {
                                continue;
                            }
                        }

                        vchEphemPK.resize(33);
                        memcpy(&vchEphemPK[0], &rctout.vData[0], 33);
                    } else if (watchonlyout.type == CTxOutWatchonly::STEALTH) {
                        auto ctout = watchonlyout.ctOut;

                        if(!KeyIdFromScriptPubKey(ctout.scriptPubKey, idk)) {
                            LogPrintf("ScanWatchOnlyAddresses() Failed to get KeyId from script.\n");
                        }

                        vchEphemPK.resize(33);
                        memcpy(&vchEphemPK[0], &ctout.vData[0], 33);
                    } else {
                        continue;
                    }
                        /// Scan through transactions for txes that are owned.
                    for (const auto& precomp : scanThese) {
                        ec_point pkExtracted;
                        // Use pre-computed ecSpendPubKey instead of calling SetPublicKey
                        if (StealthSecret(precomp.address.scan_secret, vchEphemPK, precomp.ecSpendPubKey, sShared, pkExtracted) != 0) {
                            continue;
                        }

                        CPubKey pubKeyStealthSecret(pkExtracted);
                        if (!pubKeyStealthSecret.IsValid()) {
                            continue;
                        }

                        CKeyID idExtracted = pubKeyStealthSecret.GetID();
                        if (idk != idExtracted) {
                            continue;
                        }

                        CTxOutRingCT txringCT;
                        CTxOutCT txCT;
                        if (watchonlyout.type == CTxOutWatchonly::ANON) {
                            txringCT.vRangeproof = watchonlyout.ringctOut.vRangeproof;
                            txringCT.vData = watchonlyout.ringctOut.vData;
                            txringCT.commitment = watchonlyout.ringctOut.commitment;
                            txringCT.pk = watchonlyout.ringctOut.pk;
                            txringCT.nVersion = watchonlyout.ringctOut.nVersion;
                        } else if (watchonlyout.type == CTxOutWatchonly::STEALTH) {
                            txCT.vRangeproof = watchonlyout.ctOut.vRangeproof;
                            txCT.vData = watchonlyout.ctOut.vData;
                            txCT.commitment = watchonlyout.ctOut.commitment;
                            txCT.scriptPubKey = watchonlyout.ctOut.scriptPubKey;
                            txCT.nVersion = watchonlyout.ctOut.nVersion;
                        }

                        CWatchOnlyTx watchOnlyTx;
                        watchonlyTx.type = watchonlyout.type;
                        watchonlyTx.scan_secret = precomp.address.scan_secret;
                        watchonlyTx.tx_hash = watchonlyout.tx_hash;
                        watchonlyTx.tx_index = watchonlyout.nIndex;
                        watchonlyTx.nBlockHeight = nCurrentBlockHeight;
                        watchonlyTx.nBlockTime = nCurrentBlockTime;
                        if (watchonlyout.type == CTxOutWatchonly::ANON) {
                            watchonlyTx.ringctout = txringCT;
                        } else if (watchonlyout.type == CTxOutWatchonly::STEALTH) {
                            watchonlyTx.ctout = txCT;
                        }

                        // TODO. Instead of writing to database everytime we find a new tx.
                        // TODO Maybe could put it into a cache, and only database once cache hits a certain number
                        // TODO Also make sure we database the cache on shutdowns and anyother time we database the
                        // TODO watchonly stuff.
                        AddWatchOnlyTransaction(watchonlyTx.scan_secret, watchonlyTx);
                        nBatchTxsFound++;
                    }
                }

                {
                    // Flush cached transactions for this block if caching is enabled
                    bool fUseCache = gArgs.GetBoolArg("-watchonlycache", false);
                    if (fUseCache) {
                        for (const auto &precomp : scanThese) {
                            watchonlyTxCache.Flush(precomp.address.scan_secret);
                        }
                    }

                    // Update scanned heights
                    for (const auto &precomp : scanThese) {
                        if (mapWatchOnlyAddresses.count(precomp.keyID)) {
                            LOCK(cs_watchonly);
                            mapWatchOnlyAddresses.at(precomp.keyID).fDirty = true;
                            mapWatchOnlyAddresses.at(precomp.keyID).nCurrentScannedHeight = nStartBlockHeight + i;
                        }
                    }
                }
            }

            // Batch complete - log performance metrics
            int64_t nBatchEndTime = GetTimeMicros();
            double nBatchElapsedSeconds = (nBatchEndTime - nBatchStartTime) / 1000000.0;
            double nBlocksPerSecond = nBatchElapsedSeconds > 0 ? nNumberOfBlockPerScan / nBatchElapsedSeconds : 0;
            nTotalTxsFound += nBatchTxsFound;
            nTotalBlocksScanned += nNumberOfBlockPerScan;

            double nOverallElapsedSeconds = (nBatchEndTime - nOverallStartTime) / 1000000.0;
            double nOverallBlocksPerSecond = nOverallElapsedSeconds > 0 ? nTotalBlocksScanned / nOverallElapsedSeconds : 0;

            LogPrint(BCLog::WATCHONLYIMPORT, "%s: Batch complete - scanned %d blocks in %.2f seconds (%.2f blocks/sec)\n",
                     __func__, nNumberOfBlockPerScan, nBatchElapsedSeconds, nBlocksPerSecond);
            LogPrint(BCLog::WATCHONLYIMPORT, "%s: Found %d transactions in this batch (%d total txs found)\n",
                     __func__, nBatchTxsFound, nTotalTxsFound);
            LogPrint(BCLog::WATCHONLYIMPORT, "%s: Progress - Total blocks scanned: %d, Overall speed: %.2f blocks/sec, Elapsed time: %.2f seconds\n",
                     __func__, nTotalBlocksScanned, nOverallBlocksPerSecond, nOverallElapsedSeconds);

            // Estimate time remaining
            if (nBlocksRemaining > 0 && nOverallBlocksPerSecond > 0) {
                double nEstimatedSecondsRemaining = nBlocksRemaining / nOverallBlocksPerSecond;
                int nEstimatedMinutesRemaining = (int)(nEstimatedSecondsRemaining / 60);
                int nEstimatedHoursRemaining = nEstimatedMinutesRemaining / 60;
                if (nEstimatedHoursRemaining > 0) {
                    LogPrint(BCLog::WATCHONLYIMPORT, "%s: Estimated time remaining: ~%d hours %d minutes\n",
                             __func__, nEstimatedHoursRemaining, nEstimatedMinutesRemaining % 60);
                } else if (nEstimatedMinutesRemaining > 0) {
                    LogPrint(BCLog::WATCHONLYIMPORT, "%s: Estimated time remaining: ~%d minutes\n",
                             __func__, nEstimatedMinutesRemaining);
                } else {
                    LogPrint(BCLog::WATCHONLYIMPORT, "%s: Estimated time remaining: ~%.0f seconds\n",
                             __func__, nEstimatedSecondsRemaining);
                }
            }
        }
        // Scanning complete - log final summary
        int64_t nOverallEndTime = GetTimeMicros();
        double nTotalElapsedSeconds = (nOverallEndTime - nOverallStartTime) / 1000000.0;
        double nFinalBlocksPerSecond = nTotalElapsedSeconds > 0 ? nTotalBlocksScanned / nTotalElapsedSeconds : 0;

        LogPrint(BCLog::WATCHONLYIMPORT, "%s: ========== SCAN COMPLETE ==========\n", __func__);
        LogPrint(BCLog::WATCHONLYIMPORT, "%s: Total blocks scanned: %d\n", __func__, nTotalBlocksScanned);
        LogPrint(BCLog::WATCHONLYIMPORT, "%s: Total transactions found: %d\n", __func__, nTotalTxsFound);
        LogPrint(BCLog::WATCHONLYIMPORT, "%s: Total time elapsed: %.2f seconds (%.2f minutes)\n",
                 __func__, nTotalElapsedSeconds, nTotalElapsedSeconds / 60.0);
        LogPrint(BCLog::WATCHONLYIMPORT, "%s: Average speed: %.2f blocks/sec\n", __func__, nFinalBlocksPerSecond);
        LogPrint(BCLog::WATCHONLYIMPORT, "%s: ===================================\n", __func__);

        // Flush all caches to database
        watchonlyBlockCache.FlushAll();
        watchonlyTxCache.FlushAll();  // This writes checkpoints for each address

        // Flush watch-only addresses to save updated scanned heights
        FlushWatchOnlyAddresses();

    } catch (std::exception& e) {
        LogPrintf("ScanWatchOnlyAddresses() exception\n");
        // Flush caches on exception to save any progress
        watchonlyBlockCache.FlushAll();
        watchonlyTxCache.FlushAll();
        FlushWatchOnlyAddresses();
    } catch (boost::thread_interrupted) {
        LogPrintf("ScanWatchOnlyAddresses() interrupted\n");
        // Flush caches on interruption to save any progress
        watchonlyBlockCache.FlushAll();
        watchonlyTxCache.FlushAll();
        FlushWatchOnlyAddresses();
    }

    fScanning = false;
    LogPrintf("ScanWatchOnlyAddresses stopping\n");
}

// ===== CWatchOnlyTxCache Implementation =====

size_t CWatchOnlyTxCache::GetTotalSize()
{
    LOCK(cs_cache);
    // O(1) access - no loop needed!
    return nTotalCached;
}

bool CWatchOnlyTxCache::ShouldFlush(const CKey& key)
{
    LOCK(cs_cache);
    // O(1) check using running counter
    return nTotalCached >= nMaxCacheSize;
}

void CWatchOnlyTxCache::AddTx(const CKey& key, const CWatchOnlyTx& tx)
{
    LOCK(cs_cache);
    mapPendingTxes[key].push_back(tx);
    nTotalCached++; // Increment running counter

    // Track highest block height for checkpoint writing
    if (tx.nBlockHeight > 0) {
        if (!mapHighestBlockHeight.count(key) || tx.nBlockHeight > mapHighestBlockHeight[key]) {
            mapHighestBlockHeight[key] = tx.nBlockHeight;
            // Get the block hash for this height
            const CBlockIndex* pindex = chainActive[tx.nBlockHeight];
            if (pindex) {
                mapHighestBlockHash[key] = pindex->GetBlockHash();
            }
        }
    }
}

void CWatchOnlyTxCache::GetCachedTxes(const CKey& key, std::vector<CWatchOnlyTx>& vTxes)
{
    LOCK(cs_cache);
    vTxes.clear();

    if (mapPendingTxes.count(key)) {
        vTxes = mapPendingTxes[key];
    }
}

bool CWatchOnlyTxCache::Flush(const CKey& key)
{
    LOCK(cs_cache);
    if (!mapPendingTxes.count(key) || mapPendingTxes[key].empty())
        return true;

    int current_count = 0;
    GetWatchOnlyKeyCount(key, current_count);

    size_t numTxs = mapPendingTxes[key].size();
    int64_t nHeight = mapHighestBlockHeight.count(key) ? mapHighestBlockHeight[key] : 0;
    uint256 blockHash = mapHighestBlockHash.count(key) ? mapHighestBlockHash[key] : uint256();

    LogPrint(BCLog::WATCHONLYDB, "%s: Flushing %d cached transactions for key at height %d\n",
             __func__, numTxs, nHeight);

    // Use bulk write instead of individual writes for better performance
    if (!pwatchonlyDB->WriteBulkWatchOnlyTx(key, current_count, mapPendingTxes[key])) {
        return error("%s: Failed to bulk write transactions", __func__);
    }

    // Write checkpoint if we have block height info (checkpoints are always enabled)
    if (nHeight > 0) {
        CWatchOnlyScanCheckpoint checkpoint;
        checkpoint.scan_secret = key;
        checkpoint.nBlockHeight = nHeight;
        checkpoint.nTxCount = current_count + numTxs;
        checkpoint.blockHash = blockHash;
        checkpoint.nTimestamp = GetTime();

        if (!pwatchonlyDB->WriteCheckpoint(key, checkpoint)) {
            return error("%s: Failed to write checkpoint at height %d", __func__, nHeight);
        }

        LogPrint(BCLog::WATCHONLYDB, "%s: Wrote checkpoint at height %d\n", __func__, nHeight);
    }

    mapPendingTxes[key].clear();
    mapHighestBlockHeight.erase(key);
    mapHighestBlockHash.erase(key);
    nTotalCached -= numTxs; // Decrement running counter
    LogPrint(BCLog::WATCHONLYDB, "%s: Successfully flushed cache for key (total remaining: %d)\n",
             __func__, nTotalCached);
    return true;
}

bool CWatchOnlyTxCache::FlushAll()
{
    LOCK(cs_cache);
    size_t totalTxs = nTotalCached; // Use O(1) counter instead of calculating
    size_t numAddresses = mapPendingTxes.size();
    LogPrint(BCLog::WATCHONLYDB, "%s: Flushing %d total transactions across %d addresses\n",
             __func__, totalTxs, numAddresses);

    for (const auto& pair : mapPendingTxes) {
        if (!pair.second.empty()) {
            // Temporarily unlock to allow Flush to acquire its own lock
            LEAVE_CRITICAL_SECTION(cs_cache);
            bool result = Flush(pair.first);
            ENTER_CRITICAL_SECTION(cs_cache);

            if (!result) {
                return error("%s: Failed to flush cache", __func__);
            }
        }
    }

    LogPrint(BCLog::WATCHONLYDB, "%s: Successfully flushed all addresses (total remaining: %d)\n",
             __func__, nTotalCached);
    return true;
}

size_t CWatchOnlyTxCache::GetSize(const CKey& key)
{
    LOCK(cs_cache);
    if (!mapPendingTxes.count(key))
        return 0;
    return mapPendingTxes[key].size();
}

// ===== End CWatchOnlyTxCache Implementation =====

// ===== CWatchOnlyBlockCache Implementation =====

void CWatchOnlyBlockCache::AddBlock(int64_t nHeight, const std::vector<CTxOutWatchonly>& vTxes)
{
    LOCK(cs_blockcache);
    mapPendingBlocks[nHeight] = vTxes;
}

bool CWatchOnlyBlockCache::ShouldFlush()
{
    LOCK(cs_blockcache);
    return mapPendingBlocks.size() >= nMaxBlocks;
}

size_t CWatchOnlyBlockCache::GetSize()
{
    LOCK(cs_blockcache);
    return mapPendingBlocks.size();
}

bool CWatchOnlyBlockCache::FlushAll()
{
    LOCK(cs_blockcache);

    if (mapPendingBlocks.empty())
        return true;

    size_t numBlocks = mapPendingBlocks.size();
    LogPrint(BCLog::WATCHONLYDB, "%s: Flushing %d cached blocks to database in single batch\n",
             __func__, numBlocks);

    // Single atomic write of all blocks using bulk method
    if (!pwatchonlyDB->WriteBulkBlockTransactions(mapPendingBlocks)) {
        return error("%s: Failed to write batch of %d blocks", __func__, numBlocks);
    }

    mapPendingBlocks.clear();

    LogPrint(BCLog::WATCHONLYDB, "%s: Successfully flushed %d blocks in single batch write\n",
             __func__, numBlocks);
    return true;
}

// ===== End CWatchOnlyBlockCache Implementation =====


CWatchOnlyAddress::CWatchOnlyAddress()
{
//    SetNull();
}

CWatchOnlyAddress::CWatchOnlyAddress(const CKey& scan, const CPubKey& spend, const int64_t& nStartScanning, const int64_t& nImported, const int64_t& nScannedHeight) {
    scan_secret = scan;
    spend_pubkey = spend;
    nScanStartHeight = nStartScanning;
    nImportedHeight = nImported;
    nCurrentScannedHeight = nScannedHeight;
}

CWatchOnlyTx::CWatchOnlyTx(const CKey& key, const uint256& txhash) {
    type = NOTSET;
    scan_secret = key;
    tx_hash = txhash;
    nBlockHeight = 0;
    nBlockTime = 0;
}

CWatchOnlyTx::CWatchOnlyTx() {
    type = NOTSET;
    scan_secret.Clear();
    tx_hash.SetNull();
    nBlockHeight = 0;
    nBlockTime = 0;
}

UniValue CWatchOnlyTx::GetUniValue(int& index, bool spent, std::string keyimage, uint256 txhash, bool fSkip, CAmount amount, int confirmations, int64_t blocktime, std::string rawtx) const
{
    UniValue out(UniValue::VOBJ);

    out.pushKV("type", this->type ? "anon" : "stealth");
    if (!fSkip) {
        out.pushKV("keyimage", keyimage);
        out.pushKV("amount", ValueFromAmount(amount));
        out.pushKV("spent", spent);
        if (spent) {
            out.pushKV("spent_in", txhash.GetHex());
        }
    }

    out.pushKV("dbindex", index);
    if (this->type == ANON) {
        RingCTOutputToJSON(this->tx_hash, this->tx_index, this->ringctIndex, this->ringctout, out);
    } else if (this->type == STEALTH) {
        CTOutputToJSON(this->tx_hash, this->tx_index, this->ctout, out);
    }

    // Add verbose fields if provided
    if (confirmations >= 0) {
        out.pushKV("confirmations", confirmations);
    }

    if (blocktime > 0) {
        out.pushKV("blocktime", blocktime);
    }

    if (!rawtx.empty()) {
        out.pushKV("hex", rawtx);
    }

    CWatchOnlyTxWithIndex watchonlywithindex;
    watchonlywithindex.watchonlytx = *this;
    if (this->type == ANON) {
        watchonlywithindex.ringctindex = this->ringctIndex;
    } else {
        watchonlywithindex.ringctindex = -1;
    }

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << watchonlywithindex;
    out.pushKV("raw", HexStr(ssTx));
    return out;
}

bool AddWatchOnlyAddress(const std::string& address, const CKey& scan_secret, const CPubKey& spend_pubkey, const int64_t& nStart, const int64_t& nImported)
{
    // Use CKeyID as the map key (V2)
    CKeyID keyID = scan_secret.GetPubKey().GetID();

    if (mapWatchOnlyAddresses.count(keyID)) {
        auto watchOnlyInfo = mapWatchOnlyAddresses.at(keyID);
        if (scan_secret == watchOnlyInfo.scan_secret && spend_pubkey == watchOnlyInfo.spend_pubkey) {
            LogPrint(BCLog::WATCHONLYDB, "Trying to add a watchonly address that already exists: %s (keyID: %s)\n", address, keyID.ToString());
            return true;
        } else {
            LogPrint(BCLog::WATCHONLYDB, "Trying to add a watchonly address with different scan key and pubkey then what already exists: %s\n", address);
            return false;
        }
    } else {
        CWatchOnlyAddress newAddress(scan_secret, spend_pubkey, nStart, nImported, nStart);

        // Use V2 write method
        if (!pwatchonlyDB->WriteWatchOnlyAddressV2(keyID, newAddress)) {
            LogPrint(BCLog::WATCHONLYDB, "Failed to WriteWatchOnlyAddress: %s (keyID: %s)\n", address, keyID.ToString());
            return false;
        }
        LOCK(cs_watchonly);
        mapWatchOnlyAddresses.insert(std::make_pair(keyID, newAddress));
    }

    StartWatchonlyScanningIfNotStarted();
    return true;
}

bool RemoveWatchOnlyAddress(const std::string& address, const CKey& scan_secret, const CPubKey& spend_pubkey)
{
    // Use CKeyID as the map key (V2)
    CKeyID keyID = scan_secret.GetPubKey().GetID();

    // Check if address exists
    if (!mapWatchOnlyAddresses.count(keyID)) {
        return error("%s: Watch-only address not found (keyID: %s)", __func__, keyID.ToString());
    }

    // Verify scan key matches
    auto& info = mapWatchOnlyAddresses.at(keyID);
    if (!(info.scan_secret == scan_secret)) {
        return error("%s: Scan secret mismatch", __func__);
    }

    // Verify spend pubkey matches
    if (info.spend_pubkey != spend_pubkey) {
        return error("%s: Spend public key mismatch", __func__);
    }

    {
        LOCK(cs_watchonly);
        // Remove from memory
        mapWatchOnlyAddresses.erase(keyID);
    }

    // Note: We don't remove transactions from database immediately
    // This prevents accidental data loss and allows users to re-import if needed
    // Transaction data can be cleaned up with a separate maintenance tool/RPC if desired

    LogPrintf("Removed watch-only address: %s (keyID: %s)\n", address, keyID.ToString());
    return true;
}

bool LoadWatchOnlyAddresses()
{
    LOCK(cs_watchonly);
    return pwatchonlyDB->LoadWatchOnlyAddresses();
}

bool FlushWatchOnlyAddresses()
{
    {
        LOCK(cs_watchonly);
        for (auto &pair : mapWatchOnlyAddresses) {
            if (pair.second.fDirty) {
                // Use V2 write method with CKeyID
                if (!pwatchonlyDB->WriteWatchOnlyAddressV2(pair.first, pair.second)) {
                    return error("Failed to flush watchonly addresses on keyID - %s", pair.first.ToString());
                }
                pair.second.fDirty = false;
            }
        }
    }
    return true;
}

void PrintWatchOnlyAddressInfo() {
    for (const auto& info: mapWatchOnlyAddresses) {
        LogPrintf("keyID: %s\n", info.first.ToString());
        LogPrintf("scan_secret: %s\n", HexStr(info.second.scan_secret.begin(),  info.second.scan_secret.end()));
        LogPrintf("spend_pubkey: %s\n\n", HexStr(info.second.spend_pubkey.begin(),  info.second.spend_pubkey.end()));
    }
}

bool GetWatchOnlyAddressTransactions(const CBitcoinAddress& address, std::vector<uint256>& txhashses)
{
    return true;
}

bool AddWatchOnlyTransaction(const CKey& key, const CWatchOnlyTx& watchonlytx)
{
    // Check if caching is enabled
    bool fUseCache = gArgs.GetBoolArg("-watchonlycache", false);

    if (fUseCache) {
        // Use cached implementation for better performance
        watchonlyTxCache.AddTx(key, watchonlytx);

        // Flush ALL addresses if total cache threshold reached
        if (watchonlyTxCache.ShouldFlush(key)) {
            LogPrint(BCLog::WATCHONLYDB, "%s: Cache threshold reached (%d total txs), flushing all addresses\n",
                     __func__, watchonlyTxCache.GetTotalSize());
            return watchonlyTxCache.FlushAll();
        }

        return true;
    } else {
        // Use original implementation (backward compatible)
        LOCK(cs_watchonly);
        int current_count = 0;
        if (GetWatchOnlyKeyCount(key, current_count)) {
            LogPrintf("%s: adding watchonly transaction to current count %d\n", __func__, current_count);
            // Key count exists
            return pwatchonlyDB->WriteWatchOnlyTx(key, current_count, watchonlytx);
        } else {
            // Key count didn't exist.. do the same thing?
            LogPrintf("%s: adding watchonly transaction to fresh count %d\n", __func__, current_count);
            return pwatchonlyDB->WriteWatchOnlyTx(key, -1, watchonlytx);
        }
    }
}


bool ReadWatchOnlyTransaction(const CKey& key, const int& count, CWatchOnlyTx& watchonlytx)
{
    LogPrintf("%s: reading watchonly transaction from database\n", __func__);
    return pwatchonlyDB->ReadWatchOnlyTx(key, count, watchonlytx);
}

bool WriteWatchOnlyCheckpoint(const CKey& scan_secret, const std::vector<CWatchOnlyTx>& vTxes, int64_t nHeight, const uint256& blockHash)
{
    // Checkpoints are always enabled for data safety
    LOCK(cs_watchonly);

    // Get current count
    int current_count = 0;
    GetWatchOnlyKeyCount(scan_secret, current_count);

    int starting_count = current_count;

    LogPrint(BCLog::WATCHONLYDB, "%s: Writing %d transactions atomically at height %d\n",
             __func__, vTxes.size(), nHeight);

    // Write all transactions
    for (const auto& tx : vTxes) {
        if (!pwatchonlyDB->WriteWatchOnlyTx(scan_secret, current_count, tx)) {
            return error("%s: Failed to write transaction %s", __func__, tx.tx_hash.GetHex());
        }
        current_count++;
    }

    // Create and write checkpoint atomically
    CWatchOnlyScanCheckpoint checkpoint;
    checkpoint.scan_secret = scan_secret;
    checkpoint.nBlockHeight = nHeight;
    checkpoint.nTxCount = current_count;
    checkpoint.blockHash = blockHash;
    checkpoint.nTimestamp = GetTime();

    if (!pwatchonlyDB->WriteCheckpoint(scan_secret, checkpoint)) {
        return error("%s: Failed to write checkpoint at height %d", __func__, nHeight);
    }

    LogPrint(BCLog::WATCHONLYDB, "%s: Successfully wrote checkpoint at height %d with %d total txs\n",
             __func__, nHeight, current_count);

    return true;
}

bool IncrementWatchOnlyKeyCount(const CKey& key)
{
    LogPrintf("%s: adding transaction count database\n", __func__);
    int current_count = 0;
    if (GetWatchOnlyKeyCount(key, current_count)) {
        return pwatchonlyDB->WriteKeyCount(key, current_count++);
    }
    return false;
}

bool GetWatchOnlyKeyCount(const CKey& key, int& current_count)
{
    LogPrintf("%s: reading key count from database\n", __func__);
    return pwatchonlyDB->ReadKeyCount(key, current_count);
}

void FetchWatchOnlyTransactions(const CKey& scan_secret, std::vector<std::pair<int, CWatchOnlyTx>>& vTxes, int nStartFromIndex, int nStopIndex)
{
    int nNumberofWatchonlyTxes = 0;
    GetWatchOnlyKeyCount(scan_secret, nNumberofWatchonlyTxes);

    int nStartingIndex = 0;
    if (nStartFromIndex > 0 && nStartFromIndex < nNumberofWatchonlyTxes)
        nStartFromIndex = nStartFromIndex;

    int nStoppingPoint = nNumberofWatchonlyTxes;
    if (nStopIndex > 0 && nStopIndex < nStoppingPoint && nStopIndex >= nStartFromIndex)
        nStoppingPoint = nStopIndex;


    for (int i = nStartFromIndex; i <= nStoppingPoint; i++) {
        CWatchOnlyTx watchonlytx;
        if (ReadWatchOnlyTransaction(scan_secret, i, watchonlytx)) {
            if (watchonlytx.type == CWatchOnlyTx::ANON) {
                pblocktree->ReadRCTOutputLink(watchonlytx.ringctout.pk, watchonlytx.ringctIndex);
            }
            vTxes.emplace_back(std::make_pair(i, watchonlytx));
        }
    }
}

bool GetSecretFromString(const std::string& strSecret, CKey& secret)
{
    // Check data objects
    std::string sSecret = strSecret;
    std::vector<uint8_t> vchSecret;
    CBitcoinSecret wifSecret;

    // Decode string to correct data object
    if (IsHex(sSecret)) {
        vchSecret = ParseHex(sSecret);
    } else {
        if (wifSecret.SetString(sSecret)) {
            secret = wifSecret.GetKey();
        } else {
            if (!DecodeBase58(sSecret, vchSecret, MAX_STEALTH_RAW_SIZE)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not decode secret as WIF, hex or base58.");
            }
        }
    }

    if (vchSecret.size() > 0) {
        if (vchSecret.size() != 32) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Secret is not 32 bytes.");
        }
        secret.Set(&vchSecret[0], true);
    }

    return true;
}

bool GetAmountFromWatchonly(const CWatchOnlyTx& watchonlytx, const CKey& scan_secret, const CKey& spend_secret, const CPubKey& spend_pubkey, CAmount& nAmount, uint256& blind, CCmpPubKey& keyImage)
{
    if (watchonlytx.type == CWatchOnlyTx::ANON) {
        auto txout = watchonlytx.ringctout;

        CKeyID idk = txout.pk.GetID();
        std::vector<uint8_t> vchEphemPK;
        vchEphemPK.resize(33);
        memcpy(&vchEphemPK[0], &watchonlytx.ringctout.vData[0], 33);

        CKey sShared;
        ec_point pkExtracted;
        ec_point ecPubKey;
        SetPublicKey(spend_pubkey, ecPubKey);

        if (StealthSecret(scan_secret, vchEphemPK, ecPubKey, sShared, pkExtracted) != 0)
            return error("%s: failed to generate stealth secret", __func__);

        CKey keyDestination;
        if (StealthSharedToSecretSpend(sShared, spend_secret, keyDestination) != 0)
            return error("%s: StealthSharedToSecretSpend() failed.\n", __func__);

        if (keyDestination.GetPubKey().GetID() != idk)
            return error("%s: failed to generate correct shared secret", __func__);

        // Regenerate nonce
        CPubKey pkEphem;
        pkEphem.Set(vchEphemPK.begin(), vchEphemPK.begin() + 33);
        uint256 nonce = keyDestination.ECDH(pkEphem);
        CSHA256().Write(nonce.begin(), 32).Finalize(nonce.begin());

        uint64_t min_value, max_value;
        uint8_t blindOut[32];
        unsigned char msg[256]; // Currently narration is capped at 32 bytes
        size_t mlen = sizeof(msg);
        memset(msg, 0, mlen);
        uint64_t amountOut;
        if (1 != secp256k1_rangeproof_rewind(secp256k1_ctx_blind,
                                             blindOut, &amountOut, msg, &mlen, nonce.begin(),
                                             &min_value, &max_value,
                                             &watchonlytx.ringctout.commitment,
                                             watchonlytx.ringctout.vRangeproof.data(),
                                             watchonlytx.ringctout.vRangeproof.size(),
                                             nullptr, 0,
                                             secp256k1_generator_h)) {
            return error("%s: failed to get the amount", __func__);
        }

        blind = uint256();
        memcpy(blind.begin(), blindOut, 32);
        nAmount = amountOut;

        // Keyimage is required for the tx hash
        CCmpPubKey ki;
        if (secp256k1_get_keyimage(secp256k1_ctx_blind, ki.ncbegin(), watchonlytx.ringctout.pk.begin(),
                                   keyDestination.begin()) == 0) {
            keyImage = ki;
            return true;
        }

        return error("%s: failed to get keyimage", __func__);

    } else if (watchonlytx.type == CWatchOnlyTx::STEALTH) {

        auto txout = watchonlytx.ctout;
        CKeyID id;
        if (!KeyIdFromScriptPubKey(txout.scriptPubKey, id))
            return error(" Stealth - Failed to get ID Key from Script.");

        if (txout.vData.size() < 33)
            return error("%s: Stealth - vData.size() < 33.", __func__);

        std::vector<uint8_t> vchEphemPK;
        vchEphemPK.resize(33);
        memcpy(&vchEphemPK[0], &txout.vData[0], 33);

        CKey sShared;
        ec_point pkExtracted;
        ec_point ecPubKey;
        SetPublicKey(spend_pubkey, ecPubKey);

        if (StealthSecret(scan_secret, vchEphemPK, ecPubKey, sShared, pkExtracted) != 0)
            return error("%s: Stealth - failed to generate stealth secret", __func__);

        CKey keyDestination;
        if (StealthSharedToSecretSpend(sShared, spend_secret, keyDestination) != 0)
            return error("%s: Stealth - StealthSharedToSecretSpend() failed.\n", __func__);

        if (keyDestination.GetPubKey().GetID() != id)
            return error("%s: Stealth - failed to generate correct shared secret", __func__);

        // Regenerate nonce
        CPubKey pkEphem;
        pkEphem.Set(vchEphemPK.begin(), vchEphemPK.begin() + 33);
        uint256 nonce = keyDestination.ECDH(pkEphem);
        CSHA256().Write(nonce.begin(), 32).Finalize(nonce.begin());

        uint64_t min_value, max_value;
        uint8_t blindOut[32];
        unsigned char msg[256]; // Currently narration is capped at 32 bytes
        size_t mlen = sizeof(msg);
        memset(msg, 0, mlen);
        uint64_t amountOut;
        if (1 != secp256k1_rangeproof_rewind(secp256k1_ctx_blind,
                                             blindOut, &amountOut, msg, &mlen, nonce.begin(),
                                             &min_value, &max_value,
                                             &txout.commitment, txout.vRangeproof.data(), txout.vRangeproof.size(),
                                             nullptr, 0,
                                             secp256k1_generator_h)) {
            return error("%s: secp256k1_rangeproof_rewind failed.", __func__);
        }

        blind = uint256();
        memcpy(blind.begin(), blindOut, 32);
        nAmount = amountOut;

        return true;
    }

    return error("%s: Failed because type wasn't ANON or STEALTH", __func__);
}

bool GetPubkeyFromString(const std::string& strPubkey, CPubKey& pubkey)
{
    std::string sPublic = strPubkey;
    std::vector<uint8_t> vchPublic;
    if (IsHex(sPublic)) {
        vchPublic = ParseHex(sPublic);
    }

    if (vchPublic.size() != 33) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Publickey is not 33 bytes.");
    }

    pubkey = CPubKey(vchPublic.begin(), vchPublic.end());
    return true;
}


