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

#include <boost/thread.hpp>

/** Map of watchonly keys */
std::map<std::string, CWatchOnlyAddress> mapWatchOnlyAddresses;


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

            // Write new txes to database
            for (int i = 0; i < nNumberOfBlockPerScan; i++) {
                boost::this_thread::interruption_point();
                int nIndexHeight = nStartBlockHeight + i;
                if (vecNewRingCTTransactions[i].size()) {
                    pwatchonlyDB->WriteBlockTransactions(nIndexHeight, vecNewRingCTTransactions[i]);
                    vecRingCTTranasctionToScan[i] = vecNewRingCTTransactions[i];
                }
            }

            for (int i = 0; i < nNumberOfBlockPerScan; i++) {
                boost::this_thread::interruption_point();
                // Get a list of CWatchOnlyAddress to scan with
                std::vector<std::pair<std::string,CWatchOnlyAddress>> scanThese;
                for (const auto &addr : mapWatchOnlyAddresses) {
                    if (addr.second.nCurrentScannedHeight != addr.second.nImportedHeight) {
                        if (nStartBlockHeight + i > addr.second.nCurrentScannedHeight &&
                            nStartBlockHeight + i <= addr.second.nImportedHeight) {
                            scanThese.push_back(addr);
                        }
                    }
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
                    for (const auto addr : scanThese) {
                        ec_point pkExtracted;
                        ec_point ecPubKey;
                        SetPublicKey(addr.second.spend_pubkey, ecPubKey);
                        if (StealthSecret(addr.second.scan_secret, vchEphemPK, ecPubKey, sShared, pkExtracted) !=
                            0) {
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
                        watchonlyTx.scan_secret = addr.second.scan_secret;
                        watchonlyTx.tx_hash = watchonlyout.tx_hash;
                        watchonlyTx.tx_index = watchonlyout.nIndex;
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
                    }
                }

                {
                    for (const auto &addr : scanThese) {
                        if (mapWatchOnlyAddresses.count(addr.first)) {
                            LOCK(cs_watchonly);
                            mapWatchOnlyAddresses.at(addr.first).fDirty = true;
                            mapWatchOnlyAddresses.at(addr.first).nCurrentScannedHeight = nStartBlockHeight + i;
                        }
                    }
                }
            }
        }
    } catch (std::exception& e) {
        LogPrintf("ScanWatchOnlyAddresses() exception\n");
    } catch (boost::thread_interrupted) {
        LogPrintf("ScanWatchOnlyAddresses() interrupted\n");
    }

    fScanning = false;
    LogPrintf("ScanWatchOnlyAddresses stopping\n");
}


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
}

CWatchOnlyTx::CWatchOnlyTx() {
    type = NOTSET;
    scan_secret.Clear();
    tx_hash.SetNull();
}

UniValue CWatchOnlyTx::GetUniValue(int& index, bool spent, std::string keyimage, uint256 txhash, bool fSkip, CAmount amount)
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
    if (mapWatchOnlyAddresses.count(address)) {
        auto watchOnlyInfo = mapWatchOnlyAddresses.at(address);
        if (scan_secret == watchOnlyInfo.scan_secret && spend_pubkey == watchOnlyInfo.spend_pubkey) {
            LogPrint(BCLog::WATCHONLYDB, "Trying to add a watchonly address that already exists: %s\n", address);
            return true;
        } else {
            LogPrint(BCLog::WATCHONLYDB, "Trying to add a watchonly address with different scan key and pubkey then what already exists: %s\n", address);
            return false;
        }
    } else {
        CWatchOnlyAddress newAddress(scan_secret, spend_pubkey, nStart, nImported, nStart);

        if (!pwatchonlyDB->WriteWatchOnlyAddress(address, newAddress)) {
            LogPrint(BCLog::WATCHONLYDB, "Failed to WriteWatchOnlyAddress: %s\n", address);
            return false;
        }
        LOCK(cs_watchonly);
        mapWatchOnlyAddresses.insert(std::make_pair(address, newAddress));
    }

    StartWatchonlyScanningIfNotStarted();
    return true;
}

bool RemoveWatchOnlyAddress(const std::string& address, const CKey& scan_secret, const CPubKey& spend_pubkey)
{
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
                if (!pwatchonlyDB->WriteWatchOnlyAddress(pair.first, pair.second)) {
                    return error("Failed to flush watchonly addresses on - %s", pair.first);
                }
                pair.second.fDirty = false;
            }
        }
    }
    return true;
}

void PrintWatchOnlyAddressInfo() {
    for (const auto& info: mapWatchOnlyAddresses) {
        LogPrintf("address: %s\n", info.first);
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


bool ReadWatchOnlyTransaction(const CKey& key, const int& count, CWatchOnlyTx& watchonlytx)
{
    LogPrintf("%s: reading watchonly transaction from database\n", __func__);
    return pwatchonlyDB->ReadWatchOnlyTx(key, count, watchonlytx);
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


