// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TRANSACTIONRECORD_H
#define BITCOIN_QT_TRANSACTIONRECORD_H

#include <amount.h>
#include <uint256.h>

#include <QList>
#include <QString>

namespace interfaces {
class Node;
class Wallet;
struct WalletTx;
struct WalletTxStatus;
}

/** UI model for transaction status. The transaction status is the part of a transaction that will change over time.
 */
class TransactionStatus
{
public:
    TransactionStatus():
        countsForBalance(false), sortKey(""),
        matures_in(0), status(Unconfirmed), depth(0), open_for(0), cur_num_blocks(-1)
    { }

    enum Status {
        Confirmed,          /**< Have 6 or more confirmations (normal tx) or fully mature (mined tx) **/
        /// Normal (sent/received) transactions
        OpenUntilDate,      /**< Transaction not yet final, waiting for date */
        OpenUntilBlock,     /**< Transaction not yet final, waiting for block */
        Unconfirmed,        /**< Not yet mined into a block **/
        Confirming,         /**< Confirmed, but waiting for the recommended number of confirmations **/
        Conflicted,         /**< Conflicts with other transaction or mempool **/
        Abandoned,          /**< Abandoned from the wallet **/
        /// Generated (mined) transactions
        Immature,           /**< Mined but waiting for maturity */
        NotAccepted         /**< Mined but not accepted */
    };

    /// Transaction counts towards available balance
    bool countsForBalance;
    /// Sorting key based on status
    std::string sortKey;

    /** @name Generated (mined) transactions
       @{*/
    int matures_in;
    /**@}*/

    /** @name Reported status
       @{*/
    Status status;
    qint64 depth;
    qint64 open_for; /**< Timestamp if status==OpenUntilDate, otherwise number
                      of additional blocks that need to be mined before
                      finalization */
    /**@}*/

    /** Current number of blocks (to know whether cached status is still valid) */
    int cur_num_blocks;

    bool needsUpdate;
};

/** UI model for a transaction. A core transaction can be represented by multiple UI transactions if it has
    multiple outputs.
 */
class TransactionRecord
{
public:
    enum Type
    {
        Other,
        Generated,
        SendToAddress,
        SendToOther,
        RecvWithAddress,
        RecvFromOther,
        SendToSelf,
        CTSendToSelf,
        CTSendToAddress,
        CTRecvWithAddress,
        CTGenerated,
        RingCTSendToSelf,
        RingCTSendToAddress,
        RingCTRecvWithAddress,
        RingCTGenerated,
        ZeroCoinMint,
        ZeroCoinSpend,
        ZeroCoinSpendRemint,
        ZeroCoinSpendSelf,
        ZeroCoinRecv,
        ZeroCoinStake,
        ConvertBasecoinToCT,
        ConvertBasecoinToRingCT,
        ConvertCtToRingCT,
        ConvertCtToBasecoin,
        ConvertRingCtToCt,
        ConvertRingCtToBasecoin,
        ConvertZerocoinToCt,
        ZeroCoinMintFromCt,
        ZeroCoinMintFromRingCt
    };

    /** Number of confirmation recommended for accepting a transaction */
    static const int RecommendedNumConfirmations = 6;

    TransactionRecord():
            hash(), time(0), size(0), type(Other), address(""), debit(0), credit(0), idx(0)
    {
    }

    TransactionRecord(uint256 _hash, qint64 _time, int _size):
            hash(_hash), time(_time), size(_size), type(Other), address(""), debit(0),
            credit(0), idx(0)
    {
    }

    TransactionRecord(uint256 _hash, qint64 _time, int _size,
                Type _type, const std::string &_address,
                const CAmount& _debit, const CAmount& _credit):
            hash(_hash), time(_time), size (_size), type(_type), address(_address), debit(_debit), credit(_credit),
            idx(0)
    {
    }

    TransactionRecord(uint256 _hash, qint64 _time, int _size,
                      Type _type, const std::string &_address,
                      const CAmount& _debit, const CAmount& _credit,
                        const CAmount& _fee, int _outputsSize, int _inputsSize, int _confirmations, int _computetime):

            hash(_hash), time(_time), size(_size), type(_type), address(_address), debit(_debit), credit(_credit),
            fee(_fee), outputsSize(_outputsSize), inputsSize(_inputsSize), confirmations(_confirmations), computetime(_computetime), idx(0)
    {
    }

    /** Decompose CWallet transaction to model transaction records.
     */
    static bool showTransaction();
    static QList<TransactionRecord> decomposeTransaction(const interfaces::WalletTx& wtx);

    /** @name Immutable transaction attributes
      @{*/
    uint256 hash;
    qint64 time;
    int size;
    Type type;
    std::string address;
    CAmount debit = 0;
    CAmount credit = 0;
    CAmount fee = 0;
    int outputsSize = 0;
    int inputsSize = 0;
    int confirmations = 0;
    int computetime = 0;

    /**@}*/

    /** Subtransaction index, for sort key */
    int idx;

    /** Status: can change with block chain update */
    TransactionStatus status;

    /** Whether the transaction was sent/received with a watch-only address */
    bool involvesWatchAddress;

    /** Return the unique identifier for this transaction (part) */
    QString getTxHash() const;

    uint256 getHash(){ return this->hash; }
    CAmount getAmount(){ return this->credit - this->debit; }
    CAmount getFee(){return this->fee;}
    int getOutputsSize() {return this->outputsSize;}
    int getInputsSize() {return this->inputsSize;}
    int getConfirmations() {return this->confirmations;}
    std::string getAddress() {return this->address;}
    int getComputeTime() {return this->computetime;}

    /** Return the output index of the subtransaction  */
    int getOutputIndex() const;

    /** Update status from core wallet tx.
     */
    void updateStatus(const interfaces::WalletTxStatus& wtx, int numBlocks, int64_t adjustedTime);

    /** Return whether a status update is needed.
     */
    bool statusUpdateNeeded(int numBlocks) const;

    std::string statusToString(){
        switch (status.status){
            case TransactionStatus::Abandoned:
                return "Abandoned";
            case TransactionStatus::Confirmed:
                return "Confirmed";
            case TransactionStatus::OpenUntilDate:
                return "OpenUntilDate";
            case TransactionStatus::OpenUntilBlock:
                return "OpenUntilBlock";
            case TransactionStatus::Unconfirmed:
                return "Unconfirmed";
            case TransactionStatus::Confirming:
                return "Confirming";
            case TransactionStatus::Conflicted:
                return "Conflicted";
            case TransactionStatus::Immature:
                return "Immature";
            case TransactionStatus::NotAccepted:
                return "NotAccepted";
            default:
                return "No status";
        }
    }

};

#endif // BITCOIN_QT_TRANSACTIONRECORD_H
