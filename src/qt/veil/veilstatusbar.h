// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEILSTATUSBAR_H
#define VEILSTATUSBAR_H

#include <QWidget>
#include <miner.h>
#include "unlockpassworddialog.h"

class BitcoinGUI;
class WalletModel;
class QPushButton;

namespace Ui {
class VeilStatusBar;
}

class VeilStatusBar : public QWidget
{
    Q_OBJECT

public:
    explicit VeilStatusBar(QWidget *parent = 0, BitcoinGUI* gui = 0);
    ~VeilStatusBar();

    bool getSyncStatusVisible();
    void updateSyncStatus(QString status);
    void updateSyncIndicator(int height);
    void setSyncStatusVisible(bool fVisible);
#ifdef ENABLE_WALLET
    void setWalletModel(WalletModel *model);
    void updateStakingCheckbox();
#endif

private Q_SLOTS:
    void onBtnSyncClicked();
#ifdef ENABLE_WALLET
    void onBtnLockClicked();
    void onCheckStakingClicked(bool res);
    void updateLockCheckbox();
#endif

private:
    Ui::VeilStatusBar *ui;
    BitcoinGUI* mainWindow;
    WalletModel *walletModel = nullptr;
    UnlockPasswordDialog *unlockPasswordDialog = nullptr;

    bool preparingFlag = false;
};

#endif // VEILSTATUSBAR_H
