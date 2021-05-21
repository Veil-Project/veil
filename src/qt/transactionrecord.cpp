// Copyright (c) 2011-2019 The Bitcoin Core developers
// Copyright (c) 2011-2019 The Particl developers
// Copyright (c) 2018-2021 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/transactionrecord.h>

#include <consensus/consensus.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <timedata.h>
#include <validation.h>

#include <stdint.h>

#include <veil/ringct/anonwallet.h>

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

    bool fZerocoinSpend = wtx.tx->IsZerocoinSpend();
    bool fZerocoinMint = wtx.tx->IsZerocoinMint();

    if (!fZerocoinSpend && wtx.tx->HasBlindedValues()) {
        const CTransactionRecord &rtx = wtx.rtx;

        const uint256 &hash = wtx.tx->GetHash();
        int nSize = wtx.tx->GetTotalSize();
        int64_t nTime = wtx.time;
        TransactionRecord sub(hash, nTime, nSize);
        sub.computetime = wtx.computetime;

        uint8_t nFlags = 0;
        OutputTypes outputType = OutputTypes::OUTPUT_NULL;
        OutputTypes inputType = rtx.GetInputType();
        CTxDestination address = CNoDestination();
        for (unsigned int i = 0; i < rtx.vout.size(); ++i) {
            const auto &r = rtx.vout[i];
            if (!fZerocoinMint && r.IsChange())
                continue;

            address = wtx.txout_address.at(i);

            nFlags |= r.nFlags;

            if (r.nType == OUTPUT_STANDARD) {
                outputType = OutputTypes::OUTPUT_STANDARD;
            } else if (r.nType == OUTPUT_CT) {
                outputType = OutputTypes::OUTPUT_CT;
            } else if (r.nType == OUTPUT_RINGCT) {
                outputType = OutputTypes::OUTPUT_RINGCT;
            };

            if (nFlags & ORF_OWNED)
                sub.credit += r.GetRawValue();
            if (nFlags & ORF_FROM)
                sub.debit -= r.GetRawValue();
        };

        if (sub.debit != 0)
            sub.debit -= wtx.ct_fee.second;

        if (address.type() != typeid(CNoDestination))
            sub.address = CBitcoinAddress(address).ToString();

        bool fUseStandard = false;

        if ((((nFlags & ORF_OWNED) || wtx.is_my_zerocoin_mint) && ((nFlags & ORF_FROM) || wtx.is_my_zerocoin_spend)) ||
            rtx.IsSendToSelf()) {

            sub.debit = -wtx.ct_fee.second;
            sub.credit = 0;
            sub.fee = wtx.ct_fee.second;
            if (inputType != outputType) {
                /** Type Conversion **/
                switch (inputType) {
                    case OUTPUT_STANDARD:
                        if (wtx.is_my_zerocoin_spend) {
                            sub.type = TransactionRecord::ConvertZerocoinToCt;
                        } else {
                            // converting basecoins
                            if (outputType == OUTPUT_CT)
                                sub.type = TransactionRecord::ConvertBasecoinToCT;
                            else if (outputType == OUTPUT_RINGCT)
                                sub.type = TransactionRecord::ConvertBasecoinToRingCT;
                        }
                        break;
                    case OUTPUT_CT:
                        if (wtx.is_my_zerocoin_mint) {
                            sub.type = TransactionRecord::ZeroCoinMintFromCt;
                        } else {
                            if (outputType == OUTPUT_RINGCT)
                                sub.type = TransactionRecord::ConvertCtToRingCT;
                            else if (outputType == OUTPUT_STANDARD)
                                sub.type = TransactionRecord::ConvertCtToBasecoin;
                        }
                        break;
                    case OUTPUT_RINGCT:
                        if (wtx.is_my_zerocoin_mint) {
                            sub.type = TransactionRecord::ZeroCoinMintFromRingCt;
                        } else {
                            if (outputType == OUTPUT_CT)
                                sub.type = TransactionRecord::ConvertRingCtToCt;
                            else if (outputType == OUTPUT_STANDARD)
                                sub.type = TransactionRecord::ConvertRingCtToBasecoin;
                        }
                        break;
                    default:
                        if (wtx.is_my_zerocoin_spend) {
                            sub.type = TransactionRecord::ConvertZerocoinToCt;
                        } else {
                            // converting basecoins
                            if (outputType == OUTPUT_CT)
                                sub.type = TransactionRecord::ConvertBasecoinToCT;
                            else if (outputType == OUTPUT_RINGCT)
                                sub.type = TransactionRecord::ConvertBasecoinToRingCT;
                        }
                        break;
                }
            } else {
                /** Send to Self **/
                switch (outputType) {
                    case OUTPUT_STANDARD:
                        if (wtx.is_my_zerocoin_spend && wtx.is_my_zerocoin_mint) {
                            sub.type = TransactionRecord::ZeroCoinSpendRemint;
                        } else if (wtx.is_my_zerocoin_spend) {
                            sub.type = TransactionRecord::ZeroCoinSpendSelf;
                        } else if (wtx.is_my_zerocoin_mint) {
                            sub.type = TransactionRecord::ZeroCoinMint;
                        } else {
                            sub.type = TransactionRecord::SendToSelf;
                        }
                        break;
                    case OUTPUT_CT:
                        sub.type = TransactionRecord::CTSendToSelf;
                        break;
                    case OUTPUT_RINGCT:
                        sub.type = TransactionRecord::RingCTSendToSelf;
                        break;
                    default:
                        sub.type = TransactionRecord::SendToSelf;
                        break;
                }
            }
        } else if (nFlags & ORF_OWNED && wtx.is_coinbase) {
            switch (outputType) {
                case OUTPUT_STANDARD:
                    sub.type = TransactionRecord::Generated;
                    break;
                case OUTPUT_CT:
                    sub.type = TransactionRecord::CTGenerated;
                    break;
                case OUTPUT_RINGCT:
                    sub.type = TransactionRecord::RingCTGenerated;
                    break;
                default:
                    sub.type = TransactionRecord::Generated;
                    break;
            }
        } else if (((nFlags & ORF_OWNED) || wtx.is_my_zerocoin_mint) && wtx.is_coinstake) {
            sub.type = TransactionRecord::ZeroCoinStake;
        } else if (nFlags & ORF_OWNED) {
            switch (outputType) {
                case OUTPUT_STANDARD:
                    if (wtx.tx->IsZerocoinSpend())
                        sub.type = TransactionRecord::ZeroCoinRecv;
                    else if (wtx.tx->IsZerocoinMint()) {
                        if (inputType == OUTPUT_STANDARD)
                            sub.type = TransactionRecord::ZeroCoinMint;
                        else if (inputType == OUTPUT_CT)
                            sub.type = TransactionRecord::ZeroCoinMintFromCt;
                        else if (inputType == OUTPUT_RINGCT)
                            sub.type = TransactionRecord::ZeroCoinMintFromRingCt;
                    }
                    else
                        sub.type = TransactionRecord::RecvWithAddress;
                    break;
                case OUTPUT_CT:
                    sub.type = TransactionRecord::CTRecvWithAddress;
                    break;
                case OUTPUT_RINGCT:
                    sub.type = TransactionRecord::RingCTRecvWithAddress;
                    break;
                default:
                    sub.type = TransactionRecord::RecvWithAddress;
                    break;
            }
        } else if ((nFlags & ORF_FROM) || wtx.is_my_zerocoin_spend) {
            if (rtx.nFlags & ORF_ANON_IN)
                sub.type = TransactionRecord::RingCTSendToAddress;
            else if (rtx.nFlags & ORF_BLIND_IN)
                sub.type = TransactionRecord::CTSendToAddress;
            else if (wtx.is_my_zerocoin_spend)
                sub.type = TransactionRecord::ZeroCoinSpend;
            else
                sub.type = TransactionRecord::SendToAddress;
        } else
            fUseStandard = true;

        if (!fUseStandard) {
            sub.involvesWatchAddress = nFlags & ORF_OWN_WATCH;
            parts.append(sub);
            return parts;
        }
    }

    int64_t nTime = wtx.time;
    CAmount nCredit = wtx.credit;
    CAmount nDebit = wtx.debit;
    CAmount nNet = nCredit - nDebit - wtx.ct_fee.second;

    uint256 hash = wtx.tx->GetHash();
    int nSize = wtx.tx->GetTotalSize();
    std::map<std::string, std::string> mapValue = wtx.value_map;

    if (wtx.is_coinstake) {
        if (wtx.tx->IsZerocoinSpend() && (wtx.is_my_zerocoin_spend || wtx.is_my_zerocoin_mint)) {
            TransactionRecord sub(hash, nTime, nSize);
            sub.computetime = wtx.computetime;
            sub.involvesWatchAddress = false;
            sub.type = TransactionRecord::ZeroCoinStake;
            sub.address = mapValue["zerocoinmint"];
            sub.credit = 0;
            sub.fee = 0;
            for (const auto& pOut : wtx.tx->vpout) {
                if (pOut->IsZerocoinMint())
                    sub.credit += pOut->GetValue();
            }

			// Remove the denomination amount that won the stake.
            sub.credit -= wtx.tx->vin[0].GetZerocoinSpent();

            parts.append(sub);
        }
    } else if (wtx.tx->IsZerocoinSpend()) {
        //zerocoin spend outputs
        bool fFeeAssigned = false;
        bool fAssignedSend = false;

        bool fAllToMe = true;
        for (unsigned int i = 0; i < wtx.txout_is_mine.size(); i++) {
            if (!wtx.txout_is_mine[i] && !(i == 0 && wtx.tx->HasBlindedValues())) { //don't count data output
                fAllToMe = false;
                break;
            }
        }
        for (unsigned int nOut = 0; nOut < wtx.tx->vpout.size(); nOut++) {
            const auto& pOut = wtx.tx->vpout[nOut];
            isminetype mine = wtx.txout_is_mine[nOut];
            if (pOut->GetType() == OUTPUT_DATA)
                continue;

            //Check for any records from anonwallet
            const COutputRecord* precord = nullptr;
            if (wtx.has_rtx) {
                precord = wtx.rtx.GetOutput(nOut);
            }

            // change that was reminted as zerocoins
            if (pOut->IsZerocoinMint()) {
                // do not display record if this isn't from our wallet
                if (!wtx.is_my_zerocoin_spend)
                    continue;

                TransactionRecord sub(hash, nTime, nSize);
                sub.computetime = wtx.computetime;
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

            CScript scriptPubKey;
            string strAddress = "";
            CTxDestination address;
            if (pOut->GetScriptPubKey(scriptPubKey)) {
                if (ExtractDestination(scriptPubKey, address))
                    strAddress = CBitcoinAddress(address).ToString();
            }

            // a zerocoinspend that was sent to an address held by this wallet
            if (mine || (precord && precord->IsReceive())) {
                TransactionRecord sub(hash, nTime, nSize);
                sub.computetime = wtx.computetime;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (wtx.is_my_zerocoin_spend) {
                    sub.type = TransactionRecord::ZeroCoinSpendSelf;
                    if (fAllToMe) {
                        //Only display one tx record for spend to self
                        sub.idx = parts.size();
                        parts.append(sub);
                        break;
                    }
                } else if (pOut->GetType() == OUTPUT_STANDARD) {
                    sub.type = TransactionRecord::ZeroCoinRecv;
                    sub.credit = pOut->GetValue();
                } else if (pOut->GetType() == OUTPUT_CT) {
                    sub.type = TransactionRecord::CTRecvWithAddress;
                    //Get full stealth address if available
                    std::string strKey = strprintf("stealth:%d", nOut);
                    if (wtx.value_map.count(strKey))
                        strAddress = wtx.value_map.at(strKey);

                    sub.address = strAddress;

                    // Value is blinded so must come from outputrecord
                    if (precord)
                        sub.credit = precord->GetAmount();
                }

                if (strAddress.empty())
                    sub.address = mapValue["recvzerocoinspend"];
                else
                    sub.address = strAddress;

                sub.fee = 0;
                sub.idx = parts.size();
                parts.append(sub);
                continue;
            }

            // spend is not from us, so do not display the spend side of the record
            if (!wtx.is_my_zerocoin_spend)
                continue;

            // zerocoin spend that was sent to someone else
            TransactionRecord sub(hash, nTime, nSize);
            sub.computetime = wtx.computetime;
            sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
            if (pOut->GetType() == OUTPUT_STANDARD)
                sub.debit = -pOut->GetValue();
            else if (pOut->GetType() == OUTPUT_CT) {
                sub.type = TransactionRecord::CTRecvWithAddress;
                //Get full stealth address if available
                std::string strKey = strprintf("stealth:%d", nOut);
                if (wtx.value_map.count(strKey))
                    strAddress = wtx.value_map.at(strKey);

                sub.address = strAddress;

                // Value is blinded so must come from outputrecord
                if (precord)
                    sub.debit = -precord->GetAmount();
                //todo fix this for rescans or instances of lost data
//                if (sub.debit <= 1 && !fAssignedSend && wtx.has_rtx) {
//                    sub.debit = wtx.rtx.GetValueSent();
//                    fAssignedSend = true;
//                }

            }

            sub.type = TransactionRecord::ZeroCoinSpend;
            sub.idx = parts.size();
            parts.append(sub);
        }
    } else if (nNet > 0 || wtx.is_coinbase) {
        //
        // Credit
        //
        for (unsigned int i = 0; i < wtx.tx->vpout.size(); i++) {
            isminetype mine = wtx.txout_is_mine[i];
            if (!mine)
                continue;

            TransactionRecord sub(hash, nTime, nSize);
            sub.computetime = wtx.computetime;
            sub.idx = i;
            sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;

            CTxOut txout;
            if (!wtx.tx->vpout[i]->GetTxOut(txout)) {
                //Anon type
                if (wtx.tx->vpout[i]->nVersion == OUTPUT_DATA) //probably fee
                    continue;

                //LogPrintf("%s: 148 ringctrevc credit=%d\n", __func__, wtx.map_anon_value_out.at(i));
                sub.type = TransactionRecord::RingCTRecvWithAddress;
                auto it = wtx.map_anon_value_out.find(i);
                if (it != wtx.map_anon_value_out.end()) {
                    //Make sure this is not a dummy
                    if (it->second == DUMMY_VALUE)
                        continue;
                    sub.credit = it->second;
                    parts.append(sub);
                }
                continue;
            }

            CTxDestination address;
            sub.credit = txout.nValue;
            if (wtx.txout_address_is_mine[i]) {
                // Received by Bitcoin Address
                sub.type = TransactionRecord::RecvWithAddress;
                bool fBech32 = (bool)boost::get<CScriptID>(&wtx.txout_address[i]);        
                sub.address = EncodeDestination(wtx.txout_address[i], fBech32);
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
            CAmount nTxFee = nDebit - wtx.tx->GetValueOut();
            if (wtx.ct_fee.second != 0)
                nTxFee = wtx.ct_fee.second;
            CAmount nChange = wtx.change;
            auto recordType = TransactionRecord::SendToSelf;
            if (wtx.is_anon_send || wtx.is_anon_recv)
                recordType = TransactionRecord::RingCTSendToSelf;
            else if (wtx.is_my_zerocoin_mint) {
                recordType = TransactionRecord::ZeroCoinMint;
            }

            parts.append(
                    TransactionRecord(
                            hash, nTime, nSize, recordType, "",
                    -(nDebit - nChange), nCredit - nChange, nTxFee, wtx.tx->GetNumVOuts(), wtx.tx->GetNumVOuts(), 0, wtx.computetime
                    )
            );
            parts.last().involvesWatchAddress = involvesWatchAddress;   // maybe pass to TransactionRecord as constructor argument
        } else if (fAllFromMe || wtx.tx->IsZerocoinMint()) {
            //
            // Debit
            //
            CAmount nTxFee = nDebit - wtx.tx->GetValueOut();
            if (wtx.ct_fee.second != 0)
                nTxFee = wtx.ct_fee.second;

            for (unsigned int nOut = 0; nOut < wtx.tx->vpout.size(); nOut++) {
                TransactionRecord sub(hash, nTime, nSize);
                sub.computetime = wtx.computetime;
                sub.idx = nOut;
                sub.involvesWatchAddress = involvesWatchAddress;
                sub.fee = nTxFee;

                if(wtx.txout_is_mine[nOut]) {
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
                    continue;
                }

                CTxOut txout;
                if (!wtx.tx->vpout[nOut]->GetTxOut(txout)) {
                    //Anon type
                    sub.type = TransactionRecord::RingCTSendToAddress;
                    auto it = wtx.map_anon_value_out.find(nOut);
                    if (it != wtx.map_anon_value_out.end()) {
                        auto nValue = it->second;
                        //Double check this is not a dummy
                        if (nValue == DUMMY_VALUE)
                            continue;
                        sub.debit = -nValue;
                        parts.append(sub);
                    }
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
                    bool fBech32 = (bool)boost::get<CScriptID>(&wtx.txout_address[nOut]);    
                    sub.address = EncodeDestination(wtx.txout_address[nOut], fBech32);
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
            parts.append(TransactionRecord(hash, nTime, nSize, TransactionRecord::Other, "", nNet, 0));
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
    else if(type == TransactionRecord::Generated || type == TransactionRecord::CTGenerated || type == TransactionRecord::RingCTGenerated)
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
    this->confirmations = status.depth;
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
