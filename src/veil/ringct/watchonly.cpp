// Copyright (c) 2021 The Flux Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "veil/ringct/watchonly.h"
#include "veil/ringct/watchonlydb.h"

#include <util.h>
#include <key_io.h>
#include <core_io.h>
#include <univalue.h>

#include <veil/ringct/blind.h>
#include <secp256k1_mlsag.h>
#include <rpc/protocol.h>
#include <validation.h>

/** Map of watchonly keys */
std::map<std::string, CWatchOnlyAddress> mapWatchOnlyAddresses;


CWatchOnlyAddress::CWatchOnlyAddress(const CKey& scan, const CPubKey& spend) {
    scan_secret = scan;
    spend_pubkey = spend;
}


CWatchOnlyTx::CWatchOnlyTx(const CKey& key, const uint256& txhash) {
    scan_secret = key;
    tx_hash = txhash;
}

CWatchOnlyTx::CWatchOnlyTx() {
    scan_secret.Clear();
    tx_hash.SetNull();
}

UniValue CWatchOnlyTx::GetUniValue(bool spent, uint256 txhash, bool fSkip, CAmount amount)
{
    UniValue out(UniValue::VOBJ);

    if (!fSkip) {
        out.pushKV("amount", ValueFromAmount(amount));
        out.pushKV("spent", spent);
        if (spent) {
            out.pushKV("spent_in", txhash.GetHex());
        }
    }

    RingCTOutputToJSON(this->tx_hash, this->tx_index, this->ringctIndex, this->ringctout, out);
    return out;
}

bool AddWatchOnlyAddress(const CBitcoinAddress& address, const CKey& scan_secret, const CPubKey& spend_pubkey)
{
    if (mapWatchOnlyAddresses.count(address.ToString())) {
        auto watchOnlyInfo = mapWatchOnlyAddresses.at(address.ToString());
        if (scan_secret == watchOnlyInfo.scan_secret && spend_pubkey == watchOnlyInfo.spend_pubkey) {
            LogPrint(BCLog::WATCHONLYDB, "Trying to add a watchonly address that already exists: %s\n", address.ToString());
            return true;
        } else {
            LogPrint(BCLog::WATCHONLYDB, "Trying to add a watchonly address with different scan key and pubkey then what already exists: %s\n", address.ToString());
            return false;
        }
    } else {
        if (!pwatchonlyDB->WriteAddressKey(address, scan_secret, spend_pubkey)) {
            LogPrint(BCLog::WATCHONLYDB, "Failed to WriteAddressKey: %s\n", address.ToString());
            return false;
        }

        CWatchOnlyAddress watchonly(scan_secret,spend_pubkey);
        mapWatchOnlyAddresses.insert(std::make_pair(address.ToString(), watchonly));
    }
    return true;
}

bool RemoveWatchOnlyAddress(const CBitcoinAddress& address, const CKey& scan_secret, const CPubKey& spend_pubkey)
{
    return true;
}

bool LoadWatchOnlyAddresses()
{
    return pwatchonlyDB->LoadWatchOnlyAddresses();
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
            pblocktree->ReadRCTOutputLink(watchonlytx.ringctout.pk, watchonlytx.ringctIndex);
            vTxes.emplace_back(make_pair(i, watchonlytx));
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
                                         &watchonlytx.ringctout.commitment, watchonlytx.ringctout.vRangeproof.data(), watchonlytx.ringctout.vRangeproof.size(),
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


