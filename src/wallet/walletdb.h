// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_WALLETDB_H
#define BITCOIN_WALLET_WALLETDB_H

#include <amount.h>
#include <primitives/transaction.h>
#include <primitives/zerocoin.h>
#include <wallet/db.h>
#include <key.h>

#include <list>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

/**
 * Overview of wallet database classes:
 *
 * - WalletBatch is an abstract modifier object for the wallet database, and encapsulates a database
 *   batch update as well as methods to act on the database. It should be agnostic to the database implementation.
 *
 * The following classes are implementation specific:
 * - BerkeleyEnvironment is an environment in which the database exists.
 * - BerkeleyDatabase represents a wallet database.
 * - BerkeleyBatch is a low-level database batch update.
 */

static const bool DEFAULT_FLUSHWALLET = true;

struct CBlockLocator;
class CKeyPool;
class CMasterKey;
class CScript;
class CWallet;
class CWalletTx;
class uint160;
class uint256;
class CDeterministicMint;
class CZerocoinMint;
class CZerocoinSpend;
class CoinWitnessCacheData;

/** Backend-agnostic database type. */
using WalletDatabase = BerkeleyDatabase;

/** Error statuses for the wallet database */
enum class DBErrors
{
    LOAD_OK,
    CORRUPT,
    NONCRITICAL_ERROR,
    TOO_NEW,
    LOAD_FAIL,
    NEED_REWRITE
};

/* simple HD chain data model */
class CHDChain
{
public:
    uint32_t nExternalChainCounter;
    uint32_t nInternalChainCounter;
    CKeyID seed_id; //!< seed hash160
    CKeyID seed_id_r; //!< seed hash160 if the wallet uses 512bit seed saved to two keys

    static const int VERSION_HD_BASE        = 1;
    static const int VERSION_HD_CHAIN_SPLIT = 2;
    static const int CURRENT_VERSION        = VERSION_HD_CHAIN_SPLIT;
    int nVersion;

    CHDChain() { SetNull(); }
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(this->nVersion);
        READWRITE(nExternalChainCounter);
        READWRITE(seed_id);
        READWRITE(seed_id_r);
        if (this->nVersion >= VERSION_HD_CHAIN_SPLIT)
            READWRITE(nInternalChainCounter);
    }

    void SetNull()
    {
        nVersion = CHDChain::CURRENT_VERSION;
        nExternalChainCounter = 0;
        nInternalChainCounter = 0;
        seed_id.SetNull();
        seed_id_r.SetNull();
    }

    bool Is512BitSeed() const { return !seed_id_r.IsNull(); }
};

class CKeyMetadata
{
public:
    static const int VERSION_BASIC=1;
    static const int VERSION_WITH_HDDATA=10;
    static const int CURRENT_VERSION=VERSION_WITH_HDDATA;
    int nVersion;
    int64_t nCreateTime; // 0 means unknown
    std::string hdKeypath; //optional HD/bip32 keypath
    CKeyID hd_seed_id; //id of the HD seed used to derive this key
    CKeyID hd_seed_id_r; //if the key is derived using 512 bit seed, two keys are needed

    CKeyMetadata()
    {
        SetNull();
    }
    explicit CKeyMetadata(int64_t nCreateTime_)
    {
        SetNull();
        nCreateTime = nCreateTime_;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(this->nVersion);
        READWRITE(nCreateTime);
        if (this->nVersion >= VERSION_WITH_HDDATA)
        {
            READWRITE(hdKeypath);
            READWRITE(hd_seed_id);
            READWRITE(hd_seed_id_r);
        }
    }

    void SetNull()
    {
        nVersion = CKeyMetadata::CURRENT_VERSION;
        nCreateTime = 0;
        hdKeypath.clear();
        hd_seed_id.SetNull();
        hd_seed_id_r.SetNull();
    }

    bool UsesTwoSeeds() const { return !hd_seed_id_r.IsNull(); }
};

/** Access to the wallet database.
 * This represents a single transaction at the
 * database. It will be committed when the object goes out of scope.
 * Optionally (on by default) it will flush to disk as well.
 */
class WalletBatch
{

friend class CHDWalletDB;

protected:
    template <typename K, typename T>
    bool WriteIC(const K& key, const T& value, bool fOverwrite = true)
    {
        if (!m_batch.Write(key, value, fOverwrite)) {
            return false;
        }
        m_database.IncrementUpdateCounter();
        return true;
    }

    template <typename K>
    bool EraseIC(const K& key)
    {
        if (!m_batch.Erase(key)) {
            return false;
        }
        m_database.IncrementUpdateCounter();
        return true;
    }

public:
    explicit WalletBatch(WalletDatabase& database, const char* pszMode = "r+", bool _fFlushOnClose = true) :
        m_batch(database, pszMode, _fFlushOnClose),
        m_database(database)
    {
    }
    WalletBatch(const WalletBatch&) = delete;
    WalletBatch& operator=(const WalletBatch&) = delete;

    bool WriteName(const std::string& strAddress, const std::string& strName);
    bool EraseName(const std::string& strAddress);

    bool WritePurpose(const std::string& strAddress, const std::string& purpose);
    bool ErasePurpose(const std::string& strAddress);

    bool WriteTx(const CWalletTx& wtx);
    bool EraseTx(uint256 hash);

    bool WriteKey(const CPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata &keyMeta);
    bool WriteCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret, const CKeyMetadata &keyMeta);
    bool WriteMasterKey(unsigned int nID, const CMasterKey& kMasterKey);

    bool WriteCScript(const uint160& hash, const CScript& redeemScript);

    bool WriteWatchOnly(const CScript &script, const CKeyMetadata &keymeta);
    bool EraseWatchOnly(const CScript &script);

    bool WriteBestBlock(const CBlockLocator& locator);
    bool ReadBestBlock(CBlockLocator& locator);

    bool WriteOrderPosNext(int64_t nOrderPosNext);

    bool ReadPool(int64_t nPool, CKeyPool& keypool);
    bool WritePool(int64_t nPool, const CKeyPool& keypool);
    bool ErasePool(int64_t nPool);

    bool WriteMinVersion(int nVersion);

    /// Write destination data key,value tuple to database
    bool WriteDestData(const std::string &address, const std::string &key, const std::string &value);
    /// Erase destination data tuple from wallet database
    bool EraseDestData(const std::string &address, const std::string &key);

    DBErrors LoadWallet(CWallet* pwallet);
    DBErrors FindWalletTx(std::vector<uint256>& vTxHash, std::vector<CWalletTx>& vWtx);
    DBErrors ZapWalletTx(std::vector<CWalletTx>& vWtx);
    DBErrors ZapSelectTx(std::vector<uint256>& vHashIn, std::vector<uint256>& vHashOut);
    /* Try to (very carefully!) recover wallet database (with a possible key type filter) */
    static bool Recover(const fs::path& wallet_path, void *callbackDataIn, bool (*recoverKVcallback)(void* callbackData, CDataStream ssKey, CDataStream ssValue), std::string& out_backup_filename);
    /* Recover convenience-function to bypass the key filter callback, called when verify fails, recovers everything */
    static bool Recover(const fs::path& wallet_path, std::string& out_backup_filename);
    /* Recover filter (used as callback), will only let keys (cryptographical keys) as KV/key-type pass through */
    static bool RecoverKeysOnlyFilter(void *callbackData, CDataStream ssKey, CDataStream ssValue);
    /* Function to determine if a certain KV/key-type is a key (cryptographical key) type */
    static bool IsKeyType(const std::string& strType);
    /* verifies the database environment */
    static bool VerifyEnvironment(const fs::path& wallet_path, std::string& errorStr);
    /* verifies the database file */
    static bool VerifyDatabaseFile(const fs::path& wallet_path, std::string& warningStr, std::string& errorStr);

    //! write the hdchain model (external chain child index counter)
    bool WriteHDChain(const CHDChain& chain);
    bool LoadHDChain(CHDChain& chain);

    bool WriteWalletFlags(const uint64_t flags);
    //! Begin a new transaction
    bool TxnBegin();
    //! Commit current transaction
    bool TxnCommit();
    //! Abort current transaction
    bool TxnAbort();
    //! Read wallet version
    bool ReadVersion(int& nVersion);
    //! Write wallet version
    bool WriteVersion(int nVersion);

    // zerocoin
    bool WriteDeterministicMint(const CDeterministicMint& dMint);
    bool ReadDeterministicMint(const uint256& hashPubcoin, CDeterministicMint& dMint);
    bool EraseDeterministicMint(const uint256& hashPubcoin);
    bool WriteZerocoinMint(const CZerocoinMint& zerocoinMint);
    bool EraseZerocoinMint(const CZerocoinMint& zerocoinMint);
    bool ReadZerocoinMint(const CBigNum &bnPubcoinValue, CZerocoinMint& zerocoinMint);
    bool ReadZerocoinMint(const uint256& hashPubcoin, CZerocoinMint& mint);
    bool ArchiveMintOrphan(const CZerocoinMint& zerocoinMint);
    bool ArchiveDeterministicOrphan(const CDeterministicMint& dMint);
    bool UnarchiveZerocoinMint(const uint256& hashPubcoin, CZerocoinMint& mint);
    bool UnarchiveDeterministicMint(const uint256& hashPubcoin, CDeterministicMint& dMint);
    std::list<CZerocoinMint> ListMintedCoins();
    std::list<CDeterministicMint> ListDeterministicMints();
    std::list<CZerocoinSpend> ListSpentCoins();
    std::list<CBigNum> ListSpentCoinsSerial();
    std::list<CZerocoinMint> ListArchivedZerocoins();
    std::list<CDeterministicMint> ListArchivedDeterministicMints();
    bool WriteZerocoinSpendSerialEntry(const CZerocoinSpend& zerocoinSpend);
    bool EraseZerocoinSpendSerialEntry(const CBigNum& serialEntry);
    bool ReadZerocoinSpendSerialEntry(const CBigNum& bnSerial);
    bool WriteCurrentSeedHash(const CKeyID& hashSeed);
    bool ReadCurrentSeedHash(CKeyID& hashSeed);

    bool WriteZCount(const uint32_t &nCount);
    bool ReadZCount(uint32_t &nCount);
    std::map<CKeyID, std::vector<std::pair<uint256, uint32_t> > > MapMintPool();
    bool WriteMintPoolPair(const CKeyID& hashMasterSeed, const uint256& hashPubcoin, const uint32_t& nCount);

protected:
    BerkeleyBatch m_batch;
    WalletDatabase& m_database;
};

//! Compacts BDB state so that wallet.dat is self-contained (if there are changes)
void MaybeCompactWalletDB();

#endif // BITCOIN_WALLET_WALLETDB_H
