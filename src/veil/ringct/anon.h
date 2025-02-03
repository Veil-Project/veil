// Copyright (c) 2017-2019 The Particl developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
#ifndef VEIL_ANON_H
#define VEIL_ANON_H

#include <inttypes.h>
#include <primitives/transaction.h>

class CTxMemPool;
class CValidationState;

const size_t MIN_RINGSIZE = 3;
const size_t MAX_RINGSIZE = 32;

const size_t MAX_ANON_INPUTS = 32; // To raise see MLSAG_MAX_ROWS also

const size_t ANON_FEE_MULTIPLIER = 2;


bool VerifyMLSAG(const CTransaction &tx, CValidationState &state);
bool VerifyCoinbase(CAmount nExpStakeReward, const CTransaction &tx, CValidationState &state);

bool AddKeyImagesToMempool(const CTransaction &tx, CTxMemPool &pool);
bool RemoveKeyImagesFromMempool(const uint256 &hash, const CTxIn &txin, CTxMemPool &pool);

bool AllAnonOutputsUnknown(const CTransaction &tx, CValidationState &state);

bool RollBackRCTIndex(int64_t nLastValidRCTOutput, int64_t nExpectErase, std::set<CCmpPubKey> &setKi);

bool RewindToCheckpoint(int nCheckPointHeight, int &nBlocks, std::string &sError);

std::vector<COutPoint> GetRingCtInputs(const CTxIn& txin);
bool GetRingCtInputs(const CTxIn& txin, std::vector<std::vector<COutPoint> >& vInputs);
std::vector<std::vector<COutPoint>> GetTxRingCtInputs(const CTransactionRef ptx);


#endif //VEIL_ANON_H
