// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef UNLOCKPASSWORDDIALOG_H
#define UNLOCKPASSWORDDIALOG_H

#include <QDialog>

namespace Ui {
class UnlockPasswordDialog;
}

class WalletModel;

class UnlockPasswordDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UnlockPasswordDialog(bool fForStakingOnly, WalletModel* model, QWidget *parent = nullptr);
    ~UnlockPasswordDialog();
private Q_SLOTS:
    void onEscapeClicked();
    void onUnlockClicked();
private:
    Ui::UnlockPasswordDialog *ui;
    WalletModel* walletModel;
    bool fForStakingOnly;
};

#endif // UNLOCKPASSWORDDIALOG_H
