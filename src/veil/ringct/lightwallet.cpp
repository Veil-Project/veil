//
// Created by main on 25/2/22.
//

#include "lightwallet.h"
#include <key.h>
#include <key_io.h>
#include <amount.h>
#include <util/strencodings.h>
#include <veil/ringct/watchonly.h>
#include <veil/ringct/temprecipient.h>
#include <secp256k1_mlsag.h>
#include <veil/ringct/blind.h>
#include <wallet/coincontrol.h>
#include <veil/ringct/anonwallet.h>
#include <policy/policy.h>
#include <core_io.h>


std::string TestBuildWalletTransaction(int nRandomInt) {

    std::string sSpendSecret = "cR6XNDL7dwDqAfBUNQqcoYkDbCy3xQwsFLh6tfw4EjBTUPaDaNdX";
    std::string sScanSecret = "cU72zZjUGEwp3uUdr1diuWmLbTUiv9zeThsNE5cVYFqZuySE8Gry";
    std::string sSpendPublic = "039cfeaef8697b6bb4e5a7af6b5b6bb0884c41d687cd71410e233447571e1dfeea";

    std::string toaddress = "";
    std::string sAmount = "1.26518";

    // Not even number = stealth
    if (nRandomInt % 2) {
        toaddress = "tps1qqpre9kkw83hdnnessdrx883gmhc4etnedpdet0dye004qzd57xm9ugpq250wfhh63rekdkldl4vtqrxzkkzzhnkggpm0hwk946pv3whqk49sqqqpxazhx";
        sAmount = "2.26518";
    } else {
        toaddress = "tv1qstjesjs08nys4u9gugkfx4u2qyhqx7k4yz74ax";
        sAmount = "1.26518";
    }

    std::vector<std::string> args;
    args.push_back(sSpendSecret);
    args.push_back(sScanSecret);
    args.push_back(sSpendPublic);
    args.push_back(toaddress);
    args.push_back(sAmount);


    std::string errorMsg = "";

    // Parse the arguments
    CKey spend_secret;
    CKey scan_secret;
    CPubKey spend_pubkey;
    CBitcoinAddress address;
    CAmount nValueOut;
    if (!ParseArgs(args, spend_secret, scan_secret, spend_pubkey, address, nValueOut, errorMsg)) {
        LogPrintf("Failed - %s\n", errorMsg);
        return errorMsg;
    }

    // Get Tx From API -
    std::vector<std::pair<int, CWatchOnlyTx>> vTxes;
    FetchWatchOnlyTransactions(scan_secret, vTxes);

    //Build correct vector
    std::vector<CWatchOnlyTx> myTxes;
    for(const auto& pair: vTxes){
        myTxes.push_back(pair.second);
    }

    // Get Keyimages
    std::vector<std::pair<CCmpPubKey, CWatchOnlyTx>> keyimages;
    if (!GetKeyImagesFromAPITransactions(keyimages, myTxes, spend_secret, scan_secret, spend_pubkey, errorMsg)) {
        LogPrintf("Failed - %s\n", errorMsg);
        return errorMsg;
    }

    // Check if key images are spent
    std::vector<CWatchOnlyTx> vUnspentTxes;
    for (const auto& pair : keyimages) {
        uint256 tx_hash;
        bool spent_in_chain = pblocktree->ReadRCTKeyImage(pair.first, tx_hash);
        if (!spent_in_chain) {
            vUnspentTxes.push_back(pair.second);
        }
    }

    // Get Amount and blinds of all spendable txes
    std::vector<CWatchOnlyTx> vFinalTx;
    if (!GetAmountAndBlindForUnspentTx(vFinalTx, vUnspentTxes, spend_secret, scan_secret, spend_pubkey,errorMsg)) {
        LogPrintf("Failed - %s\n", errorMsg);
        return errorMsg;
    }

    // Get Random outputs
    std::set<int64_t> setIndexSelected;
    std::vector<CLightWalletAnonOutputData> vecSelectedOutputs;
    std::string sError;
    GetMainWallet()->GetAnonWallet()->GetRandomHidingOutputs(11, 11, setIndexSelected,vecSelectedOutputs, errorMsg);

    std::string txHex = "";

    if (!BuildLightWalletTransaction(args, vFinalTx, vecSelectedOutputs, txHex, errorMsg)) {
        LogPrintf("Failed testing transaction");
    }

    return txHex;
}



/** Before you build the transaction, you must of already called
 * API - Get Transaction for the scan_secret
 * Light Node - Get the keyimages for the transactions -> GetKeyImagesFromAPITransactions
 * API - Check if Key Images are spent
 * Light Node - Get Amounts and blinds of all transactions that aren't spent -> GetAmountAndBlindForUnspentTx
 * Light Node - Check you have at least the send amount + 0.01 Veil as a fee
 * API - Fetch Valid Dummy Outputs
 */
// Args include, the following
// Spend Secret
// Scan Secret
// Spend Public Key
// Address to spend to
// Amount to spend
bool BuildLightWalletTransaction(const std::vector<std::string>& args, const std::vector<CWatchOnlyTx>& vSpendableTx, const std::vector<CLightWalletAnonOutputData>& vDummyOutputs, std::string& txHex, std::string& errorMsg)
{
    // Parse the arguments
    CKey spend_secret;
    CKey scan_secret;
    CPubKey spend_pubkey;
    CBitcoinAddress address;
    CAmount nValueOut;
    if (!ParseArgs(args, spend_secret, scan_secret, spend_pubkey, address, nValueOut, errorMsg)) {
        LogPrintf("Failed - %s\n", errorMsg);
        return false;
    }

    // Get the output types
    OutputTypes outputType;
    CTxDestination destination;

    if (!GetTypeOut(address, args[3], outputType, destination, errorMsg)){
        LogPrintf("Failed - %s\n", errorMsg);
        return false;
    }

    // Build the Output
    std::vector<CTempRecipient> vecSend;
    {
        CTempRecipient r;
        r.nType = outputType;
        r.SetAmount(nValueOut);
        r.fSubtractFeeFromAmount = false;
        r.address = destination;
        if (r.nType == OUTPUT_STANDARD) {
            r.fScriptSet = true;
            r.scriptPubKey = GetScriptForDestination(r.address);
        }

        vecSend.push_back(r);
    }

    std::vector<CWatchOnlyTx> vectorTxesWithAmountSet;
    if (!GetAmountAndBlindForUnspentTx(vectorTxesWithAmountSet, vSpendableTx, spend_secret, scan_secret, spend_pubkey, errorMsg)) {
        LogPrintf("Failed - GetAmountAndBlindForUnspentTx");
        return false;
    }


    if (!CheckAmounts(nValueOut, vectorTxesWithAmountSet)) {
        LogPrintf("Failed - amount is over the balance of this address");
        errorMsg = "Amount is over the balance of this address";
        return false;
    }

    // Default ringsize is 11
    int nRingSize = 5;
    int nInputsPerSig = nRingSize;

    // Get change address - this is the same address we are sending from
    CStealthAddress sxAddr;
    sxAddr.label = "";
    sxAddr.scan_secret = scan_secret;
    sxAddr.spend_secret_id = spend_pubkey.GetID();
    sxAddr.prefix.number_bits = 0;

    if (0 != SecretToPublicKey(sxAddr.scan_secret, sxAddr.scan_pubkey)) {
        LogPrintf("Failed - Could not get scan public key.");
        errorMsg = "Could not get scan public key.";
        return false;
    }

    if (spend_secret.IsValid() && 0 != SecretToPublicKey(spend_secret, sxAddr.spend_pubkey)) {
        LogPrintf("Failed - Could not get spend public key.");
        errorMsg = "Could not get spend public key.";
        return false;
    } else {
        SetPublicKey(spend_pubkey, sxAddr.spend_pubkey);
    }

    CBitcoinAddress addrChange;
    addrChange.Set(sxAddr, true);

    if (!addrChange.IsValidStealthAddress()) {
        LogPrintf("Invalid change address %s\n", addrChange.ToString());
        errorMsg = "Invalid change address";
        return false;
    }

    // TODO - if we can, remove coincontrol if we don't need to use it. bypass if we can
    // Set the change address in coincontrol
    CCoinControl coincontrol;
    coincontrol.destChange = addrChange.Get();

    // Check we are sending to atleast one address
    if (vecSend.size() < 1) {
        LogPrintf("Failed - Transaction must have at least one recipient.");
        errorMsg = "Transaction must have at least one recipient.";
        return false;
    }

    // Get total value we are sending in vecSend
    CAmount nValue = 0;
    for (const auto &r : vecSend) {
        nValue += r.nAmount;
        if (nValue < 0 || r.nAmount < 0) {
            LogPrintf("Transaction amounts must not be negative");
            errorMsg = "Transaction amounts must not be negative";
            return false;
        }
    }

    // Check ringsize
    if (nRingSize < 3 || nRingSize > 32) {
        LogPrintf("Ring size out of range.");
        errorMsg = "Ring size out of range.";
        return false;
    }

    // Check inputspersig
    if (nInputsPerSig < 1 || nInputsPerSig > 32) {
        LogPrintf("Num inputs per signature out of range.");
        errorMsg = "Num inputs per signature out of range.";
        return false;
    }

    // Build the recipient data
    if (!BuildRecipientData(vecSend, errorMsg)) {
        LogPrintf("Failed - %s\n", errorMsg);
        return false;
    }

    // Create tx object
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

    int nSubtractFeeFromAmount = 0;

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

    // Select tx to spend
    CAmount nTempChange;
    if (!SelectSpendableTxForValue(vSelectedTxes, nTempChange, nValueOut, vectorTxesWithAmountSet)) {
        LogPrintf("Failed - SelectSpendableTxForValue\n");
        errorMsg = "SelectSpendableTxForValue failure";
        return false;
    }

    // Build the change recipient
    const CAmount nChange = nTempChange;
    if (!BuildChangeData(vecSend, nChangePosInOut, nFeeRet, nChange, coincontrol.destChange, errorMsg)) {
        LogPrintf("Failed BuildChangeData - %s\n", errorMsg);
        return false;
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

    // Add CT DATA to txNew
    if (!LightWalletAddCTData(txNew, vecSend, nFeeRet, nValueOutPlain, nChangePosInOut, nSubtractFeeFromAmount, errorMsg)) {
        LogPrintf("Failed LightWalletAddCTData - %s\n", errorMsg);
        return false;
    }

    // Add in real outputs
    if (!LightWalletAddRealOutputs(txNew, vSelectedTxes, vInputBlinds, vSecretColumns, vMI, errorMsg)) {
        LogPrintf("Failed LightWalletAddCTData - %s\n", errorMsg);
        return false;
    }

    // Add in dummy outputs
    LightWalletFillInDummyOutputs(txNew,vDummyOutputs,vSecretColumns,vMI);

    // Get the amout of bytes
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
            LogPrintf("Failed Transaction fee and change calculation failed\n");
            errorMsg = "Failed Transaction fee and change calculation failed";
            return false;
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

    // Include more fee and try again.
    nFeeRet = nFeeNeeded;

    vSelectedWatchOnly = vSelectedTxes;

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
        if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, &plainCommitment, &vBlindPlain[0],
                                       (uint64_t) nValueOutPlain, secp256k1_generator_h)) {
            LogPrintf("Pedersen Commit failed for plain out.");
            errorMsg = "Pedersen Commit failed for plain out.";
            return false;
        }

        vpOutCommits.push_back(plainCommitment.data);
        vpOutBlinds.push_back(&vBlindPlain[0]);
    }

    if (!LightWalletUpdateChangeOutputCommitment(txNew, vecSend, nChangePosInOut, vpOutCommits, vpOutBlinds, errorMsg) ) {
        LogPrintf("Failed LightWalletUpdateChangeOutputCommitment - %s\n", errorMsg);
        return false;
    }

    //Add actual fee to CT Fee output
    std::vector<uint8_t> &vData = ((CTxOutData *) txNew.vpout[0].get())->vData;
    vData.resize(1);
    if (0 != PutVarInt(vData, nFeeRet)) {
        LogPrintf("Failed to add fee to transaction.\n");
        errorMsg = "Failed to add fee to transaction.";
        return false;
    }

    std::vector<CKey> vSplitCommitBlindingKeys(txNew.vin.size()); // input amount commitment when > 1 mlsag
    int nTotalInputs = 0;

    std::vector<std::pair<int64_t, CKey>> vSigningKeys;
    if (!LightWalletInsertKeyImages(txNew, vSigningKeys, vSelectedTxes, vSecretColumns, vMI, spend_pubkey, scan_secret, spend_secret, errorMsg)) {
        LogPrintf("Failed LightWalletInsertKeyImages - %s.\n", errorMsg);
        return false;
    }

    if (!LightWalletSignAndVerifyTx(txNew, vInputBlinds, vpOutCommits, vpOutBlinds, vSplitCommitBlindingKeys, vSigningKeys, vDummyOutputs, vSelectedTxes, vSecretColumns, vMI, errorMsg)) {
        LogPrintf("Failed LightWalletSignAndVerifyTx - %s.\n", errorMsg);
        return false;
    }

    CTransactionRef txRef = MakeTransactionRef(std::move(txNew));

    txHex = EncodeHexTx(*txRef);
    return true;
}


bool GetTypeOut(const CBitcoinAddress& address, const std::string& strAddress, OutputTypes& outputType, CTxDestination& destination, std::string& errorMsg)
{
    if (address.IsValidStealthAddress()) {
        outputType = OUTPUT_RINGCT;
        if (!address.IsValid()) {
            errorMsg = "Invalid stealth address";
            return false;
        }
        destination = address.Get();
    } else {
        outputType = OUTPUT_STANDARD;
        destination = DecodeDestination(strAddress);
        if (!IsValidDestination(destination)) {
            errorMsg = "Invalid basecoin address";
            return false;
        }
    }
    return true;
}

bool ParseArgs(const std::vector<std::string>& args, CKey& spend_secret, CKey& scan_secret, CPubKey& spend_pubkey, CBitcoinAddress& address, CAmount& nValue, std::string& errorMsg) {
    // Decode params 0
    GetSecretFromString(args[0], spend_secret);

    // Decode params 1
    GetSecretFromString(args[1], scan_secret);

    // Decode params 2
    GetPubkeyFromString(args[2], spend_pubkey);

    CBitcoinAddress temp_address(args[3]);
    address = temp_address;

    if (!ParseFixedPoint(args[4], 8, &nValue)) {
        errorMsg = "Invalid ParseFixedPoint arg nValue";
        return false;
    }

    return true;
}

bool GetDestinationKeyForOutput(CKey& destinationKey, const CWatchOnlyTx& tx, const CKey& spend_secret, const CKey& scan_secret, const CPubKey& spend_pubkey, std::string& errorMsg)
{
    if (tx.type == CWatchOnlyTx::ANON) {
        CKeyID idk = tx.ringctout.pk.GetID();

        std::vector<uint8_t> vchEphemPK;
        vchEphemPK.resize(33);
        memcpy(&vchEphemPK[0], &tx.ringctout.vData[0], 33);

        CKey sShared;
        ec_point pkExtracted;
        ec_point ecPubKey;
        SetPublicKey(spend_pubkey, ecPubKey);

        if (StealthSecret(scan_secret, vchEphemPK, ecPubKey, sShared, pkExtracted) != 0) {
            errorMsg = "StealthSecret failed to generate stealth secret";
            return false;
        }

        if (StealthSharedToSecretSpend(sShared, spend_secret, destinationKey) != 0) {
            errorMsg = "StealthSharedToSecretSpend failed";
            return false;
        }

        if (destinationKey.GetPubKey().GetID() != idk) {
            errorMsg = "GetDestinationKeyForOutput failed to generate correct shared secret";
            return false;
        }

        return true;
    } else if (tx.type == CWatchOnlyTx::STEALTH) {
        CKeyID id;
        if (!KeyIdFromScriptPubKey(tx.ctout.scriptPubKey, id))
            return error(" Stealth - Failed to get ID Key from Script.");

        CPubKey pkEphem;
        pkEphem.Set(tx.ctout.vData.begin(), tx.ctout.vData.begin() + 33);

        std::vector<uint8_t> vchEphemPK;
        vchEphemPK.resize(33);
        memcpy(&vchEphemPK[0], &tx.ctout.vData[0], 33);

        CKey sShared;
        ec_point pkExtracted;
        ec_point ecPubKey;
        SetPublicKey(spend_pubkey, ecPubKey);

        if (StealthSecret(scan_secret, vchEphemPK, ecPubKey, sShared, pkExtracted) != 0) {
            errorMsg = "Stealth - StealthSecret failed to generate stealth secret";
            return false;
        }

        CKey keyDestination;
        if (StealthSharedToSecretSpend(sShared, spend_secret, keyDestination) != 0) {
            errorMsg = "Stealth -  StealthSharedToSecretSpend failed";
            return false;
        }

        if (keyDestination.GetPubKey().GetID() != id) {
            errorMsg = "Stealth - GetDestinationKeyForOutput failed to generate correct shared secret";
            return false;
        }

        return true;
    }

    errorMsg = "WatchonlyTx Type not set to ANON or STEALTH";
    return false;
}


bool GetKeyImagesFromAPITransactions(std::vector<std::pair<CCmpPubKey, CWatchOnlyTx>>& keyimages, const std::vector<CWatchOnlyTx>& vectorWatchOnlyTxFromAPI, const CKey& spend_secret, const CKey& scan_secret, const CPubKey& spend_pubkey, std::string& errorMsg)
{
    for (int i = 0; i < vectorWatchOnlyTxFromAPI.size(); i++) {
        CWatchOnlyTx tx = vectorWatchOnlyTxFromAPI[i];

        CKey destinationKey;
        if (!GetDestinationKeyForOutput(destinationKey, tx, spend_secret, scan_secret, spend_pubkey, errorMsg)) {
            return false;
        }

        // Keyimage is required for the tx hash
        CCmpPubKey ki;
        if (secp256k1_get_keyimage(secp256k1_ctx_blind, ki.ncbegin(), tx.ringctout.pk.begin(),
                                   destinationKey.begin()) == 0) {
            keyimages.push_back(std::make_pair(ki, tx));
        } else {
            errorMsg = "Failed to get keyimage for watchonlytx";
            return false;
        }
    }

    return true;
}

bool GetAmountAndBlindForUnspentTx(std::vector<CWatchOnlyTx>& vTxes, const std::vector<CWatchOnlyTx>& vectorUnspentWatchonlyTx, const CKey& spend_secret, const CKey& scan_secret, const CPubKey& spend_pubkey, std::string& errorMsg)
{
    for (int i = 0; i < vectorUnspentWatchonlyTx.size(); i++) {

        CWatchOnlyTx currenttx = vectorUnspentWatchonlyTx[i];

        CKey destinationKey;
        if (!GetDestinationKeyForOutput(destinationKey, currenttx, spend_secret, scan_secret, spend_pubkey, errorMsg)) {
            return false;
        }

        std::vector<uint8_t> vchEphemPK;
        vchEphemPK.resize(33);
        memcpy(&vchEphemPK[0], &currenttx.ringctout.vData[0], 33);

        // Regenerate nonce
        CPubKey pkEphem;
        pkEphem.Set(vchEphemPK.begin(), vchEphemPK.begin() + 33);
        uint256 nonce = destinationKey.ECDH(pkEphem);
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
                                             &currenttx.ringctout.commitment, currenttx.ringctout.vRangeproof.data(), currenttx.ringctout.vRangeproof.size(),
                                             nullptr, 0,
                                             secp256k1_generator_h)) {
            errorMsg = "failed to get the amount";
            return false;
        }

        currenttx.blind = uint256();
        memcpy(currenttx.blind.begin(), blindOut, 32);
        currenttx.nAmount = amountOut;
        vTxes.emplace_back(currenttx);
    }
    return true;
}

bool CheckAmounts(const CAmount& nValueOut, const std::vector<CWatchOnlyTx>& vSpendableTx)
{
    CAmount nSum;
    for (const auto& tx : vSpendableTx) {
        LogPrintf("Getting amounts from inputs: %d\n", tx.nAmount);
        nSum += tx.nAmount;

        if ((nValueOut + CENT) < nSum ) {
            return true;
        }
    }

    return false;
}

bool BuildRecipientData(std::vector<CTempRecipient>& vecSend, std::string& errorMsg)
{
    for (CTempRecipient &r: vecSend) {
        if (r.nType == OUTPUT_STANDARD) {
            if (r.address.type() == typeid(CExtKeyPair)) {
                errorMsg = "sending to extkeypair";
                return false;
            } else if (r.address.type() == typeid(CKeyID)) {
                r.scriptPubKey = GetScriptForDestination(r.address);
            } else {
                if (!r.fScriptSet) {
                    r.scriptPubKey = GetScriptForDestination(r.address);
                    if (r.scriptPubKey.empty()) {
                        errorMsg = "Unknown address type and no script set.";
                        return false;
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
                    errorMsg = "Could not generate receiving public key";
                    return false;
                }

                r.pkTo = CPubKey(pkSendTo);
                CKeyID idTo = r.pkTo.GetID();

                if (sx.prefix.number_bits > 0) {
                    r.nStealthPrefix = FillStealthPrefix(sx.prefix.number_bits, sx.prefix.bitfield);
                }
            } else {
                errorMsg = "RINGCT Outputs - Only able to send to stealth address for now.";
                return false;
            }

            r.sEphem = sEphem;
        }
    }

    return true;
}

bool BuildChangeData(std::vector<CTempRecipient>& vecSend, int& nChangePositionOut, CAmount& nFeeReturned, const CAmount& nChange, CTxDestination& changeDestination, std::string& errorMsg)
{
    // Insert a sender-owned 0 value output that becomes the change output if needed
    // Fill an output to ourself
    CTempRecipient recipient;
    recipient.nType = OUTPUT_RINGCT;
    recipient.fChange = true;
    recipient.sEphem.MakeNewKey(true);

    recipient.address = changeDestination;

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

        if (k >= nTries) {
            errorMsg = "Could not generate receiving public key";
            return false;
        }

        CPubKey pkEphem = recipient.sEphem.GetPubKey();
        if (!pkEphem.IsValid()) {
            errorMsg = "Ephemeral pubkey is not valid";
            return false;
        }

        recipient.pkTo = CPubKey(pkSendTo);

        CKeyID idTo = recipient.pkTo.GetID();
        recipient.scriptPubKey = GetScriptForDestination(idTo);

    } else {
        errorMsg = "Change address wasn't of CStealthAddress Type";
        return false;
    }

    if (nChange > ::minRelayTxFee.GetFee(2048)) {
        recipient.SetAmount(nChange);
    } else {
        recipient.SetAmount(0);
        nFeeReturned += nChange;
    }

    if (nChangePositionOut < 0) {
        nChangePositionOut = GetRandInt(vecSend.size() + 1);
    } else {
        nChangePositionOut = std::min(nChangePositionOut, (int) vecSend.size());
    }

    if (nChangePositionOut < (int) vecSend.size() && vecSend[nChangePositionOut].nType == OUTPUT_DATA) {
        nChangePositionOut++;
    }

    vecSend.insert(vecSend.begin() + nChangePositionOut, recipient);

    return true;
}

bool SelectSpendableTxForValue(std::vector<CWatchOnlyTx>& vSpendTheseTx, CAmount& nChange, const CAmount& nValueOut, const std::vector<CWatchOnlyTx>& vSpendableTx)
{
    CAmount currentMinimumChange = 0;
    CAmount tempsingleamountchange = 0;
    CAmount tempmultipleamountchange = 0;

    bool fSingleInput = false;
    bool fMultipleInput = false;



    // TODO - this can be improved, but works for now
    for (const CWatchOnlyTx &tx : vSpendableTx) {
        LogPrintf("tx amounts %d, ", tx.nAmount);
        if (tx.nAmount > nValueOut) {
            tempsingleamountchange = tx.nAmount - nValueOut;
            if (tempsingleamountchange < currentMinimumChange || currentMinimumChange == 0) {
                vSpendTheseTx.clear();
                fSingleInput = true;
                vSpendTheseTx.emplace_back(tx);
                currentMinimumChange = tempsingleamountchange;
            }
        }
    }

    if (!fSingleInput) {
        vSpendTheseTx.clear();
        // We can use a single input for this transaction
        CAmount currentSelected = 0;
        for (const CWatchOnlyTx &tx : vSpendableTx) {
            currentSelected += tx.nAmount;
            vSpendTheseTx.emplace_back(tx);
            if (currentSelected > nValueOut) {
                tempmultipleamountchange = currentSelected - nValueOut;
                fMultipleInput = true;
                break;
            }
        }
    }

    LogPrintf("nValueOut %d, ", nValueOut);

    if (fSingleInput) {
        nChange = tempsingleamountchange;
    } else if (fMultipleInput) {
        nChange = tempmultipleamountchange;
    } else {
        return false;
    }

    return true;
}

bool LightWalletAddCTData(CMutableTransaction& txNew, std::vector<CTempRecipient>& vecSend, CAmount& nFeeReturned, CAmount& nValueOutPlain, int& nChangePositionOut, int& nSubtractFeeFromAmount, std::string& errorMsg)
{
    bool fFirst = true;
    for (size_t i = 0; i < vecSend.size(); ++i) {
        auto &recipient = vecSend[i];

        // TODO - do we need this if fSubtractFeeFromAmount is never true? Keep this as we might enable that feature later
        // Only need to worry about this if fSubtractFeeFromAmount is true, which it isn't
        recipient.ApplySubFee(nFeeReturned, nSubtractFeeFromAmount, fFirst);

        OUTPUT_PTR<CTxOutBase> txbout;
        std::string sError;
        if (CreateOutput(txbout, recipient, sError) != 0) {
            errorMsg = "Failed to CreateOutput";
            return false;
        }

        if (recipient.nType == OUTPUT_STANDARD) {
            nValueOutPlain += recipient.nAmount;
        }

        if (recipient.fChange && recipient.nType == OUTPUT_RINGCT) {
            nChangePositionOut = i;
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
                    errorMsg = "Unable to get CT pointers for output type";
                    return false;
                }

                uint64_t nValue = recipient.nAmount;
                if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, pCommitment, (uint8_t *) &recipient.vBlind[0],
                                               nValue, secp256k1_generator_h)) {
                    errorMsg = "Pedersen commit failed";
                    return false;
                }

                uint256 nonce;
                if (recipient.fNonceSet) {
                    nonce = recipient.nonce;
                } else {
                    if (!recipient.sEphem.IsValid()) {
                        errorMsg = "Invalid ephemeral key.";
                        return false;
                    }
                    if (!recipient.pkTo.IsValid()) {
                        errorMsg = "Invalid recipient public key.";
                        return false;
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
                    errorMsg = "Failed to select range proof parameters.";
                    return false;
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
                    errorMsg = "Failed to sign range proof.";
                    return false;
                }

                pvRangeproof->resize(nRangeProofLen);
            }
        }
    }

    return true;
}


bool LightWalletAddRealOutputs(CMutableTransaction& txNew, std::vector<CWatchOnlyTx>& vSelectedTxes, std::vector<std::vector<uint8_t>>& vInputBlinds, std::vector<size_t>& vSecretColumns, std::vector<std::vector<std::vector<int64_t>>>& vMI, std::string& errorMsg)
{
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
            if (nSigRingSize < 3 || nSigRingSize > 32) {
                errorMsg = "Ring size out of range";
                return false;
            }

            vSecretColumns[l] = GetRandInt(nSigRingSize);

            vMI[l].resize(currentSize);

            for (size_t k = 0; k < vSelectedTxes.size(); ++k) {
                vMI[l][k].resize(nSigRingSize);
                for (size_t i = 0; i < nSigRingSize; ++i) {
                    if (i == vSecretColumns[l]) {
                        const auto &coin = vSelectedTxes[k];
                        const uint256 &txhash = vSelectedTxes[k].tx_hash;

                        CCmpPubKey pk = vSelectedTxes[k].ringctout.pk;
                        memcpy(&vInputBlinds[l][k * 32], &vSelectedTxes[k].blind, 32);

                        int64_t index = vSelectedTxes[k].ringctIndex;

                        if (setHave.count(index)) {
                            errorMsg = "Duplicate index found";
                            return false;
                        }

                        vMI[l][k][i] = index;
                        setHave.insert(index);
                    }
                }
            }
        }
    }
    return true;
}


void LightWalletFillInDummyOutputs(CMutableTransaction& txNew, const std::vector<CLightWalletAnonOutputData>& vDummyOutputs, std::vector<size_t>& vSecretColumns, std::vector<std::vector<std::vector<int64_t>>>& vMI)
{
    // Fill in dummy signatures for fee calculation.
    for (size_t l = 0; l < txNew.vin.size(); ++l) {
        auto &txin = txNew.vin[l];
        uint32_t nSigInputs, nSigRingSize;
        txin.GetAnonInfo(nSigInputs, nSigRingSize);

        // Place Hiding Outputs
        {
            int nCurrentLocation = 0;
            for (size_t k = 0; k < vMI[l].size(); ++k) {
                for (size_t i = 0; i < nSigRingSize; ++i) {
                    if (i == vSecretColumns[l]) {
                        continue;
                    }

                    LogPrintf("looking at vector index :%d, setting index for dummy: %d\n",nCurrentLocation, vDummyOutputs[nCurrentLocation].index);
                    vMI[l][k][i] = vDummyOutputs[nCurrentLocation].index;
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
}


bool LightWalletUpdateChangeOutputCommitment(CMutableTransaction& txNew, std::vector<CTempRecipient>& vecSend, int& nChangePositionOut, std::vector<const uint8_t *>& vpOutCommits, std::vector<const uint8_t *>& vpOutBlinds, std::string& errorMsg)
{
    // Update the change output commitment
    for (size_t i = 0; i < vecSend.size(); ++i) {
        auto &r = vecSend[i];

        if ((int) i == nChangePositionOut) {
            // Change amount may have changed

            if (r.nType != OUTPUT_RINGCT) {
                errorMsg = "Change output is not RingCT type.";
                return false;
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
                    errorMsg = "Unable to get CT pointers for output type";
                    return false;
                }

                uint64_t nValue = r.nAmount;
                if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, pCommitment, (uint8_t *) &r.vBlind[0], nValue,
                                               secp256k1_generator_h)) {
                    errorMsg = "Pedersen commit failed.";
                    return false;
                }

                uint256 nonce;
                if (r.fNonceSet) {
                    nonce = r.nonce;
                } else {
                    if (!r.sEphem.IsValid()) {
                        errorMsg = "Invalid ephemeral key";
                        return false;
                    }
                    if (!r.pkTo.IsValid()) {
                        errorMsg = "Invalid recipient public key";
                        return false;
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
                    errorMsg = "Failed to select range proof parameters.";
                    return false;
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
                    errorMsg = "Failed to sign range proof";
                    return false;
                }

                pvRangeproof->resize(nRangeProofLen);
            }
        }

        if (r.nType == OUTPUT_CT || r.nType == OUTPUT_RINGCT) {
            vpOutCommits.push_back(txNew.vpout[r.n]->GetPCommitment()->data);
            vpOutBlinds.push_back(&r.vBlind[0]);
        }
    }

    return true;
}


bool LightWalletInsertKeyImages(CMutableTransaction& txNew, std::vector<std::pair<int64_t, CKey>>& vSigningKeys, const std::vector<CWatchOnlyTx>& vSelectedTxes, const std::vector<size_t>& vSecretColumns, const std::vector<std::vector<std::vector<int64_t>>>& vMI, const CPubKey& spend_pubkey, const CKey& scan_secret, const CKey& spend_secret, std::string& errorMsg)
{
    int rv;
    for (size_t l = 0; l < txNew.vin.size(); ++l) {
        auto &txin = txNew.vin[l];

        uint32_t nSigInputs, nSigRingSize;
        txin.GetAnonInfo(nSigInputs, nSigRingSize);

        std::vector<uint8_t> &vKeyImages = txin.scriptData.stack[0];
        vKeyImages.resize(33 * nSigInputs);

        for (size_t k = 0; k < nSigInputs; ++k) {
            size_t i = vSecretColumns[l];
            int64_t nIndex = vMI[l][k][i];

            std::vector<uint8_t> vchEphemPK;
            CWatchOnlyTx foundTx;

            for (const CWatchOnlyTx& tx : vSelectedTxes) {
                if (tx.ringctIndex == nIndex) {
                    vchEphemPK.resize(33);
                    memcpy(&vchEphemPK[0], &tx.ringctout.vData[0], 33);
                    LogPrintf("Found the correct outpoint to generate vchEphemPK for index %d\n", nIndex);
                    foundTx = tx;
                    break;
                }
            }

            CKey sShared;
            ec_point pkExtracted;
            ec_point ecPubKey;
            SetPublicKey(spend_pubkey, ecPubKey);

            CKey keyDestination;

            if (!GetDestinationKeyForOutput(keyDestination, foundTx, spend_secret, scan_secret, spend_pubkey, errorMsg)) {
                return false;
            }

            vSigningKeys.emplace_back(foundTx.ringctIndex, keyDestination);

            // Keyimage is required for the tx hash
            rv = secp256k1_get_keyimage(secp256k1_ctx_blind, &vKeyImages[k * 33], foundTx.ringctout.pk.begin(), keyDestination.begin());
            if (0 != rv) {
                errorMsg = "Failed to get keyimage";
                return false;
            }
        }
    }

    return true;
}


bool LightWalletSignAndVerifyTx(CMutableTransaction& txNew, std::vector<std::vector<uint8_t>>& vInputBlinds, std::vector<const uint8_t *>& vpOutCommits, std::vector<const uint8_t *>& vpOutBlinds, std::vector<CKey>& vSplitCommitBlindingKeys, const std::vector<std::pair<int64_t, CKey>>& vSigningKeys, const std::vector<CLightWalletAnonOutputData>& vDummyOutputs, const std::vector<CWatchOnlyTx>& vSelectedTx, const std::vector<size_t>& vSecretColumns, const std::vector<std::vector<std::vector<int64_t>>>& vMI, std::string& errorMsg)
{
    size_t nTotalInputs = 0;
    int rv;

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

        LogPrintf("nSigInputs %d , nCols %d\n",nSigInputs, nCols);
        for (size_t k = 0; k < nSigInputs; ++k) {
            for (size_t i = 0; i < nCols; ++i) {
                int64_t nIndex = vMI[l][k][i];

                // Actual output
                if (i == vSecretColumns[l]) {
                    bool fFoundKey = false;
                    for (const auto& pair: vSigningKeys) {
                        if (pair.first == nIndex) {
                            vsk[k] = pair.second;
                            fFoundKey = true;
                            break;
                        }
                    }

                    if (!fFoundKey) {
                        errorMsg = "No key for index";
                        return false;
                    }

                    vpsk[k] = vsk[k].begin();

                    vpBlinds.push_back(&vInputBlinds[l][k * 32]);

                    bool fFound = false;
                    for (const CWatchOnlyTx& tx : vSelectedTx) {
                        if (tx.ringctIndex == nIndex) {
                            fFound = true;
                            memcpy(&vm[(i + k * nCols) * 33], tx.ringctout.pk.begin(), 33);
                            vCommitments.push_back(tx.ringctout.commitment);
                            vpInCommits[i + k * nCols] = vCommitments.back().data;
                            break;
                        }
                    }

                    if (!fFound) {
                        errorMsg = "No pubkey found for real output";
                        return false;
                    }
                } else {
                    bool fFound = false;
                    for (const auto& item : vDummyOutputs) {
                        if (item.index == nIndex) {
                            fFound = true;
                            memcpy(&vm[(i + k * nCols) * 33], item.output.pubkey.begin(), 33);
                            vCommitments.push_back(item.output.commitment);
                            vpInCommits[i + k * nCols] = vCommitments.back().data;
                            break;
                        }
                    }

                    if (!fFound) {
                        LogPrintf("Couldn't find dummy index for nIndex=%d\n", nIndex);
                        errorMsg = "No pubkey found for dummy output";
                        return false;
                    }
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
                errorMsg = "Failed to prepare mlsag";
                return false;
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
                    errorMsg ="Pedersen blind sum failed.";
                    return false;
                }
            } else {
                vSplitCommitBlindingKeys[l].MakeNewKey(true);
            }

            CAmount nCommitValue = 0;
            for (size_t k = 0; k < vSelectedTx.size(); k++) {
                nCommitValue += vSelectedTx[k].nAmount;
            }

            nTotalInputs += nSigInputs;

            secp256k1_pedersen_commitment splitInputCommit;
            if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, &splitInputCommit,
                                           (uint8_t *) vSplitCommitBlindingKeys[l].begin(),
                                           nCommitValue, secp256k1_generator_h)) {
                errorMsg ="Pedersen commit failed.";
                return false;
            }

            memcpy(&vDL[(1 + (nSigInputs + 1) * nSigRingSize) * 32], splitInputCommit.data, 33);
            vpBlinds.emplace_back(vSplitCommitBlindingKeys[l].begin());

            const uint8_t *pSplitCommit = splitInputCommit.data;
            if (0 != (rv = secp256k1_prepare_mlsag(&vm[0], blindSum, 1, 1, nCols, nRows,
                                                   &vpInCommits[0], &pSplitCommit, &vpBlinds[0]))) {
                errorMsg ="Failed to prepare mlsag with";
                return false;
            }

            vpBlinds.pop_back();
        };

        uint256 hashOutputs = txNew.GetOutputsHash();
        if (0 != (rv = secp256k1_generate_mlsag(secp256k1_ctx_blind, &vKeyImages[0], &vDL[0], &vDL[32],
                                                randSeed, hashOutputs.begin(), nCols, nRows, vSecretColumns[l],
                                                &vpsk[0], &vm[0]))) {
            errorMsg ="Failed to generate mlsag with";
            return false;
        }

        // Validate the mlsag
        if (0 != (rv = secp256k1_verify_mlsag(secp256k1_ctx_blind, hashOutputs.begin(), nCols,
                                              nRows, &vm[0], &vKeyImages[0], &vDL[0], &vDL[32]))) {
            errorMsg ="Failed to generate mlsag on initial generation";
            return false;
        }
    }

    return true;
}








