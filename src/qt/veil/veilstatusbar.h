// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEILSTATUSBAR_H
#define VEILSTATUSBAR_H

#include <QWidget>
#include <QDateTime>
#include <miner.h>
#include "unlockpassworddialog.h"

class BitcoinGUI;
class WalletModel;
class ClientModel;
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
    void setClientModel(ClientModel *clientModel);
    void updateStakingCheckbox();
    void setNumBlocks(const QDateTime& blockDate);
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
    ClientModel *clientModel = nullptr;
    UnlockPasswordDialog *unlockPasswordDialog = nullptr;
#ifdef ENABLE_WALLET
    void setStakingText();
#endif

    bool preparingFlag = false;
    bool syncFlag = true;
    static const int64_t ACTIVE_STAKING_MAX_TIME = 70;
};

#endif // VEILSTATUSBAR_H
