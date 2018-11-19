// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/transactionrecord.h>

#include <consensus/consensus.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <timedata.h>
#include <validation.h>

#include <stdint.h>


/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction()
{
    // There are currently no cases where we hide transactions, but
    // we may want to use this in the future for things like RBF.
    return true;
}

/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const interfaces::WalletTx& wtx)
{
    QList<TransactionRecord> parts;
    int64_t nTime = wtx.time;
    CAmount nCredit = wtx.credit;
    CAmount nDebit = wtx.debit;
    CAmount nNet = nCredit - nDebit;
    uint256 hash = wtx.tx->GetHash();
    std::map<std::string, std::string> mapValue = wtx.value_map;

    if (wtx.is_coinstake) {
        if (wtx.tx->IsZerocoinSpend() && (wtx.is_my_zerocoin_spend || wtx.is_my_zerocoin_mint)) {
            TransactionRecord sub(hash, nTime);
            sub.involvesWatchAddress = false;
            sub.type = TransactionRecord::ZeroCoinStake;
            sub.address = mapValue["zerocoinmint"];
            sub.credit = 0;
            for (const auto& pOut : wtx.tx->vpout) {
                if (pOut->IsZerocoinMint())
                    sub.credit += pOut->GetValue();
            }
            sub.debit -= wtx.tx->vin[0].GetZerocoinSpent();
            parts.append(sub);
        }
    } else if (wtx.tx->IsZerocoinSpend()) {
        //zerocoin spend outputs
        bool fFeeAssigned = false;
        for (unsigned int nOut = 0; nOut < wtx.tx->vpout.size(); nOut++) {
            const auto& pOut = wtx.tx->vpout[nOut];
            isminetype mine = wtx.txout_is_mine[nOut];

            // Process ringct and stealth elsewhere
            CTxOut txout;
            if (!pOut->GetTxOut(txout))
                continue;

            // change that was reminted as zerocoins
            if (txout.IsZerocoinMint()) {
                // do not display record if this isn't from our wallet
                if (!wtx.is_my_zerocoin_spend)
                    continue;

                TransactionRecord sub(hash, nTime);
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                sub.type = TransactionRecord::ZeroCoinSpendRemint;
                sub.address = mapValue["zerocoinmint"];
                if (!fFeeAssigned) {
                    sub.debit -= (wtx.tx->GetZerocoinSpent() - wtx.tx->GetValueOut());
                    fFeeAssigned = true;
                }
                sub.idx = parts.size();
                parts.append(sub);
                continue;
            }

            string strAddress = "";
            CTxDestination address;
            if (ExtractDestination(txout.scriptPubKey, address))
                strAddress = CBitcoinAddress(address).ToString();

            // a zerocoinspend that was sent to an address held by this wallet
            if (mine) {
                TransactionRecord sub(hash, nTime);
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (wtx.is_my_zerocoin_spend) {
                    sub.type = TransactionRecord::ZeroCoinSpendSelf;
                } else {
                    sub.type = TransactionRecord::ZeroCoinRecv;
                    sub.credit = txout.nValue;
                }
                sub.address = mapValue["recvzerocoinspend"];
                if (strAddress != "")
                    sub.address = strAddress;
                sub.idx = parts.size();
                parts.append(sub);
                continue;
            }

            // spend is not from us, so do not display the spend side of the record
            if (!wtx.is_my_zerocoin_spend)
                continue;

            // zerocoin spend that was sent to someone else
            TransactionRecord sub(hash, nTime);
            sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
            sub.debit = -txout.nValue;
            sub.type = TransactionRecord::ZeroCoinSpend;
            sub.address = mapValue["zerocoinspend"];
            if (strAddress != "")
                sub.address = strAddress;
            sub.idx = parts.size();
            parts.append(sub);
        }
    } else if (nNet > 0 || wtx.is_coinbase) {
        //
        // Credit
        //
        for (unsigned int i = 0; i < wtx.tx->vpout.size(); i++) {
            CTxOut txout;
            if (!wtx.tx->vpout[i]->GetTxOut(txout))
                continue;
            isminetype mine = wtx.txout_is_mine[i];
            if (mine) {
                TransactionRecord sub(hash, nTime);
                CTxDestination address;
                sub.idx = i; // vout index
                sub.credit = txout.nValue;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (wtx.txout_address_is_mine[i]) {
                    // Received by Bitcoin Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.address = EncodeDestination(wtx.txout_address[i]);
                } else {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.address = mapValue["from"];
                }
                if (wtx.is_coinbase) {
                    // Generated
                    sub.type = TransactionRecord::Generated;
                }

                parts.append(sub);
            }
        }
    } else {
        bool involvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        for (isminetype mine : wtx.txin_is_mine) {
            if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if (fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        for (isminetype mine : wtx.txout_is_mine) {
            if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if (fAllToMe > mine) fAllToMe = mine;
        }

        if (fAllFromMe && fAllToMe) {
            // Payment to self
            CAmount nChange = wtx.change;
            parts.append(TransactionRecord(hash, nTime, TransactionRecord::SendToSelf, "",
                    -(nDebit - nChange), nCredit - nChange));
            parts.last().involvesWatchAddress = involvesWatchAddress;   // maybe pass to TransactionRecord as constructor argument
        } else if (fAllFromMe || wtx.tx->IsZerocoinMint()) {
            //
            // Debit
            //
            CAmount nTxFee = nDebit - wtx.tx->GetValueOut();

            for (unsigned int nOut = 0; nOut < wtx.tx->vpout.size(); nOut++) {
                CTxOut txout;
                if (!wtx.tx->vpout[nOut]->GetTxOut(txout))
                    continue;
                TransactionRecord sub(hash, nTime);
                sub.idx = nOut;
                sub.involvesWatchAddress = involvesWatchAddress;

                if(wtx.txout_is_mine[nOut]) {
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
                    continue;
                }

                if (txout.IsZerocoinMint()) {
                    sub.type = TransactionRecord::ZeroCoinMint;
                    sub.address = mapValue["zerocoinmint"];
                    sub.credit += txout.nValue;
                } else if (!boost::get<CNoDestination>(&wtx.txout_address[nOut])) {
                    if (wtx.tx->IsZerocoinMint())
                        continue;
                    // Sent to Bitcoin Address
                    sub.type = TransactionRecord::SendToAddress;
                    sub.address = EncodeDestination(wtx.txout_address[nOut]);
                } else {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.type = TransactionRecord::SendToOther;
                    sub.address = mapValue["to"];
                }

                CAmount nValue = txout.nValue;
                /* Add fee to first output */
                if (nTxFee > 0) {
                    nValue += nTxFee;
                    nTxFee = 0;
                }
                sub.debit = -nValue;

                parts.append(sub);
            }
        } else {
            //
            // Mixed debit transaction, can't break down payees
            //
            parts.append(TransactionRecord(hash, nTime, TransactionRecord::Other, "", nNet, 0));
            parts.last().involvesWatchAddress = involvesWatchAddress;
        }
    }

    return parts;
}

void TransactionRecord::updateStatus(const interfaces::WalletTxStatus& wtx, int numBlocks, int64_t adjustedTime)
{
    // Determine transaction status

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        wtx.block_height,
        wtx.is_coinbase ? 1 : 0,
        wtx.time_received,
        idx);
    status.countsForBalance = wtx.is_trusted && !(wtx.blocks_to_maturity > 0);
    status.depth = wtx.depth_in_main_chain;
    status.cur_num_blocks = numBlocks;

    if (!wtx.is_final)
    {
        if (wtx.lock_time < LOCKTIME_THRESHOLD)
        {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.lock_time - numBlocks;
        }
        else
        {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.lock_time;
        }
    }
    // For generated transactions, determine maturity
    else if(type == TransactionRecord::Generated)
    {
        if (wtx.blocks_to_maturity > 0)
        {
            status.status = TransactionStatus::Immature;

            if (wtx.is_in_main_chain)
            {
                status.matures_in = wtx.blocks_to_maturity;
            }
            else
            {
                status.status = TransactionStatus::NotAccepted;
            }
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    else
    {
        if (status.depth < 0)
        {
            status.status = TransactionStatus::Conflicted;
        }
        else if (status.depth == 0)
        {
            status.status = TransactionStatus::Unconfirmed;
            if (wtx.is_abandoned)
                status.status = TransactionStatus::Abandoned;
        }
        else if (status.depth < RecommendedNumConfirmations)
        {
            status.status = TransactionStatus::Confirming;
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    status.needsUpdate = false;
}

bool TransactionRecord::statusUpdateNeeded(int numBlocks) const
{
    return status.cur_num_blocks != numBlocks || status.needsUpdate;
}

QString TransactionRecord::getTxHash() const
{
    return QString::fromStdString(hash.ToString());
}

int TransactionRecord::getOutputIndex() const
{
    return idx;
}
