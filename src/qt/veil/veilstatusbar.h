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
    void setSyncStatusVisible(bool fVisible);
    void setWalletModel(WalletModel *model);
    void updateStakingCheckbox();

private Q_SLOTS:
    void onBtnSyncClicked();
    void onBtnLockClicked();
    void onCheckStakingClicked(bool res);

private:
    Ui::VeilStatusBar *ui;
    BitcoinGUI* mainWindow;
    WalletModel *walletModel = nullptr;
    UnlockPasswordDialog *unlockPasswordDialog = nullptr;

    bool preparingFlag = false;

    void updateLockCheckbox();
};

#endif // VEILSTATUSBAR_H
