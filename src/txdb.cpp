// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2019-2020 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <txdb.h>

#include <chainparams.h>
#include <hash.h>
#include <random.h>
#include <pow.h>
#include <shutdown.h>
#include <uint256.h>
#include <util.h>
#include <ui_interface.h>

#include <stdint.h>

#include <boost/thread.hpp>
#include <primitives/zerocoin.h>
#include <veil/invalid.h>

static const char DB_COIN = 'C';
static const char DB_ADDRESSINDEX = 'a';
static const char DB_ADDRESSUNSPENTINDEX = 'u';
static const char DB_TIMESTAMPINDEX = 's';
static const char DB_BLOCKHASHINDEX = 'z';
static const char DB_SPENTINDEX = 'p';

static const char DB_COINS = 'c';
static const char DB_BLOCK_FILES = 'f';
static const char DB_BLOCK_INDEX = 'b';

static const char DB_BEST_BLOCK = 'B';
static const char DB_HEAD_BLOCKS = 'H';
static const char DB_FLAG = 'F';
static const char DB_REINDEX_FLAG = 'R';
static const char DB_LAST_BLOCK = 'l';

static const char DB_BLACKLISTOUT = 'X';
static const char DB_BLACKLISTPUB = 'P';

namespace {

struct CoinEntry {
    COutPoint* outpoint;
    char key;
    explicit CoinEntry(const COutPoint* ptr) : outpoint(const_cast<COutPoint*>(ptr)), key(DB_COIN)  {}

    template<typename Stream>
    void Serialize(Stream &s) const {
        s << key;
        s << outpoint->hash;
        s << VARINT(outpoint->n);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        s >> key;
        s >> outpoint->hash;
        s >> VARINT(outpoint->n);
    }
};

}

CCoinsViewDB::CCoinsViewDB(size_t nCacheSize, bool fMemory, bool fWipe) : db(GetDataDir() / "chainstate", nCacheSize, fMemory, fWipe, true)
{
}

bool CCoinsViewDB::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    return db.Read(CoinEntry(&outpoint), coin);
}

bool CCoinsViewDB::HaveCoin(const COutPoint &outpoint) const {
    return db.Exists(CoinEntry(&outpoint));
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!db.Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

std::vector<uint256> CCoinsViewDB::GetHeadBlocks() const {
    std::vector<uint256> vhashHeadBlocks;
    if (!db.Read(DB_HEAD_BLOCKS, vhashHeadBlocks)) {
        return std::vector<uint256>();
    }
    return vhashHeadBlocks;
}

bool CCoinsViewDB::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock) {
    CDBBatch batch(db);
    size_t count = 0;
    size_t changed = 0;
    size_t batch_size = (size_t)gArgs.GetArg("-dbbatchsize", nDefaultDbBatchSize);
    int crash_simulate = gArgs.GetArg("-dbcrashratio", 0);
    assert(!hashBlock.IsNull());

    uint256 old_tip = GetBestBlock();
    if (old_tip.IsNull()) {
        // We may be in the middle of replaying.
        std::vector<uint256> old_heads = GetHeadBlocks();
        if (old_heads.size() == 2) {
            assert(old_heads[0] == hashBlock);
            old_tip = old_heads[1];
        }
    }

    // In the first batch, mark the database as being in the middle of a
    // transition from old_tip to hashBlock.
    // A vector is used for future extensibility, as we may want to support
    // interrupting after partial writes from multiple independent reorgs.
    batch.Erase(DB_BEST_BLOCK);
    batch.Write(DB_HEAD_BLOCKS, std::vector<uint256>{hashBlock, old_tip});

    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            CoinEntry entry(&it->first);
            if (it->second.coin.IsSpent())
                batch.Erase(entry);
            else
                batch.Write(entry, it->second.coin);
            changed++;
        }
        count++;
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
        if (batch.SizeEstimate() > batch_size) {
            LogPrint(BCLog::COINDB, "Writing partial batch of %.2f MiB\n", batch.SizeEstimate() * (1.0 / 1048576.0));
            db.WriteBatch(batch);
            batch.Clear();
            if (crash_simulate) {
                static FastRandomContext rng;
                if (rng.randrange(crash_simulate) == 0) {
                    LogPrintf("Simulating a crash. Goodbye.\n");
                    _Exit(0);
                }
            }
        }
    }

    // In the last batch, mark the database as consistent with hashBlock again.
    batch.Erase(DB_HEAD_BLOCKS);
    batch.Write(DB_BEST_BLOCK, hashBlock);

    LogPrint(BCLog::COINDB, "Writing final batch of %.2f MiB\n", batch.SizeEstimate() * (1.0 / 1048576.0));
    bool ret = db.WriteBatch(batch);
    LogPrint(BCLog::COINDB, "Committed %u changed transaction outputs (out of %u) to coin database...\n", (unsigned int)changed, (unsigned int)count);
    return ret;
}

size_t CCoinsViewDB::EstimateSize() const
{
    return db.EstimateSize(DB_COIN, (char)(DB_COIN+1));
}

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(gArgs.IsArgSet("-blocksdir") ? GetDataDir() / "blocks" / "index" : GetBlocksDir() / "index", nCacheSize, fMemory, fWipe) {
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(std::make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

void CBlockTreeDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) {
    return Read(DB_LAST_BLOCK, nFile);
}

CCoinsViewCursor *CCoinsViewDB::Cursor() const
{
    CCoinsViewDBCursor *i = new CCoinsViewDBCursor(const_cast<CDBWrapper&>(db).NewIterator(), GetBestBlock());
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    i->pcursor->Seek(DB_COIN);
    // Cache key of first record
    if (i->pcursor->Valid()) {
        CoinEntry entry(&i->keyTmp.second);
        i->pcursor->GetKey(entry);
        i->keyTmp.first = entry.key;
    } else {
        i->keyTmp.first = 0; // Make sure Valid() and GetKey() return false
    }
    return i;
}

bool CCoinsViewDBCursor::GetKey(COutPoint &key) const
{
    // Return cached key
    if (keyTmp.first == DB_COIN) {
        key = keyTmp.second;
        return true;
    }
    return false;
}

bool CCoinsViewDBCursor::GetValue(Coin &coin) const
{
    return pcursor->GetValue(coin);
}

unsigned int CCoinsViewDBCursor::GetValueSize() const
{
    return pcursor->GetValueSize();
}

bool CCoinsViewDBCursor::Valid() const
{
    return keyTmp.first == DB_COIN;
}

void CCoinsViewDBCursor::Next()
{
    pcursor->Next();
    CoinEntry entry(&keyTmp.second);
    if (!pcursor->Valid() || !pcursor->GetKey(entry)) {
        keyTmp.first = 0; // Invalidate cached key after last record so that Valid() and GetKey() return false
    } else {
        keyTmp.first = entry.key;
    }
}

bool CBlockTreeDB::WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<int, const CBlockFileInfo*> >::const_iterator it=fileInfo.begin(); it != fileInfo.end(); it++) {
        batch.Write(std::make_pair(DB_BLOCK_FILES, it->first), *it->second);
    }
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (std::vector<const CBlockIndex*>::const_iterator it=blockinfo.begin(); it != blockinfo.end(); it++) {
        CDiskBlockIndex diskindex = CDiskBlockIndex(*it);
        CBlockHeader bHeader = (*it)->GetBlockHeader();
        uint256 hashBlock = diskindex.GetBlockHash();
        if (arith_uint256(hashBlock.GetHex()) != arith_uint256((*it)->GetBlockHeader().GetHash().GetHex())) {
            LogPrintf("%s: *** Warning - Block %d: Hash: %s doesn't match Header Hash: %s\n", __func__,
                      diskindex.nHeight, hashBlock.GetHex(), bHeader.GetHash().GetHex());
            continue; // Don't write it
        }
        batch.Write(std::make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()), CDiskBlockIndex(*it));
    }
    return WriteBatch(batch, true);
}

bool CBlockTreeDB::WriteFlag(const std::string &name, bool fValue) {
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const std::string &name, bool &fValue) {
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts(const Consensus::Params& consensusParams, std::function<CBlockIndex*(const uint256&)> insertBlockIndex)
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(DB_BLOCK_INDEX, uint256()));
    
    std::set<uint256> setBlockHash;

    // Load mapBlockIndex
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_BLOCK_INDEX) {
            CDiskBlockIndex diskindex;
            if (pcursor->GetValue(diskindex)) {
                // For some reason Veil has a tendency to duplicate an index, and store the second under a different key
                // ignore any duplicates and mark them to be erased
                uint256 hashBlock = diskindex.GetBlockHash();
                if (hashBlock != key.second) {
                    LogPrintf("%s: Skipping Block %d (status=%d): %s - Block Hash does not match Index Key\n",
                               __func__, diskindex.nHeight, diskindex.nStatus, hashBlock.GetHex());
                    pcursor->Next();
                    continue;
                }

                // Construct block index object
                CBlockIndex* pindexNew = insertBlockIndex(diskindex.GetBlockHash());
                pindexNew->pprev          = insertBlockIndex(diskindex.hashPrev);
                pindexNew->nHeight        = diskindex.nHeight;
                pindexNew->nFile          = diskindex.nFile;
                pindexNew->nDataPos       = diskindex.nDataPos;
                pindexNew->nUndoPos       = diskindex.nUndoPos;
                pindexNew->nVersion       = diskindex.nVersion;
                pindexNew->hashVeilData   = diskindex.hashVeilData;
                pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
                pindexNew->hashWitnessMerkleRoot = diskindex.hashWitnessMerkleRoot;
                pindexNew->hashPoFN       = diskindex.hashPoFN;
                pindexNew->nTime          = diskindex.nTime;
                pindexNew->nBits          = diskindex.nBits;
                pindexNew->nNonce         = diskindex.nNonce;
                pindexNew->nStatus        = diskindex.nStatus;
                pindexNew->nTx            = diskindex.nTx;
                pindexNew->nNetworkRewardReserve = diskindex.nNetworkRewardReserve;

                //Proof Of Stake
                pindexNew->nMint = diskindex.nMint;
                pindexNew->nMoneySupply = diskindex.nMoneySupply;
                pindexNew->fProofOfStake = diskindex.fProofOfStake;
                pindexNew->vHashProof = diskindex.vHashProof;

                //PoFN
                pindexNew->fProofOfFullNode = diskindex.fProofOfFullNode;

                //RingCT
                pindexNew->nAnonOutputs             = diskindex.nAnonOutputs;

                // zerocoin
                pindexNew->mapAccumulatorHashes = diskindex.mapAccumulatorHashes;
                pindexNew->hashAccumulators = diskindex.hashAccumulators;
                pindexNew->mapZerocoinSupply = diskindex.mapZerocoinSupply;
                pindexNew->vMintDenominationsInBlock = diskindex.vMintDenominationsInBlock;

                // ProgPow
                pindexNew->nNonce64         = diskindex.nNonce64;
                pindexNew->mixHash          = diskindex.mixHash;

//                if (pindexNew->IsProofOfWork() && !CheckProofOfWork(pindexNew->GetBlockPoWHash(), pindexNew->nBits, consensusParams))
//                    return error("%s: CheckProofOfWork failed: %s", __func__, pindexNew->ToString());

                pcursor->Next();
            } else {
                return error("%s: failed to read value", __func__);
            }
        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::ReadRCTOutput(int64_t i, CAnonOutput &ao)
{
    return Read(std::make_pair(DB_RCTOUTPUT, i), ao);
};

bool CBlockTreeDB::WriteRCTOutput(int64_t i, const CAnonOutput &ao)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_RCTOUTPUT, i), ao);
    return WriteBatch(batch);
};

bool CBlockTreeDB::EraseRCTOutput(int64_t i)
{
    CDBBatch batch(*this);
    batch.Erase(std::make_pair(DB_RCTOUTPUT, i));
    return WriteBatch(batch);
};


bool CBlockTreeDB::ReadRCTOutputLink(const CCmpPubKey &pk, int64_t &i)
{
    return Read(std::make_pair(DB_RCTOUTPUT_LINK, pk), i);
};

bool CBlockTreeDB::WriteRCTOutputLink(const CCmpPubKey &pk, int64_t i)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_RCTOUTPUT_LINK, pk), i);
    return WriteBatch(batch);
};

bool CBlockTreeDB::EraseRCTOutputLink(const CCmpPubKey &pk)
{
    CDBBatch batch(*this);
    batch.Erase(std::make_pair(DB_RCTOUTPUT_LINK, pk));
    return WriteBatch(batch);
};

bool CBlockTreeDB::ReadRCTKeyImage(const CCmpPubKey &ki, uint256 &txhash)
{
    return Read(std::make_pair(DB_RCTKEYIMAGE, ki), txhash);
};

bool CBlockTreeDB::WriteRCTKeyImage(const CCmpPubKey &ki, const uint256 &txhash)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_RCTKEYIMAGE, ki), txhash);
    return WriteBatch(batch);
};

bool CBlockTreeDB::EraseRCTKeyImage(const CCmpPubKey &ki)
{
    CDBBatch batch(*this);
    batch.Erase(std::make_pair(DB_RCTKEYIMAGE, ki));
    return WriteBatch(batch);
};

namespace {

//! Legacy class to deserialize pre-pertxout database entries without reindex.
class CCoins
{
public:
    //! whether transaction is a coinbase
    bool fCoinBase;

    //! unspent transaction outputs; spent outputs are .IsNull(); spent outputs at the end of the array are dropped
    std::vector<CTxOut> vout;

    //! at which height this transaction was included in the active block chain
    int nHeight;

    //! empty constructor
    CCoins() : fCoinBase(false), vout(0), nHeight(0) { }

    template<typename Stream>
    void Unserialize(Stream &s) {
        unsigned int nCode = 0;
        // version
        unsigned int nVersionDummy;
        ::Unserialize(s, VARINT(nVersionDummy));
        // header code
        ::Unserialize(s, VARINT(nCode));
        fCoinBase = nCode & 1;
        std::vector<bool> vAvail(2, false);
        vAvail[0] = (nCode & 2) != 0;
        vAvail[1] = (nCode & 4) != 0;
        unsigned int nMaskCode = (nCode / 8) + ((nCode & 6) != 0 ? 0 : 1);
        // spentness bitmask
        while (nMaskCode > 0) {
            unsigned char chAvail = 0;
            ::Unserialize(s, chAvail);
            for (unsigned int p = 0; p < 8; p++) {
                bool f = (chAvail & (1 << p)) != 0;
                vAvail.push_back(f);
            }
            if (chAvail != 0)
                nMaskCode--;
        }
        // txouts themself
        vout.assign(vAvail.size(), CTxOut());
        for (unsigned int i = 0; i < vAvail.size(); i++) {
            if (vAvail[i])
                ::Unserialize(s, CTxOutCompressor(vout[i]));
        }
        // coinbase height
        ::Unserialize(s, VARINT_MODE(nHeight, VarIntMode::NONNEGATIVE_SIGNED));
    }
};

}

/** Upgrade the database from older formats.
 *
 * Currently implemented: from the per-tx utxo model (0.8..0.14.x) to per-txout.
 */
bool CCoinsViewDB::Upgrade() {
    std::unique_ptr<CDBIterator> pcursor(db.NewIterator());
    pcursor->Seek(std::make_pair(DB_COINS, uint256()));
    if (!pcursor->Valid()) {
        return true;
    }

    int64_t count = 0;
    LogPrintf("Upgrading utxo-set database...\n");
    LogPrintf("[0%%]..."); /* Continued */
    uiInterface.ShowProgress(_("Upgrading UTXO database"), 0, true);
    size_t batch_size = 1 << 24;
    CDBBatch batch(db);
    int reportDone = 0;
    std::pair<unsigned char, uint256> key;
    std::pair<unsigned char, uint256> prev_key = {DB_COINS, uint256()};
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        if (ShutdownRequested()) {
            break;
        }
        if (pcursor->GetKey(key) && key.first == DB_COINS) {
            if (count++ % 256 == 0) {
                uint32_t high = 0x100 * *key.second.begin() + *(key.second.begin() + 1);
                int percentageDone = (int)(high * 100.0 / 65536.0 + 0.5);
                uiInterface.ShowProgress(_("Upgrading UTXO database"), percentageDone, true);
                if (reportDone < percentageDone/10) {
                    // report max. every 10% step
                    LogPrintf("[%d%%]...", percentageDone); /* Continued */
                    reportDone = percentageDone/10;
                }
            }
            CCoins old_coins;
            if (!pcursor->GetValue(old_coins)) {
                return error("%s: cannot parse CCoins record", __func__);
            }
            COutPoint outpoint(key.second, 0);
            for (size_t i = 0; i < old_coins.vout.size(); ++i) {
                if (!old_coins.vout[i].IsNull() && !old_coins.vout[i].scriptPubKey.IsUnspendable()) {
                    Coin newcoin(std::move(old_coins.vout[i]), old_coins.nHeight, old_coins.fCoinBase);
                    outpoint.n = i;
                    CoinEntry entry(&outpoint);
                    batch.Write(entry, newcoin);
                }
            }
            batch.Erase(key);
            if (batch.SizeEstimate() > batch_size) {
                db.WriteBatch(batch);
                batch.Clear();
                db.CompactRange(prev_key, key);
                prev_key = key;
            }
            pcursor->Next();
        } else {
            break;
        }
    }
    db.WriteBatch(batch);
    db.CompactRange({DB_COINS, uint256()}, key);
    uiInterface.ShowProgress("", 100, false);
    LogPrintf("[%s].\n", ShutdownRequested() ? "CANCELLED" : "DONE");
    return !ShutdownRequested();
}

CZerocoinDB::CZerocoinDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "zerocoin", nCacheSize, fMemory, fWipe)
{
}

//TODO: add prefixes for zerocoindb to the top of the file insteadof using chars when doing database operations
bool CZerocoinDB::WriteCoinMintBatch(const std::map<libzerocoin::PublicCoin, uint256>& mintInfo)
{
    CDBBatch batch(*this);
    size_t count = 0;
    for (auto it=mintInfo.begin(); it != mintInfo.end(); it++) {
        libzerocoin::PublicCoin pubCoin = it->first;
        uint256 hash = GetPubCoinHash(pubCoin.getValue());
        batch.Write(std::make_pair('m', hash), it->second);
        ++count;
    }

    LogPrint(BCLog::ZEROCOINDB, "Writing %u coin mints to db.\n", (unsigned int)count);
    return WriteBatch(batch, true);
}

bool CZerocoinDB::ReadCoinMint(const CBigNum& bnPubcoin, uint256& hashTx)
{
    return ReadCoinMint(GetPubCoinHash(bnPubcoin), hashTx);
}

bool CZerocoinDB::ReadCoinMint(const uint256& hashPubcoin, uint256& hashTx)
{
    return Read(std::make_pair('m', hashPubcoin), hashTx);
}

bool CZerocoinDB::EraseCoinMint(const CBigNum& bnPubcoin)
{
    uint256 hash = GetPubCoinHash(bnPubcoin);
    return Erase(std::make_pair('m', hash));
}

bool CZerocoinDB::WriteCoinSpendBatch(const std::map<libzerocoin::CoinSpend, uint256>& spendInfo)
{
    CDBBatch batch(*this);
    size_t count = 0;
    for (auto it=spendInfo.begin(); it != spendInfo.end(); it++) {
        CBigNum bnSerial = it->first.getCoinSerialNumber();
        CDataStream ss(SER_GETHASH, 0);
        ss << bnSerial;
        uint256 hash = Hash(ss.begin(), ss.end());
        batch.Write(std::make_pair('s', hash), it->second);
        ++count;
    }

    LogPrint(BCLog::ZEROCOINDB, "Writing %u coin spends to db.\n", (unsigned int)count);
    return WriteBatch(batch, true);
}

bool CZerocoinDB::ReadCoinSpend(const CBigNum& bnSerial, uint256& txHash)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << bnSerial;
    uint256 hash = Hash(ss.begin(), ss.end());

    return Read(std::make_pair('s', hash), txHash);
}

bool CZerocoinDB::ReadCoinSpend(const uint256& hashSerial, uint256 &txHash)
{
    return Read(std::make_pair('s', hashSerial), txHash);
}

bool CZerocoinDB::EraseCoinSpend(const CBigNum& bnSerial)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << bnSerial;
    uint256 hash = Hash(ss.begin(), ss.end());

    return Erase(std::make_pair('s', hash));
}

bool CZerocoinDB::WritePubcoinSpendBatch(std::map<uint256, uint256>& mapPubcoinSpends, const uint256& hashBlock)
{
    CDBBatch batch(*this);
    for (const auto& pair : mapPubcoinSpends) {
        const uint256& hashPubcoin = pair.first;
        const uint256& txid = pair.second;
        batch.Write(std::make_pair('l', hashPubcoin), std::make_pair(txid, hashBlock));
    }
    return WriteBatch(batch, true);
}

bool CZerocoinDB::ReadPubcoinSpend(const uint256& hashPubcoin, uint256& txHash, uint256& hashBlock)
{
    std::pair<uint256, uint256> pairTxHashblock;
    if (!Read(std::make_pair('l', hashPubcoin), pairTxHashblock))
        return false;
    txHash = pairTxHashblock.first;
    hashBlock = pairTxHashblock.second;
    return true;
}

bool CZerocoinDB::ErasePubcoinSpend(const uint256& hashPubcoin)
{
    return Erase(std::make_pair('l', hashPubcoin));
}

bool CZerocoinDB::WipeCoins(std::string strType)
{
    if (strType != "spends" && strType != "mints")
        return error("%s: did not recognize type %s", __func__, strType);

    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    char type = (strType == "spends" ? 's' : 'm');
    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << std::make_pair(type, uint256());
    pcursor->Seek(ssKeySet.str());
    // Load mapBlockIndex
    std::set<uint256> setDelete;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            char chType = 0;
            pcursor->GetKey(chType);

            if (chType == type) {
                uint256 hash;
                pcursor->GetValue(hash);
                setDelete.insert(hash);
                pcursor->Next();
            } else {
                break; // if shutdown requested or finished loading block index
            }
        } catch (std::exception& e) {
            return error("%s : Deserialize or I/O error - %s", __func__, e.what());
        }
    }

    for (auto& hash : setDelete) {
        if (!Erase(std::make_pair(type, hash)))
            LogPrintf("%s: error failed to delete %s\n", __func__, hash.GetHex());
    }

    return true;
}

bool CZerocoinDB::WriteAccumulatorValue(const uint256& hashChecksum, const CBigNum& bnValue)
{
    LogPrint(BCLog::ZEROCOINDB,"%s : checksum:%d val:%s\n", __func__, hashChecksum.GetHex(), bnValue.GetHex());
    return Write(std::make_pair('2', hashChecksum), bnValue);
}

bool CZerocoinDB::ReadAccumulatorValue(const uint256& hashChecksum, CBigNum& bnValue)
{
    return Read(std::make_pair('2', hashChecksum), bnValue);
}

bool CZerocoinDB::EraseAccumulatorValue(const uint256& hashChecksum)
{
    LogPrint(BCLog::ZEROCOINDB, "%s : checksum:%d\n", __func__, hashChecksum.GetHex());
    return Erase(std::make_pair('2', hashChecksum));
}

bool CZerocoinDB::LoadBlacklistOutPoints()
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(DB_BLACKLISTOUT, COutPoint()));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, COutPoint> key;

        if (pcursor->GetKey(key) && key.first == DB_BLACKLISTOUT) {
            int nType;
            if (pcursor->GetValue(nType)) {
                switch(nType) {
                    case OUTPUT_STANDARD : {
                        blacklist::AddBasecoinOutPoint(key.second);
                    }
                    case OUTPUT_CT : {
                        blacklist::AddStealthOutPoint(key.second);
                    }
                    case OUTPUT_RINGCT : {
                        blacklist::AddRctOutPoint(key.second);
                    }
                    default: {
                        //nothing
                    }
                }
                pcursor->Next();
            } else {
                return error("failed to read blacklist outpoints");
            }
        } else {
            break;
        }
    }
    return true;
}

bool CZerocoinDB::LoadBlacklistPubcoins()
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(DB_BLACKLISTPUB, uint256()));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;

        if (pcursor->GetKey(key) && key.first == DB_BLACKLISTPUB) {
            int value;
            if (pcursor->GetValue(value)) {
                blacklist::AddPubcoinHash(key.second);
            } else {
                return error("failed to read blacklisted pubcoins");
            }
            pcursor->Next();
        } else {
            break;
        }
    }
    return true;
}

bool CZerocoinDB::WriteBlacklistedOutpoint(const COutPoint& outpoint, int nType)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_BLACKLISTOUT, outpoint), nType);
    return WriteBatch(batch);
}

bool CZerocoinDB::EraseBlacklistedOutpoint(const COutPoint& outpoint)
{
    return Erase(std::make_pair(DB_BLACKLISTOUT, outpoint));
}

bool CZerocoinDB::WriteBlacklistedPubcoin(const uint256& hashPubcoin)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_BLACKLISTPUB, hashPubcoin), int()); //have to add value? only need key
    return WriteBatch(batch);
}

bool CZerocoinDB::EraseBlacklisterPubcoin(const uint256& hashPubcoin)
{
    return Erase(std::make_pair(DB_BLACKLISTPUB, hashPubcoin));
}
