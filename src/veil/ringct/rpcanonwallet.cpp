// Copyright (c) 2017-2019 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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
#include <util.h>
#include <txdb.h>
#include <veil/ringct/blind.h>
#include <veil/ringct/anon.h>
#include <utilmoneystr.h>
#include <veil/ringct/anonwallet.h>
#include <veil/ringct/anonwalletdb.h>
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
#include <chainparams.h>
#include <veil/mnemonic/mnemonic.h>
#include <crypto/sha256.h>
#include <warnings.h>

#include <univalue.h>
#include <stdint.h>


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
                "Rescans all transactions in the ringct wallet (CT and RingCT transactions)"
                + HelpRequiringPassphrase(wallet.get()));

    EnsureWalletIsUnlocked(wallet.get());
    auto pAnonWallet = wallet->GetAnonWallet();
    LOCK2(cs_main, wallet->cs_wallet);

    pAnonWallet->RescanWallet();
    return NullUniValue;
}

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

    CAmount nTotal = 0;

    std::vector<CTempRecipient> vecSend;
    std::string sError;

    size_t nRingSizeOfs = 6;
    size_t nTestFeeOfs = 99;
    size_t nCoinControlOfs = 99;

    if (request.params[0].isArray()) {
        const UniValue &outputs = request.params[0].get_array();

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

            bool fSubtractFeeFromAmount = false;
            if (obj.exists("subfee")) {
                fSubtractFeeFromAmount = obj["subfee"].get_bool();
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

        bool fSubtractFeeFromAmount = false;
        if (request.params.size() > 4) {
            fSubtractFeeFromAmount = request.params[4].get_bool();
        }

        if (0 != AddOutput(typeOut, vecSend, dest, nAmount, fSubtractFeeFromAmount, sError)) {
            throw JSONRPCError(RPC_MISC_ERROR, strprintf("AddOutput failed: %s.", sError));
        }
    }

    switch (typeIn) {
        case OUTPUT_STANDARD:
            if (nTotal > wallet->GetBalance()) {
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

    CTransactionRef tx_new;
    CWalletTx wtx(wallet.get(), tx_new);
    CTransactionRecord rtx;

    CAmount nFeeRet = 0;
    switch (typeIn) {
        case OUTPUT_STANDARD:
        {
            if (0 !=
                pwalletAnon->AddStandardInputs(wtx, rtx, vecSend, !fCheckFeeOnly, nFeeRet, &coincontrol, sError, false, 0))
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("AddStandardInputs failed: %s.", sError));
            break;
        }
        case OUTPUT_CT:
            if (0 != pwalletAnon->AddBlindedInputs(wtx, rtx, vecSend, !fCheckFeeOnly, nFeeRet, &coincontrol, sError))
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("AddBlindedInputs failed: %s.", sError));
            break;
        case OUTPUT_RINGCT:
            if (0 != pwalletAnon->AddAnonInputs(wtx, rtx, vecSend, !fCheckFeeOnly, nRingSize, nInputsPerSig, nFeeRet, &coincontrol, sError))
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("AddAnonInputs failed: %s.", sError));
            break;
        default:
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Unknown input type: %d.", typeIn));
    }

    UniValue result(UniValue::VOBJ);
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
        if (fCheckFeeOnly) {
            return result;
        }
    }

    CValidationState state;
    CReserveKey reservekey(wallet.get());
   // if (typeIn == OUTPUT_STANDARD && typeOut == OUTPUT_STANDARD) {
        if (!wallet->CommitTransaction(wtx.tx, wtx.mapValue, wtx.vOrderForm, &reservekey, g_connman.get(), state)) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Transaction commit failed: %s", FormatStateMessage(state)));
        }
    //} else {
      //  if (!wallet->CommitTransaction(wtx, rtx, reservekey, g_connman.get(), state)) {
        //    throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Transaction commit failed: %s", FormatStateMessage(state)));
        //}
    //}

    /*
    UniValue vErrors(UniValue::VARR);
    if (!state.IsValid()) // Should be caught in CommitTransaction
    {
        // This can happen if the mempool rejected the transaction.  Report
        // what happened in the "errors" response.
        vErrors.push_back(strprintf("Error: The transaction was rejected: %s", FormatStateMessage(state)));

        UniValue result(UniValue::VOBJ);
        result.pushKV("txid", wtx.GetHash().GetHex());
        result.pushKV("errors", vErrors);
        return result;
    };
    */

    //pwalletAnon->PostProcessTempRecipients(vecSend);

    if (fShowFee) {
        result.pushKV("txid", wtx.GetHash().GetHex());
        return result;
    }

    return wtx.GetHash().GetHex();
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
                "8. inputs_per_sig  (int, optional, default=32).\n";

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
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 6)
        throw std::runtime_error(SendHelp(wallet, OUTPUT_CT, OUTPUT_STANDARD));

    return SendToInner(request, OUTPUT_CT, OUTPUT_STANDARD);
};

static UniValue sendstealthtostealth(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 6)
        throw std::runtime_error(SendHelp(wallet, OUTPUT_CT, OUTPUT_CT));

    return SendToInner(request, OUTPUT_CT, OUTPUT_CT);
};

static UniValue sendstealthtoringct(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 6)
        throw std::runtime_error(SendHelp(wallet, OUTPUT_CT, OUTPUT_RINGCT));

    return SendToInner(request, OUTPUT_CT, OUTPUT_RINGCT);
};


static UniValue sendringcttobasecoin(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 8)
        throw std::runtime_error(SendHelp(wallet, OUTPUT_RINGCT, OUTPUT_STANDARD));

    return SendToInner(request, OUTPUT_RINGCT, OUTPUT_STANDARD);
}

static UniValue sendringcttostealth(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 8)
        throw std::runtime_error(SendHelp(wallet, OUTPUT_RINGCT, OUTPUT_CT));

    return SendToInner(request, OUTPUT_RINGCT, OUTPUT_CT);
}

static UniValue sendringcttoringct(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 8)
        throw std::runtime_error(SendHelp(wallet, OUTPUT_RINGCT, OUTPUT_RINGCT));

    return SendToInner(request, OUTPUT_RINGCT, OUTPUT_RINGCT);
}

UniValue sendtypeto(const JSONRPCRequest &request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(wallet.get(), request.fHelp))
        return NullUniValue;
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 9)
        throw std::runtime_error(
                "sendtypeto \"typein\" \"typeout\" [{address: , amount: , narr: , subfee:},...] (\"comment\" \"comment-to\" ringsize inputs_per_sig test_fee coin_control)\n"
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
            if (0 != pAnonWallet->AddCTData(txbout.get(), r, sError)) {
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
            //if (0 != pwallet->AddAnonInputs(wtx, rtx, vecSend, false, nFee, &coinControl, sError))
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("AddAnonInputs failed: %s.", sError));
        } else if (sInputType == "blind") {
            if (0 != pAnonWallet->AddBlindedInputs(wtx, rtx, vecSend, false, nFee, &coinControl, sError)) {
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
        TxToUniv(CTransaction(std::move(mtx)), uint256(), txn, false);
        result.pushKV("txn", txn);
    }

    //result.pushKV("hex", EncodeHexTx(mtx));
    result.pushKV("complete", fComplete);
    if (!vErrors.empty()) {
        result.pushKV("errors", vErrors);
    }

    return result;
};

static const CRPCCommand commands[] =
        { //  category              name                                actor (function)                argNames
                //  --------------------- ------------------------            -----------------------         ----------
                { "wallet",             "getnewaddress",             &getnewaddress,          {"label","num_prefix_bits","prefix_num","bech32","makeV2"} },
                { "wallet",             "restoreaddresses",          &restoreaddresses,          {"generate_count"} },
                { "wallet",             "rescanringctwallet",          &rescanringctwallet,          {} },

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
        };

void RegisterHDWalletRPCCommands(CRPCTable &t)
{
    if (gArgs.GetBoolArg("-disablewallet", false))
        return;

    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
