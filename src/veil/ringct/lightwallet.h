//
// Created by main on 25/2/22.
//

#ifndef VEIL_LIGHTWALLET_H
#define VEIL_LIGHTWALLET_H

#include <vector>
#include <string>
#include <primitives/transaction.h>
#include <script/standard.h>

class CBitcoinAddress;
class CKey;
class CPubKey;
class CWatchOnlyTx;
class CTempRecipient;
class CAnonOutput;
class CLightWalletAnonOutputData;

// For testing purposes only
std::string TestBuildWalletTransaction(int nRandomInt);
// TEST ONLY ^^

// Args include, the following
// Spend Secret
// Scan Secret
// Spend Public Key
// Address to spend to
// Amount to spend
bool BuildLightWalletTransaction(const std::vector<std::string>& args, const std::vector<CWatchOnlyTx>& vSpendableTx, const std::vector<CLightWalletAnonOutputData>& vDummyOutputs, std::string& txHex, std::string& errorMsg);
bool BuildLightWalletRingCTTransaction(const std::vector<std::string>& args, const std::vector<CWatchOnlyTx>& vSpendableTx, const std::vector<CLightWalletAnonOutputData>& vDummyOutputs, std::string& txHex, std::string& errorMsg);
bool BuildLightWalletStealthTransaction(const std::vector<std::string>& args, const std::vector<CWatchOnlyTx>& vSpendableTx, std::string& txHex, std::string& errorMsg);
bool ParseArgs(const std::vector<std::string>& args, CKey& spend_secret, CKey& scan_secret, CPubKey& spend_pubkey, CBitcoinAddress& address, CAmount& nValue, std::string& errorMsg);

bool GetTypeOut(const CBitcoinAddress& address, const std::string& strAddress, OutputTypes& outputType, CTxDestination& destination, std::string& errorMsg);

bool GetDestinationKeyForOutput(CKey& destinationKey, const CWatchOnlyTx& tx, const CKey& spend_secret, const CKey& scan_secret, const CPubKey& spend_pubkey, std::string& errorMsg);

bool GetKeyImagesFromAPITransactions(std::vector<std::pair<CCmpPubKey, CWatchOnlyTx>>& keyimages, const std::vector<CWatchOnlyTx>& vectorWatchOnlyTxFromAPI, const CKey& spend_secret, const CKey& scan_secret, const CPubKey& spend_pubkey, std::string& errorMsg);
bool GetAmountAndBlindForUnspentTx(std::vector<CWatchOnlyTx>& vTxes, const std::vector<CWatchOnlyTx>& vectorUnspentWatchonlyTx, const CKey& spend_secret, const CKey& scan_secret, const CPubKey& spend_pubkey, std::string& errorMsg);

bool CheckAmounts(const CAmount& nValueOut, const std::vector<CWatchOnlyTx>& vSpendableTx);

bool BuildRecipientData(std::vector<CTempRecipient>& vecSend, std::string& errorMsg);
bool BuildChangeData(std::vector<CTempRecipient>& vecSend, int& nChangePositionOut, CAmount& nFeeReturned, const CAmount& nChange, CTxDestination& changeDestination, std::string& errorMsg);

bool SelectSpendableTxForValue(std::vector<CWatchOnlyTx>& vSpendTheseTx, CAmount& nChange, const CAmount& nValueOut, const std::vector<CWatchOnlyTx>& vSpendableTx);

bool LightWalletAddCTData(CMutableTransaction& txNew, std::vector<CTempRecipient>& vecSend, CAmount& nFeeReturned, CAmount& nValueOutPlain, int& nChangePositionOut, int& nSubtractFeeFromAmount, std::string& errorMsg);

bool LightWalletAddRealOutputs(CMutableTransaction& txNew, std::vector<CWatchOnlyTx>& vSelectedTxes, std::vector<std::vector<uint8_t>>& vInputBlinds, std::vector<size_t>& vSecretColumns, std::vector<std::vector<std::vector<int64_t>>>& vMI, std::string& errorMsg);

void LightWalletFillInDummyOutputs(CMutableTransaction& txNew, const std::vector<CLightWalletAnonOutputData>& vDummyOutputs, std::vector<size_t>& vSecretColumns, std::vector<std::vector<std::vector<int64_t>>>& vMI);

bool LightWalletUpdateChangeOutputCommitment(CMutableTransaction& txNew, std::vector<CTempRecipient>& vecSend, int& nChangePositionOut, std::vector<const uint8_t *>& vpOutCommits, std::vector<const uint8_t *>& vpOutBlinds, std::string& errorMsg);

bool LightWalletInsertKeyImages(CMutableTransaction& txNew, std::vector<std::pair<int64_t, CKey>>& vSigningKeys, const std::vector<CWatchOnlyTx>& vSelectedTxes, const std::vector<size_t>& vSecretColumns, const std::vector<std::vector<std::vector<int64_t>>>& vMI, const CPubKey& spend_pubkey, const CKey& scan_secret, const CKey& spend_secret, std::string& errorMsg);


bool LightWalletSignAndVerifyTx(CMutableTransaction& txNew, std::vector<std::vector<uint8_t>>& vInputBlinds, std::vector<const uint8_t *>& vpOutCommits, std::vector<const uint8_t *>& vpOutBlinds, std::vector<CKey>& vSplitCommitBlindingKeys, const std::vector<std::pair<int64_t, CKey>>& vSigningKeys, const std::vector<CLightWalletAnonOutputData>& vDummyOutputs, const std::vector<CWatchOnlyTx>& vSelectedTx, const std::vector<size_t>& vSecretColumns, const std::vector<std::vector<std::vector<int64_t>>>& vMI, std::string& errorMsg);

#endif //VEIL_LIGHTWALLET_H
