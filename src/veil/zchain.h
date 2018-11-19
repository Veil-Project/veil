#ifndef VEIL_ZCHAIN_H
#define VEIL_ZCHAIN_H

#include "libzerocoin/Coin.h"
#include "libzerocoin/Denominations.h"
#include "libzerocoin/CoinSpend.h"
#include <list>
#include <string>
#include <primitives/transaction.h>

class CBlock;
class CBigNum;
struct CMintMeta;
class CTransaction;
class CTxIn;
class CTxOut;
class CValidationState;
class CZerocoinMint;
class uint256;

bool BlockToMintValueVector(const CBlock& block, const libzerocoin::CoinDenomination denom, std::vector<CBigNum>& vValues);
bool BlockToPubcoinList(const CBlock& block, std::list<libzerocoin::PublicCoin>& listPubcoins);
bool BlockToZerocoinMintList(const CBlock& block, std::list<CZerocoinMint>& vMints);
void FindMints(std::vector<CMintMeta> vMintsToFind, std::vector<CMintMeta>& vMintsToUpdate, std::vector<CMintMeta>& vMissingMints);
int GetZerocoinStartHeight();
bool GetZerocoinMint(const CBigNum& bnPubcoin, uint256& txHash);
bool IsPubcoinInBlockchain(const uint256& hashPubcoin, uint256& txid);
bool IsSerialKnown(const CBigNum& bnSerial);
bool IsSerialInBlockchain(const CBigNum& bnSerial, int& nHeightTx);
bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend);
bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend, CTransactionRef txRef);
bool RemoveSerialFromDB(const CBigNum& bnSerial);
std::string ReindexZerocoinDB();
std::shared_ptr<libzerocoin::CoinSpend> TxInToZerocoinSpend(const CTxIn& txin);
bool OutputToPublicCoin(const CTxOutBase* out, libzerocoin::PublicCoin& coin);
bool TxOutToPublicCoin(const CTxOut& txout, libzerocoin::PublicCoin& pubCoin);
std::list<libzerocoin::CoinDenomination> ZerocoinSpendListFromBlock(const CBlock& block);

#endif //VEIL_ZCHAIN_H
