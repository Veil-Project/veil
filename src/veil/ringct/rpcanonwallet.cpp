// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2019-2022 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <algorithm>

#include <amount.h>
#include <base58.h>
#include <chain.h>
#include <consensus/validation.h>
#include <consensus/tx_verify.h>
#include <core_io.h>
#include <init.h>
#include <httpserver.h>
#include <validation.h>
#include <net.h>
#include <policy/policy.h>
#include <policy/rbf.h>
#include <rpc/server.h>
#include <rpc/mining.h>
#include <rpc/util.h>
#include <script/sign.h>
#include <timedata.h>
#include <util/system.h>
#include <txdb.h>
#include <veil/ringct/blind.h>
#include <veil/ringct/anon.h>
#include <util/moneystr.h>
#include <veil/ringct/anonwallet.h>
#include <veil/ringct/anonwalletdb.h>
#include <veil/ringct/receipt.h>
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
#include <chainparams.h>
#include <veil/mnemonic/mnemonic.h>
#include <crypto/sha256.h>
#include <warnings.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h> // For DEFAULT_DISABLE_WALLET
#endif
#include <univalue.h>
#include <stdint.h>
#include <core_io.h>
#include <veil/ringct/watchonly.h>
#include <secp256k1_mlsag.h>

static const std::string WALLET_ENDPOINT_BASE = "/wallet/";

static UniValue getnewaddress(const JSONRPCRequest &request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;

    if (request.fHelp || request.params.size() > 5)
        throw std::runtime_error(
                "getnewaddress ( \"label\" num_prefix_bits prefix_num bech32 makeV2 )\n"
                "Returns a new stealth address for receiving payments."
                + HelpRequiringPassphrase(wallet.get()) +
                "\nArguments:\n"
                "1. \"label\"             (string, optional) If specified the key is added to the address book.\n"
                "2. num_prefix_bits     (int, optional) If specified and > 0, the stealth address is created with a prefix.\n"
                "3. prefix_num          (int, optional) If prefix_num is not specified the prefix will be selected deterministically.\n"
                "           prefix_num can be specified in base2, 10 or 16, for base 2 prefix_num must begin with 0b, 0x for base16.\n"
                "           A 32bit integer will be created from prefix_num and the least significant num_prefix_bits will become the prefix.\n"
                "           A stealth address created without a prefix will scan all incoming stealth transactions, irrespective of transaction prefixes.\n"
                "           Stealth addresses with prefixes will scan only incoming stealth transactions with a matching prefix.\n"
                "4. bech32              (bool, optional, default=false) Use Bech32 encoding.\n"
                "5. makeV2              (bool, optional, default=false) Generate an address from the same method used for hardware wallets.\n"
                "\nResult:\n"
                "\"address\"              (string) The new stealth address\n"
                "\nExamples:\n"
                + HelpExampleCli("getnewaddress", "\"lblTestSxAddrPrefix\" 3 \"0b101\"")
                + HelpExampleRpc("getnewaddress", "\"lblTestSxAddrPrefix\", 3, \"0b101\""));

    EnsureWalletIsUnlocked(wallet.get());

    std::string sLabel;
    if (request.params.size() > 0)
        sLabel = request.params[0].get_str();

    uint32_t num_prefix_bits = 0;
    if (request.params.size() > 1)
    {
        std::string s = request.params[1].get_str();
        if (s.length() && !ParseUInt32(s, &num_prefix_bits))
            throw JSONRPCError(RPC_INVALID_PARAMETER, _("num_prefix_bits invalid number."));
    };

    if (num_prefix_bits > 32)
        throw JSONRPCError(RPC_INVALID_PARAMETER, _("num_prefix_bits must be <= 32."));

    std::string sPrefix_num;
    if (request.params.size() > 2)
        sPrefix_num = request.params[2].get_str();

    bool fBech32 = request.params.size() > 3 ? request.params[3].get_bool() : true;
    bool fMakeV2 = request.params.size() > 4 ? request.params[4].get_bool() : true;

    if (fMakeV2 && !fBech32)
        throw JSONRPCError(RPC_INVALID_PARAMETER, _("bech32 must be true when using makeV2."));

    auto pAnonWallet = wallet->GetAnonWallet();

    CEKAStealthKey akStealth;
    std::string sError;
    CStealthAddress stealthAddress;
    if (!pAnonWallet->NewStealthKey(stealthAddress, num_prefix_bits, sPrefix_num.empty() ? nullptr : sPrefix_num.c_str()))
        throw JSONRPCError(RPC_WALLET_ERROR, _("NewStealthKeyFromAccount failed."));

    return stealthAddress.ToString(fBech32);
}

static UniValue getstealthchangeaddress(const JSONRPCRequest &request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;

    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
                "getstealthchangeaddress\n"
                "Returns the default stealth change address."
                + HelpRequiringPassphrase(wallet.get()) +
                "\nResult:\n"
                "\"address\"              (string) The stealth change address\n"
                "\nExamples:\n"
                + HelpExampleCli("getstealthchangeaddress", "")
                + HelpExampleRpc("getstealthchangeaddress", ""));

    EnsureWalletIsUnlocked(wallet.get());
    auto pAnonWallet = wallet->GetAnonWallet();
    auto address = pAnonWallet->GetStealthChangeAddress();

    return address.ToString(true);
}

static UniValue restoreaddresses(const JSONRPCRequest &request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
                "restoreaddresses (generate_count)\n"
                "Regenerates deterministic stealth addresses to give wallet knowledge that they are owned"
                + HelpRequiringPassphrase(wallet.get()) +
                "\nArguments:\n"
                "1. generate_count       (int, required) Amount of addresses to add to the wallet internally. WARNING: Generate as few as needed. High address counts will slow down sync times.\n"
                "\nResult:\n"
                "\"address\"              (string) The new stealth address\n"
                "\nExamples:\n"
                + HelpExampleCli("restoreaddresses", "10")
                + HelpExampleRpc("restoreaddresses", "10"));

    EnsureWalletIsUnlocked(wallet.get());
    auto pAnonWallet = wallet->GetAnonWallet();

    unsigned int n = request.params[0].get_int();

    for (unsigned int i = 0; i < n; i++) {
        CStealthAddress stealthAddress;
        if (!pAnonWallet->NewStealthKey(stealthAddress, 0, nullptr))
            throw JSONRPCError(RPC_WALLET_ERROR, _("NewStealthKeyFromAccount failed."));
    }
    return NullUniValue;
}

static UniValue rescanringctwallet(const JSONRPCRequest &request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;

    if (request.fHelp || !request.params.empty())
        throw std::runtime_error(
                "rescanringctwallet\n"
                "Rescans all transactions in the RingCT & CT Wallets."
                + HelpRequiringPassphrase(wallet.get()) +
                "\nExamples:\n"
                + HelpExampleCli("rescanringctwallet", "")
                + HelpExampleRpc("rescanringctwallet", ""));



    EnsureWalletIsUnlocked(wallet.get());
    auto pAnonWallet = wallet->GetAnonWallet();
    LOCK2(cs_main, wallet->cs_wallet);

    pAnonWallet->RescanWallet();
    return NullUniValue;
}

/*
static void push(UniValue & entry, std::string key, UniValue const & value)
{
    if (entry[key].getType() == 0) {
        entry.pushKV(key, value);
    }
}

static std::string getAddress(UniValue const & transaction)
{
    if (transaction["stealth_address"].getType() != 0) {
        return transaction["stealth_address"].get_str();
    }
    if (transaction["address"].getType() != 0) {
        return transaction["address"].get_str();
    }
    if (transaction["outputs"][0]["stealth_address"].getType() != 0) {
        return transaction["outputs"][0]["stealth_address"].get_str();
    }
    if (transaction["outputs"][0]["address"].getType() != 0) {
        return transaction["outputs"][0]["address"].get_str();
    }
    return std::string();
}
*/

enum SortCodes
{
    SRT_LABEL_ASC,
    SRT_LABEL_DESC,
};

class AddressComp {
public:
    int nSortCode;
    AddressComp(int nSortCode_) : nSortCode(nSortCode_) {}
    bool operator() (
            const std::map<CTxDestination, CAddressBookData>::iterator a,
            const std::map<CTxDestination, CAddressBookData>::iterator b) const
    {
        switch (nSortCode)
        {
            case SRT_LABEL_DESC:
                return b->second.name.compare(a->second.name) < 0;
            default:
                break;
        };
        //default: case SRT_LABEL_ASC:
        return a->second.name.compare(b->second.name) < 0;
    }
};

extern double GetDifficulty(const CBlockIndex* blockindex = nullptr);

static int AddOutput(uint8_t nType, std::vector<CTempRecipient> &vecSend, const CTxDestination &address, CAmount nValue,
                     bool fSubtractFeeFromAmount, std::string &sError)
{
    CTempRecipient r;
    r.nType = nType;
    r.SetAmount(nValue);
    r.fSubtractFeeFromAmount = fSubtractFeeFromAmount;
    r.address = address;

    vecSend.push_back(r);
    return 0;
}

static UniValue SendToInner(const JSONRPCRequest &request, OutputTypes typeIn, OutputTypes typeOut)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp)) {
        return NullUniValue;
    }
    auto pwalletAnon = wallet->GetAnonWallet();


    //todo: needed?
//    // Make sure the results are valid at least up to the most recent block
//    // the user could have gotten from another RPC command prior to now
//    if (!request.fSkipBlock) {
//        pwallet->BlockUntilSyncedToCurrentChain();
//    }

    EnsureWalletIsUnlocked(wallet.get());

    if (wallet->GetBroadcastTransactions() && !g_connman) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    int64_t nComputeTimeStart = GetTimeMillis();

    CAmount nTotal = 0;

    std::vector<CTempRecipient> vecSend;
    std::string sError;

    size_t nRingSizeOfs = 99;
    size_t nMaxInputsOfs = 99;
    size_t nTestFeeOfs = 99;
    size_t nCoinControlOfs = 99;

    if (typeIn == OUTPUT_CT) {
        nMaxInputsOfs = 6;
    } else if (typeIn == OUTPUT_RINGCT) {
        nRingSizeOfs = 6;
        nMaxInputsOfs = 8;
    }

    size_t nMaxInputsPerTx = gArgs.GetBoolArg("-multitx", false) ? 32 : 0;
    bool fSubtractFeeFromTotal = false;

    if (request.params[0].isArray()) {
        const UniValue &outputs = request.params[0].get_array();

        nMaxInputsOfs = 7;
        if (request.params.size() > nMaxInputsOfs) {
            nMaxInputsPerTx = request.params[nMaxInputsOfs].get_int();
        }
        for (size_t k = 0; k < outputs.size(); ++k) {
            if (!outputs[k].isObject()) {
                throw JSONRPCError(RPC_TYPE_ERROR, "Not an object");
            }
            const UniValue &obj = outputs[k].get_obj();

            std::string sAddress;
            CAmount nAmount;

            if (obj.exists("address")) {
                sAddress = obj["address"].get_str();
            } else {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Must provide an address.");
            }

            CBitcoinAddress address(sAddress);

            if (typeOut == OUTPUT_RINGCT && !address.IsValidStealthAddress()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid stealth address");
            }

            if (!obj.exists("script") && !address.IsValid()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
            }

            if (obj.exists("amount")) {
                nAmount = AmountFromValue(obj["amount"]);
            } else {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Must provide an amount.");
            }

            if (nAmount <= 0) {
                throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
            }
            nTotal += nAmount;

            bool fSubtractFeeFromAmount = nMaxInputsPerTx > 0;
            if (obj.exists("subfee")) {
                fSubtractFeeFromAmount = obj["subfee"].get_bool();
                fSubtractFeeFromTotal |= fSubtractFeeFromAmount;
            }

            if (0 != AddOutput(typeOut, vecSend, address.Get(), nAmount, fSubtractFeeFromAmount, sError)) {
                throw JSONRPCError(RPC_MISC_ERROR, strprintf("AddOutput failed: %s.", sError));
            }

            if (obj.exists("script")) {
                CTempRecipient &r = vecSend.back();

                if (sAddress != "script") {
                    JSONRPCError(RPC_INVALID_PARAMETER, "Address parameter must be 'script' to set script explicitly.");
                }

                std::string sScript = obj["script"].get_str();
                std::vector<uint8_t> scriptData = ParseHex(sScript);
                r.scriptPubKey = CScript(scriptData.begin(), scriptData.end());
                r.fScriptSet = true;

                if (typeOut != OUTPUT_STANDARD) {
                    throw std::runtime_error("TODO: Currently setting a script only works for standard outputs.");
                }
            }
        }
        // Must be sendtypeto
        nRingSizeOfs = 3;
        nTestFeeOfs = 5;
        nCoinControlOfs = 6;
    } else {
        std::string sAddress = request.params[0].get_str();
        CBitcoinAddress address(sAddress);

        if (typeOut == OUTPUT_RINGCT && !address.IsValidStealthAddress()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid stealth address");
        }
        CTxDestination dest;
        if (typeOut == OUTPUT_STANDARD) {
            dest = DecodeDestination(request.params[0].get_str());
            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid basecoin address");
            }
        } else {
            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid stealth address");
            dest = address.Get();
        }

        CAmount nAmount = AmountFromValue(request.params[1]);
        if (nAmount <= 0) {
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
        }
        nTotal += nAmount;

        if (request.params.size() > nMaxInputsOfs) {
            nMaxInputsPerTx = request.params[nMaxInputsOfs].get_int();
        }

        bool fSubtractFeeFromAmount = false;
        if (request.params.size() > 4) {
            fSubtractFeeFromAmount = fSubtractFeeFromTotal = request.params[4].get_bool();
        }
        if (nMaxInputsPerTx > 0)
            fSubtractFeeFromAmount = true;

        if (0 != AddOutput(typeOut, vecSend, dest, nAmount, fSubtractFeeFromAmount, sError)) {
            throw JSONRPCError(RPC_MISC_ERROR, strprintf("AddOutput failed: %s.", sError));
        }
    }

    switch (typeIn) {
        case OUTPUT_STANDARD:
            if (nTotal > wallet->GetBasecoinBalance()) {
                throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");
            }
            break;
        case OUTPUT_CT:
            if (nTotal > pwalletAnon->GetBlindBalance()) {
                throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient blinded funds");
            }
            break;
        case OUTPUT_RINGCT:
            if (nTotal > pwalletAnon->GetAnonBalance()) {
                throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient anon funds");
            }
            break;
        default:
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Unknown input type: %d.", typeIn));
    }

    auto nv = nRingSizeOfs;
    size_t nRingSize = Params().DefaultRingSize();
    if (request.params.size() > nv) {
        nRingSize = request.params[nv].get_int();
    }

    nv++;
    size_t nInputsPerSig = 32;
    if (request.params.size() > nv) {
        nInputsPerSig = request.params[nv].get_int();
    }

    bool fShowHex = false;
    bool fShowFee = false;
    bool fCheckFeeOnly = false;
    nv = nTestFeeOfs;
    if (request.params.size() > nv) {
        fCheckFeeOnly = request.params[nv].get_bool();
    }

    CCoinControl coincontrol;

    nv = nCoinControlOfs;
    if (request.params.size() > nv && request.params[nv].isObject()) {
        const UniValue &uvCoinControl = request.params[nv].get_obj();

        if (uvCoinControl.exists("changeaddress")) {
            std::string sChangeAddress = uvCoinControl["changeaddress"].get_str();

            // Check for script
            bool fHaveScript = false;
            if (IsHex(sChangeAddress)) {
                std::vector<uint8_t> vScript = ParseHex(sChangeAddress);
                CScript script(vScript.begin(), vScript.end());

                txnouttype whichType;
                if (IsStandard(script, whichType)) {
                    coincontrol.scriptChange = script;
                    fHaveScript = true;
                }
            }

            if (!fHaveScript) {
                CBitcoinAddress addrChange(sChangeAddress);
                coincontrol.destChange = addrChange.Get();
            }
        }

        const UniValue &uvInputs = uvCoinControl["inputs"];
        if (uvInputs.isArray()) {
            for (size_t i = 0; i < uvInputs.size(); ++i) {
                const UniValue &uvi = uvInputs[i];
                RPCTypeCheckObj(uvi,
                                {
                                        {"tx", UniValueType(UniValue::VSTR)},
                                        {"n", UniValueType(UniValue::VNUM)},
                                });

                COutPoint op(uint256S(uvi["tx"].get_str()), uvi["n"].get_int());
                coincontrol.setSelected.insert(op);
            }
        }

        if (uvCoinControl.exists("feeRate") && uvCoinControl.exists("estimate_mode")) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot specify both estimate_mode and feeRate");
        }

        if (uvCoinControl.exists("feeRate") && uvCoinControl.exists("conf_target")) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot specify both conf_target and feeRate");
        }

        if (uvCoinControl.exists("replaceable")) {
            if (!uvCoinControl["replaceable"].isBool())
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Replaceable parameter must be boolean.");
            coincontrol.m_signal_bip125_rbf = uvCoinControl["replaceable"].get_bool();
        }

        if (uvCoinControl.exists("conf_target")) {
            if (!uvCoinControl["conf_target"].isNum())
                throw JSONRPCError(RPC_INVALID_PARAMETER, "conf_target parameter must be numeric.");
            coincontrol.m_confirm_target = ParseConfirmTarget(uvCoinControl["conf_target"]);
        }

        if (uvCoinControl.exists("estimate_mode")) {
            if (!uvCoinControl["estimate_mode"].isStr())
                throw JSONRPCError(RPC_INVALID_PARAMETER, "estimate_mode parameter must be a string.");
            if (!FeeModeFromString(uvCoinControl["estimate_mode"].get_str(), coincontrol.m_fee_mode))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
        }

        if (uvCoinControl.exists("feeRate")) {
            coincontrol.m_feerate = CFeeRate(AmountFromValue(uvCoinControl["feeRate"]));
            coincontrol.fOverrideFeeRate = true;
        }

        if (uvCoinControl["debug"].isBool() && uvCoinControl["debug"].get_bool() == true) {
            fShowHex = true;
        }

        if (uvCoinControl["show_fee"].isBool() && uvCoinControl["show_fee"].get_bool() == true) {
            fShowFee = true;
        }
    }

    UniValue results(UniValue::VARR);
    CValidationState state;
    // TODO: Surface receipt status to the caller.
    CMultiTxReceipt receipt;
    int nStatus = SEND_ERROR;
    std::vector<CWalletTx> vwtx;
    while (nTotal > 0) {
        // Make new keys for the outputs after the first tx
        if (!vwtx.empty()) {
            for (auto& r : vecSend)
                r.sEphem.MakeNewKey(true);
        }
        // Create the transactions and add inputs
        CTransactionRef tx_new = std::make_shared<CTransaction>();
        CWalletTx& wtx = *vwtx.emplace(vwtx.end(), wallet.get(), tx_new);
        CTransactionRecord rtx;

        CAmount nFeeRet = 0;
        // We might need to make a copy of vecSend here.
        switch (typeIn) {
            case OUTPUT_STANDARD:
            {
                if (0 !=
                    pwalletAnon->AddStandardInputs(wtx, rtx, vecSend, !fCheckFeeOnly, nFeeRet, &coincontrol, sError, false, 0))
                    throw JSONRPCError(RPC_WALLET_ERROR, strprintf("AddStandardInputs failed: %s.", sError));
                break;
            }
            case OUTPUT_CT:
                if (0 != pwalletAnon->AddBlindedInputs(wtx, rtx, vecSend, !fCheckFeeOnly, nMaxInputsPerTx, nFeeRet, &coincontrol, sError))
                    throw JSONRPCError(RPC_WALLET_ERROR, strprintf("AddBlindedInputs failed: %s.", sError));
                break;
            case OUTPUT_RINGCT:
                if (!pwalletAnon->AddAnonInputs(wtx, rtx, vecSend, !fCheckFeeOnly, nRingSize,
                                                nInputsPerSig, nMaxInputsPerTx, nFeeRet, &coincontrol, sError))
                    throw JSONRPCError(RPC_WALLET_ERROR, sError);
                break;
            default:
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Unknown input type: %d.", typeIn));
        }

        // subtract total sent from nTotal
        for (auto& r : vecSend) {
            if (!r.fChange)
                nTotal -= r.nAmount;
        }
        if (fSubtractFeeFromTotal)
            nTotal -= nFeeRet;

        UniValue result(UniValue::VOBJ);

        // Report fee for this transaction
        if (fCheckFeeOnly || fShowFee) {
            result.pushKV("fee", ValueFromAmount(nFeeRet));
            result.pushKV("bytes", (int)GetVirtualTransactionSize(*(wtx.tx)));

            if (fShowHex) {
                std::string strHex = EncodeHexTx(*(wtx.tx), RPCSerializationFlags());
                result.pushKV("hex", strHex);
            }

            UniValue objChangedOutputs(UniValue::VOBJ);
            std::map<std::string, CAmount> mapChanged; // Blinded outputs are split, join the values for display
            for (const auto &r : vecSend) {
                if (!r.fChange && r.nAmount != r.nAmountSelected) {
                    std::string sAddr = CBitcoinAddress(r.address).ToString();
                    if (mapChanged.count(sAddr)) {
                        mapChanged[sAddr] += r.nAmount;
                    } else {
                        mapChanged[sAddr] = r.nAmount;
                    }
                }
            }

            for (const auto &v : mapChanged) {
                objChangedOutputs.pushKV(v.first, v.second);
            }

            result.pushKV("outputs_fee", objChangedOutputs);
        }

        receipt.AddTransaction(tx_new, rtx);
        if (fShowFee) {
            result.pushKV("txid", wtx.GetHash().GetHex());
            results.push_back(result);
        } else {
            results.push_back(wtx.GetHash().GetHex());
        }

        // Update amounts left to be sent.
        for (auto& r : vecSend) {
            if (!r.fChange)
                r.nAmount = r.nAmountSelected = std::max<CAmount>(r.nAmountSelected - r.nAmount, 0);
        }

        // We did a partial test in PreAcceptMempoolTx (anonwallet.cpp), but we need to do the full one
        LOCK(cs_main);
        if (!AcceptToMemoryPool(mempool, state, wtx.tx, nullptr /* pfMissingInputs */, nullptr /* plTxnReplaced */,
                                false /* bypass_limits */, maxTxFee, true /* test accept */)) {
            // failed mempool validation for one of the transactions so no partial transaction is being committed
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Transaction failed accept to memory pool"));
        }
    }

    if (fCheckFeeOnly) {
        return results;
    }

    int64_t nComputeTimeFinish = GetTimeMillis();

    // Commit transactions
    for (int i = 0; i < vwtx.size(); ++i) {
        CReserveKey reservekey(wallet.get());
        if (!wallet->CommitTransaction(vwtx[i].tx, vwtx[i].mapValue, vwtx[i].vOrderForm, &reservekey, g_connman.get(), state, nComputeTimeFinish - nComputeTimeStart)) {
            receipt.SetStatus(
                    "Error: The transaction was rejected! This might happen if some of the coins in your wallet "
                    "were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy "
                    "but not marked as spent here.", nStatus);
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Transaction commit failed: %s", FormatStateMessage(state)));
        }
    }

    receipt.SetStatus("Transactions successful.", SEND_OKAY);

    return results;
}

#include <veil/ringct/lightwallet.h>
static UniValue runtest(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
                "runtest \"scan_secret\", \"spend_secret\", \"spend_pubkey\", \"destination\", \"amount\""
                "\nTest RPC call to build the ringct transaction."
                "\n"
                "\nArguments:\n"
                "\nResult:\n"
                "\"[\n"
                "Total,       (number) The total amount of veil\n"
                "...\n"
                "]\"\n"
        );


    std::string txHex = TestBuildWalletTransaction();

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("data", txHex);
    return ret;
}

static UniValue testbuildlightwallettransaction(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() != 5)
        throw std::runtime_error(
                "testbuildlightwallettransaction \"scan_secret\", \"spend_secret\", \"spend_pubkey\", \"destination\", \"amount\""
                "\nTest RPC call to build the ringct transaction."
                "\n"
                "\nArguments:\n"
                "1. \"scan_secret\"         (string, required) The scan secret\n"
                "2. \"spend_secret\"         (string, required) The spend secret (Used to decrypt txes and find out if it is spent and the amount\n"
                "3. \"spend_pubkey\"         (string, required) The spend pubkey\n"
                "4. \"destination\"         (string, required) The destination for the funds\n"
                "5. \"amount\"         (number, required) The amount to send\n"
                "\nResult:\n"
                "\"[\n"
                "Total,       (number) The total amount of veil\n"
                "...\n"
                "]\"\n"
        );

    int64_t nComputeTimeStart = GetTimeMillis();

    CAmount nTotal = 0;
    size_t nRingSizeOfs = 6;
    size_t nTestFeeOfs = 99;
    size_t nCoinControlOfs = 99;
    std::vector<CTempRecipient> vecSend;
    std::string sError;

    // Decode params 0
    CKey scan_secret;
    GetSecretFromString(request.params[0].get_str(), scan_secret);

    // Decode params 1
    CKey spend_secret;
    GetSecretFromString(request.params[1].get_str(), spend_secret);

    // Decode params 2
    CPubKey spend_pubkey;
    GetPubkeyFromString(request.params[2].get_str(), spend_pubkey);


    std::string sAddress = request.params[3].get_str();
    CBitcoinAddress address(sAddress);

    OutputTypes typeOut;
    OutputTypes typeIn = OUTPUT_RINGCT;
    CTxDestination dest;

    if (address.IsValidStealthAddress()) {
        typeOut = OUTPUT_RINGCT;
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid stealth address");
        dest = address.Get();
    } else {
        typeOut = OUTPUT_STANDARD;
        dest = DecodeDestination(sAddress);
        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid basecoin address");
        }
    }

    CAmount nAmount = AmountFromValue(request.params[4]);
    if (nAmount <= 0) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
    }


    nTotal += nAmount;

    bool fSubtractFeeFromAmount = false;
//    if (request.params.size() > 4) {
//        fSubtractFeeFromAmount = request.params[4].get_bool();
//    }

    if (0 != AddOutput(typeOut, vecSend, dest, nAmount, fSubtractFeeFromAmount, sError)) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("AddOutput failed: %s.", sError));
    }

    /** Get our total that we can spend */
    // Fetch watchonly transactions
    // This check will be happening via api
    std::vector<std::pair<int, CWatchOnlyTx>> vTxes;
    FetchWatchOnlyTransactions(scan_secret, vTxes);

    // Store totals
    CAmount nTotalAmountAvailable = 0;
    int nTotalAvailableTxes = 0;

    std::vector<CWatchOnlyTx> vAvailableWatchOnly;

    // Loop through all txes, gathering a balance.
    // Check to make sure they aren't spent by query the fullnode
    if (vTxes.size()) {
        for (int i = 0; i < vTxes.size(); i++) {
            CAmount nAmount;
            uint256 blind;
            CCmpPubKey keyImage;
            if (GetAmountFromWatchonly(vTxes[i].second, scan_secret, spend_secret, spend_pubkey, nAmount, blind,
                                       keyImage)) {

                // Check if key image is spent
                // This check will happening via api
                uint256 tx_hash;
                bool spent_in_chain = pblocktree->ReadRCTKeyImage(keyImage, tx_hash);
                if (!spent_in_chain) {
                    nTotalAmountAvailable += nAmount;
                    nTotalAvailableTxes += 1;
                    CWatchOnlyTx temp = vTxes[i].second;
                    temp.nAmount = nAmount;
                    temp.blind = blind;
                    vAvailableWatchOnly.emplace_back(temp);
                }
            }
        }
    }

    if (nTotalAmountAvailable < nTotal) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient anon funds");
    }

    // Assign rign size and inputs per sig
    size_t nRingSize = Params().DefaultRingSize();
    size_t nInputsPerSig = nRingSize;

    // Get change address
    CStealthAddress sxAddr;
    sxAddr.label = "";
    sxAddr.scan_secret = scan_secret;
    sxAddr.spend_secret_id = spend_pubkey.GetID();
    sxAddr.prefix.number_bits = 0;

    if (0 != SecretToPublicKey(sxAddr.scan_secret, sxAddr.scan_pubkey)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not get scan public key.");
    }

    if (spend_secret.IsValid() && 0 != SecretToPublicKey(spend_secret, sxAddr.spend_pubkey)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not get spend public key.");
    } else {
        SetPublicKey(spend_pubkey, sxAddr.spend_pubkey);
    }

    CBitcoinAddress addrChange;
    addrChange.Set(sxAddr, true);

    // Set the change address in coincontrol
    CCoinControl coincontrol;
    coincontrol.destChange = addrChange.Get();

    if (vecSend.size() < 1) {
        sError = strprintf("Transaction must have at least one recipient.");
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Transaction must have at least one recipient.");
    }

    CAmount nValue = 0;
    for (const auto &r : vecSend) {
        nValue += r.nAmount;
        if (nValue < 0 || r.nAmount < 0) {
            sError = strprintf("Transaction amounts must not be negative.");
            throw JSONRPCError(RPC_TRANSACTION_ERROR, "Transaction amounts must not be negative.");
        }
    }

    if (nRingSize < MIN_RINGSIZE || nRingSize > MAX_RINGSIZE) {
        sError = strprintf("Ring size out of range.");
        return error("%s: %s", __func__, sError);
    }

    if (nInputsPerSig < 1 || nInputsPerSig > MAX_ANON_INPUTS) {
        sError = strprintf("Num inputs per signature out of range.");
        return error("%s: %s", __func__, sError);
    }

    for (CTempRecipient &r: vecSend) {
        if (r.nType == OUTPUT_STANDARD) {
            if (r.address.type() == typeid(CExtKeyPair)) {
                throw std::runtime_error(strprintf("%s: sending to extkeypair", __func__));
            } else if (r.address.type() == typeid(CKeyID)) {
                r.scriptPubKey = GetScriptForDestination(r.address);
            } else {
                if (!r.fScriptSet) {
                    r.scriptPubKey = GetScriptForDestination(r.address);
                    if (r.scriptPubKey.empty()) {
                        sError = strprintf("Unknown address type and no script set.");
                        return error("%s: %s", __func__, sError);
                    }
                }
            }
        } else if (r.nType == OUTPUT_RINGCT) {
            CKey sEphem = r.sEphem;
            if (!sEphem.IsValid()) {
                sEphem.MakeNewKey(true);
            }

            if (r.address.type() == typeid(CStealthAddress)) {
                CStealthAddress sx = boost::get<CStealthAddress>(r.address);

                CKey sShared;
                ec_point pkSendTo;
                int k, nTries = 24;
                for (k = 0; k < nTries; ++k) {
                    if (StealthSecret(sEphem, sx.scan_pubkey, sx.spend_pubkey, sShared, pkSendTo) == 0) {
                        break;
                    }
                    sEphem.MakeNewKey(true);
                }
                if (k >= nTries) {
                    sError = strprintf("Could not generate receiving public key.");
                    return error("%s: %s", __func__, sError);
                }

                r.pkTo = CPubKey(pkSendTo);
                CKeyID idTo = r.pkTo.GetID();

                if (sx.prefix.number_bits > 0) {
                    r.nStealthPrefix = FillStealthPrefix(sx.prefix.number_bits, sx.prefix.bitfield);
                }
            } else {
                throw JSONRPCError(RPC_TRANSACTION_ERROR,
                                   "RINGCT Outputs - Only able to send to stealth address for now.");
            }

            r.sEphem = sEphem;
        }
    }

    CMutableTransaction txNew;
    txNew.nLockTime = 0;

    FeeCalculation feeCalc;
    CAmount nFeeNeeded;
    unsigned int nBytes;

    // Get inputs = vAvailableWatchOnly
    CAmount nValueOutPlain = 0;
    int nChangePosInOut = -1;

    std::vector<std::vector<std::vector<int64_t> > > vMI;
    std::vector<std::vector<uint8_t> > vInputBlinds;
    std::vector<size_t> vSecretColumns;

    size_t nSubFeeTries = 100;
    bool pick_new_inputs = true;
    CAmount nValueIn = 0;

    CAmount nFeeRet = 0;

    size_t nSubtractFeeFromAmount = 0;

    std::vector<CWatchOnlyTx> vSelectedWatchOnly;

    txNew.vin.clear();
    txNew.vpout.clear();
    vSelectedWatchOnly.clear();

    CAmount nValueToSelect = nValue;
    if (nSubtractFeeFromAmount == 0) {
        nValueToSelect += nFeeRet;
    }

    nValueIn = 0;
    std::vector<CWatchOnlyTx> vSelectedTxes;

    CAmount currentMinimumChange = 0;
    CAmount tempsingleamountchange = 0;
    CAmount tempmultipleamountchange = 0;

    bool fSingleInput = false;
    bool fMultipleInput = false;

    for (const CWatchOnlyTx &tx : vAvailableWatchOnly) {
        if (tx.nAmount > nAmount) {
            tempsingleamountchange = tx.nAmount - nAmount;
            if (tempsingleamountchange < currentMinimumChange || currentMinimumChange == 0) {
                vSelectedTxes.clear();
                fSingleInput = true;
                vSelectedTxes.emplace_back(tx);
                currentMinimumChange = tempsingleamountchange;
            }
        }
    }

    if (!fSingleInput) {
        vSelectedTxes.clear();
        // We can use a single input for this transaction
        CAmount currentSelected = 0;
        for (const CWatchOnlyTx &tx : vAvailableWatchOnly) {
            currentSelected += tx.nAmount;
            vSelectedTxes.emplace_back(tx);
            if (currentSelected > nAmount) {
                tempmultipleamountchange = currentSelected - nAmount;
                fMultipleInput = true;
                break;
            }
        }
    }

    CAmount nTempChange = 0;
    if (fSingleInput) {
        nTempChange = tempsingleamountchange;
    } else if (fMultipleInput) {
        nTempChange = tempmultipleamountchange;
    } else {
        throw JSONRPCError(RPC_TRANSACTION_ERROR,
                           "RINGCT Outputs - Couldn't find enough ringct inputs for selected amount");
    }

    const CAmount nChange = nTempChange;

    // Insert a sender-owned 0 value output that becomes the change output if needed
    {
        // Fill an output to ourself
        CTempRecipient recipient;
        recipient.nType = OUTPUT_RINGCT;
        recipient.fChange = true;
        recipient.sEphem.MakeNewKey(true);

        recipient.address = coincontrol.destChange;

        if (recipient.address.type() == typeid(CStealthAddress)) {
            CStealthAddress sx = boost::get<CStealthAddress>(recipient.address);
            CKey keyShared;
            ec_point pkSendTo;

            int k, nTries = 24;
            for (k = 0; k < nTries; ++k) {
                if (StealthSecret(recipient.sEphem, sx.scan_pubkey, sx.spend_pubkey, keyShared, pkSendTo) == 0)
                    break;
                recipient.sEphem.MakeNewKey(true);
            }
            if (k >= nTries)
                return error("%s: Could not generate receiving public key.", __func__);

            CPubKey pkEphem = recipient.sEphem.GetPubKey();
            if (!pkEphem.IsValid())
                return error("%s: Ephemeral pubkey is not valid\n", __func__);
            recipient.pkTo = CPubKey(pkSendTo);
            CKeyID idTo = recipient.pkTo.GetID();
            recipient.scriptPubKey = GetScriptForDestination(idTo);
        } else {
            throw JSONRPCError(RPC_TRANSACTION_ERROR, "Change address wasn't of CStealthAddress Type");
        }

        if (nChange > ::minRelayTxFee.GetFee(2048)) {
            recipient.SetAmount(nChange);
        } else {
            recipient.SetAmount(0);
            nFeeRet += nChange;
        }

        if (nChangePosInOut < 0) {
            nChangePosInOut = GetRandInt(vecSend.size() + 1);
        } else {
            nChangePosInOut = std::min(nChangePosInOut, (int) vecSend.size());
        }

        if (nChangePosInOut < (int) vecSend.size() && vecSend[nChangePosInOut].nType == OUTPUT_DATA) {
            nChangePosInOut++;
        }

        vecSend.insert(vecSend.begin() + nChangePosInOut, recipient);

        // TODO - commenting out this section, because i don't think it is needed.
//            // Insert data output for stealth address if required
//            if (recipient.vData.size() > 0) {
//                CTempRecipient rd;
//                rd.nType = OUTPUT_DATA;
//                rd.fChange = true;
//                rd.vData = recipient.vData;
//                recipient.vData.clear();
//                //vecSend.insert(vecSend.begin()+nChangePosInOut+1, rd);
//            }
    }

    int nRemainder = vSelectedTxes.size() % nInputsPerSig;
    int nTxRingSigs = vSelectedTxes.size() / nInputsPerSig + (nRemainder == 0 ? 0 : 1);


    size_t nRemainingInputs = vSelectedTxes.size();

    //Add blank anon inputs as anon inputs
    for (int k = 0; k < nTxRingSigs; ++k) {
        size_t nInputs = (k == nTxRingSigs - 1 ? nRemainingInputs : nInputsPerSig);
        CTxIn txin;
        txin.nSequence = CTxIn::SEQUENCE_FINAL;
        txin.prevout.n = COutPoint::ANON_MARKER;
        txin.SetAnonInfo(nInputs, nRingSize);
        txNew.vin.emplace_back(txin);

        nRemainingInputs -= nInputs;
    }

    vMI.clear();
    vInputBlinds.clear();
    vSecretColumns.clear();
    vMI.resize(nTxRingSigs);
    vInputBlinds.resize(nTxRingSigs);
    vSecretColumns.resize(nTxRingSigs);
    nValueOutPlain = 0;
    nChangePosInOut = -1;

    OUTPUT_PTR<CTxOutData> outFee = MAKE_OUTPUT<CTxOutData>();
    outFee->vData.push_back(DO_FEE);
    outFee->vData.resize(9); // More bytes than varint fee could use
    txNew.vpout.push_back(outFee);

    bool fFirst = true;
    for (size_t i = 0; i < vecSend.size(); ++i) {
        auto &recipient = vecSend[i];

        recipient.ApplySubFee(nFeeRet, nSubtractFeeFromAmount, fFirst);

        OUTPUT_PTR<CTxOutBase> txbout;
        CreateOutput(txbout, recipient, sError);

        if (recipient.nType == OUTPUT_STANDARD) {
            nValueOutPlain += recipient.nAmount;
        }

        if (recipient.fChange && recipient.nType == OUTPUT_RINGCT) {
            nChangePosInOut = i;
        }

        recipient.n = txNew.vpout.size();
        txNew.vpout.push_back(txbout);
        if (recipient.nType == OUTPUT_RINGCT) {
            if (recipient.vBlind.size() != 32) {
                recipient.vBlind.resize(32);
                GetStrongRandBytes(&recipient.vBlind[0], 32);
            }

            //ADD CT DATA
            {
                secp256k1_pedersen_commitment *pCommitment = txbout->GetPCommitment();
                std::vector<uint8_t> *pvRangeproof = txbout->GetPRangeproof();

                if (!pCommitment || !pvRangeproof) {
                    sError = strprintf("Unable to get CT pointers for output type %d.", txbout->GetType());
                    return error("%s: %s", __func__, sError);
                }

                uint64_t nValue = recipient.nAmount;
                if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, pCommitment, (uint8_t *) &recipient.vBlind[0],
                                               nValue, secp256k1_generator_h)) {
                    sError = strprintf("Pedersen commit failed.");
                    return error("%s: %s", __func__, sError);
                }

                uint256 nonce;
                if (recipient.fNonceSet) {
                    nonce = recipient.nonce;
                } else {
                    if (!recipient.sEphem.IsValid()) {
                        sError = strprintf("Invalid ephemeral key.");
                        return error("%s: %s", __func__, sError);
                    }
                    if (!recipient.pkTo.IsValid()) {
                        sError = strprintf("Invalid recipient public key.");
                        return error("%s: %s", __func__, sError);
                    }
                    nonce = recipient.sEphem.ECDH(recipient.pkTo);
                    CSHA256().Write(nonce.begin(), 32).Finalize(nonce.begin());
                    recipient.nonce = nonce;
                }

                const char *message = recipient.sNarration.c_str();
                size_t mlen = strlen(message);

                size_t nRangeProofLen = 5134;
                pvRangeproof->resize(nRangeProofLen);

                uint64_t min_value = 0;
                int ct_exponent = 2;
                int ct_bits = 32;

                if (0 != SelectRangeProofParameters(nValue, min_value, ct_exponent, ct_bits)) {
                    sError = strprintf("Failed to select range proof parameters.");
                    return error("%s: %s", __func__, sError);
                }

                if (recipient.fOverwriteRangeProofParams == true) {
                    min_value = recipient.min_value;
                    ct_exponent = recipient.ct_exponent;
                    ct_bits = recipient.ct_bits;
                }

                if (1 != secp256k1_rangeproof_sign(secp256k1_ctx_blind,
                                                   &(*pvRangeproof)[0], &nRangeProofLen,
                                                   min_value, pCommitment,
                                                   &recipient.vBlind[0], nonce.begin(),
                                                   ct_exponent, ct_bits, nValue,
                                                   (const unsigned char *) message, mlen,
                                                   nullptr, 0, secp256k1_generator_h)) {
                    sError = strprintf("Failed to sign range proof.");
                    return error("%s: %s", __func__, sError);
                }

                pvRangeproof->resize(nRangeProofLen);
            }
        }
    }

    std::set<int64_t> setHave; // Anon prev-outputs can only be used once per transaction.
    size_t nTotalInputs = 0;
    for (size_t l = 0; l < txNew.vin.size(); ++l) { // Must add real outputs to setHave before picking decoys
        auto &txin = txNew.vin[l];
        uint32_t nSigInputs, nSigRingSize;
        txin.GetAnonInfo(nSigInputs, nSigRingSize);

        vInputBlinds[l].resize(32 * nSigInputs);

        size_t currentSize = nTotalInputs + nSigInputs;
        nTotalInputs += nSigInputs;

        // Placing real inputs
        {
            if (nSigRingSize < MIN_RINGSIZE || nSigRingSize > MAX_RINGSIZE) {
                sError = strprintf("Ring size out of range [%d, %d].", MIN_RINGSIZE, MAX_RINGSIZE);
                return error("%s: %s", __func__, sError);
            }

            //GetStrongRandBytes((unsigned char*)&nSecretColumn, sizeof(nSecretColumn));
            //nSecretColumn %= nRingSize;
            vSecretColumns[l] = GetRandInt(nSigRingSize);

            //TODO, what size is this supposed to be?
            vMI[l].resize(currentSize);

            for (size_t k = 0; k < vSelectedTxes.size(); ++k) {
                vMI[l][k].resize(nSigRingSize);
                for (size_t i = 0; i < nSigRingSize; ++i) {
                    if (i == vSecretColumns[l]) {
                        // TODO: Store pubkey on COutputRecord - in scriptPubKey?
                        const auto &coin = vSelectedTxes[k];
                        const uint256 &txhash = vSelectedTxes[k].tx_hash;

                        CCmpPubKey pk = vSelectedTxes[k].ringctout.pk;
                        memcpy(&vInputBlinds[l][k * 32], &vSelectedTxes[k].blind, 32);

                        int64_t index = vSelectedTxes[k].ringctIndex;

                        if (setHave.count(index)) {
                            sError = strprintf("Duplicate index found: %d.", index);
                            return error("%s: %s", __func__, sError);
                        }

                        vMI[l][k][i] = index;
                        setHave.insert(index);
                    }
                }
            }
        }
    }

    // Fill in dummy signatures for fee calculation.
    for (size_t l = 0; l < txNew.vin.size(); ++l) {
        auto &txin = txNew.vin[l];
        uint32_t nSigInputs, nSigRingSize;
        txin.GetAnonInfo(nSigInputs, nSigRingSize);

        // Place Hiding Outputs
        {
            std::set<int64_t> setIndexSelected;
            std::vector<std::pair<int64_t, CAnonOutput> > vecSelectedOutputs;
            std::string sError;

            // TODO - verify the nsiginputs and nringsize are set correctly
            if (!GetMainWallet()->GetAnonWallet()->GetRandomHidingOutputs(vMI[l].size(), nSigRingSize, setIndexSelected,
                                                                          vecSelectedOutputs, sError)) {
                throw JSONRPCError(RPC_WALLET_ERROR, sError);
            }

            int nCurrentLocation = 0;
            for (size_t k = 0; k < vMI[l].size(); ++k) {
                for (size_t i = 0; i < nSigRingSize; ++i) {
                    if (i == vSecretColumns[l]) {
                        continue;
                    }

                    vMI[l][k][i] = vecSelectedOutputs[nCurrentLocation].first;
                    nCurrentLocation++;
                }
            }
        }

        std::vector<uint8_t> vPubkeyMatrixIndices;

        for (size_t k = 0; k < nSigInputs; ++k)
            for (size_t i = 0; i < nSigRingSize; ++i) {
                PutVarInt(vPubkeyMatrixIndices, vMI[l][k][i]);
            }

        std::vector<uint8_t> vKeyImages(33 * nSigInputs);
        txin.scriptData.stack.emplace_back(vKeyImages);

        txin.scriptWitness.stack.emplace_back(vPubkeyMatrixIndices);

        std::vector<uint8_t> vDL((1 + (nSigInputs + 1) * nSigRingSize) *
                                 32 // extra element for C, extra row for commitment row
                                 + (txNew.vin.size() > 1 ? 33
                                                         : 0)); // extra commitment for split value if multiple sigs
        txin.scriptWitness.stack.emplace_back(vDL);
    }

    nBytes = GetVirtualTransactionSize(txNew);

    // TODO, have the server give us the feerate per Byte, when asking for txes
    // TODO, for now set to CENT
    nFeeNeeded = CENT;

    if (nFeeRet >= nFeeNeeded) {
        // Reduce fee to only the needed amount if possible. This
        // prevents potential overpayment in fees if the coins
        // selected to meet nFeeNeeded result in a transaction that
        // requires less fee than the prior iteration.
        if (nFeeRet > nFeeNeeded && nChangePosInOut != -1 && nSubtractFeeFromAmount == 0) {
            auto &r = vecSend[nChangePosInOut];

            CAmount extraFeePaid = nFeeRet - nFeeNeeded;

            r.nAmount += extraFeePaid;
            nFeeRet -= extraFeePaid;
        }
    } else if (!pick_new_inputs) {
        // This shouldn't happen, we should have had enough excess
        // fee to pay for the new output and still meet nFeeNeeded
        // Or we should have just subtracted fee from recipients and
        // nFeeNeeded should not have changed

        if (!nSubtractFeeFromAmount || !(--nSubFeeTries)) {
            sError = strprintf("Transaction fee and change calculation failed.");
            return error("%s: %s", __func__, sError);
        }
    }

    // Try to reduce change to include necessary fee
    if (nChangePosInOut != -1 && nSubtractFeeFromAmount == 0) {
        auto &r = vecSend[nChangePosInOut];
        CAmount additionalFeeNeeded = nFeeNeeded - nFeeRet;
        if (r.nAmount >= MIN_FINAL_CHANGE + additionalFeeNeeded) {
            r.nAmount -= additionalFeeNeeded;
            nFeeRet += additionalFeeNeeded;
        }
    }

    // If subtracting fee from recipients, we now know what fee we
    // need to subtract, we have no reason to reselect inputs
    if (nSubtractFeeFromAmount > 0) {
        pick_new_inputs = false;
    }

    // Include more fee and try again.
    nFeeRet = nFeeNeeded;

    vSelectedWatchOnly = vSelectedTxes;

    coincontrol.nChangePos = nChangePosInOut;
    nValueOutPlain += nFeeRet;

    // Remove scriptSigs to eliminate the fee calculation dummy signatures
    for (auto &txin : txNew.vin) {
        txin.scriptData.stack[0].resize(0);
        txin.scriptWitness.stack[1].resize(0);
    }

    std::vector<const uint8_t *> vpOutCommits;
    std::vector<const uint8_t *> vpOutBlinds;
    std::vector<uint8_t> vBlindPlain;
    secp256k1_pedersen_commitment plainCommitment;
    vBlindPlain.resize(32);
    memset(&vBlindPlain[0], 0, 32);

    if (nValueOutPlain > 0) {
        LogPrintf("nValueOutPlain is greater than 0. DOing some stuff to the commits, and blinds - %d\n", FormatMoney(nValueOutPlain));
        if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, &plainCommitment, &vBlindPlain[0],
                                       (uint64_t) nValueOutPlain, secp256k1_generator_h)) {
            sError = strprintf("Pedersen Commit failed for plain out.");
            return error("%s: %s", __func__, sError);
        }

        vpOutCommits.push_back(plainCommitment.data);
        vpOutBlinds.push_back(&vBlindPlain[0]);
    }

    // Update the change output commitment
    for (size_t i = 0; i < vecSend.size(); ++i) {
        auto &r = vecSend[i];

        if ((int) i == nChangePosInOut) {
            // Change amount may have changed

            if (r.nType != OUTPUT_RINGCT) {
                sError = strprintf("Change output is not RingCT type.");
                return error("%s: %s", __func__, sError);
            }

            if (r.vBlind.size() != 32) {
                r.vBlind.resize(32);
                GetStrongRandBytes(&r.vBlind[0], 32);
            }


            {
                CTxOutBase* txout = txNew.vpout[r.n].get();

                secp256k1_pedersen_commitment *pCommitment = txout->GetPCommitment();
                std::vector<uint8_t> *pvRangeproof = txout->GetPRangeproof();

                if (!pCommitment || !pvRangeproof) {
                    sError = strprintf("Unable to get CT pointers for output type %d.", txout->GetType());
                    return error("%s: %s", __func__, sError);
                }

                uint64_t nValue = r.nAmount;
                if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, pCommitment, (uint8_t *) &r.vBlind[0], nValue,
                                               secp256k1_generator_h)) {
                    sError = strprintf("Pedersen commit failed.");
                    return error("%s: %s", __func__, sError);
                }

                uint256 nonce;
                if (r.fNonceSet) {
                    nonce = r.nonce;
                } else {
                    if (!r.sEphem.IsValid()) {
                        sError = strprintf("Invalid ephemeral key.");
                        return error("%s: %s", __func__, sError);
                    }
                    if (!r.pkTo.IsValid()) {
                        sError = strprintf("Invalid recipient public key.");
                        return error("%s: %s", __func__, sError);
                    }
                    nonce = r.sEphem.ECDH(r.pkTo);
                    CSHA256().Write(nonce.begin(), 32).Finalize(nonce.begin());
                    r.nonce = nonce;
                }

                const char *message = r.sNarration.c_str();
                size_t mlen = strlen(message);

                size_t nRangeProofLen = 5134;
                pvRangeproof->resize(nRangeProofLen);

                uint64_t min_value = 0;
                int ct_exponent = 2;
                int ct_bits = 32;

                if (0 != SelectRangeProofParameters(nValue, min_value, ct_exponent, ct_bits)) {
                    sError = strprintf("Failed to select range proof parameters.");
                    return error("%s: %s", __func__, sError);
                }

                if (r.fOverwriteRangeProofParams == true) {
                    min_value = r.min_value;
                    ct_exponent = r.ct_exponent;
                    ct_bits = r.ct_bits;
                }

                if (1 != secp256k1_rangeproof_sign(secp256k1_ctx_blind,
                                                   &(*pvRangeproof)[0], &nRangeProofLen,
                                                   min_value, pCommitment,
                                                   &r.vBlind[0], nonce.begin(),
                                                   ct_exponent, ct_bits, nValue,
                                                   (const unsigned char *) message, mlen,
                                                   nullptr, 0, secp256k1_generator_h)) {
                    sError = strprintf("Failed to sign range proof.");
                    return error("%s: %s", __func__, sError);
                }

                pvRangeproof->resize(nRangeProofLen);
            }
        }

        if (r.nType == OUTPUT_CT || r.nType == OUTPUT_RINGCT) {
            vpOutCommits.push_back(txNew.vpout[r.n]->GetPCommitment()->data);
            vpOutBlinds.push_back(&r.vBlind[0]);
        }
    }

    std::vector<std::pair<COutPoint, CKey>> vKeysForSigningInputs(vSelectedWatchOnly.size());

    //Add actual fee to CT Fee output
    std::vector<uint8_t> &vData = ((CTxOutData *) txNew.vpout[0].get())->vData;
    vData.resize(1);
    if (0 != PutVarInt(vData, nFeeRet)) {
        sError = strprintf("Failed to add fee to transaction.");
        return error("%s: %s", __func__, sError);
    }

    std::vector<CKey> vSplitCommitBlindingKeys(txNew.vin.size()); // input amount commitment when > 1 mlsag
    int rv;
    nTotalInputs = 0;

    for (size_t l = 0; l < txNew.vin.size(); ++l) {
        auto &txin = txNew.vin[l];

        uint32_t nSigInputs, nSigRingSize;
        txin.GetAnonInfo(nSigInputs, nSigRingSize);

        std::vector<uint8_t> &vKeyImages = txin.scriptData.stack[0];
        vKeyImages.resize(33 * nSigInputs);

        for (size_t k = 0; k < nSigInputs; ++k) {
            size_t i = vSecretColumns[l];
            int64_t nIndex = vMI[l][k][i];

            CAnonOutput anonOutput;
            if (!pblocktree->ReadRCTOutput(nIndex, anonOutput)) {
                sError = strprintf("Output %d not found in database.", nIndex);
                return error("%s: %s", __func__, sError);
            }

            std::vector<uint8_t> vchEphemPK;

            for (auto tx : vSelectedWatchOnly) {
                if (COutPoint(tx.tx_hash, tx.tx_index) == anonOutput.outpoint) {
                    vchEphemPK.resize(33);
                    memcpy(&vchEphemPK[0], &tx.ringctout.vData[0], 33);
                    LogPrintf("Found the correct outpoint to generate vchEphemPK\n");
                    break;
                }
            }

            CKey sShared;
            ec_point pkExtracted;
            ec_point ecPubKey;
            SetPublicKey(spend_pubkey, ecPubKey);

            CKey keyDestination;
            CKeyID idk = anonOutput.pubkey.GetID();

            if (StealthSecret(scan_secret, vchEphemPK, ecPubKey, sShared, pkExtracted) != 0)
                error("%s: failed to generate stealth secret", __func__);

            if (StealthSharedToSecretSpend(sShared, spend_secret, keyDestination) != 0)
                error("%s: StealthSharedToSecretSpend() failed.\n", __func__);

            if (keyDestination.GetPubKey().GetID() != idk)
                error("%s: failed to generate correct shared secret", __func__);

            vKeysForSigningInputs.emplace_back(anonOutput.outpoint, keyDestination);

            // Keyimage is required for the tx hash
            rv = secp256k1_get_keyimage(secp256k1_ctx_blind, &vKeyImages[k * 33], anonOutput.pubkey.begin(), keyDestination.begin());
            if (0 != rv) {
                sError = strprintf("Failed to get keyimage.");
                return error("%s: %s", __func__, sError);
            }
        }
    }

    for (size_t l = 0; l < txNew.vin.size(); ++l) {
        auto &txin = txNew.vin[l];

        uint32_t nSigInputs, nSigRingSize;
        txin.GetAnonInfo(nSigInputs, nSigRingSize);

        size_t nCols = nSigRingSize;
        size_t nRows = nSigInputs + 1;

        uint8_t randSeed[32];
        GetStrongRandBytes(randSeed, 32);

        std::vector<CKey> vsk(nSigInputs);
        std::vector<const uint8_t *> vpsk(nRows);

        std::vector<uint8_t> vm(nCols * nRows * 33);
        std::vector<secp256k1_pedersen_commitment> vCommitments;
        vCommitments.reserve(nCols * nSigInputs);
        std::vector<const uint8_t *> vpInCommits(nCols * nSigInputs);
        std::vector<const uint8_t *> vpBlinds;


        std::vector<uint8_t> &vKeyImages = txin.scriptData.stack[0];

        for (size_t k = 0; k < nSigInputs; ++k) {
            for (size_t i = 0; i < nCols; ++i) {
                int64_t nIndex = vMI[l][k][i];

                CAnonOutput ao;
                if (!pblocktree->ReadRCTOutput(nIndex, ao)) {
                    sError = strprintf("Output %d not found in database.", nIndex);
                    return error("%s: %s", __func__, sError);
                }

                memcpy(&vm[(i + k * nCols) * 33], ao.pubkey.begin(), 33);
                vCommitments.push_back(ao.commitment);
                vpInCommits[i + k * nCols] = vCommitments.back().data;

                if (i == vSecretColumns[l]) {
                    CKeyID idk = ao.pubkey.GetID();
                    bool fFoundKey = false;
                    for (const auto &pair: vKeysForSigningInputs) {
                        if (pair.first == ao.outpoint) {
                            vsk[k] = pair.second;
                            fFoundKey = true;
                            break;
                        }
                    }

                    if (!fFoundKey) {
                        sError = strprintf("No key for output: %s", HexStr(ao.pubkey.begin(), ao.pubkey.end()));
                        return error("%s: %s", __func__, sError);
                    }

                    vpsk[k] = vsk[k].begin();

                    vpBlinds.push_back(&vInputBlinds[l][k * 32]);
                }
            }
        }

        uint8_t blindSum[32];
        memset(blindSum, 0, 32);
        vpsk[nRows - 1] = blindSum;

        std::vector<uint8_t> &vDL = txin.scriptWitness.stack[1];

        if (txNew.vin.size() == 1) {
            vDL.resize((1 + (nSigInputs + 1) * nSigRingSize) * 32); // extra element for C, extra row for commitment row
            vpBlinds.insert(vpBlinds.end(), vpOutBlinds.begin(), vpOutBlinds.end());

            if (0 != (rv = secp256k1_prepare_mlsag(&vm[0], blindSum,
                                                   vpOutCommits.size(), vpOutCommits.size(), nCols, nRows,
                                                   &vpInCommits[0], &vpOutCommits[0], &vpBlinds[0]))) {
                sError = strprintf("Failed to prepare mlsag with %d.", rv);
                return error("%s: %s", __func__, sError);
            }
        } else {
            // extra element for C extra, extra row for commitment row, split input commitment
            vDL.resize((1 + (nSigInputs + 1) * nSigRingSize) * 32 + 33);

            if (l == txNew.vin.size() - 1) {
                std::vector<const uint8_t *> vpAllBlinds = vpOutBlinds;

                for (size_t k = 0; k < l; ++k) {
                    vpAllBlinds.push_back(vSplitCommitBlindingKeys[k].begin());
                }

                if (!secp256k1_pedersen_blind_sum(secp256k1_ctx_blind,
                                                  vSplitCommitBlindingKeys[l].begin_nc(),
                                                  &vpAllBlinds[0], vpAllBlinds.size(),
                                                  vpOutBlinds.size())) {
                    sError = strprintf("Pedersen blind sum failed.");
                    return error("%s: %s", __func__, sError);
                }
            } else {
                vSplitCommitBlindingKeys[l].MakeNewKey(true);
            }

            CAmount nCommitValue = 0;
            for (size_t k = 0; k < vSelectedWatchOnly.size(); k++) {
                nCommitValue += vSelectedWatchOnly[k].nAmount;
            }

            nTotalInputs += nSigInputs;

            secp256k1_pedersen_commitment splitInputCommit;
            if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, &splitInputCommit,
                                           (uint8_t *) vSplitCommitBlindingKeys[l].begin(),
                                           nCommitValue, secp256k1_generator_h)) {
                sError = strprintf("Pedersen commit failed.");
                return error("%s: %s", __func__, sError);
            }

            memcpy(&vDL[(1 + (nSigInputs + 1) * nSigRingSize) * 32], splitInputCommit.data, 33);
            vpBlinds.emplace_back(vSplitCommitBlindingKeys[l].begin());

            const uint8_t *pSplitCommit = splitInputCommit.data;
            if (0 != (rv = secp256k1_prepare_mlsag(&vm[0], blindSum, 1, 1, nCols, nRows,
                                                   &vpInCommits[0], &pSplitCommit, &vpBlinds[0]))) {
                sError = strprintf("Failed to prepare mlsag with %d.", rv);
                return error("%s: %s", __func__, sError);
            }

            vpBlinds.pop_back();
        };

        uint256 hashOutputs = txNew.GetOutputsHash();
        if (0 != (rv = secp256k1_generate_mlsag(secp256k1_ctx_blind, &vKeyImages[0], &vDL[0], &vDL[32],
                                                randSeed, hashOutputs.begin(), nCols, nRows, vSecretColumns[l],
                                                &vpsk[0], &vm[0]))) {
            sError = strprintf("Failed to generate mlsag with %d.", rv);
            return error("%s: %s", __func__, sError);
        }

        // Validate the mlsag
        if (0 != (rv = secp256k1_verify_mlsag(secp256k1_ctx_blind, hashOutputs.begin(), nCols,
                                              nRows, &vm[0], &vKeyImages[0], &vDL[0], &vDL[32]))) {
            sError = strprintf("Failed to generate mlsag on initial generation %d.", rv);
            return error("%s: %s", __func__, sError);
        }
    }

    CValidationState state;
    CTransactionRef txRef = MakeTransactionRef(std::move(txNew));
    if (AcceptToMemoryPool(mempool, state, txRef, nullptr /* pfMissingInputs */,
                       nullptr /* plTxnReplaced */, true /* bypass_limits */, 0 /* nAbsurdFee */)) {
        return txRef->GetHash().GetHex();
    } else {
        return strprintf("Failed to submit to mempool %s\n", state.GetRejectReason());
    }
}

static const char *TypeToWord(OutputTypes type)
{
    switch (type)
    {
        case OUTPUT_STANDARD:
            return "basecoin";
        case OUTPUT_CT:
            return "stealth";
        case OUTPUT_RINGCT:
            return "ringct";
        default:
            break;
    }

    return "unknown";
}

static OutputTypes WordToType(std::string &s)
{
    if (s == "basecoin")
        return OUTPUT_STANDARD;
    if (s == "stealth")
        return OUTPUT_CT;
    if (s == "ringct")
        return OUTPUT_RINGCT;

    return OUTPUT_NULL;
}

static std::string SendHelp(std::shared_ptr<CWallet> pwallet, OutputTypes typeIn, OutputTypes typeOut)
{
    std::string rv;

    std::string cmd = std::string("send") + TypeToWord(typeIn) + "to" + TypeToWord(typeOut);

    rv = cmd + " \"address\" amount ( \"comment\" \"comment-to\" subtractfeefromamount \"narration\"";
    if (typeIn == OUTPUT_RINGCT)
        rv += " ringsize inputs_per_sig";
    if (typeIn == OUTPUT_CT || typeIn == OUTPUT_RINGCT)
        rv += " inputs_per_tx";
    rv += ")\n";

    rv += "\nSend an amount of ";
    rv += typeIn == OUTPUT_RINGCT ? "ringct" : typeIn == OUTPUT_CT ? "stealth" : "";
    rv += std::string(" veil in a") + (typeOut == OUTPUT_RINGCT || typeOut == OUTPUT_CT ? " stealth" : "") + " payment to a given address"
          + (typeOut == OUTPUT_CT ? " in ringct veil": "") + ".\n";

    rv += HelpRequiringPassphrase(pwallet.get());

    rv +=   "\nArguments:\n"
            "1. \"address\"     (string, required) The address to send to.\n"
            "2. \"amount\"      (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
            "3. \"comment\"     (string, optional) A comment used to store what the transaction is for. \n"
            "                            This is not part of the transaction, just kept in your wallet.\n"
            "4. \"comment_to\"  (string, optional) A comment to store the name of the person or organization \n"
            "                            to which you're sending the transaction. This is not part of the \n"
            "                            transaction, just kept in your wallet.\n"
            "5. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
            "                            The recipient will receive less " + CURRENCY_UNIT + " than you enter in the amount field.\n"
            "6. \"narration\"   (string, optional) Up to 24 characters sent with the transaction.\n"
                                                                                                                               "                            The narration is stored in the blockchain and is sent encrypted when destination is a stealth address and uncrypted otherwise.\n";
    if (typeIn == OUTPUT_RINGCT)
        rv +=
                "7. ringsize        (int, optional, default=4).\n"
                "8. inputs_per_sig  (int, optional, default=32).\n"
                "9. inputs_per_tx   (int, optional, default=0, max=32). Allows sending in multiple transactions if necessary.\n"
                "                            If 0, will attempt to accomplish in one transaction.\n"
                "                            If greater than 0, may use multiple transactions,\n"
                "                            and will sweep dust into change using extra input slots if possible.\n";
    else if (typeIn == OUTPUT_CT)
        rv += "7. inputs_per_tx   (int, optional, default=0, max=32). Allows sending in multiple transactions if necessary.\n"
              "                            If 0, will attempt to accomplish in one transaction.\n"
              "                            If greater than 0, may use multiple transactions,\n"
              "                            and will sweep dust into change using extra input slots if possible.\n";

    rv +=
            "\nResult:\n"
            "\"txid\"           (string) The transaction id.\n";

    rv +=   "\nExamples:\n"
            + HelpExampleCli(cmd, "\"SPGyji8uZFip6H15GUfj6bsutRVLsCyBFL3P7k7T7MUDRaYU8GfwUHpfxonLFAvAwr2RkigyGfTgWMfzLAAP8KMRHq7RE8cwpEEekH\" 0.1");

    return rv;
}

static UniValue sendbasecointostealth(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 6)
        throw std::runtime_error(SendHelp(wallet, OUTPUT_STANDARD, OUTPUT_CT));

    return SendToInner(request, OUTPUT_STANDARD, OUTPUT_CT);
};

static UniValue sendstealthtobasecoin(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 7)
        throw std::runtime_error(SendHelp(wallet, OUTPUT_CT, OUTPUT_STANDARD));

    return SendToInner(request, OUTPUT_CT, OUTPUT_STANDARD);
};

static UniValue sendstealthtostealth(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 7)
        throw std::runtime_error(SendHelp(wallet, OUTPUT_CT, OUTPUT_CT));

    return SendToInner(request, OUTPUT_CT, OUTPUT_CT);
};

static UniValue sendstealthtoringct(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 7)
        throw std::runtime_error(SendHelp(wallet, OUTPUT_CT, OUTPUT_RINGCT));

    return SendToInner(request, OUTPUT_CT, OUTPUT_RINGCT);
};


static UniValue sendringcttobasecoin(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 9)
        throw std::runtime_error(SendHelp(wallet, OUTPUT_RINGCT, OUTPUT_STANDARD));

    return SendToInner(request, OUTPUT_RINGCT, OUTPUT_STANDARD);
}

static UniValue sendringcttostealth(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 9)
        throw std::runtime_error(SendHelp(wallet, OUTPUT_RINGCT, OUTPUT_CT));

    return SendToInner(request, OUTPUT_RINGCT, OUTPUT_CT);
}

static UniValue sendringcttoringct(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 9)
        throw std::runtime_error(SendHelp(wallet, OUTPUT_RINGCT, OUTPUT_RINGCT));

    return SendToInner(request, OUTPUT_RINGCT, OUTPUT_RINGCT);
}

UniValue sendtypeto(const JSONRPCRequest &request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 10)
        throw std::runtime_error(
                "sendtypeto \"typein\" \"typeout\" [{address: , amount: , narr: , subfee:},...] (\"comment\" \"comment-to\" ringsize inputs_per_sig test_fee coin_control inputs_per_tx)\n"
                "\nSend basecoin to multiple outputs.\n"
                + HelpRequiringPassphrase(wallet.get()) +
                "\nArguments:\n"
                "1. \"typein\"          (string, required) basecoin/stealth/ringct\n"
                "2. \"typeout\"         (string, required) basecoin/stealth/ringct\n"
                "3. \"outputs\"         (json, required) Array of output objects\n"
                "    3.1 \"address\"    (string, required) The address to send to.\n"
                "    3.2 \"amount\"     (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
                "    3.x \"narr\"       (string, optional) Up to 24 character narration sent with the transaction.\n"
                "    3.x \"subfee\"     (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
                "    3.x \"script\"     (string, optional) Hex encoded script, will override the address.\n"
                "4. \"comment\"         (string, optional) A comment used to store what the transaction is for. \n"
                "                            This is not part of the transaction, just kept in your wallet.\n"
                "5. \"comment_to\"      (string, optional) A comment to store the name of the person or organization \n"
                "                            to which you're sending the transaction. This is not part of the \n"
                "                            transaction, just kept in your wallet.\n"
                "6. ringsize         (int, optional, default=4) Only applies when typein is ringct.\n"
                "7. inputs_per_sig   (int, optional, default=32) Only applies when typein is ringct.\n"
                "8. test_fee         (bool, optional, default=false) Only return the fee it would cost to send, txn is discarded.\n"
                "9. coin_control     (json, optional) Coincontrol object.\n"
                "   {\"changeaddress\": ,\n"
                "    \"inputs\": [{\"tx\":, \"n\":},...],\n"
                "    \"replaceable\": boolean,\n"
                "       Allow this transaction to be replaced by a transaction with higher fees via BIP 125\n"
                "    \"conf_target\": numeric,\n"
                "       Confirmation target (in blocks)\n"
                "    \"estimate_mode\": string,\n"
                "       The fee estimate mode, must be one of:\n"
                "           \"UNSET\"\n"
                "           \"ECONOMICAL\"\n"
                "           \"CONSERVATIVE\"\n"
                "     \"feeRate\"                (numeric, optional, default not set: makes wallet determine the fee) Set a specific feerate (" + CURRENCY_UNIT + " per KB)\n"
                "   }\n"
                "10. inputs_per_tx  (int, optional, default=0). Allows sending in multiple transactions if necessary.\n"
                "                            If 0, will attempt to accomplish in one transaction.\n"
                "                            If greater than 0, may use multiple transactions,\n"
                "                            and will sweep dust into change using extra input slots if possible.\n"
                "\nResult:\n"
                "\"txid\"              (string) The transaction id.\n"
                "\nExamples:\n"
                + HelpExampleCli("sendtypeto", "ringct basecoin \"[{\\\"address\\\":\\\"PbpVcjgYatnkKgveaeqhkeQBFwjqR7jKBR\\\",\\\"amount\\\":0.1}]\""));

    std::string sTypeIn = request.params[0].get_str();
    std::string sTypeOut = request.params[1].get_str();

    OutputTypes typeIn = WordToType(sTypeIn);
    OutputTypes typeOut = WordToType(sTypeOut);

    if (typeIn == OUTPUT_NULL)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown input type.");
    if (typeOut == OUTPUT_NULL)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown output type.");

    JSONRPCRequest req = request;
    req.params.erase(0, 2);

    return SendToInner(req, typeIn, typeOut);
}

static UniValue createrawbasecointransaction(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;

    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
                "createrawbasecointransaction [{\"txid\":\"id\",\"vout\":n},...] [{\"address\":amount,\"data\":\"hex\",...}] ( locktime replaceable \"fundfrombalance\" )\n"
                "\nCreate a transaction spending the given inputs and creating new confidential outputs.\n"
                "Outputs can be addresses or data.\n"
                "Returns hex-encoded raw transaction.\n"
                "Note that the transaction's inputs are not signed, and\n"
                "it is not stored in the wallet or transmitted to the network.\n"

                "\nArguments:\n"
                "1. \"inputs\"                (array, required) A json array of json objects\n"
                "     [\n"
                "       {\n"
                "         \"txid\":\"id\",              (string, required) The transaction id\n"
                "         \"vout\":n,                   (numeric, required) The output number\n"
                "         \"sequence\":n                (numeric, optional) The sequence number\n"
                "         \"blindingfactor\":\"hex\",   (string, optional) The blinding factor, required if blinded input is unknown to wallet\n"
                "       } \n"
                "       ,...\n"
                "     ]\n"
                "2. \"outputs\"               (array, required) A json array of json objects\n"
                "     [\n"
                "       {\n"
                "         \"address\": \"str\"          (string, required) The address\n"
                "         \"amount\": x.xxx           (numeric or string, required) The numeric value (can be string) in " + CURRENCY_UNIT + " of the output\n"
                "         \"data\": \"hex\",            (string, required) The key is \"data\", the value is hex encoded data\n"
                "         \"data_ct_fee\": x.xxx,     (numeric, optional) If type is \"data\" and output is at index 0, then it will be treated as a CT fee output\n"
                "         \"script\": \"str\",          (string, optional) Specify script directly.\n"
                "         \"type\": \"str\",            (string, optional, default=\"plain\") The type of output to create, plain, blind or anon.\n"
                "         \"pubkey\": \"hex\",          (string, optional) The key is \"pubkey\", the value is hex encoded public key for encrypting the metadata\n"
                //            "         \"subfee\":bool      (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
                "         \"narration\" \"str\",        (string, optional) Up to 24 character narration sent with the transaction.\n"
                "         \"blindingfactor\" \"hex\",   (string, optional) Blinding factor to use. Blinding factor is randomly generated if not specified.\n"
                "         \"rangeproof_params\":  ,     (object, optional) Overwrite the rangeproof parameters of an output \n"
                "           {\n"
                "             \"min_value\": x.xxx            (numeric, required) the minimum value to prove for.\n"
                "             \"ct_exponent\": x              (numeric, required) the exponent to  use.\n"
                "             \"ct_bits\": x                  (numeric, required) the amount of bits to prove for.\n"
                "           }\n"
                "         \"ephemeral_key\" \"hex\",    (string, optional) Ephemeral secret key for blinded outputs.\n"
                "         \"nonce\":\"hex\"\n           (string, optional) Nonce for blinded outputs.\n"
                "       }\n"
                "       ,...\n"
                "     ]\n"
                "3. locktime                  (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs\n"
                "4. replaceable               (boolean, optional, default=false) Marks this transaction as BIP125 replaceable.\n"
                "                             Allows this transaction to be replaced by a transaction with higher fees. If provided, it is an error if explicit sequence numbers are incompatible.\n"
                //"5. \"fundfrombalance\"       (string, optional, default=none) Fund transaction from standard, blinded or anon balance.\n"
                "\nResult:\n"
                "{\n"
                "  \"transaction\"      (string) hex string of the transaction\n"
                "  \"amounts\"          (json) Coin values of outputs with blinding factors of blinded outputs.\n"
                "}\n"
                "\nExamples:\n"
                + HelpExampleCli("createrawbasecointransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"")
                + HelpExampleCli("createrawbasecointransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"data\\\":\\\"00010203\\\"}\"")
                + HelpExampleRpc("createrawbasecointransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"address\\\":0.01}\"")
                + HelpExampleRpc("createrawbasecointransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"data\\\":\\\"00010203\\\"}\"")
        );

    EnsureWalletIsUnlocked(wallet.get());

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VARR, UniValue::VNUM, UniValue::VBOOL, UniValue::VSTR}, true);
    if (request.params[0].isNull() || request.params[1].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");
    }

    UniValue inputs = request.params[0].get_array();
    UniValue outputs = request.params[1].get_array();

    CMutableTransaction rawTx;

    if (!request.params[2].isNull()) {
        int64_t nLockTime = request.params[2].get_int64();
        if (nLockTime < 0 || nLockTime > std::numeric_limits<uint32_t>::max()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        }
        rawTx.nLockTime = nLockTime;
    }

    bool rbfOptIn = request.params[3].isTrue();

    CAmount nCtFee = 0;
    std::map<int, uint256> mInputBlinds;
    for (unsigned int idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        }
        int nOutput = vout_v.get_int();
        if (nOutput < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");
        }

        uint32_t nSequence;
        if (rbfOptIn) {
            nSequence = MAX_BIP125_RBF_SEQUENCE;
        } else if (rawTx.nLockTime) {
            nSequence = std::numeric_limits<uint32_t>::max() - 1;
        } else {
            nSequence = std::numeric_limits<uint32_t>::max();
        }

        // set the sequence number if passed in the parameters object
        const UniValue& sequenceObj = find_value(o, "sequence");
        if (sequenceObj.isNum()) {
            int64_t seqNr64 = sequenceObj.get_int64();
            if (seqNr64 < 0 || seqNr64 > std::numeric_limits<uint32_t>::max()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
            } else {
                nSequence = (uint32_t)seqNr64;
            }
        }

        const UniValue &blindObj = find_value(o, "blindingfactor");
        if (blindObj.isStr()) {
            std::string s = blindObj.get_str();
            if (!IsHex(s) || !(s.size() == 64)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Blinding factor must be 32 bytes and hex encoded.");
            }

            uint256 blind;
            blind.SetHex(s);
            mInputBlinds[rawTx.vin.size()] = blind;
        }

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    std::vector<CTempRecipient> vecSend;
    for (size_t idx = 0; idx < outputs.size(); idx++) {
        const UniValue &o = outputs[idx].get_obj();
        CTempRecipient r;

        uint8_t nType = OUTPUT_STANDARD;
        const UniValue &typeObj = find_value(o, "type");
        if (typeObj.isStr()) {
            std::string s = typeObj.get_str();
            if (s == "standard") {
                nType = OUTPUT_STANDARD;
            } else
            if (s == "stealth") {
                nType = OUTPUT_CT;
            } else
            if (s == "ringct") {
                nType = OUTPUT_RINGCT;
            } else
            if (s == "data") {
                nType = OUTPUT_DATA;
            } else {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown output type.");
            }
        }

        CAmount nAmount = AmountFromValue(o["amount"]);

        bool fSubtractFeeFromAmount = false;
        //if (o.exists("subfee"))
        //    fSubtractFeeFromAmount = obj["subfee"].get_bool();

        if (o["pubkey"].isStr()) {
            std::string s = o["pubkey"].get_str();
            if (!IsHex(s) || !(s.size() == 66)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Public key must be 33 bytes and hex encoded.");
            }
            std::vector<uint8_t> v = ParseHex(s);
            r.pkTo = CPubKey(v.begin(), v.end());
        }

        if (o["ephemeral_key"].isStr()) {
            std::string s = o["ephemeral_key"].get_str();
            if (!IsHex(s) || !(s.size() == 64)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "\"ephemeral_key\" must be 32 bytes and hex encoded.");
            }
            std::vector<uint8_t> v = ParseHex(s);
            r.sEphem.Set(v.data(), true);
        }

        if (o["nonce"].isStr()) {
            std::string s = o["nonce"].get_str();
            if (!IsHex(s) || !(s.size() == 64)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "\"nonce\" must be 32 bytes and hex encoded.");
            }
            std::vector<uint8_t> v = ParseHex(s);
            r.nonce.SetHex(s);
            r.fNonceSet = true;
        }

        if (o["data"].isStr()) {
            std::string s = o["data"].get_str();
            if (!IsHex(s)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "\"data\" must be hex encoded.");
            }
            r.vData = ParseHex(s);
        }

        if (o["data_ct_fee"].isStr() || o["data_ct_fee"].isNum()) {
            if (nType != OUTPUT_DATA) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "\"data_ct_fee\" can only appear in output of type \"data\".");
            }

            if (idx != 0) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "\"data_ct_fee\" can only appear in vout 0.");
            }
            nCtFee = AmountFromValue(o["data_ct_fee"]);
        }

        if (o["address"].isStr() && o["script"].isStr()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Can't specify both \"address\" and \"script\".");
        }

        if (o["address"].isStr()) {
            CTxDestination dest = DecodeDestination(o["address"].get_str());
            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
            }
            r.address = dest;
        }

        if (o["script"].isStr()) {
            r.scriptPubKey = ParseScript(o["script"].get_str());
            r.fScriptSet = true;
        }

        r.nType = nType;
        r.SetAmount(nAmount);
        r.fSubtractFeeFromAmount = fSubtractFeeFromAmount;
        //r.address = address;

        // Need to know the fee before calculating the blind sum
        if (r.nType == OUTPUT_CT || r.nType == OUTPUT_RINGCT) {
            r.vBlind.resize(32);
            if (o["blindingfactor"].isStr()) {
                std::string s = o["blindingfactor"].get_str();
                if (!IsHex(s) || !(s.size() == 64)) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Blinding factor must be 32 bytes and hex encoded.");
                }

                uint256 blind;
                blind.SetHex(s);
                memcpy(r.vBlind.data(), blind.begin(), 32);
            } else {
                // Generate a random blinding factor if not provided
                GetStrongRandBytes(r.vBlind.data(), 32);
            }

            if (o["rangeproof_params"].isObject()) {
                const UniValue &rangeproofParams = o["rangeproof_params"].get_obj();

                if (!rangeproofParams["min_value"].isNum() || !rangeproofParams["ct_exponent"].isNum() || !rangeproofParams["ct_bits"].isNum()) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "All range proof parameters must be numeric.");
                }

                r.fOverwriteRangeProofParams = true;
                r.min_value = rangeproofParams["min_value"].get_int64();
                r.ct_exponent = rangeproofParams["ct_exponent"].get_int();
                r.ct_bits = rangeproofParams["ct_bits"].get_int();
            }
        }

        vecSend.push_back(r);
    }

    std::string sError;
    // Note: wallet is only necessary when sending to  an extkey address
    auto* pAnonWallet = wallet->GetAnonWallet();
    if (!pAnonWallet->ExpandTempRecipients(vecSend, sError)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("ExpandTempRecipients failed: %s.", sError));
    }

    UniValue amounts(UniValue::VOBJ);

    CAmount nFeeRet = 0;
    //bool fFirst = true;
    for (size_t i = 0; i < vecSend.size(); ++i) {
        auto &r = vecSend[i];

        //r.ApplySubFee(nFeeRet, nSubtractFeeFromAmount, fFirst);

        OUTPUT_PTR<CTxOutBase> txbout;
        if (0 != CreateOutput(txbout, r, sError)) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("CreateOutput failed: %s.", sError));
        }

        if (!CheckOutputValue(r, &*txbout, nFeeRet, sError)) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("CheckOutputValue failed: %s.", sError));
        }
        /*
        if (r.nType == OUTPUT_STANDARD)
            nValueOutPlain += r.nAmount;

        if (r.fChange && r.nType == OUTPUT_CT)
            nChangePosInOut = i;
        */
        r.n = rawTx.vpout.size();
        rawTx.vpout.push_back(txbout);

        if (nCtFee != 0 && i == 0) {
            txbout->SetCTFee(nCtFee);
            continue;
        }

        UniValue amount(UniValue::VOBJ);
        amount.pushKV("value", ValueFromAmount(r.nAmount));

        if (r.nType == OUTPUT_CT || r.nType == OUTPUT_RINGCT) {
            if (!pAnonWallet->AddCTData(txbout.get(), r, sError)) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("AddCTData failed: %s.", sError));
            }
            amount.pushKV("nonce", r.nonce.ToString());
        }

        if (r.nType != OUTPUT_DATA) {
            amounts.pushKV(strprintf("%d", r.n), amount);
        }
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(rawTx));
    result.pushKV("amounts", amounts);

    return result;
};


static UniValue fundrawtransactionfrom(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 4 || request.params.size() > 5)
        throw std::runtime_error(
                "fundrawtransactionfrom \"input_type\" \"hexstring\" input_amounts output_amounts ( options iswitness )\n"
                "\nAdd inputs to a transaction until it has enough in value to meet its out value.\n"
                "This will not modify existing inputs, and will add at most one change output to the outputs.\n"
                "No existing outputs will be modified unless \"subtractFeeFromOutputs\" is specified.\n"
                "Note that inputs which were signed may need to be resigned after completion since in/outputs have been added.\n"
                "The inputs added will not be signed, use signrawtransaction for that.\n"
                "Note that all existing inputs must have their previous output transaction be in the wallet or have their amount and blinding factor specified in input_amounts.\n"
                /*"Note that all inputs selected must be of standard form and P2SH scripts must be\n"
                "in the wallet using importaddress or addmultisigaddress (to calculate fees).\n"
                "You can see whether this is the case by checking the \"solvable\" field in the listunspent output.\n"
                "Only pay-to-pubkey, multisig, and P2SH versions thereof are currently supported for watch-only\n"*/
                "\nArguments:\n"
                "1. \"input_type\"          (string, required) The type of inputs to use standard/anon/blind.\n"
                "2. \"hexstring\"           (string, required) The hex string of the raw transaction\n"
                "3. input_amounts         (object, required)\n"
                "   {\n"
                "       \"n\":\n"
                "         {\n"
                "           \"value\":amount\n"
                "           \"blind\":\"hex\"\n"
                "           \"witnessstack\":[\"hex\",]\n" // needed to estimate fee
                //"           \"script\":\"hex\"\n"
                //"           \"redeemscript\":\"hex\"\n"
                "         }\n"
                "   }\n"
                "4. output_amounts        (object, required)\n"
                "   {\n"
                "       \"n\":\n"
                "         {\n"
                "           \"value\":amount\n"
                //"           \"blind\":\"hex\"\n"
                "           \"nonce\":\"hex\"\n"
                "         }\n"
                "   }\n"
                "5. \"options\"             (object, optional)\n"
                "   {\n"
                "     \"changeAddress\"          (string, optional, default pool address) The address to receive the change\n"
                "     \"changePosition\"         (numeric, optional, default random) The index of the change output\n"
                "     \"change_type\"            (string, optional) The output type to use. Only valid if changeAddress is not specified. Options are \"legacy\", \"p2sh-segwit\", and \"bech32\". Default is set by -changetype.\n"
                "     \"includeWatching\"        (boolean, optional, default false) Also select inputs which are watch only\n"
                "     \"lockUnspents\"           (boolean, optional, default false) Lock selected unspent outputs\n"
                "     \"feeRate\"                (numeric, optional, default not set: makes wallet determine the fee) Set a specific fee rate in " + CURRENCY_UNIT + "/kB\n"
                "     \"subtractFeeFromOutputs\" (array, optional) A json array of integers.\n"
                "                              The fee will be equally deducted from the amount of each specified output.\n"
                "                              The outputs are specified by their zero-based index, before any change output is added.\n"
                "                              Those recipients will receive less coins than you enter in their corresponding amount field.\n"
                "                              If no outputs are specified here, the sender pays the fee.\n"
                "                                  [vout_index,...]\n"
                "     \"replaceable\"            (boolean, optional) Marks this transaction as BIP125 replaceable.\n"
                "     \"allow_other_inputs\"     (boolean, optional, default=true) Allow inputs to be added if any inputs already exist.\n"
                "     \"allow_change_output\"    (boolean, optional, default=true) Allow change output to be added if needed (only for 'blind' input_type).\n"
                "                              Allows this transaction to be replaced by a transaction with higher fees\n"
                "     \"conf_target\"            (numeric, optional) Confirmation target (in blocks)\n"
                "     \"estimate_mode\"          (string, optional, default=UNSET) The fee estimate mode, must be one of:\n"
                "         \"UNSET\"\n"
                "         \"ECONOMICAL\"\n"
                "         \"CONSERVATIVE\"\n"
                "   }\n"
                "\nResult:\n"
                "{\n"
                "  \"hex\":       \"value\", (string)  The resulting raw transaction (hex-encoded string)\n"
                "  \"fee\":       n,       (numeric) Fee in " + CURRENCY_UNIT + " the resulting transaction pays\n"
                "  \"changepos\": n        (numeric) The position of the added change output, or -1\n"
                "  \"output_amounts\": obj (json) Output values and blinding factors\n"
                "}\n"
                "\nExamples:\n"
                "\nCreate a transaction with no inputs\n"
                + HelpExampleCli("createrawctransaction", "\"[]\" \"{\\\"myaddress\\\":0.01}\"") +
                "\nAdd sufficient unsigned inputs to meet the output value\n"
                + HelpExampleCli("fundrawtransactionfrom", "\"blind\" \"rawtransactionhex\"") +
                "\nSign the transaction\n"
                + HelpExampleCli("signrawtransactionwithwallet", "\"fundedtransactionhex\"") +
                "\nSend the transaction\n"
                + HelpExampleCli("sendrawtransaction", "\"signedtransactionhex\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR, UniValue::VOBJ, UniValue::VOBJ, UniValue::VOBJ}, true);

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    wallet->BlockUntilSyncedToCurrentChain();

    std::string sInputType = request.params[0].get_str();

    if (sInputType != "standard" && sInputType != "anon" && sInputType != "blind") {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown input type.");
    }

    CCoinControl coinControl;
    int changePosition = -1;
    bool lockUnspents = false;
    UniValue subtractFeeFromOutputs;
    std::set<int> setSubtractFeeFromOutputs;

    coinControl.fAllowOtherInputs = true;

    if (request.params[4].isObject()) {
        UniValue options = request.params[4];

        RPCTypeCheckObj(options,
                        {
                                {"changeAddress", UniValueType(UniValue::VSTR)},
                                {"changePosition", UniValueType(UniValue::VNUM)},
                                {"change_type", UniValueType(UniValue::VSTR)},
                                {"includeWatching", UniValueType(UniValue::VBOOL)},
                                {"lockUnspents", UniValueType(UniValue::VBOOL)},
                                {"feeRate", UniValueType()}, // will be checked below
                                {"subtractFeeFromOutputs", UniValueType(UniValue::VARR)},
                                {"replaceable", UniValueType(UniValue::VBOOL)},
                                {"allow_other_inputs", UniValueType(UniValue::VBOOL)},
                                {"allow_change_output", UniValueType(UniValue::VBOOL)},
                                {"conf_target", UniValueType(UniValue::VNUM)},
                                {"estimate_mode", UniValueType(UniValue::VSTR)},
                        },
                        true, true);

        if (options.exists("changeAddress")) {
            CTxDestination dest = DecodeDestination(options["changeAddress"].get_str());

            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "changeAddress must be a valid address");
            }

            coinControl.destChange = dest;
        }

        if (options.exists("changePosition")) {
            changePosition = options["changePosition"].get_int();
        }

        if (options.exists("change_type")) {
            if (options.exists("changeAddress")) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot specify both changeAddress and address_type options");
            }
            coinControl.m_change_type = wallet->m_default_change_type;
            if (!ParseOutputType(options["change_type"].get_str(), *coinControl.m_change_type)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Unknown change type '%s'", options["change_type"].get_str()));
            }
        }

        if (options.exists("includeWatching")) {
            coinControl.fAllowWatchOnly = options["includeWatching"].get_bool();
        }

        if (options.exists("lockUnspents")) {
            lockUnspents = options["lockUnspents"].get_bool();
        }

        if (options.exists("feeRate")) {
            coinControl.m_feerate = CFeeRate(AmountFromValue(options["feeRate"]));
            coinControl.fOverrideFeeRate = true;
        }

        if (options.exists("subtractFeeFromOutputs")) {
            subtractFeeFromOutputs = options["subtractFeeFromOutputs"].get_array();
        }

        if (options.exists("replaceable")) {
            coinControl.m_signal_bip125_rbf = options["replaceable"].get_bool();
        }

        if (options.exists("allow_other_inputs")) {
            coinControl.fAllowOtherInputs = options["allow_other_inputs"].get_bool();
        }

        if (options.exists("allow_change_output")) {
            coinControl.m_addChangeOutput = options["allow_change_output"].get_bool();
        }

        if (options.exists("conf_target")) {
            if (options.exists("feeRate")) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot specify both conf_target and feeRate");
            }
            coinControl.m_confirm_target = ParseConfirmTarget(options["conf_target"]);
        }

        if (options.exists("estimate_mode")) {
            if (options.exists("feeRate")) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot specify both estimate_mode and feeRate");
            }
            if (!FeeModeFromString(options["estimate_mode"].get_str(), coinControl.m_fee_mode)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
            }
        }
    }

    // parse hex string from parameter
    CMutableTransaction tx;
    if (!DecodeHexTx(tx, request.params[1].get_str(), true)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    size_t nOutputs = tx.GetNumVOuts();
    if (nOutputs == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "TX must have at least one output");
    }

    if (changePosition != -1 && (changePosition < 0 || (unsigned int)changePosition > nOutputs)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "changePosition out of bounds");
    }

    coinControl.nChangePos = changePosition;

    for (unsigned int idx = 0; idx < subtractFeeFromOutputs.size(); idx++) {
        int pos = subtractFeeFromOutputs[idx].get_int();
        if (setSubtractFeeFromOutputs.count(pos)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, duplicated position: %d", pos));
        }
        if (pos < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, negative position: %d", pos));
        }
        if (pos >= int(nOutputs)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, position too large: %d", pos));
        }
        setSubtractFeeFromOutputs.insert(pos);
    }

    UniValue inputAmounts = request.params[2];
    UniValue outputAmounts = request.params[3];
    std::map<int, uint256> mInputBlinds, mOutputBlinds;
    std::map<int, CAmount> mOutputAmounts;

    std::vector<CTempRecipient> vecSend(nOutputs);

    const std::vector<std::string> &vInputKeys = inputAmounts.getKeys();
    auto pAnonWallet = wallet->GetAnonWallet();
    pAnonWallet->mapTempRecords.clear();
    for (const std::string &sKey : vInputKeys) {
        int64_t n;
        if (!ParseInt64(sKey, &n) || n >= (int64_t)tx.vin.size() || n < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Bad index for input blinding factor.");
        }

        CInputData im;
        COutputRecord r;
        r.nType = OUTPUT_STANDARD;

        if (tx.vin[n].prevout.n >= OR_PLACEHOLDER_N) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Input offset too large for output record.");
        }
        r.n = tx.vin[n].prevout.n;

        uint256 blind;
        if (inputAmounts[sKey]["blind"].isStr()) {
            std::string s = inputAmounts[sKey]["blind"].get_str();
            if (!IsHex(s) || !(s.size() == 64)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Blinding factor must be 32 bytes and hex encoded.");
            }

            blind.SetHex(s);
            mInputBlinds[n] = blind;
            r.nType = OUTPUT_CT;
        }

        if (inputAmounts[sKey]["value"].isNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing 'value' for input.");
        }

        r.SetValue(AmountFromValue(inputAmounts[sKey]["value"]));

        if (inputAmounts[sKey]["witnessstack"].isArray()) {
            const UniValue &stack = inputAmounts[sKey]["witnessstack"].get_array();

            for (size_t k = 0; k < stack.size(); ++k) {
                if (!stack[k].isStr()) {
                    continue;
                }
                std::string s = stack.get_str();
                if (!IsHex(s)) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Input witness must be hex encoded.");
                }
                std::vector<uint8_t> v = ParseHex(s);
                im.scriptWitness.stack.push_back(v);
            }
        }

        //r.scriptPubKey = ; // TODO
        auto ret = pAnonWallet->mapTempRecords.insert(std::make_pair(tx.vin[n].prevout.hash, CTransactionRecord()));
        ret.first->second.InsertOutput(r);

        im.nValue = r.GetRawValue();
        im.blind = blind;

        coinControl.m_inputData[tx.vin[n].prevout] = im;
    }

    const std::vector<std::string> &vOutputKeys = outputAmounts.getKeys();
    for (const std::string &sKey : vOutputKeys) {
        int64_t n;
        if (!ParseInt64(sKey, &n) || n >= (int64_t)tx.GetNumVOuts() || n < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Bad index for output blinding factor.");
        }

        const auto &txout = tx.vpout[n];

        if (!outputAmounts[sKey]["value"].isNull()) {
            mOutputAmounts[n] = AmountFromValue(outputAmounts[sKey]["value"]);
        }

        if (outputAmounts[sKey]["nonce"].isStr()
            && txout->GetPRangeproof()) {
            CTempRecipient &r = vecSend[n];
            std::string s = outputAmounts[sKey]["nonce"].get_str();
            if (!IsHex(s) || !(s.size() == 64)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Nonce must be 32 bytes and hex encoded.");
            }

            r.fNonceSet = true;
            r.nonce.SetHex(s);

            uint64_t min_value, max_value;
            uint8_t blindOut[32];
            unsigned char msg[256]; // Currently narration is capped at 32 bytes
            size_t mlen = sizeof(msg);
            memset(msg, 0, mlen);
            uint64_t amountOut;
            if (1 != secp256k1_rangeproof_rewind(secp256k1_ctx_blind, blindOut, &amountOut, msg, &mlen, r.nonce.begin(),
                    &min_value, &max_value, txout->GetPCommitment(), txout->GetPRangeproof()->data(), txout->GetPRangeproof()->size(),
                    nullptr, 0, secp256k1_generator_h)) {
                throw JSONRPCError(RPC_MISC_ERROR, strprintf("secp256k1_rangeproof_rewind failed, output %d.", n));
            }

            uint256 blind;
            memcpy(blind.begin(), blindOut, 32);

            mOutputBlinds[n] = blind;
            mOutputAmounts[n] = amountOut;

            msg[mlen-1] = '\0';
            size_t nNarr = strlen((const char*)msg);
            if (nNarr > 0) {
                r.sNarration.assign((const char*)msg, nNarr);
            }
        } else {
            if (txout->GetPRangeproof()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Missing nonce for output %d.", n));
            }
        }
        /*
        if (outputAmounts[sKey]["blind"].isStr())
        {
            std::string s = outputAmounts[sKey]["blind"].get_str();
            if (!IsHex(s) || !(s.size() == 64))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Blinding factor must be 32 bytes and hex encoded.");

            uint256 blind;
            blind.SetHex(s);
            mOutputBlinds[n] = blind;
        };
        */

        vecSend[n].SetAmount(mOutputAmounts[n]);
    }

    CAmount nTotalOutput = 0;

    for (size_t i = 0; i < tx.vpout.size(); ++i) {
        const auto &txout = tx.vpout[i];
        CTempRecipient &r = vecSend[i];

        if (txout->IsType(OUTPUT_CT) || txout->IsType(OUTPUT_RINGCT)) {
            // Check commitment matches
            std::map<int, CAmount>::iterator ita = mOutputAmounts.find(i);
            std::map<int, uint256>::iterator itb = mOutputBlinds.find(i);

            if (ita == mOutputAmounts.end()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Missing amount for blinded output %d.", i));
            }

            if (itb == mOutputBlinds.end()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Missing blinding factor for blinded output %d.", i));
            }

            secp256k1_pedersen_commitment commitment;
            if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, &commitment, (const uint8_t*)(itb->second.begin()),
                    ita->second, secp256k1_generator_h)) {
                throw JSONRPCError(RPC_MISC_ERROR, strprintf("secp256k1_pedersen_commit failed, output %d.", i));
            }

            if (memcmp(txout->GetPCommitment()->data, commitment.data, 33) != 0) {
                throw JSONRPCError(RPC_MISC_ERROR, strprintf("Bad blinding factor, output %d.", i));
            }

            nTotalOutput += mOutputAmounts[i];

            r.vBlind.resize(32);
            memcpy(r.vBlind.data(), itb->second.begin(), 32);
        } else if (txout->IsType(OUTPUT_STANDARD)) {
            mOutputAmounts[i] = txout->GetValue();
            nTotalOutput += mOutputAmounts[i];
        }

        r.nType = txout->GetType();
        if (txout->IsType(OUTPUT_DATA)) {
            r.vData = ((CTxOutData*)txout.get())->vData;
        } else {
            r.SetAmount(mOutputAmounts[i]);
            r.fSubtractFeeFromAmount = setSubtractFeeFromOutputs.count(i);

            if (txout->IsType(OUTPUT_CT)) {
                r.vData = ((CTxOutCT*)txout.get())->vData;
            } else if (txout->IsType(OUTPUT_RINGCT)) {
                r.vData = ((CTxOutRingCT*)txout.get())->vData;
            }

            if (txout->GetPScriptPubKey()) {
                r.fScriptSet = true;
                r.scriptPubKey = *txout->GetPScriptPubKey();
            }
        }
    }

    for (const CTxIn& txin : tx.vin) {
        coinControl.Select(txin.prevout, 0); //todo select amount
    }

    CTransactionRef tx_new;
    CWalletTx wtx(wallet.get(), tx_new);
    CTransactionRecord rtx;
    CAmount nFee;
    std::string sError;
    {
        LOCK2(cs_main, wallet->cs_wallet);
        if (sInputType == "standard") {
            if (0 != pAnonWallet->AddStandardInputs(wtx, rtx, vecSend, false, nFee, &coinControl, sError, false, 0)) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("AddStandardInputs failed: %s.", sError));
            }
        } else if (sInputType == "anon") {
            sError = "TODO";
            //if (!pwallet->AddAnonInputs(wtx, rtx, vecSend, false, nFee, &coinControl, sError))
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("AddAnonInputs failed: %s.", sError));
        } else if (sInputType == "blind") {
            if (0 != pAnonWallet->AddBlindedInputs(wtx, rtx, vecSend, false, 0, nFee, &coinControl, sError)) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("AddBlindedInputs failed: %s.", sError));
            }
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown input type.");
        }
    }

    tx.vpout = wtx.tx->vpout;
    // keep existing sequences
    for (const auto &txin : wtx.tx->vin) {
        if (!coinControl.IsSelected(txin.prevout)) {
            tx.vin.push_back(txin);
        }
        if (lockUnspents) {
            LOCK2(cs_main, wallet->cs_wallet);
            wallet->LockCoin(txin.prevout);
        }
    }


    UniValue outputValues(UniValue::VOBJ);
    for (size_t i = 0; i < vecSend.size(); ++i) {
        auto &r = vecSend[i];

        UniValue outputValue(UniValue::VOBJ);
        if (r.vBlind.size() == 32) {
            uint256 blind(r.vBlind);
            outputValue.pushKV("blind", blind.ToString());
        }
        if (r.nType != OUTPUT_DATA) {
            outputValue.pushKV("value", ValueFromAmount(r.nAmount));
            outputValues.pushKV(strprintf("%d", r.n), outputValue);
        }
    }

    pAnonWallet->mapTempRecords.clear();

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(tx));
    result.pushKV("fee", ValueFromAmount(nFee));
    result.pushKV("changepos", coinControl.nChangePos);
    result.pushKV("output_amounts", outputValues);

    return result;
}

static UniValue verifycommitment(const JSONRPCRequest &request)
{
    if (request.fHelp || request.params.size() != 3)
        throw std::runtime_error(
                "verifycommitment \"commitment\" \"blind\" amount\n"
                "\nVerify value commitment.\n"

                "\nArguments:\n"
                "1. \"commitment\"                     (string, required) The 33byte commitment hex string\n"
                "2. \"blind\"                          (string, required) The 32byte blinding factor hex string\n"
                "3. amount                           (numeric or string, required) The amount committed to\n"
                "\nResult:\n"
                "{\n"
                "  \"result\": true,                   (boolean) If valid commitment, else throw error.\n"
                "}\n"
                "\nExamples:\n"
                + HelpExampleCli("verifycommitment", "\"commitment\" \"blind\" 1.1")
                + HelpExampleRpc("verifycommitment", "\"commitment\", \"blind\", 1.1")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR});

    std::vector<uint8_t> vchCommitment;
    uint256 blind;

    {
        std::string s = request.params[0].get_str();
        if (!IsHex(s) || !(s.size() == 66)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "commitment must be 33 bytes and hex encoded.");
        }
        vchCommitment = ParseHex(s);
    }

    {
        std::string s = request.params[1].get_str();
        if (!IsHex(s) || !(s.size() == 64)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Blinding factor must be 32 bytes and hex encoded.");
        }
        blind.SetHex(s);
    }

    CAmount nValue = AmountFromValue(request.params[2]);

    secp256k1_pedersen_commitment commitment;
    if (!secp256k1_pedersen_commit(secp256k1_ctx_blind,
                                   &commitment, blind.begin(),
                                   nValue, secp256k1_generator_h)) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("secp256k1_pedersen_commit failed."));
    }

    if (memcmp(vchCommitment.data(), commitment.data, 33) != 0) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Mismatched commitment."));
    }

    UniValue result(UniValue::VOBJ);
    bool rv = true;
    result.pushKV("result", rv);
    return result;
}

static UniValue verifyrawtransaction(const JSONRPCRequest &request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
                "verifyrawtransaction \"hexstring\" ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] returndecoded )\n"
                "\nVerify inputs for raw transaction (serialized, hex-encoded).\n"
                "The second optional argument (may be null) is an array of previous transaction outputs that\n"
                "this transaction depends on but may not yet be in the block chain.\n"
                "\nArguments:\n"
                "1. \"hexstring\"                      (string, required) The transaction hex string\n"
                "2. \"prevtxs\"                        (string, optional) An json array of previous dependent transaction outputs\n"
                "     [                              (json array of json objects, or 'null' if none provided)\n"
                "       {\n"
                "         \"txid\":\"id\",               (string, required) The transaction id\n"
                "         \"vout\":n,                  (numeric, required) The output number\n"
                "         \"scriptPubKey\": \"hex\",     (string, required) script key\n"
                //"         \"redeemScript\": \"hex\",     (string, required for P2SH or P2WSH) redeem script\n"
                "         \"amount\": value            (numeric, required) The amount spent\n"
                "         \"amount_commitment\": \"hex\",(string, required) The amount commitment spent\n"
                "       }\n"
                "       ,...\n"
                "    ]\n"
                "3. returndecoded                     (bool, optional) Return the decoded txn as a json object\n"
                "\nResult:\n"
                "{\n"
                "  \"complete\" : true|false,          (boolean) If the transaction has a complete set of signatures\n"
                "  \"errors\" : [                      (json array of objects) Script verification errors (if there are any)\n"
                "    {\n"
                "      \"txid\" : \"hash\",              (string) The hash of the referenced, previous transaction\n"
                "      \"vout\" : n,                   (numeric) The index of the output to spent and used as input\n"
                "      \"scriptSig\" : \"hex\",          (string) The hex-encoded signature script\n"
                "      \"sequence\" : n,               (numeric) Script sequence number\n"
                "      \"error\" : \"text\"              (string) Verification or signing error related to the input\n"
                "    }\n"
                "    ,...\n"
                "  ]\n"
                "}\n"
                "\nExamples:\n"
                + HelpExampleCli("verifyrawtransaction", "\"myhex\"")
                + HelpExampleRpc("verifyrawtransaction", "\"myhex\"")
        );

    // TODO: verify amounts / commitment sum

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR, UniValue::VBOOL}, true);

    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str(), true)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK2(cs_main, mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : mtx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    // Add previous txouts given in the RPC call:
    if (!request.params[1].isNull()) {
        UniValue prevTxs = request.params[1].get_array();
        for (unsigned int idx = 0; idx < prevTxs.size(); ++idx) {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject()) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");
            }

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut,
                            {
                                    {"txid", UniValueType(UniValue::VSTR)},
                                    {"vout", UniValueType(UniValue::VNUM)},
                                    {"scriptPubKey", UniValueType(UniValue::VSTR)},
                            });

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");
            }

            COutPoint out(txid, nOut);
            std::vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                const Coin& coin = view.AccessCoin(out);

                if (coin.nType != OUTPUT_STANDARD && coin.nType != OUTPUT_CT)
                    throw JSONRPCError(RPC_MISC_ERROR, strprintf("Bad input type: %d", coin.nType));

                if (!coin.IsSpent() && coin.out.scriptPubKey != scriptPubKey) {
                    std::string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coin.out.scriptPubKey) + "\nvs:\n"+
                          ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                Coin newcoin;
                newcoin.out.scriptPubKey = scriptPubKey;
                newcoin.out.nValue = 0;
                if (prevOut.exists("amount")) {
                    if (prevOut.exists("amount_commitment")) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Both \"amount\" and \"amount_commitment\" found.");
                    }
                    newcoin.nType = OUTPUT_STANDARD;
                    newcoin.out.nValue = AmountFromValue(find_value(prevOut, "amount"));
                } else if (prevOut.exists("amount_commitment")) {
                    std::string s = prevOut["amount_commitment"].get_str();
                    if (!IsHex(s) || !(s.size() == 66)) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "\"amount_commitment\" must be 33 bytes and hex encoded.");
                    }
                    std::vector<uint8_t> vchCommitment = ParseHex(s);
                    assert(vchCommitment.size() == 33);
                    memcpy(newcoin.commitment.data, vchCommitment.data(), 33);
                    newcoin.nType = OUTPUT_CT;
                } else {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "\"amount\" or \"amount_commitment\" is required");
                }

                newcoin.nHeight = 1;
                view.AddCoin(out, std::move(newcoin), true);
            }
        }
    }


    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mtx);

    // Script verification errors
    UniValue vErrors(UniValue::VARR);


    int nSpendHeight = 0; // TODO: make option
    {
        LOCK(cs_main);
        nSpendHeight = chainActive.Tip()->nHeight;
    }

    {
        CValidationState state;
        CAmount nFee = 0, nValueIn = 0, nValueOut = 0;
        if (!Consensus::CheckTxInputs(txConst, state, view, nSpendHeight, nFee, nValueIn, nValueOut)) {
            vErrors.push_back("CheckTxInputs: \"" + state.GetRejectReason() + "\"");
        }
    }

    // Sign what we can:
    for (unsigned int i = 0; i < mtx.vin.size(); i++) {
        CTxIn& txin = mtx.vin[i];
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            continue;
        }

        CScript prevPubKey = coin.out.scriptPubKey;

        std::vector<uint8_t> vchAmount;
        if (coin.nType == OUTPUT_STANDARD) {
            vchAmount.resize(8);
            memcpy(vchAmount.data(), &coin.out.nValue, 8);
        } else
        if (coin.nType == OUTPUT_CT) {
            vchAmount.resize(33);
            memcpy(vchAmount.data(), coin.commitment.data, 33);
        } else {
            throw JSONRPCError(RPC_MISC_ERROR, strprintf("Bad input type: %d", coin.nType));
        }

        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, &txin.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&txConst, i, vchAmount), &serror)) {
            //TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
        }
    }
    bool fComplete = vErrors.empty();

    UniValue result(UniValue::VOBJ);

    if (!request.params[2].isNull() && request.params[2].get_bool()) {
        UniValue txn(UniValue::VOBJ);
        TxToUniv(CTransaction(std::move(mtx)), uint256(), {{}}, txn, false);
        result.pushKV("txn", txn);
    }

    //result.pushKV("hex", EncodeHexTx(mtx));
    result.pushKV("complete", fComplete);
    if (!vErrors.empty()) {
        result.pushKV("errors", vErrors);
    }

    return result;
};


static UniValue importstealthaddress(const JSONRPCRequest &request)
{
    if (request.fHelp || request.params.size() < 2)
        throw std::runtime_error(
                "importstealthaddress \"scan_secret\" \"spend_secret\" \"label\" num_prefix_bits \"prefix_num\" \"bech32\" \n"
                "\nImport a stealth addresses. If spend_secret is valid privatekey it will be fully imported. If spend_secret is the spend pubkey it will be added as a watchonly address\n"
                "\nArguments:\n"
                "1. \"scan_secret\"                      (string, required) Scan secret key\n"
                "2. \"spend_secret\"                     (string, required) Spend secret key or Spend Public Key (watchonly)\n"
                "3. \"label\"                            (string, optional default=\"\") Label for address in addressbook\n"
                "4. \"num_prefix_bits\"                  (number, optional default=0) Number of prefix bits\n"
                "5. \"prefix_num\"                       (string, optional default=\"\") prefix_num\n"
                "6. bech32                               (bool, optional default=true) Is address bech32\n"
                + HelpExampleCli("importstealthaddress", "\"cQS2VU6R4CjsnrXn6jJWSvtzvrcjKHBB4mVSe17o4toJPPBqkbRm\" \"0362ae0f399a0a9f32c71f91ad75e009440c773ae663029acc486c4fb855c287a4\"")
                + HelpExampleRpc("importstealthaddress", "\"cQS2VU6R4CjsnrXn6jJWSvtzvrcjKHBB4mVSe17o4toJPPBqkbRm\" \"0362ae0f399a0a9f32c71f91ad75e009440c773ae663029acc486c4fb855c287a4\"")
        );

      std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
      if (!wallet) return NullUniValue;

      CWallet* const pwallet = wallet.get();
      auto anonwallet = pwallet->GetAnonWallet();

      if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
          throw JSONRPCError(RPC_WALLET_ERROR, "Error: Private keys are disabled for this wallet");
      }

      EnsureWalletIsUnlocked(pwallet);

      std::string sScanSecret  = request.params[0].get_str();
      std::string sLabel, sSpendSecret;

      if (request.params.size() > 1) {
          sSpendSecret = request.params[1].get_str();
      }
      if (request.params.size() > 2) {
          sLabel = request.params[2].get_str();
      }

      uint32_t num_prefix_bits = request.params.size() > 3 ? request.params[3].get_int() : 0;
      if (num_prefix_bits > 32) {
          throw JSONRPCError(RPC_INVALID_PARAMETER, "num_prefix_bits must be <= 32.");
      }

      uint32_t nPrefix = 0;
      std::string sPrefix_num;
      if (request.params.size() > 4) {
          sPrefix_num = request.params[4].get_str();
          if (!ExtractStealthPrefix(sPrefix_num.c_str(), nPrefix)) {
              throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not convert prefix to number.");
          }
      }

      bool fBech32 = request.params.size() > 5 ? request.params[5].get_bool() : true;

      std::vector<uint8_t> vchScanSecret, vchSpendSecret;
      CBitcoinSecret wifScanSecret, wifSpendSecret;
      CKey skScan, skSpend;
      CPubKey pkSpend;
      if (IsHex(sScanSecret)) {
          vchScanSecret = ParseHex(sScanSecret);
      } else
      if (wifScanSecret.SetString(sScanSecret)) {
          skScan = wifScanSecret.GetKey();
      } else {
          if (!DecodeBase58(sScanSecret, vchScanSecret, MAX_STEALTH_RAW_SIZE)) {
              throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not decode scan secret as WIF, hex or base58.");
          }
      }
      if (vchScanSecret.size() > 0) {
          if (vchScanSecret.size() != 32) {
              throw JSONRPCError(RPC_INVALID_PARAMETER, "Scan secret is not 32 bytes.");
          }
          skScan.Set(&vchScanSecret[0], true);
      }

      if (IsHex(sSpendSecret)) {
          vchSpendSecret = ParseHex(sSpendSecret);
      } else
      if (wifSpendSecret.SetString(sSpendSecret)) {
          skSpend = wifSpendSecret.GetKey();
      } else {
          if (!DecodeBase58(sSpendSecret, vchSpendSecret, MAX_STEALTH_RAW_SIZE)) {
              throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not decode spend secret as hex or base58.");
          }
      }
      if (vchSpendSecret.size() > 0) {
          if (vchSpendSecret.size() == 32) {
              skSpend.Set(&vchSpendSecret[0], true);
          } else
          if (vchSpendSecret.size() == 33) {
              // watchonly
              pkSpend = CPubKey(vchSpendSecret.begin(), vchSpendSecret.end());
          } else {
              throw JSONRPCError(RPC_INVALID_PARAMETER, "Spend secret is not 32 or 33 bytes.");
          }
      }

      if (!pkSpend.IsValid() && !skSpend.IsValid()) {
          throw JSONRPCError(RPC_INVALID_PARAMETER, "Must provide the spend key or pubkey.");
      }

      if (skSpend == skScan) {
          throw JSONRPCError(RPC_INVALID_PARAMETER, "Spend secret must be different to scan secret.");
      }

      CStealthAddress sxAddr;
      sxAddr.label = sLabel;
      sxAddr.scan_secret = skScan;
      if (skSpend.IsValid()) {
          pkSpend = skSpend.GetPubKey();
          sxAddr.spend_secret_id = pkSpend.GetID();
      } else {
          sxAddr.spend_secret_id = pkSpend.GetID();
      }

      sxAddr.prefix.number_bits = num_prefix_bits;
      if (sxAddr.prefix.number_bits > 0) {
          if (sPrefix_num.empty()) {
              // if pPrefix is null, set nPrefix from the hash of kSpend
              uint8_t tmp32[32];
              CSHA256().Write(skSpend.begin(), 32).Finalize(tmp32);
              memcpy(&nPrefix, tmp32, 4);
              nPrefix = le32toh(nPrefix);
          }

          uint32_t nMask = SetStealthMask(num_prefix_bits);
          nPrefix = nPrefix & nMask;
          sxAddr.prefix.bitfield = nPrefix;
      }

      if (0 != SecretToPublicKey(sxAddr.scan_secret, sxAddr.scan_pubkey)) {
          throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not get scan public key.");
      }
      if (skSpend.IsValid() && 0 != SecretToPublicKey(skSpend, sxAddr.spend_pubkey)) {
          throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not get spend public key.");
      } else {
          SetPublicKey(pkSpend, sxAddr.spend_pubkey);
      }

      UniValue result(UniValue::VOBJ);
      if (anonwallet->HaveStealthAddress(sxAddr)) { // check for extkeys, no update possible
          throw JSONRPCError(RPC_WALLET_ERROR, "Import failed - stealth address exists.");
      }

//      pwallet->SetAddressBook(sxAddr, sLabel, "", fBech32);

      if (!anonwallet->ImportStealthAddress(sxAddr, skSpend)) {
          throw JSONRPCError(RPC_WALLET_ERROR, "Could not save to wallet.");
      }
      result.pushKV("result", "Success");
      result.pushKV("stealth_address", sxAddr.ToString(fBech32));

      if (!skSpend.IsValid()) {
          result.pushKV("watchonly", true);
      }


      return result;
};


static UniValue getanonoutputs(const JSONRPCRequest &request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
                "getanonoutputs \"inputsize\" \"ringsize\" \n"
                "\nGet random valid ringct outputs based on inputsize and ringsize given\n"
                "\nArguments:\n"
                "1. \"inputsize\"                      (number, required) The number of inputs being spent in the transaction\n"
                "2. \"ringsize\"                       (number, required) The number of ring signatures per input in the transaction \n"
                + HelpExampleCli("getanonoutputs", "5 5")
                + HelpExampleRpc("getanonoutputs", "5 5")
        );

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;

    CWallet* const pwallet = wallet.get();
    auto anonwallet = pwallet->GetAnonWallet();

    if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Private keys are disabled for this wallet");
    }

    EnsureWalletIsUnlocked(pwallet);

    const int nInputSize = request.params[0].get_int();
    const int nRingSize = request.params[1].get_int();

    UniValue result(UniValue::VARR);

    std::set<int64_t> setIndexSelected;
    std::vector< std::pair<int64_t,CAnonOutput> > vecSelectedOutputs;
    std::string sError;
    if (anonwallet->GetRandomHidingOutputs(nInputSize, nRingSize, setIndexSelected, vecSelectedOutputs, sError)) {
        for (const auto& out : vecSelectedOutputs) {
            UniValue entry(UniValue::VOBJ);
            AnonOutputToJSON(out.second, out.first, entry);
            result.push_back(entry);
        }
    } else {
        throw JSONRPCError(RPC_WALLET_ERROR, sError);
    }

    return result;
};



static const CRPCCommand commands[] =
        { //  category              name                                actor (function)                argNames
                //  --------------------- ------------------------            -----------------------         ----------
                { "wallet",             "getnewaddress",             &getnewaddress,          {"label","num_prefix_bits","prefix_num","bech32","makeV2"} },
                { "wallet",             "restoreaddresses",          &restoreaddresses,          {"generate_count"} },
                { "wallet",             "rescanringctwallet",          &rescanringctwallet,          {} },
                { "wallet",             "getstealthchangeaddress",          &getstealthchangeaddress,          {} },
                { "wallet",             "sendbasecointostealth", &sendbasecointostealth,               {"address","amount","comment","comment_to","subtractfeefromamount","narration"} },

                { "wallet",             "sendstealthtobasecoin", &sendstealthtobasecoin,               {"address","amount","comment","comment_to","subtractfeefromamount","narration"} },
                { "wallet",             "sendstealthtostealth", &sendstealthtostealth,              {"address","amount","comment","comment_to","subtractfeefromamount","narration"} },
                { "wallet",             "sendstealthtoringct", &sendstealthtoringct,              {"address","amount","comment","comment_to","subtractfeefromamount","narration"} },

                { "wallet",             "sendringcttobasecoin", &sendringcttobasecoin,                {"address","amount","comment","comment_to","subtractfeefromamount","narration","ringsize","inputs_per_sig"} },
                { "wallet",             "sendringcttostealth", &sendringcttostealth,               {"address","amount","comment","comment_to","subtractfeefromamount","narration","ringsize","inputs_per_sig"} },
                { "wallet",             "sendringcttoringct", &sendringcttoringct,                {"address","amount","comment","comment_to","subtractfeefromamount","narration","ringsize","inputs_per_sig"} },

                { "wallet",             "sendtypeto",                       &sendtypeto,                    {"typein","typeout","outputs","comment","comment_to","ringsize","inputs_per_sig","test_fee","coincontrol"} },

                { "rawtransactions",    "createrawbasecointransaction", &createrawbasecointransaction,      {"inputs","outputs","locktime","replaceable"} },
                { "rawtransactions",    "fundrawtransactionfrom",           &fundrawtransactionfrom,        {"input_type","hexstring","input_amounts","output_amounts","options"} },
                { "rawtransactions",    "verifycommitment",                 &verifycommitment,              {"commitment","blind","amount"} },
                { "rawtransactions",    "verifyrawtransaction",             &verifyrawtransaction,          {"hexstring","prevtxs","returndecoded"} },


                { "wallet",             "importstealthaddress",          &importstealthaddress,          {"scan_secret", "spend_secret", "label", "num_prefix_bits", "prefix_num", "bech32"} },
                { "wallet",             "getanonoutputs",                &getanonoutputs,                {"inputsize", "ringsize"} },
                { "wallet",             "testbuildlightwallettransaction",                &testbuildlightwallettransaction,                {"scan_secret", "spend_secret", "spend_pubkey", "destination", "amount"} },
                { "wallet",             "runtest",                &runtest,                {} },

        };

void RegisterHDWalletRPCCommands(CRPCTable &t)
{
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        return;

    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
