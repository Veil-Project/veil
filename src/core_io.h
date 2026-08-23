// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CORE_IO_H
#define BITCOIN_CORE_IO_H

#include <amount.h>
#include <uint256.h>

#include <string>
#include <vector>

class CBlock;
class COutPoint;
class CScript;
class CTransaction;
struct CMutableTransaction;
struct PartiallySignedTransaction;
class uint256;
class UniValue;
class CTxOutBase;
class CTxOutRingCT;
class CTxOutCT;
class CAnonOutput;

/** A ring member referenced by a RingCT input, resolved from the global
 *  RingCT output index. Real spends and decoys are indistinguishable. */
struct RingCtInputMember
{
    uint32_t nInput = 0;                  //!< which real input's ring this member belongs to (0-based)
    int64_t nRingCtIndex = 0;             //!< global RingCT output index
    uint256 txhash;                       //!< transaction the member output was created in
    uint32_t n = 0;                       //!< output position within that transaction
    std::vector<uint8_t> vchPubkey;       //!< 33-byte one-time output pubkey
    std::vector<uint8_t> vchCommitment;   //!< 33-byte Pedersen value commitment
};

// core_read.cpp
CScript ParseScript(const std::string& s);
std::string ScriptToAsmStr(const CScript& script, const bool fAttemptSighashDecode = false);
bool DecodeHexTx(CMutableTransaction& tx, const std::string& hex_tx, bool try_no_witness = false, bool try_witness = true);
bool DecodeHexBlk(CBlock&, const std::string& strHexBlk);
uint256 ParseHashStr(const std::string&, const std::string& strName);
std::vector<unsigned char> ParseHexUV(const UniValue& v, const std::string& strName);
bool DecodePSBT(PartiallySignedTransaction& psbt, const std::string& base64_tx, std::string& error);
int ParseSighashString(const UniValue& sighash);

// core_write.cpp
UniValue ValueFromAmount(const CAmount& amount);
std::string FormatScript(const CScript& script);
std::string EncodeHexTx(const CTransaction& tx, const int serializeFlags = 0);
std::string SighashToStr(unsigned char sighash_type);
void ScriptPubKeyToUniv(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);
void ScriptToUniv(const CScript& script, UniValue& out, bool include_address);
void TxToUniv(const CTransaction& tx, const uint256& hashBlock, const std::vector<std::vector<RingCtInputMember>>& vTxRingCtInputs, UniValue& entry, bool include_hex = true, int serialize_flags = 0);

void OutputToJSON(const uint256 &txid, const int& i,const CTxOutBase *baseOut, UniValue &entry, bool isCoinBase = false);
void RingCTOutputToJSON(const uint256& txid, const int& i, const int64_t& ringctIndex, const CTxOutRingCT& ringctOut, UniValue &entry);
void CTOutputToJSON(const uint256& txid, const int& i, const CTxOutCT& ctOut, UniValue &entry);
void AnonOutputToJSON(const CAnonOutput& output, const int& ringctindex, UniValue &entry);

#endif // BITCOIN_CORE_IO_H
