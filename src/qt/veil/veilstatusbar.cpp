#include <qt/veil/veilstatusbar.h>
#include <qt/veil/forms/ui_veilstatusbar.h>

#include <qt/bitcoingui.h>
#include <qt/walletmodel.h>
#include <qt/veil/qtutils.h>
#include <iostream>

VeilStatusBar::VeilStatusBar(QWidget *parent, BitcoinGUI* gui) :
    QWidget(parent),
    mainWindow(gui),
    ui(new Ui::VeilStatusBar)
{
    ui->setupUi(this);

    ui->checkStaking->setProperty("cssClass" , "switch");

    connect(ui->btnLock, SIGNAL(clicked()), this, SLOT(onBtnLockClicked()));
    connect(ui->btnSync, SIGNAL(clicked()), this, SLOT(onBtnSyncClicked()));
}

bool VeilStatusBar::getSyncStatusVisible() {
    return ui->btnSync->isVisible();
}

void VeilStatusBar::updateSyncStatus(QString status){
    ui->btnSync->setText(status);
}

void VeilStatusBar::setSyncStatusVisible(bool fVisible) {
    ui->btnSync->setVisible(fVisible);
}

void VeilStatusBar::onBtnSyncClicked(){
    mainWindow->showModalOverlay();
}

bool fBlockNextStakeCheckSignal = false;
void VeilStatusBar::onCheckStakingClicked(bool res) {
    // When our own dialog internally changes the checkstate, block signal from executing
    if (fBlockNextStakeCheckSignal) {
        fBlockNextStakeCheckSignal = false;
        return;
    }

    // Miner thread starts in init.cpp, but staking enabled flag is checked each iteration of the miner, so can be enabled or disabled here
    WalletModel::EncryptionStatus lockState = walletModel->getEncryptionStatus();
    if (res){
        if (gArgs.GetBoolArg("-exchangesandservicesmode", false) || lockState == WalletModel::Locked) {
            QString dialogMsg = gArgs.GetBoolArg("-exchangesandservicesmode", false) ? "Staking is disabled in exchange mode" : "Must unlock wallet before staking can be enabled";
            openToastDialog(dialogMsg, mainWindow);
            fBlockNextStakeCheckSignal = true;
            ui->checkStaking->setChecked(false);
            return;
        }else{
            this->walletModel->setStakingEnabled(true);
            mainWindow->updateWalletStatus();
            openToastDialog("Staking enabled", mainWindow);
        }
    } else {
        this->walletModel->setStakingEnabled(false);
        mainWindow->updateWalletStatus();
        openToastDialog("Staking disabled", mainWindow);
    }

}

bool fBlockNextBtnLockSignal = false;
void VeilStatusBar::onBtnLockClicked()
{
    // When our own dialog internally changes the checkstate, block signal from executing
    if (fBlockNextBtnLockSignal) {
        fBlockNextBtnLockSignal = false;
        return;
    }

    if(walletModel->getEncryptionStatus() == WalletModel::Unlocked || walletModel->getEncryptionStatus() == WalletModel::UnlockedForStakingOnly){
        if (walletModel->setWalletLocked(true, false)){
            openToastDialog("Wallet locked", mainWindow);
        }else{
            openToastDialog("Wallet not locked", mainWindow);
        }
    }else{
        bool isLocked = walletModel->getEncryptionStatus() == WalletModel::Locked;
        if (isLocked) {
            mainWindow->showHide(true);
            if(!unlockPasswordDialog)
                unlockPasswordDialog = new UnlockPasswordDialog(/*fUnlockForStakingOnly*/false, walletModel, mainWindow);
            if (openDialogWithOpaqueBackground(unlockPasswordDialog, mainWindow, 4)) {
                mainWindow->updateWalletStatus();
                openToastDialog("Wallet unlocked", mainWindow);
            } else {
                openToastDialog("Wallet failed to unlock", mainWindow);
            }
        } else {
            mainWindow->encryptWallet(true);
            openToastDialog("Wallet locked", mainWindow);
        }

    }

    updateStakingCheckbox();
}

void VeilStatusBar::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    connect(ui->checkStaking, SIGNAL(toggled(bool)), this, SLOT(onCheckStakingClicked(bool)));
    updateStakingCheckbox();
}

void VeilStatusBar::updateLockCheckbox(){
    if(walletModel) {
        WalletModel::EncryptionStatus lockState = walletModel->getEncryptionStatus();
        bool lockStatus = lockState == WalletModel::Locked || lockState == WalletModel::UnlockedForStakingOnly;
        if (ui->btnLock->isChecked() != lockStatus) {
            ui->btnLock->setChecked(lockStatus);
            fBlockNextBtnLockSignal = true;
        }
    }
}

void VeilStatusBar::updateStakingCheckbox()
{
    if(walletModel) {
        WalletModel::EncryptionStatus lockState = walletModel->getEncryptionStatus();
        bool stakingStatus = walletModel->isStakingEnabled() && lockState != WalletModel::Locked;
        if (ui->checkStaking->isChecked() != stakingStatus) {
            fBlockNextStakeCheckSignal = true;
            ui->checkStaking->setChecked(stakingStatus);
            return;
        }
    }
}

VeilStatusBar::~VeilStatusBar()
{
    delete ui;
}
