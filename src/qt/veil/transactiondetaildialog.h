// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TRANSACTIONDETAILDIALOG_H
#define TRANSACTIONDETAILDIALOG_H

#include <QDialog>
#include <QWidget>
#include <QUrl>

class WalletModel;
class TransactionRecord;
class OptionsModel;

namespace Ui {
class TransactionDetailDialog;
}

class TransactionDetailDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TransactionDetailDialog(QWidget *parent = nullptr, TransactionRecord *rec = nullptr, WalletModel *walletModel = nullptr);
    ~TransactionDetailDialog();

private Q_SLOTS:
    void onEscapeClicked();
    void onExplorerClicked();
private:
    Ui::TransactionDetailDialog *ui;
    QUrl explorerLink;
};

#endif // TRANSACTIONDETAILDIALOG_H
