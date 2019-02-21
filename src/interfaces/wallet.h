// Copyright (c) 2018-2019 The Bitcoin Core developers
// Copyright (c) 2018-2019 The Particl developers
// Copyright (c) 2018-2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INTERFACES_WALLET_H
#define BITCOIN_INTERFACES_WALLET_H

#include <amount.h>                    // For CAmount
#include <pubkey.h>                    // For CKeyID and CScriptID (definitions needed in CTxDestination instantiation)
#include <script/ismine.h>             // For isminefilter, isminetype
#include <script/standard.h>           // For CTxDestination
#include <support/allocators/secure.h> // For SecureString
#include <ui_interface.h>              // For ChangeType

#include <functional>
#include <map>
#include <memory>
#include <stdint.h>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <veil/ringct/transactionrecord.h>
#include <veil/ringct/stealth.h>               // For CTxDestination
#include <veil/ringct/extkey.h>
#include <wallet/deterministicmint.h>

class CCoinControl;
class CFeeRate;
class CKey;
class CReserveKey;
class CTempRecipient;
class CWallet;
class CWalletTx;
class CDeterministicMint;
class CZerocoinSpendReceipt;
class CZerocoinMint;

enum class FeeReason;
enum class OutputType;
struct CRecipient;

namespace interfaces {

class Handler;
class PendingWalletTx;
struct WalletAddress;
struct WalletBalances;
struct WalletTx;
struct WalletTxOut;
struct WalletTxStatus;

using WalletOrderForm = std::vector<std::pair<std::string, std::string>>;
using WalletValueMap = std::map<std::string, std::string>;

#define DUMMY_VALUE -78917411

//! Interface for accessing a wallet.
class Wallet
{
public:
    virtual ~Wallet() {}

    //! Encrypt wallet.
    virtual bool encryptWallet(const SecureString& wallet_passphrase) = 0;

    //! Return whether wallet is encrypted.
    virtual bool isCrypted() = 0;

    //! Lock wallet.
    virtual bool lock() = 0;

    //! Unlock wallet.
    virtual bool unlock(const SecureString& wallet_passphrase, bool fUnlockForStakingOnly) = 0;

    //! Return whether wallet is locked.
    virtual bool isLocked() = 0;

    //! Return whether wallet is unlocked for staking only.
    virtual bool isUnlockedForStakingOnly() = 0;

    //! Change wallet passphrase.
    virtual bool changeWalletPassphrase(const SecureString& old_wallet_passphrase,
        const SecureString& new_wallet_passphrase) = 0;

    //! Abort a rescan.
    virtual void abortRescan() = 0;

    //! Back up wallet.
    virtual bool backupWallet(const std::string& filename) = 0;

    //! Get wallet name.
    virtual std::string getWalletName() = 0;

    // Get key from pool.
    virtual bool getKeyFromPool(bool internal, CPubKey& pub_key) = 0;

    //! Get public key.
    virtual bool getPubKey(const CKeyID& address, CPubKey& pub_key) = 0;

    //! Get private key.
    virtual bool getPrivKey(const CKeyID& address, CKey& key) = 0;

    //! Return whether wallet has private key.
    virtual bool isSpendable(const CTxDestination& dest) = 0;

    virtual std::string mintZerocoin(CAmount nValue, std::vector<CDeterministicMint>& vDMints, OutputTypes inputtype,
            const CCoinControl* coinControl) = 0;

    virtual std::unique_ptr<PendingWalletTx> prepareZerocoinSpend(CAmount nValue, int nSecurityLevel,
            CZerocoinSpendReceipt& receipt, std::vector<CZerocoinMint>& vMintsSelected, bool fMintChange,
            bool fMinimizeChange, std::vector<std::tuple<CWalletTx, std::vector<CDeterministicMint>,
                    std::vector<CZerocoinMint>>>& vCommitData,
                    libzerocoin::CoinDenomination denomFilter = libzerocoin::CoinDenomination::ZQ_ERROR,
                    CTxDestination* addressTo = NULL) = 0;

    virtual bool commitZerocoinSpend(CZerocoinSpendReceipt& receipt, std::vector<std::tuple<CWalletTx,
            std::vector<CDeterministicMint>, std::vector<CZerocoinMint>>>& vCommitData) = 0;

    //! Return whether wallet has watch only keys.
    virtual bool haveWatchOnly() = 0;

    //! Add or update address.
    virtual bool setAddressBook(const CTxDestination& dest, const std::string& name, const std::string& purpose, bool bench32 = true) = 0;

    // Remove address.
    virtual bool delAddressBook(const CTxDestination& dest) = 0;

    //! Look up address in wallet, return whether exists.
    virtual bool getAddress(const CTxDestination& dest,
        std::string* name,
        isminetype* is_mine,
        std::string* purpose) = 0;

    virtual std::vector<WalletAddress> getLabelAddress(const std::string label) = 0;
    //! Get wallet address list.
    virtual std::vector<WalletAddress> getAddresses() = 0;
    virtual std::vector<WalletAddress> getAddresses(bool IsMineAddresses) = 0;
    virtual std::vector<WalletAddress> getStealthAddresses(bool IsMineAddresses) = 0;

    //! Add scripts to key store so old so software versions opening the wallet
    //! database can detect payments to newer address types.
    virtual void learnRelatedScripts(const CPubKey& key, OutputType type) = 0;

    //! Add dest data.
    virtual bool addDestData(const CTxDestination& dest, const std::string& key, const std::string& value) = 0;

    //! Erase dest data.
    virtual bool eraseDestData(const CTxDestination& dest, const std::string& key) = 0;

    //! Get dest values with prefix.
    virtual std::vector<std::string> getDestValues(const std::string& prefix) = 0;

    //! Lock coin.
    virtual void lockCoin(const COutPoint& output) = 0;

    //! Unlock coin.
    virtual void unlockCoin(const COutPoint& output) = 0;

    //! Return whether coin is locked.
    virtual bool isLockedCoin(const COutPoint& output) = 0;

    //! List locked coins.
    virtual void listLockedCoins(std::vector<COutPoint>& outputs) = 0;

    //! Create transaction.
    virtual std::unique_ptr<PendingWalletTx> createTransaction(std::vector<CTempRecipient>& recipients,
        const CCoinControl& coin_control,
        bool sign,
        int& change_pos,
        CAmount& fee,
        OutputTypes inputType,
        std::string& fail_reason) = 0;

    //! Return whether transaction can be abandoned.
    virtual bool transactionCanBeAbandoned(const uint256& txid) = 0;

    //! Abandon transaction.
    virtual bool abandonTransaction(const uint256& txid) = 0;

    //! Return whether transaction can be bumped.
    virtual bool transactionCanBeBumped(const uint256& txid) = 0;

    //! Create bump transaction.
    virtual bool createBumpTransaction(const uint256& txid,
        const CCoinControl& coin_control,
        CAmount total_fee,
        std::vector<std::string>& errors,
        CAmount& old_fee,
        CAmount& new_fee,
        CMutableTransaction& mtx) = 0;

    //! Sign bump transaction.
    virtual bool signBumpTransaction(CMutableTransaction& mtx) = 0;

    //! Commit bump transaction.
    virtual bool commitBumpTransaction(const uint256& txid,
        CMutableTransaction&& mtx,
        std::vector<std::string>& errors,
        uint256& bumped_txid) = 0;

    //! Get a transaction.
    virtual CTransactionRef getTx(const uint256& txid) = 0;

    //! Get transaction information.
    virtual WalletTx getWalletTx(const uint256& txid) = 0;

    //! Get list of all wallet transactions.
    virtual std::vector<WalletTx> getWalletTxs() = 0;

    //! Try to get updated status for a particular transaction, if possible without blocking.
    virtual bool tryGetTxStatus(const uint256& txid,
        WalletTxStatus& tx_status,
        int& num_blocks,
        int64_t& adjusted_time) = 0;

    //! Get transaction details.
    virtual WalletTx getWalletTxDetails(const uint256& txid,
        WalletTxStatus& tx_status,
        WalletOrderForm& order_form,
        bool& in_mempool,
        int& num_blocks,
        int64_t& adjusted_time) = 0;

    //! Get balances.
    virtual WalletBalances getBalances() = 0;

    //! Get balances if possible without blocking.
    virtual bool tryGetBalances(WalletBalances& balances, int& num_blocks) = 0;

    //! Get balance.
    virtual CAmount getBalance() = 0;

    //! Get available balance.
    virtual CAmount getAvailableBalance(const CCoinControl& coin_control) = 0;
    virtual CAmount getAvailableCTBalance(const CCoinControl& coin_control) { return 0; }
    virtual CAmount getAvailableRingCTBalance(const CCoinControl& coin_control) { return 0; }

    //! Create a WalletTx without all the data filled in yet.
    virtual CWallet* getWalletPointer() { return nullptr; }

    //! Return whether transaction input belongs to wallet.
    virtual isminetype txinIsMine(const CTxIn& txin) = 0;

    //! Return whether transaction output belongs to wallet.
    virtual isminetype txoutbaseIsMine(const CTxOutBase* txout) = 0;
    virtual isminetype txoutIsMine(const CTxOut& txout) = 0;

    //! Return debit amount if transaction input belongs to wallet.
    virtual CAmount getDebit(const CTxIn& txin, isminefilter filter) = 0;

    //! Return credit amount if transaction input belongs to wallet.
    virtual CAmount getCredit(const CTxOut& txout, isminefilter filter) = 0;

    //! Return credit amount if transaction input belongs to wallet and it is an anon input
    virtual CAmount getAnonCredit(const COutPoint& outpoint, isminefilter filter) = 0;

    virtual bool getNewStealthAddress(CStealthAddress& address) = 0;

    //! Return AvailableCoins + LockedCoins grouped by wallet address.
    //! (put change in one group with wallet address)
    using CoinsList = std::map<CTxDestination, std::vector<std::tuple<COutPoint, WalletTxOut>>>;
    virtual CoinsList listCoins() = 0;

    //! Return wallet transaction output information.
    virtual std::vector<WalletTxOut> getCoins(const std::vector<COutPoint>& outputs) = 0;

    //! Get required fee.
    virtual CAmount getRequiredFee(unsigned int tx_bytes) = 0;

    //! Get minimum fee.
    virtual CAmount getMinimumFee(unsigned int tx_bytes,
        const CCoinControl& coin_control,
        int* returned_target,
        FeeReason* reason) = 0;

    //! Get tx confirm target.
    virtual unsigned int getConfirmTarget() = 0;

    // Return whether HD enabled.
    virtual bool hdEnabled() = 0;

    // check if a certain wallet flag is set.
    virtual bool IsWalletFlagSet(uint64_t flag) = 0;

    // Get default address type.
    virtual OutputType getDefaultAddressType() = 0;

    // Get default change type.
    virtual OutputType getDefaultChangeType() = 0;

    // Enable or disable staking
    virtual void setStakingEnabled(bool fEnableStaking) = 0;
    virtual bool isStakingEnabled() = 0;

    //! Register handler for unload message.
    using UnloadFn = std::function<void()>;
    virtual std::unique_ptr<Handler> handleUnload(UnloadFn fn) = 0;

    //! Register handler for show progress messages.
    using ShowProgressFn = std::function<void(const std::string& title, int progress)>;
    virtual std::unique_ptr<Handler> handleShowProgress(ShowProgressFn fn) = 0;

    //! Register handler for status changed messages.
    using StatusChangedFn = std::function<void()>;
    virtual std::unique_ptr<Handler> handleStatusChanged(StatusChangedFn fn) = 0;

    //! Register handler for address book changed messages.
    using AddressBookChangedFn = std::function<void(const CTxDestination& address,
        const std::string& label,
        bool is_mine,
        const std::string& purpose,
        ChangeType status)>;
    virtual std::unique_ptr<Handler> handleAddressBookChanged(AddressBookChangedFn fn) = 0;

    //! Register handler for transaction changed messages.
    using TransactionChangedFn = std::function<void(const uint256& txid, ChangeType status)>;
    virtual std::unique_ptr<Handler> handleTransactionChanged(TransactionChangedFn fn) = 0;

    //! Register handler for watchonly changed messages.
    using WatchOnlyChangedFn = std::function<void(bool have_watch_only)>;
    virtual std::unique_ptr<Handler> handleWatchOnlyChanged(WatchOnlyChangedFn fn) = 0;
};

//! Tracking object returned by CreateTransaction and passed to CommitTransaction.
class PendingWalletTx
{
public:
    virtual ~PendingWalletTx() {}

    //! Get transaction data.
    virtual const CTransaction& get() = 0;

    //! Get virtual transaction size.
    virtual int64_t getVirtualSize() = 0;

    //! Send pending transaction and commit to wallet.
    virtual bool commit(WalletValueMap value_map,
        WalletOrderForm order_form,
        std::string& reject_reason) = 0;
};

//! Information about one wallet address.
struct WalletAddress
{
    CTxDestination dest;
    isminetype is_mine;
    std::string name;
    std::string purpose;

    WalletAddress(CTxDestination dest, isminetype is_mine, std::string name, std::string purpose)
        : dest(std::move(dest)), is_mine(is_mine), name(std::move(name)), purpose(std::move(purpose))
    {
    }
};

//! Collection of wallet balances.
struct WalletBalances
{
    CAmount total_balance = 0;
    CAmount total_unconfirmed_balance = 0;
    CAmount total_immature_balance = 0;
    bool have_watch_only = false;
    CAmount watch_only_balance = 0;
    CAmount unconfirmed_watch_only_balance = 0;
    CAmount immature_watch_only_balance = 0;

    CAmount basecoin_balance = 0;
    CAmount basecoin_unconfirmed_balance = 0;
    CAmount basecoin_immature_balance = 0;
    CAmount ct_balance = 0;
    CAmount ct_unconfirmed_balance = 0;
    CAmount ct_immature_balance = 0;
    CAmount ring_ct_balance = 0;
    CAmount ring_ct_unconfirmed_balance = 0;
    CAmount ring_ct_immature_balance = 0;
    CAmount zerocoin_balance = 0;
    CAmount zerocoin_unconfirmed_balance = 0;
    CAmount zerocoin_immature_balance = 0;

    bool balanceChanged(const WalletBalances& prev) const
    {
        return total_balance != prev.total_balance || total_unconfirmed_balance != prev.total_unconfirmed_balance ||
               total_immature_balance != prev.total_immature_balance || watch_only_balance != prev.watch_only_balance ||
               unconfirmed_watch_only_balance != prev.unconfirmed_watch_only_balance ||
               immature_watch_only_balance != prev.immature_watch_only_balance ||
               basecoin_balance != prev.basecoin_balance || basecoin_immature_balance != prev.basecoin_immature_balance ||
               basecoin_unconfirmed_balance != prev.basecoin_unconfirmed_balance ||
               ct_balance != prev.ct_balance || ct_unconfirmed_balance != prev.ct_unconfirmed_balance ||
               ct_immature_balance != prev.ct_immature_balance || ring_ct_balance != prev.ring_ct_balance ||
               ring_ct_unconfirmed_balance != prev.ring_ct_unconfirmed_balance ||
               ring_ct_immature_balance != prev.ring_ct_immature_balance || zerocoin_balance != prev.zerocoin_balance ||
               zerocoin_unconfirmed_balance != prev.zerocoin_unconfirmed_balance ||
               zerocoin_immature_balance != prev.zerocoin_unconfirmed_balance;
    }
};

// Wallet transaction information.
struct WalletTx
{
    CTransactionRef tx;
    std::vector<isminetype> txin_is_mine;
    std::vector<isminetype> txout_is_mine;
    std::vector<CTxDestination> txout_address;
    std::vector<isminetype> txout_address_is_mine;
    CAmount credit = 0;
    CAmount debit = 0;
    CAmount change = 0;
    int64_t time;
    std::map<std::string, std::string> value_map;
    bool is_coinbase = false;
    bool is_coinstake = false;
    bool is_my_zerocoin_mint = false;
    bool is_my_zerocoin_spend = false;
    bool is_anon_send = false;
    bool is_anon_recv = false;
    bool has_rtx;
    std::map<unsigned int, CAmount> map_anon_value_out;
    std::map<unsigned int, CAmount> map_anon_value_in;
    std::pair<int, CAmount> ct_fee;
    CTransactionRecord rtx;
};

//! Updated transaction status.
struct WalletTxStatus
{
    int block_height;
    int blocks_to_maturity;
    int depth_in_main_chain;
    unsigned int time_received;
    uint32_t lock_time;
    bool is_final;
    bool is_trusted;
    bool is_abandoned;
    bool is_coinbase;
    bool is_in_main_chain;
};

//! Wallet transaction output.
struct WalletTxOut
{
    CTxOutBaseRef pout;
    int64_t time;
    int depth_in_main_chain = -1;
    bool is_spent = false;
};

//! Return implementation of Wallet interface. This function will be undefined
//! in builds where ENABLE_WALLET is false.
std::unique_ptr<Wallet> MakeWallet(const std::shared_ptr<CWallet>& wallet);

} // namespace interfaces

#endif // BITCOIN_INTERFACES_WALLET_H
