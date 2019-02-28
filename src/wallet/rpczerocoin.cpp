// Copyright (c) 2015-2019 The PIVX developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/rpcwallet.h>
#include <core_io.h>
#include <key_io.h>
#include <wallet/deterministicmint.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <utilmoneystr.h>
#include <validation.h>
#include <veil/zerocoin/accumulators.h>
#include <veil/zerocoin/zwallet.h>
#include <veil/zerocoin/zchain.h>
#include <veil/ringct/transactionrecord.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#include <veil/zerocoin/denomination_functions.h>
#include <veil/zerocoin/mintmeta.h>
#include <txmempool.h>


#include <boost/assign.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/thread.hpp>

#include <memory>

UniValue MintMetaToUniValue(const CMintMeta& mint)
{
    std::map<libzerocoin::CoinDenomination, int> mapMaturity = GetMintMaturityHeight();
    int nMemFlags = CzTracker::GetMintMemFlags(mint, chainActive.Height(), mapMaturity);

    UniValue m(UniValue::VOBJ);
    m.push_back(Pair("txid", mint.txid.GetHex()));
    m.push_back(Pair("height", (double)mint.nHeight));
    m.push_back(Pair("value", ValueFromAmount(libzerocoin::ZerocoinDenominationToAmount(mint.denom))));
    m.push_back(Pair("pubcoinhash", mint.hashPubcoin.GetHex()));
    m.push_back(Pair("serialhash", mint.hashSerial.GetHex()));
    m.pushKV("is_spent", mint.isUsed);
    m.pushKV("is_archived", mint.isArchived);
    m.pushKV("flags", nMemFlags);
    return m;
}

UniValue lookupzerocoin(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    UniValue params = request.params;

    if (request.fHelp || params.size() < 1 || params.size() > 3)
        throw std::runtime_error(
                "lookupzerocoin identifier_type identifier\n"
                "\nLookup a zerocoin held by the wallet\n"
                "\nArguments:\n"
                "1. id_type   (string, required) <serial, serialhash, pubcoin, pubcoinhash>\n"
                "2. id        (string, required) The id associated with id_type"
                "setting to true will result in loss of privacy and can reveal information about your wallet to the public!!\n"

                "\nResult:\n"
                "[\n"
                "  {\n"
                "    \"txid\": \"xxx\",         (string) Transaction ID.\n"
                "    \"height\": \"xxx\",       (numeric) Height mint added to chain.\n"
                "    \"value\": amount,       (numeric) Denomination amount.\n"
                "    \"pubcoinhash\": \"xxx\",  (string) Hash of pubcoin in hex format.\n"
                "    \"serialhash\": \"xxx\",   (string) Hash of serial in hex format.\n"
                "    \"is_spent\": \"xxx\",     (bool) Whether the coin has been spent.\n"
                "    \"is_archived\": nnn       (bool) Whether the coin has been archived by the wallet (considered an orphan or invalid).\n"
                "  }\n"
                "  ,...\n"
                "]\n" +
                HelpExampleCli("lookupzerocoin", "pubcoinhash f468259db68b913dadb8ed2b07631e56670dc3e1ab0ee73a71b0db6d19fc73a9") +
                "\nAs a json rpc call\n" +
                HelpExampleRpc("lookupzerocoin", "pubcoinhash f468259db68b913dadb8ed2b07631e56670dc3e1ab0ee73a71b0db6d19fc73a9"));

    LOCK2(cs_main, pwallet->cs_wallet);

    auto ztracker = pwallet->GetZTrackerPointer();

    std::string strType = request.params[0].get_str();
    std::string strID = request.params[1].get_str();

    uint256 hashSerial = uint256();
    CMintMeta mint;
    if (strType == "serial") {
        CBigNum bnSerial;
        bnSerial.SetHex(strID);
        hashSerial = GetSerialHash(bnSerial);
    } else if (strType == "serialhash") {
        hashSerial = uint256S(strID);
    }

    uint256 hashPubcoin = uint256();
    if (hashSerial != uint256()) {
        mint = ztracker->Get(hashSerial);
    } else if (strType == "pubcoin") {
        CBigNum bnPubcoin;
        bnPubcoin.SetHex(strID);
        hashPubcoin = GetPubCoinHash(bnPubcoin);
    } else if (strType == "pubcoinhash") {
        hashPubcoin = uint256S(strID);
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "param 1 id_type is invalid");
    }

    if (hashPubcoin != uint256())
        mint = ztracker->GetMetaFromPubcoin(hashPubcoin);

    // Search archived coins if not found yet
    if (mint.hashSerial == uint256()) {
        //Check if it is archived
        std::list<CDeterministicMint> listDMints = WalletBatch(pwallet->GetDBHandle()).ListArchivedDeterministicMints();
        bool found = false;
        for (const auto& dmint : listDMints) {
            if (dmint.GetSerialHash() == hashSerial || dmint.GetPubcoinHash() == hashPubcoin) {
                mint = dmint.ToMintMeta();
                mint.isArchived = true;
                found = true;
                break;
            }
        }
        if (!found)
            throw JSONRPCError(RPC_WALLET_NOT_FOUND, "could not find zerocoin in wallet");
    }

    return MintMetaToUniValue(mint);
}

UniValue mintzerocoin(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    UniValue params = request.params;

    if (request.fHelp || params.size() < 1 || params.size() > 3)
        throw std::runtime_error(
                "mintzerocoin amount ( utxos )\n"
                "\nMint the specified zerocoin amount\n"
                "\nArguments:\n"
                "1. amount      (numeric, required) Enter an amount of veil to convert to zerocoin\n"
                "2. allowbasecoin (bool, optional) Whether to allow transparent, non-private, coins to be included (WARNING: "
                "setting to true will result in loss of privacy and can reveal information about your wallet to the public!!\n"
                "3. utxos       (string, optional) A json array of objects.\n"
                "                   Each object needs the txid (string) and vout (numeric)\n"
                "  [\n"
                "    {\n"
                "      \"txid\":\"txid\",    (string) The transaction id\n"
                "      \"vout\": n         (numeric) The output number\n"
                "    }\n"
                "    ,...\n"
                "  ]\n"

                "\nResult:\n"
                "[\n"
                "  {\n"
                "    \"txid\": \"xxx\",         (string) Transaction ID.\n"
                "    \"value\": amount,       (numeric) Minted amount.\n"
                "    \"pubcoin\": \"xxx\",      (string) Pubcoin in hex format.\n"
                "    \"randomness\": \"xxx\",   (string) Hex encoded randomness.\n"
                "    \"serial\": \"xxx\",       (string) Serial in hex format.\n"
                "    \"time\": nnn            (numeric) Time to mint this transaction.\n"
                "  }\n"
                "  ,...\n"
                "]\n" +
                HelpExampleCli("mintzerocoin", "false, 50") +
                "\nMint 13 from a specific output\n" +
                HelpExampleCli("mintzerocoin", "13 \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
                "\nAs a json rpc call\n" +
                HelpExampleRpc("mintzerocoin", "13, \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\""));

    LOCK2(cs_main, pwallet->cs_wallet);
    TRY_LOCK(mempool.cs, fMempoolLocked);
    if (!fMempoolLocked)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "failed to lock mempool");

//    if (params.size() == 1) {
//        RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));
//    } else {
//        RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM)(UniValue::VARR));
//    }

    int64_t nTime = GetTimeMillis();

    EnsureWalletIsUnlocked(pwallet);

    CAmount nAmount = params[0].get_int() * COIN;

    CWalletTx wtx(pwallet, nullptr);
    std::vector<CDeterministicMint> vDMints;
    std::string strError;
    std::vector<COutPoint> vOutpts;

    bool fUseBasecoin = false;
    if (params.size() > 1)
        fUseBasecoin = params[1].get_bool();

    BalanceList balances;
    pwallet->GetBalances(balances);

    OutputTypes inputtype = OUTPUT_NULL;
    if (fUseBasecoin) {
        if (balances.nVeil > nAmount)
            inputtype = OUTPUT_STANDARD;
    } else if (balances.nRingCT > nAmount && chainActive.Tip()->nAnonOutputs > 20) {
        inputtype = OUTPUT_RINGCT;
    } else if (balances.nCT > nAmount) {
        inputtype = OUTPUT_CT;
    }

    if (inputtype == OUTPUT_NULL)
        throw JSONRPCError(RPC_WALLET_ERROR, "Insufficient Balance");

    if (params.size() > 2) {
        UniValue outputs = params[2].get_array();
        for (unsigned int idx = 0; idx < outputs.size(); idx++) {
            const UniValue& output = outputs[idx];
            if (!output.isObject())
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");
            const UniValue& o = output.get_obj();

            RPCTypeCheckObj(o, boost::assign::map_list_of("txid", UniValue::VSTR)("vout", UniValue::VNUM));

            std::string txid = find_value(o, "txid").get_str();
            if (!IsHex(txid))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");

            int nOutput = find_value(o, "vout").get_int();
            if (nOutput < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

            COutPoint outpt(uint256S(txid), nOutput);
            vOutpts.push_back(outpt);
        }
        strError = pwallet->MintZerocoinFromOutPoint(nAmount, wtx, vDMints, vOutpts);
    } else {
        strError = pwallet->MintZerocoin(nAmount, wtx, vDMints, inputtype, nullptr);
    }

    if (strError != "")
        throw JSONRPCError(RPC_WALLET_ERROR, strError);

    UniValue arrMints(UniValue::VARR);
    for (CDeterministicMint dMint : vDMints) {
        UniValue m(UniValue::VOBJ);
        m.push_back(Pair("txid", wtx.tx->GetHash().GetHex()));
        m.push_back(Pair("value", ValueFromAmount(libzerocoin::ZerocoinDenominationToAmount(dMint.GetDenomination()))));
        m.push_back(Pair("pubcoinhash", dMint.GetPubcoinHash().GetHex()));
        m.push_back(Pair("serialhash", dMint.GetSerialHash().GetHex()));
        m.push_back(Pair("seedhash", dMint.GetSeedHash().GetHex()));
        m.push_back(Pair("count", (int64_t)dMint.GetCount()));
        m.push_back(Pair("time", GetTimeMillis() - nTime));
        arrMints.push_back(m);
    }

    return arrMints;
}

UniValue spendzerocoin(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    UniValue params = request.params;

    if (request.fHelp || params.size() > 6 || params.size() < 4)
        throw std::runtime_error(
                "spendzerocoin amount mintchange minimizechange securitylevel d( \"address\" denomination)\n"
                "\nSpend zerocoin to a veil address.\n"
                "\nArguments:\n"
                "1. amount          (numeric, required) Amount to spend.\n"
                "2. mintchange      (boolean, required) Re-mint any leftover change.\n"
                "3. minimizechange  (boolean, required) Try to minimize the returning change  [false]\n"
                "4. securitylevel   (numeric, required) Amount of checkpoints to add to the accumulator.\n"
                "                       A checkpoint contains 10 blocks worth of zerocoinmints.\n"
                "                       The more checkpoints that are added, the more untraceable the transaction.\n"
                "                       Use [100] to add the maximum amount of checkpoints available.\n"
                "                       Adding more checkpoints makes the minting process take longer\n"
                "5. \"address\"     (string, optional, default=change) Send to specified address or to a new change address.\n"
                "                       If there is change then an address is required\n"
                "6. denomination    (numeric, optional) Only select from a specific zerocoin denomination\n"

                "\nResult:\n"
                "{\n"
                "  \"txid\": \"xxx\",             (string) Transaction hash.\n"
                "  \"bytes\": nnn,              (numeric) Transaction size.\n"
                "  \"fee\": amount,             (numeric) Transaction fee (if any).\n"
                "  \"spends\": [                (array) JSON array of input objects.\n"
                "    {\n"
                "      \"denomination\": nnn,   (numeric) Denomination value.\n"
                "      \"pubcoin\": \"xxx\",      (string) Pubcoin in hex format.\n"
                "      \"serial\": \"xxx\",       (string) Serial number in hex format.\n"
                "      \"acc_checksum\": \"xxx\", (string) Accumulator checksum in hex format.\n"
                "    }\n"
                "    ,...\n"
                "  ],\n"
                "  \"outputs\": [                 (array) JSON array of output objects.\n"
                "    {\n"
                "      \"value\": amount,         (numeric) Value in veil.\n"
                "      \"address\": \"xxx\"         (string) veil address or \"zerocoinmint\" for reminted change.\n"
                "    }\n"
                "    ,...\n"
                "  ]\n"
                "}\n"

                "\nExamples\n" +
                HelpExampleCli("spendzerocoin", "5000 false true 100 \"VSJRSsuU9zfyrvxVaAEFQqK4MxZg6vgeS6\"") +
                HelpExampleRpc("spendzerocoin", "5000 false true 100 \"VSJRSsuU9zfyrvxVaAEFQqK4MxZg6vgeS6\""));

    LOCK2(cs_main, pwallet->cs_wallet);

    int64_t nTimeStart = GetTimeMillis();
    CAmount nAmount = AmountFromValue(params[0]);   // Spending amount
    bool fMintChange = params[1].get_bool();        // Mint change to zerocoin
    bool fMinimizeChange = params[2].get_bool();    // Minimize change
    int nSecurityLevel = params[3].get_int();       // Security level

    if (nSecurityLevel < 1 || nSecurityLevel > 100) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid security level value (%d). Must be in [1, 100] range.", nSecurityLevel));
    }

    CTxDestination dest; // Optional sending address. Dummy initialization here.
    if (params.size() > 4) {
        // Destination address was supplied as request[4]. Optional parameters MUST be at the end
        // to avoid type confusion from the JSON interpreter
        dest = DecodeDestination(params[4].get_str());
    }

    libzerocoin::CoinDenomination denomFilter = libzerocoin::CoinDenomination::ZQ_ERROR;
    if (params.size() > 5) {
        denomFilter = libzerocoin::IntToZerocoinDenomination(params[5].get_int());
        if (denomFilter == libzerocoin::CoinDenomination::ZQ_ERROR)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not parse denomination");
    }

    std::vector<CZerocoinMint> vMintsSelected;
    CZerocoinSpendReceipt receipt;
    bool fSuccess;

    if(params.size() > 4) // Spend to supplied destination address
        fSuccess = pwallet->SpendZerocoin(nAmount, nSecurityLevel, receipt, vMintsSelected, fMintChange, fMinimizeChange, denomFilter, &dest);
    else                   // Spend to newly generated local address
        fSuccess = pwallet->SpendZerocoin(nAmount, nSecurityLevel, receipt, vMintsSelected, fMintChange, fMinimizeChange, denomFilter);

    if (!fSuccess)
        throw JSONRPCError(RPC_WALLET_ERROR, receipt.GetStatusMessage());

    CAmount nValueIn = 0;
    CAmount nValueOut = 0;
    UniValue arrtx(UniValue::VARR);
    UniValue vout(UniValue::VARR);
    std::vector<CTransactionRef> vtx = receipt.GetTransactions();
    for (unsigned int j = 0; j < vtx.size(); j++) {
        const CTransactionRef& tx = vtx[j];
        CTransactionRecord rtx = receipt.GetTransactionRecord(j);
        for (unsigned int i = 0; i < tx->vpout.size(); i++) {
            const auto &pout = tx->vpout[i];
            UniValue out(UniValue::VOBJ);

            CTxDestination dest;
            CAmount nValueOutput = 0;
            if (pout->IsZerocoinMint()) {
                out.pushKV("type", "zerocoinmint");
                out.pushKV("address", "zerocoinmint");
                nValueOutput = pout->GetValue();
                out.pushKV("value", ValueFromAmount(nValueOutput));
            } else if (pout->IsStandardOutput() && ExtractDestination(*pout->GetPScriptPubKey(), dest)) {
                out.pushKV("type", "basecoin");
                out.pushKV("address", EncodeDestination(dest));
                nValueOutput = pout->GetValue();
                out.pushKV("value", ValueFromAmount(nValueOutput));
            } else {
                if (pout->nVersion == OUTPUT_RINGCT) {
                    COutputRecord *record = rtx.GetOutput(i);
                    auto ismine = pwallet->IsMine(pout.get());
                    out.pushKV("type", "ringct");
                    CKeyID idStealth;
                    if (record->GetStealthID(idStealth))
                        out.pushKV("stealthID", idStealth.GetHex());
                    out.pushKV("is_mine", bool(ismine));
                    if (ismine)
                        out.pushKV("is_change", record->IsChange());
                    nValueOutput = record->GetRawValue();
                    out.pushKV("value", ValueFromAmount(nValueOutput));
                } else if (pout->nVersion == OUTPUT_DATA) {
                    CAmount nValue;
                    CTxOutData *s = (CTxOutData *) pout.get();
                    if (s->GetCTFee(nValue))
                        out.pushKV("ct_fee", ValueFromAmount(nValue));
                }
            }
            vout.push_back(out);
            nValueOut += nValueOutput;
        }

        UniValue arrSpends(UniValue::VARR);
        for (const CZerocoinSpend& spend : receipt.GetSpends(j)) {
            UniValue obj(UniValue::VOBJ);
            obj.push_back(Pair("denomination", spend.GetDenomination()));
            obj.push_back(Pair("pubcoin", spend.GetPubCoin().GetHex()));
            obj.push_back(Pair("serial", spend.GetSerial().GetHex()));
            uint256 nChecksum = spend.GetAccumulatorChecksum();
            obj.push_back(Pair("acc_checksum", HexStr(BEGIN(nChecksum), END(nChecksum))));
            arrSpends.push_back(obj);
            nValueIn += libzerocoin::ZerocoinDenominationToAmount(spend.GetDenomination());
        }

        //construct JSON to return
        UniValue tx_obj(UniValue::VOBJ);
        tx_obj.push_back(Pair("txid", tx->GetHash().ToString()));
        tx_obj.push_back(Pair("bytes", (int) ::GetSerializeSize(*tx, SER_NETWORK, CTransaction::CURRENT_VERSION)));
        tx_obj.push_back(Pair("outputs", vout));
        tx_obj.push_back(Pair("spends", arrSpends));
        arrtx.push_back(tx_obj);
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("fee", ValueFromAmount(nValueIn - nValueOut)));
    ret.push_back(Pair("duration_millis", (GetTimeMillis() - nTimeStart)));
    ret.push_back(Pair("transactions", arrtx));

    return ret;
}

UniValue getzerocoinbalance(const JSONRPCRequest& request)
{
    UniValue params = request.params;
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    if (request.fHelp || params.size() != 0)
        throw runtime_error(
                "getzerocoinbalance\n"
                "\nReturn the wallet's total zerocoin balance.\n" +
                HelpRequiringPassphrase(pwallet) + "\n"

                                            "\nResult:\n"
                                            "amount         (numeric) Total zerocoin balance.\n"

                                            "\nExamples:\n" +
                HelpExampleCli("getzerocoinbalance", "") + HelpExampleRpc("getzerocoinbalance", ""));
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("Total", ValueFromAmount(pwallet->GetZerocoinBalance(false))));
    ret.push_back(Pair("Mature", ValueFromAmount(pwallet->GetZerocoinBalance(true))));
    //ret.push_back(Pair("Unconfirmed", ValueFromAmount(pwallet->GetUnconfirmedZerocoinBalance())));
    //ret.push_back(Pair("Immature", ValueFromAmount(pwallet->GetImmatureZerocoinBalance())));

    return ret;
}

UniValue listmintedzerocoins(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;
    UniValue params = request.params;
    if (request.fHelp || params.size() > 2)
        throw runtime_error(
                "listmintedzerocoins (fVerbose) (fMatureOnly)\n"
                "\nList all zerocoin mints in the wallet.\n" +
                HelpRequiringPassphrase(pwallet) + "\n"

                "\nArguments:\n"
                "1. fVerbose      (boolean, optional, default=false) Output mints metadata.\n"
                "2. fMatureOnly      (boolean, optional, default=false) List only mature mints. (Set only if fVerbose is specified)\n"

                "\nResult (with fVerbose=false):\n"
                "[\n"
                "  \"xxx\"      (string) mint serial hash in hex format.\n"
                "  ,...\n"
                "]\n"

                "\nResult (with fVerbose=true):\n"
                "[\n"
                "  {\n"
                "    \"serial hash\": \"xxx\",   (string) Mint serial hash in hex format.\n"
                "    \"version\": n,   (numeric) Zerocoin version number.\n"
                "    \"zerocoin ID\": \"xxx\",   (string) Pubcoin in hex format.\n"
                "    \"denomination\": n,   (numeric) Coin denomination.\n"
                "    \"confirmations\": n   (numeric) Number of confirmations.\n"
                "  }\n"
                "  ,..."
                "]\n"

                "\nExamples:\n" +
                HelpExampleCli("listmintedzerocoins", "") + HelpExampleRpc("listmintedzerocoins", "") +
                HelpExampleCli("listmintedzerocoins", "true") + HelpExampleRpc("listmintedzerocoins", "true") +
                HelpExampleCli("listmintedzerocoins", "true true") + HelpExampleRpc("listmintedzerocoins", "true, true"));

    bool fVerbose = (params.size() > 0) ? params[0].get_bool() : false;
    bool fMatureOnly = (params.size() > 1) ? params[1].get_bool() : false;

    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    auto zwallet = pwallet->GetZWallet();
    zwallet->SyncWithChain(false);

    set<CMintMeta> setMints = pwallet->ListMints(true, fMatureOnly, true);

    // sort the mints by descending confirmations
    std::list<CMintMeta> listMints(setMints.begin(), setMints.end());
    listMints.sort(oldest_first);

    int nBestHeight = chainActive.Height();

    UniValue jsonList(UniValue::VARR);
    if (fVerbose) {
        for (const CMintMeta& m : listMints) {
            // Construct mint object
            UniValue objMint(UniValue::VOBJ);
            objMint.push_back(Pair("serial hash", m.hashSerial.GetHex()));
            objMint.push_back(Pair("version", m.nVersion));
            objMint.push_back(Pair("zerocoin ID", m.hashPubcoin.GetHex()));
            int denom = libzerocoin::ZerocoinDenominationToInt(m.denom);
            objMint.push_back(Pair("denomination", denom));
            int nConfirmations = (m.nHeight && nBestHeight > m.nHeight) ? nBestHeight - m.nHeight : 0;
            objMint.push_back(Pair("confirmations", nConfirmations));
            // Push back mint object
            jsonList.push_back(objMint);
        }
    } else {
        for (const CMintMeta& m : listMints)
            // Push back PubCoin
            jsonList.push_back(m.hashSerial.GetHex());
    }
    return jsonList;
}

UniValue listzerocoinamounts(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;
    UniValue params = request.params;
    if (request.fHelp || params.size() != 0)
        throw runtime_error(
                "listzerocoinamounts\n"
                "\nGet information about your zerocoin amounts.\n" +
                HelpRequiringPassphrase(pwallet) + "\n"

                                            "\nResult:\n"
                                            "[\n"
                                            "  {\n"
                                            "    \"denomination\": n,   (numeric) Denomination Value.\n"
                                            "    \"mints\": n           (numeric) Number of mints.\n"
                                            "  }\n"
                                            "  ,..."
                                            "]\n"

                                            "\nExamples:\n" +
                HelpExampleCli("listzerocoinamounts", "") + HelpExampleRpc("listzerocoinamounts", ""));

    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    std::pair<ZerocoinSpread, ZerocoinSpread> pZerocoinDist = pwallet->GetMyZerocoinDistribution();
    UniValue ret(UniValue::VARR);
    for (const auto& mi : pZerocoinDist.first) {
        UniValue val(UniValue::VOBJ);
        auto denom = mi.first;
        val.push_back(Pair("denomination", libzerocoin::ZerocoinDenominationToInt(denom)));
        val.push_back(Pair("mints_spendable", (int64_t)mi.second));
        val.push_back(Pair("mints_pending", (int64_t)pZerocoinDist.second.at(denom)));
        ret.push_back(val);
    }
    return ret;
}

UniValue listspentzerocoins(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;
    UniValue params = request.params;
    if (request.fHelp || params.size() != 0)
        throw runtime_error(
                "listspentzerocoins\n"
                "\nList all the spent zerocoin mints in the wallet.\n" +
                HelpRequiringPassphrase(pwallet) + "\n"

                                            "\nResult:\n"
                                            "[\n"
                                            "  \"xxx\"      (string) Pubcoin in hex format.\n"
                                            "  ,...\n"
                                            "]\n"

                                            "\nExamples:\n" +
                HelpExampleCli("listspentzerocoins", "") + HelpExampleRpc("listspentzerocoins", ""));
    
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    WalletBatch walletdb(pwallet->GetDBHandle());
    list<CBigNum> listPubCoin = walletdb.ListSpentCoinsSerial();

    UniValue jsonList(UniValue::VARR);
    for (const CBigNum& pubCoinItem : listPubCoin) {
        jsonList.push_back(pubCoinItem.GetHex());
    }

    return jsonList;
}

UniValue DoZerocoinSpend(CWallet* pwallet, const CAmount nAmount, bool fMintChange, bool fMinimizeChange, const int nSecurityLevel, vector<CZerocoinMint>& vMintsSelected, std::string address_str)
{
    int64_t nTimeStart = GetTimeMillis();
    CTxDestination dest; // Optional sending address. Dummy initialization here.
    CZerocoinSpendReceipt receipt;
    bool fSuccess;

    if(address_str != "") { // Spend to supplied destination address
        CBitcoinAddress address(address_str);
        dest = CBitcoinAddress(address_str).Get();
        if(!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid VEIL address");
        fSuccess = pwallet->SpendZerocoin(nAmount, nSecurityLevel, receipt, vMintsSelected, fMintChange, fMinimizeChange, libzerocoin::CoinDenomination::ZQ_ERROR, &dest);
    } else                   // Spend to newly generated local address
        fSuccess = pwallet->SpendZerocoin(nAmount, nSecurityLevel, receipt, vMintsSelected, fMintChange, fMinimizeChange, libzerocoin::CoinDenomination::ZQ_ERROR);

    if (!fSuccess)
        throw JSONRPCError(RPC_WALLET_ERROR, receipt.GetStatusMessage());

    CAmount nValueIn = 0;
    UniValue arrSpends(UniValue::VARR);
    for (const CZerocoinSpend& spend : receipt.GetSpends_back()) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("denomination", spend.GetDenomination()));
        obj.push_back(Pair("pubcoin", spend.GetPubCoin().GetHex()));
        obj.push_back(Pair("serial", spend.GetSerial().GetHex()));
        auto nChecksum = spend.GetAccumulatorChecksum();
        obj.push_back(Pair("acc_checksum", HexStr(BEGIN(nChecksum), END(nChecksum))));
        arrSpends.push_back(obj);
        nValueIn += libzerocoin::ZerocoinDenominationToAmount(spend.GetDenomination());
    }

    CAmount nValueOut = 0;
    UniValue vout(UniValue::VARR);
    std::vector<CTransactionRef> vtx = receipt.GetTransactions();

    for (unsigned int i = 0; i < vtx[0]->vpout.size(); i++) {
        const auto &pout = vtx[0]->vpout[i];
        UniValue out(UniValue::VOBJ);
        out.push_back(Pair("value", ValueFromAmount(pout->GetValue())));
        nValueOut += pout->GetValue();

        CTxDestination dest;
        if (pout->IsZerocoinMint())
            out.push_back(Pair("address", "zerocoinmint"));
        else if (pout->IsStandardOutput() && ExtractDestination(*pout->GetPScriptPubKey(), dest))
            out.push_back(Pair("address", CBitcoinAddress(dest).ToString()));
        vout.push_back(out);
    }

    //construct JSON to return
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txid", vtx[0]->GetHash().ToString()));
    ret.push_back(Pair("bytes", (int) ::GetSerializeSize(*vtx[0], SER_NETWORK, CTransaction::CURRENT_VERSION)));
    ret.push_back(Pair("fee", ValueFromAmount(nValueIn - nValueOut)));
    ret.push_back(Pair("duration_millis", (GetTimeMillis() - nTimeStart)));
    ret.push_back(Pair("spends", arrSpends));
    ret.push_back(Pair("outputs", vout));

    return ret;
}

UniValue spendzerocoinmints(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    UniValue params = request.params;
    if (request.fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
                "spendzerocoinmints mints_list (\"address\") \n"
                "\nSpend zerocoin mints to a VEIL address.\n" +
                HelpRequiringPassphrase(pwallet) + "\n"

                "\nArguments:\n"
                "1. mints_list     (string, required) A json array of zerocoin mints serial hashes\n"
                "2. \"address\"     (string, optional, default=change) Send to specified address or to a new change address.\n"

                "\nResult:\n"
                "{\n"
                "  \"txid\": \"xxx\",             (string) Transaction hash.\n"
                "  \"bytes\": nnn,              (numeric) Transaction size.\n"
                "  \"fee\": amount,             (numeric) Transaction fee (if any).\n"
                "  \"spends\": [                (array) JSON array of input objects.\n"
                "    {\n"
                "      \"denomination\": nnn,   (numeric) Denomination value.\n"
                "      \"pubcoin\": \"xxx\",      (string) Pubcoin in hex format.\n"
                "      \"serial\": \"xxx\",       (string) Serial number in hex format.\n"
                "      \"acc_checksum\": \"xxx\", (string) Accumulator checksum in hex format.\n"
                "    }\n"
                "    ,...\n"
                "  ],\n"
                "  \"outputs\": [                 (array) JSON array of output objects.\n"
                "    {\n"
                "      \"value\": amount,         (numeric) Value in VEIL.\n"
                "      \"address\": \"xxx\"         (string) VEIL address or \"zerocoinmint\" for reminted change.\n"
                "    }\n"
                "    ,...\n"
                "  ]\n"
                "}\n"

                                            "\nExamples\n" +
                HelpExampleCli("spendzerocoinmints", "'[\"0d8c16eee7737e3cc1e4e70dc006634182b175e039700931283b202715a0818f\", \"dfe585659e265e6a509d93effb906d3d2a0ac2fe3464b2c3b6d71a3ef34c8ad7\"]' \"VSJRSsuU9zfyrvxVaAEFQqK4MxZg6vgeS6\"") +
                HelpExampleRpc("spendzerocoinmints", "[\"0d8c16eee7737e3cc1e4e70dc006634182b175e039700931283b202715a0818f\", \"dfe585659e265e6a509d93effb906d3d2a0ac2fe3464b2c3b6d71a3ef34c8ad7\"], \"VSJRSsuU9zfyrvxVaAEFQqK4MxZg6vgeS6\""));

    LOCK2(cs_main, pwallet->cs_wallet);

    std::string address_str = "";
    if (params.size() > 1) {
        RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VSTR));
        address_str = params[1].get_str();
    } else
        RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR));

    EnsureWalletIsUnlocked(pwallet);

    UniValue arrMints = params[0].get_array();
    if (arrMints.size() == 0)
        throw JSONRPCError(RPC_WALLET_ERROR, "No zerocoin selected");
    if (arrMints.size() > Params().Zerocoin_MaxSpendsPerTransaction())
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Too many mints included. Maximum zerocoins per spend: %d", Params().Zerocoin_MaxSpendsPerTransaction()));

    CAmount nAmount(0);   // Spending amount

    // fetch mints and update nAmount
    vector<CZerocoinMint> vMintsSelected;
    for(unsigned int i=0; i < arrMints.size(); i++) {

        CZerocoinMint mint;
        std::string strSerialHash = arrMints[i].get_str();

        if (!IsHex(strSerialHash))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex serial hash");

        uint256 hashSerial;
        hashSerial.SetHex(strSerialHash);
        if (!pwallet->GetMint(hashSerial, mint)) {
            std::string strErr = "Failed to fetch mint associated with serial hash " + strSerialHash;
            throw JSONRPCError(RPC_WALLET_ERROR, strErr);
        }

        vMintsSelected.emplace_back(mint);
        nAmount += mint.GetDenominationAsAmount();
    }

    return DoZerocoinSpend(pwallet, nAmount, false, true, 100, vMintsSelected, address_str);
}

UniValue ResetMints(CWallet* pwallet)
{
    WalletBatch walletdb(pwallet->GetDBHandle());
    set<CMintMeta> setMints = pwallet->ListMints(false, false, true);
    vector<CMintMeta> vMintsToFind(setMints.begin(), setMints.end());
    vector<CMintMeta> vMintsMissing;
    vector<CMintMeta> vMintsToUpdate;

    // search all of our available data for these mints
    FindMints(vMintsToFind, vMintsToUpdate, vMintsMissing);

    // update the meta data of mints that were marked for updating
    UniValue arrUpdated(UniValue::VARR);
    for (const CMintMeta& meta : vMintsToUpdate) {
        pwallet->UpdateZerocoinState(meta);
        arrUpdated.push_back(meta.hashPubcoin.GetHex());
    }

    // delete any mints that were unable to be located on the blockchain
    UniValue arrDeleted(UniValue::VARR);
    for (CMintMeta& meta : vMintsMissing) {
        pwallet->ArchiveZerocoin(meta);
        arrDeleted.push_back(meta.hashPubcoin.GetHex());
    }

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("updated", arrUpdated));
    obj.push_back(Pair("archived", arrDeleted));
    return obj;
}

UniValue ResetSpends(CWallet* pwallet)
{
    WalletBatch walletdb(pwallet->GetDBHandle());
    CzTracker* zTracker = pwallet->GetZTrackerPointer();
    set<CMintMeta> setMints = zTracker->ListMints(false, false, false);
    list<CZerocoinSpend> listSpends = walletdb.ListSpentCoins();
    list<CZerocoinSpend> listUnconfirmedSpends;

    for (CZerocoinSpend& spend : listSpends) {
        CTransactionRef txRef;
        uint256 hashBlock;
        if (!GetTransaction(spend.GetTxHash(), txRef, Params().GetConsensus(), hashBlock)) {
            listUnconfirmedSpends.push_back(spend);
            continue;
        }

        //no confirmations
        if (hashBlock == uint256())
            listUnconfirmedSpends.push_back(spend);
    }

    UniValue objRet(UniValue::VOBJ);
    UniValue arrRestored(UniValue::VARR);
    for (CZerocoinSpend& spend : listUnconfirmedSpends) {
        for (auto& meta : setMints) {
            if (meta.hashSerial == GetSerialHash(spend.GetSerial())) {
                zTracker->SetPubcoinNotUsed(meta.hashPubcoin);
                walletdb.EraseZerocoinSpendSerialEntry(spend.GetSerial());
                //RemoveSerialFromDB(spend.GetSerial());
                UniValue obj(UniValue::VOBJ);
                obj.push_back(Pair("serial", spend.GetSerial().GetHex()));
                arrRestored.push_back(obj);
                continue;
            }
        }
    }

    objRet.push_back(Pair("restored", arrRestored));
    return objRet;
}

UniValue ReconsiderZerocoins(CWallet* pwallet)
{
    list<CZerocoinMint> listMints;
    list<CDeterministicMint> listDMints;

    pwallet->ReconsiderZerocoins(listMints, listDMints);

    UniValue arrRet(UniValue::VARR);
    for (const CZerocoinMint& mint : listMints) {
        UniValue objMint(UniValue::VOBJ);
        objMint.push_back(Pair("txid", mint.GetTxHash().GetHex()));
        objMint.push_back(Pair("denomination", ValueFromAmount(mint.GetDenominationAsAmount())));
        objMint.push_back(Pair("pubcoin", mint.GetValue().GetHex()));
        objMint.push_back(Pair("height", mint.GetHeight()));
        arrRet.push_back(objMint);
    }
    for (const CDeterministicMint& dMint : listDMints) {
        UniValue objMint(UniValue::VOBJ);
        objMint.push_back(Pair("txid", dMint.GetTxHash().GetHex()));
        objMint.push_back(Pair("denomination", FormatMoney(libzerocoin::ZerocoinDenominationToAmount(dMint.GetDenomination()))));
        objMint.push_back(Pair("pubcoinhash", dMint.GetPubcoinHash().GetHex()));
        objMint.push_back(Pair("height", dMint.GetHeight()));
        arrRet.push_back(objMint);
    }

    return arrRet;
}

UniValue rescanzerocoinwallet(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    UniValue params = request.params;
    if (request.fHelp || params.size() > 1)
        throw runtime_error(
                "resetmintzerocoin ( fullscan )\n"
                "\nScan the blockchain for all of the zerocoins that are held in the wallet.dat.\n"
                "Update any meta-data that is incorrect. Archive any mints that are not able to be found.\n" +
                HelpRequiringPassphrase(pwallet) + "\n"

                                                   "\nArguments:\n"
                                                   "1. fullscan          (boolean, optional) Rescan each block of the blockchain.\n"
                                                   "                               WARNING - may take 30+ minutes!\n"

                                                   "\nResult:\n"
                                                   "{\n"
                                                   "  \"updated\": [       (array) JSON array of updated mints.\n"
                                                   "    \"xxx\"            (string) Hex encoded mint.\n"
                                                   "    ,...\n"
                                                   "  ],\n"
                                                   "  \"archived\": [      (array) JSON array of archived mints.\n"
                                                   "    \"xxx\"            (string) Hex encoded mint.\n"
                                                   "    ,...\n"
                                                   "  ]\n"
                                                   "}\n"

                                                   "\nExamples:\n" +
                HelpExampleCli("resetmintzerocoin", "true") + HelpExampleRpc("resetmintzerocoin", "true"));

    LOCK2(cs_main, pwallet->cs_wallet);

    UniValue objMintReset = ResetMints(pwallet);
    UniValue objSpendReset = ResetSpends(pwallet);
    UniValue arrReconsider = ReconsiderZerocoins(pwallet);
    UniValue arrRet(UniValue::VARR);

    arrRet.pushKV("mints_reset", objMintReset);
    arrRet.pushKV("spends_reset", objSpendReset);
    arrRet.pushKV("unarchived_zerocoins", arrReconsider);
    return arrRet;
}

UniValue resetmintzerocoin(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    UniValue params = request.params;
    if (request.fHelp || params.size() > 1)
        throw runtime_error(
                "resetmintzerocoin ( fullscan )\n"
                "\nScan the blockchain for all of the zerocoins that are held in the wallet.dat.\n"
                "Update any meta-data that is incorrect. Archive any mints that are not able to be found.\n" +
                HelpRequiringPassphrase(pwallet) + "\n"

                                            "\nArguments:\n"
                                            "1. fullscan          (boolean, optional) Rescan each block of the blockchain.\n"
                                            "                               WARNING - may take 30+ minutes!\n"

                                            "\nResult:\n"
                                            "{\n"
                                            "  \"updated\": [       (array) JSON array of updated mints.\n"
                                            "    \"xxx\"            (string) Hex encoded mint.\n"
                                            "    ,...\n"
                                            "  ],\n"
                                            "  \"archived\": [      (array) JSON array of archived mints.\n"
                                            "    \"xxx\"            (string) Hex encoded mint.\n"
                                            "    ,...\n"
                                            "  ]\n"
                                            "}\n"

                                            "\nExamples:\n" +
                HelpExampleCli("resetmintzerocoin", "true") + HelpExampleRpc("resetmintzerocoin", "true"));

    LOCK2(cs_main, pwallet->cs_wallet);

    return ResetMints(pwallet);
}

UniValue resetspentzerocoin(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    UniValue params = request.params;
    if (request.fHelp || params.size() != 0)
        throw runtime_error(
                "resetspentzerocoin\n"
                "\nScan the blockchain for all of the zerocoins that are held in the wallet.dat.\n"
                "Reset mints that are considered spent that did not make it into the blockchain.\n"

                "\nResult:\n"
                "{\n"
                "  \"restored\": [        (array) JSON array of restored objects.\n"
                "    {\n"
                "      \"serial\": \"xxx\"  (string) Serial in hex format.\n"
                "    }\n"
                "    ,...\n"
                "  ]\n"
                "}\n"

                "\nExamples:\n" +
                HelpExampleCli("resetspentzerocoin", "") + HelpExampleRpc("resetspentzerocoin", ""));

    LOCK2(cs_main, pwallet->cs_wallet);

    return ResetSpends(pwallet);
}

UniValue getarchivedzerocoin(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    UniValue params = request.params;
    if(request.fHelp || params.size() != 0)
        throw runtime_error(
                "getarchivedzerocoin\n"
                "\nDisplay zerocoins that were archived because they were believed to be orphans.\n"
                "Provides enough information to recover mint if it was incorrectly archived.\n" +
                HelpRequiringPassphrase(pwallet) + "\n"

                                            "\nResult:\n"
                                            "[\n"
                                            "  {\n"
                                            "    \"txid\": \"xxx\",           (string) Transaction ID for archived mint.\n"
                                            "    \"denomination\": amount,  (numeric) Denomination value.\n"
                                            "    \"serial\": \"xxx\",         (string) Serial number in hex format.\n"
                                            "    \"randomness\": \"xxx\",     (string) Hex encoded randomness.\n"
                                            "    \"pubcoin\": \"xxx\"         (string) Pubcoin in hex format.\n"
                                            "  }\n"
                                            "  ,...\n"
                                            "]\n"

                                            "\nExamples:\n" +
                HelpExampleCli("getarchivedzerocoin", "") + HelpExampleRpc("getarchivedzerocoin", ""));

    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    WalletBatch walletdb(pwallet->GetDBHandle());
    list<CZerocoinMint> listMints = walletdb.ListArchivedZerocoins();
    list<CDeterministicMint> listDMints = walletdb.ListArchivedDeterministicMints();

    UniValue arrRet(UniValue::VARR);
    for (const CZerocoinMint& mint : listMints) {
        UniValue objMint(UniValue::VOBJ);
        objMint.push_back(Pair("txid", mint.GetTxHash().GetHex()));
        objMint.push_back(Pair("denomination", ValueFromAmount(mint.GetDenominationAsAmount())));
        objMint.push_back(Pair("serial", mint.GetSerialNumber().GetHex()));
        objMint.push_back(Pair("randomness", mint.GetRandomness().GetHex()));
        objMint.push_back(Pair("pubcoin", mint.GetValue().GetHex()));
        arrRet.push_back(objMint);
    }

    for (const CDeterministicMint& dMint : listDMints) {
        UniValue objDMint(UniValue::VOBJ);
        objDMint.push_back(Pair("txid", dMint.GetTxHash().GetHex()));
        objDMint.push_back(Pair("denomination", ValueFromAmount(libzerocoin::ZerocoinDenominationToAmount(dMint.GetDenomination()))));
        objDMint.push_back(Pair("serialhash", dMint.GetSerialHash().GetHex()));
        objDMint.push_back(Pair("pubcoinhash", dMint.GetPubcoinHash().GetHex()));
        objDMint.push_back(Pair("seedhash", dMint.GetSeedHash().GetHex()));
        objDMint.push_back(Pair("count", (int64_t)dMint.GetCount()));
        arrRet.push_back(objDMint);
    }

    return arrRet;
}

UniValue exportzerocoins(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    UniValue params = request.params;
    if(request.fHelp || params.empty() || params.size() > 2)
        throw runtime_error(
                "exportzerocoins include_spent ( denomination )\n"
                "\nExports zerocoin mints that are held by this wallet.dat\n" +
                HelpRequiringPassphrase(pwallet) + "\n"

                                            "\nArguments:\n"
                                            "1. \"include_spent\"        (bool, required) Include mints that have already been spent\n"
                                            "2. \"denomination\"         (integer, optional) Export a specific denomination of zerocoin\n"

                                            "\nResult:\n"
                                            "[                   (array of json object)\n"
                                            "  {\n"
                                            "    \"d\": n,         (numeric) the mint's zerocoin denomination \n"
                                            "    \"p\": \"pubcoin\", (string) The public coin\n"
                                            "    \"s\": \"serial\",  (string) The secret serial number\n"
                                            "    \"r\": \"random\",  (string) The secret random number\n"
                                            "    \"t\": \"txid\",    (string) The txid that the coin was minted in\n"
                                            "    \"h\": n,         (numeric) The height the tx was added to the blockchain\n"
                                            "    \"u\": used,      (boolean) Whether the mint has been spent\n"
                                            "    \"v\": version,   (numeric) The version of the zerocoin\n"
                                            "    \"k\": \"privkey\"  (string) The zerocoin private key (V2+ zerocoin only)\n"
                                            "    \"sh\": \"serialhash\"  (string) The zerocoin's serialhash\n"
                                            "  }\n"
                                            "  ,...\n"
                                            "]\n"

                                            "\nExamples:\n" +
                HelpExampleCli("exportzerocoins", "false 5") + HelpExampleRpc("exportzerocoins", "false 5"));

    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    WalletBatch walletdb(pwallet->GetDBHandle());

    bool fIncludeSpent = params[0].get_bool();
    libzerocoin::CoinDenomination denomination = libzerocoin::ZQ_ERROR;
    if (params.size() == 2)
        denomination = libzerocoin::IntToZerocoinDenomination(params[1].get_int());

    CzTracker* zTracker = pwallet->GetZTrackerPointer();
    set<CMintMeta> setMints = zTracker->ListMints(!fIncludeSpent, false, false);

    UniValue jsonList(UniValue::VARR);
    for (const CMintMeta& meta : setMints) {
        if (denomination != libzerocoin::ZQ_ERROR && denomination != meta.denom)
            continue;

        CZerocoinMint mint;
        if (!pwallet->GetMint(meta.hashSerial, mint))
            continue;

        UniValue objMint(UniValue::VOBJ);
        objMint.push_back(Pair("d", mint.GetDenomination()));
        objMint.push_back(Pair("p", mint.GetValue().GetHex()));
        objMint.push_back(Pair("s", mint.GetSerialNumber().GetHex()));
        objMint.push_back(Pair("r", mint.GetRandomness().GetHex()));
        objMint.push_back(Pair("t", mint.GetTxHash().GetHex()));
        objMint.push_back(Pair("h", mint.GetHeight()));
        objMint.push_back(Pair("u", mint.IsUsed()));
        objMint.push_back(Pair("v", mint.GetVersion()));
        if (mint.GetVersion() >= 2) {
            CKey key;
            key.SetPrivKey(mint.GetPrivKey(), true);
            CBitcoinSecret cBitcoinSecret;
            cBitcoinSecret.SetKey(key);
            objMint.push_back(Pair("k", cBitcoinSecret.ToString()));
        }
        uint256 hashSerial = GetSerialHash(mint.GetSerialNumber());
        objMint.push_back(Pair("sh", hashSerial.GetHex()));
        jsonList.push_back(objMint);
    }

    return jsonList;
}

UniValue importzerocoins(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    UniValue params = request.params;
    if(request.fHelp || params.size() == 0)
        throw runtime_error(
                "importzerocoins importdata \n"
                "\n[{\"d\":denomination,\"p\":\"pubcoin_hex\",\"s\":\"serial_hex\",\"r\":\"randomness_hex\",\"t\":\"txid\",\"h\":height, \"u\":used},{\"d\":...}]\n"
                "\nImport zerocoin mints.\n"
                "Adds raw zerocoin mints to the wallet.dat\n"
                "Note it is recommended to use the json export created from the exportzerocoins RPC call\n" +
                HelpRequiringPassphrase(pwallet) + "\n"

                                            "\nArguments:\n"
                                            "1. \"importdata\"    (string, required) A json array of json objects containing zerocoin mints\n"

                                            "\nResult:\n"
                                            "{\n"
                                            "  \"added\": n,        (numeric) The quantity of zerocoin mints that were added\n"
                                            "  \"value\": amount    (numeric) The total zerocoin value of zerocoin mints that were added\n"
                                            "}\n"

                                            "\nExamples\n" +
                HelpExampleCli("importzerocoins", "\'[{\"d\":100,\"p\":\"mypubcoin\",\"s\":\"myserial\",\"r\":\"randomness_hex\",\"t\":\"mytxid\",\"h\":104923, \"u\":false},{\"d\":5,...}]\'") +
                HelpExampleRpc("importzerocoins", "[{\"d\":100,\"p\":\"mypubcoin\",\"s\":\"myserial\",\"r\":\"randomness_hex\",\"t\":\"mytxid\",\"h\":104923, \"u\":false},{\"d\":5,...}]"));

    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ));
    UniValue arrMints = params[0].get_array();
    WalletBatch walletdb(pwallet->GetDBHandle());

    int count = 0;
    CAmount nValue = 0;
    for (unsigned int idx = 0; idx < arrMints.size(); idx++) {
        const UniValue &val = arrMints[idx];
        const UniValue &o = val.get_obj();

        const UniValue& vDenom = find_value(o, "d");
        if (!vDenom.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing d key");
        int d = vDenom.get_int();
        if (d < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, d must be positive");

        libzerocoin::CoinDenomination denom = libzerocoin::IntToZerocoinDenomination(d);
        CBigNum bnValue = 0;
        bnValue.SetHex(find_value(o, "p").get_str());
        CBigNum bnSerial = 0;
        bnSerial.SetHex(find_value(o, "s").get_str());
        CBigNum bnRandom = 0;
        bnRandom.SetHex(find_value(o, "r").get_str());
        uint256 txid;
        txid.SetHex(find_value(o, "t").get_str());

        int nHeight = find_value(o, "h").get_int();
        if (nHeight < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, h must be positive");

        bool fUsed = find_value(o, "u").get_bool();

        //Assume coin is version 1 unless it has the version actually set
        uint8_t nVersion = 1;
        const UniValue& vVersion = find_value(o, "v");
        if (vVersion.isNum())
            nVersion = static_cast<uint8_t>(vVersion.get_int());

        //Set the privkey if applicable
        CPrivKey privkey;
        if (nVersion >= libzerocoin::PrivateCoin::PUBKEY_VERSION) {
            std::string strPrivkey = find_value(o, "k").get_str();
            CBitcoinSecret vchSecret;
            bool fGood = vchSecret.SetString(strPrivkey);
            CKey key = vchSecret.GetKey();
            if (!key.IsValid() && fGood)
                return JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "privkey is not valid");
            privkey = key.GetPrivKey();
        }

        CZerocoinMint mint(denom, bnValue, bnRandom, bnSerial, fUsed, nVersion, &privkey);
        mint.SetTxHash(txid);
        mint.SetHeight(nHeight);
        pwallet->GetZTrackerPointer()->Add(mint, true);
        count++;
        nValue += libzerocoin::ZerocoinDenominationToAmount(denom);
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("added", count));
    ret.push_back(Pair("value", ValueFromAmount(nValue)));
    return ret;
}

UniValue reconsiderzerocoins(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;
    UniValue params = request.params;
    if(request.fHelp || !params.empty())
        throw runtime_error(
                "reconsiderzerocoins\n"
                "\nCheck archived zerocoin list to see if any mints were added to the blockchain.\n" +
                HelpRequiringPassphrase(pwallet) + "\n"

                                            "\nResult:\n"
                                            "[\n"
                                            "  {\n"
                                            "    \"txid\" : \"xxx\",           (string) the mint's zerocoin denomination \n"
                                            "    \"denomination\" : amount,  (numeric) the mint's zerocoin denomination\n"
                                            "    \"pubcoin\" : \"xxx\",        (string) The mint's public identifier\n"
                                            "    \"height\" : n              (numeric) The height the tx was added to the blockchain\n"
                                            "  }\n"
                                            "  ,...\n"
                                            "]\n"

                                            "\nExamples\n" +
                HelpExampleCli("reconsiderzerocoins", "") + HelpExampleRpc("reconsiderzerocoins", ""));

    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    return ReconsiderZerocoins(pwallet);
}

UniValue generatemintlist(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;
    UniValue params = request.params;
    if(request.fHelp || params.size() != 2)
        throw runtime_error(
                "generatemintlist\n"
                "\nShow mints that are derived from the deterministic zerocoin seed.\n" +
                HelpRequiringPassphrase(pwallet) + "\n"

                                            "\nArguments\n"
                                            "1. \"count\"  : n,  (numeric) Which sequential zerocoin to start with.\n"
                                            "2. \"range\"  : n,  (numeric) How many zerocoin to generate.\n"

                                            "\nResult:\n"
                                            "[\n"
                                            "  {\n"
                                            "    \"count\": n,          (numeric) Deterministic Count.\n"
                                            "    \"value\": \"xxx\",    (string) Hex encoded pubcoin value.\n"
                                            "    \"randomness\": \"xxx\",   (string) Hex encoded randomness.\n"
                                            "    \"serial\": \"xxx\"        (string) Hex encoded Serial.\n"
                                            "  }\n"
                                            "  ,...\n"
                                            "]\n"

                                            "\nExamples\n" +
                HelpExampleCli("generatemintlist", "1, 100") + HelpExampleRpc("generatemintlist", "1, 100"));

    EnsureWalletIsUnlocked(pwallet);

    int nCount = params[0].get_int();
    int nRange = params[1].get_int();
    if (pwallet->GetZWallet()->HasEmptySeed())
        throw JSONRPCError(RPC_WALLET_ERROR, "Zerocoin seed is not loaded");

    CKeyID seedID = pwallet->GetZWallet()->GetMasterSeedID();
    UniValue arrRet(UniValue::VARR);
    for (int i = nCount; i < nCount + nRange; i++) {
        libzerocoin::CoinDenomination denom = libzerocoin::CoinDenomination::ZQ_TEN;
        libzerocoin::PrivateCoin coin(Params().Zerocoin_Params(), denom, false);
        CDeterministicMint dMint;
        pwallet->GetZWallet()->GenerateMint(seedID, i, denom, coin, dMint);
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("count", i));
        obj.push_back(Pair("value", coin.getPublicCoin().getValue().GetHex()));
        obj.push_back(Pair("pubcoinhash", GetPubCoinHash(coin.getPublicCoin().getValue()).GetHex()));
        obj.push_back(Pair("randomness", coin.getRandomness().GetHex()));
        obj.push_back(Pair("serial", coin.getSerialNumber().GetHex()));
        arrRet.push_back(obj);
    }

    return arrRet;
}

UniValue deterministiczerocoinstate(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;
    UniValue params = request.params;
    if (request.fHelp || params.size() != 0)
        throw runtime_error(
                "deterministiczerocoinstate\n"
                "\nThe current state of the mintpool of the deterministic zerocoin wallet.\n"
                        "\nResult:\n"
                        "{\n"
                        "  \"zerocoin_master_seed_hash\": \"xxx\",   (string) Hash of the master seed used for all zerocoin derivation.\n"
                        "  \"count\": n,    (numeric) The count of the next zerocoin that will be derived.\n"
                        "  \"mintpool_count\": \"xxx\",   (string) The count of the mintpool\n"
                        "}\n" +


                HelpRequiringPassphrase(pwallet) + "\n"

                                            "\nExamples\n" +
                HelpExampleCli("deterministiczerocoinstate", "") + HelpExampleRpc("deterministiczerocoinstate", ""));

    CzWallet* zwallet = pwallet->GetZWallet();
    UniValue obj(UniValue::VOBJ);
    int nCount, nCountLastUsed;
    zwallet->GetState(nCount, nCountLastUsed);
    CKeyID seedID = zwallet->GetMasterSeedID();
    obj.push_back(Pair("zerocoin_master_seed_hash", seedID.GetHex()));
    obj.push_back(Pair("count", nCount));
    obj.push_back(Pair("mintpool_count", nCountLastUsed));

    return obj;
}

void static SearchThread(CzWallet* zwallet, int nCountStart, int nCountEnd)
{
    LogPrintf("%s: start=%d end=%d\n", __func__, nCountStart, nCountEnd);
    WalletBatch walletdb(zwallet->GetDBHandle());
    try {
        CKey keyMaster;
        if (!zwallet->GetMasterSeed(keyMaster))
            return;

        CKeyID hashSeed = keyMaster.GetPubKey().GetID();
        for(int i = nCountStart; i < nCountEnd; i++) {
            boost::this_thread::interruption_point();
            CDataStream ss(SER_GETHASH, 0);
            ss << keyMaster.GetPrivKey_256() << i;
            uint512 zerocoinSeed = Hash512(ss.begin(), ss.end());

            CBigNum bnValue;
            CBigNum bnSerial;
            CBigNum bnRandomness;
            CKey key;
            zwallet->SeedToZerocoin(zerocoinSeed, bnValue, bnSerial, bnRandomness, key);

            uint256 hashPubcoin = GetPubCoinHash(bnValue);
            zwallet->AddToMintPool(make_pair(hashPubcoin, i), true);
            walletdb.WriteMintPoolPair(hashSeed, hashPubcoin, i);
        }
    } catch (std::exception& e) {
        LogPrintf("SearchThread() exception");
    } catch (...) {
        LogPrintf("SearchThread() exception");
    }
}

UniValue searchdeterministiczerocoin(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    UniValue params = request.params;
    if(request.fHelp || params.size() != 3)
        throw runtime_error(
                "searchdeterministiczerocoin\n"
                "\nMake an extended search for deterministically generated zerocoins that have not yet been recognized by the wallet.\n" +
                HelpRequiringPassphrase(pwallet) + "\n"

                                            "\nArguments\n"
                                            "1. \"count\"       (numeric) Which sequential zerocoin to start with.\n"
                                            "2. \"range\"       (numeric) How many zerocoin to generate.\n"
                                            "3. \"threads\"     (numeric) How many threads should this operation consume.\n"

                                            "\nExamples\n" +
                HelpExampleCli("searchdeterministiczerocoin", "1, 100, 2") + HelpExampleRpc("searchdeterministiczerocoin", "1, 100, 2"));

    EnsureWalletIsUnlocked(pwallet);

    int nCount = params[0].get_int();
    if (nCount < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Count cannot be less than 0");

    int nRange = params[1].get_int();
    if (nRange < 1)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Range has to be at least 1");

    int nThreads = params[2].get_int();
    if (nThreads < 1)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Threads has to be at least 1");

    CzWallet* zwallet = pwallet->GetZWallet();

    boost::thread_group* dzThreads = new boost::thread_group();
    int nRangePerThread = nRange / nThreads;

    int nPrevThreadEnd = nCount - 1;
    for (int i = 0; i < nThreads; i++) {
        int nStart = nPrevThreadEnd + 1;;
        int nEnd = nStart + nRangePerThread;
        nPrevThreadEnd = nEnd;
        dzThreads->create_thread(boost::bind(&SearchThread, zwallet, nStart, nEnd));
    }

    dzThreads->join_all();

    zwallet->RemoveMintsFromPool(pwallet->GetZTrackerPointer()->GetSerialHashes());
    zwallet->SyncWithChain(false);

    //todo: better response
    return "done";
}

static const CRPCCommand commands[] =
{
     //  category              name                                actor (function)                argNames
    //  --------------------- ------------------------          -----------------------         ----------
    { "zerocoin",           "spendzerocoin",                    &spendzerocoin,                 {"amount", "mintchange", "minimizechange", "securitylevel", "address", "denomination"} },
    { "zerocoin",           "mintzerocoin",                     &mintzerocoin,                  {"amount", "utxos"} },
    { "zerocoin",           "searchdeterministiczerocoin",      &searchdeterministiczerocoin,   {"count", "range", "threads"} },
    { "zerocoin",           "deterministiczerocoinstate",       &deterministiczerocoinstate,    {} },
    { "zerocoin",           "generatemintlist",                 &generatemintlist,              {"count", "range"} },
    { "zerocoin",           "reconsiderzerocoins",              &reconsiderzerocoins,           {} },
    { "zerocoin",           "importzerocoins",                  &importzerocoins,               {"importdata"} },
    { "zerocoin",           "exportzerocoins",                  &exportzerocoins,               {"include_spent", "denomination"} },
    { "zerocoin",           "getarchivedzerocoin",              &getarchivedzerocoin,           {} },
    { "zerocoin",           "rescanzerocoinwallet",             &rescanzerocoinwallet,          {} },
    { "zerocoin",           "resetspentzerocoin",               &resetspentzerocoin,            {} },
    { "zerocoin",           "resetmintzerocoin",                &resetmintzerocoin,             {} },
    { "zerocoin",           "spendzerocoinmints",               &spendzerocoinmints,            {"mints_list", "address"} },
    { "zerocoin",           "listspentzerocoins",               &listspentzerocoins,            {} },
    { "zerocoin",           "listzerocoinamounts",              &listzerocoinamounts,           {} },
    { "zerocoin",           "listmintedzerocoins",              &listmintedzerocoins,           {"fVerbose", "vMatureOnly"} },
    { "zerocoin",           "lookupzerocoin",                   &lookupzerocoin,                {"id_type", "id"} },
    { "zerocoin",           "getzerocoinbalance",               &getzerocoinbalance,            {} },
};


void RegisterZerocoinRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
