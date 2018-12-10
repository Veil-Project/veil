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

    ui->checkStacking->setProperty("cssClass" , "switch");

    connect(ui->btnLock, SIGNAL(clicked()), this, SLOT(onBtnLockClicked()));
    connect(ui->btnSync, SIGNAL(clicked()), this, SLOT(onBtnSyncClicked()));
    connect(ui->checkStacking, SIGNAL(toggled(bool)), this, SLOT(onCheckStakingClicked(bool)));
}

void VeilStatusBar::updateSyncStatus(QString status){
    ui->btnSync->setText(status);
}

void VeilStatusBar::onBtnSyncClicked(){
    mainWindow->showModalOverlay();
}

bool fBlockNextStakeCheckSignal = false;
void VeilStatusBar::onCheckStakingClicked(bool res)
{
    // When our own dialog internally changes the checkstate, block signal from executing
    if (fBlockNextStakeCheckSignal) {
        fBlockNextStakeCheckSignal = false;
        return;
    }

    // Miner thread starts in init.cpp, but staking enabled flag is checked each iteration of the miner, so can be enabled or disabled here
    WalletModel::EncryptionStatus lockState = walletModel->getEncryptionStatus();
    if (res){
        if (lockState == WalletModel::Locked) {
            openToastDialog("Must unlock wallet before staking can be enabled", mainWindow);
            fBlockNextStakeCheckSignal = true;
            ui->checkStacking->setCheckState(Qt::CheckState::Unchecked);
        } else {
            openToastDialog("Miner started", mainWindow);
        }
    }else{
        openToastDialog("Miner stopped", mainWindow);
    }

    if (lockState != WalletModel::Locked)
        this->walletModel->setStakingEnabled(res);

}

bool fBlockNextBtnLockSignal = false;
void VeilStatusBar::onBtnLockClicked()
{
    // When our own dialog internally changes the checkstate, block signal from executing
    if (fBlockNextBtnLockSignal) {
        fBlockNextBtnLockSignal = false;
        return;
    }

    mainWindow->encryptWallet(walletModel->getEncryptionStatus() != WalletModel::Locked);
    fBlockNextBtnLockSignal = true;
    updateStakingCheckbox();
}

void VeilStatusBar::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    updateStakingCheckbox();
}

void VeilStatusBar::updateStakingCheckbox()
{
    WalletModel::EncryptionStatus lockState = walletModel->getEncryptionStatus();
    if(lockState == WalletModel::Locked || lockState == WalletModel::UnlockedForStakingOnly){
        ui->btnLock->setChecked(true);
    }else{
        ui->btnLock->setChecked(false);
    }

    if (walletModel->isStakingEnabled() && lockState != WalletModel::Locked)
        ui->checkStacking->setCheckState(Qt::CheckState::Checked);
    else
        ui->checkStacking->setCheckState(Qt::CheckState::Unchecked);
}

VeilStatusBar::~VeilStatusBar()
{
    delete ui;
}
