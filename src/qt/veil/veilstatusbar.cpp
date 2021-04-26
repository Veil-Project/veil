// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/veil/veilstatusbar.h>
#include <qt/veil/forms/ui_veilstatusbar.h>

#include <qt/bitcoingui.h>
#include <qt/clientmodel.h>
#include <qt/walletmodel.h>
#include <qt/veil/qtutils.h>
#include <iostream>
#include <timedata.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h> // For DEFAULT_DISABLE_WALLET
#endif

VeilStatusBar::VeilStatusBar(QWidget *parent, BitcoinGUI* gui) :
    QWidget(parent),
    ui(new Ui::VeilStatusBar),
    mainWindow(gui)
{
    ui->setupUi(this);

    connect(ui->btnSync, SIGNAL(clicked()), this, SLOT(onBtnSyncClicked()));
    connect(ui->btnSyncIndicator, SIGNAL(clicked()), this, SLOT(onBtnSyncClicked()));
#ifdef ENABLE_WALLET
    if (!gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
        connect(ui->btnLock, SIGNAL(clicked()), this, SLOT(onBtnLockClicked()));
    }
    else {
#endif
        ui->btnLock->setVisible(false);
        ui->checkStaking->setVisible(false);
#ifdef ENABLE_WALLET
    }
#endif
}

bool VeilStatusBar::getSyncStatusVisible() {
    return ui->btnSync->isVisible();
}

void VeilStatusBar::updateSyncStatus(QString status){
    ui->btnSync->setText(status);
}

void VeilStatusBar::updateSyncIndicator(int height){
    QString str;
    ui->btnSyncIndicator->setText(tr("Height: ") + str.setNum(height));
}

void VeilStatusBar::setSyncStatusVisible(bool fVisible) {
    ui->btnSync->setVisible(fVisible);
    syncFlag = fVisible;
}

void VeilStatusBar::onBtnSyncClicked(){
    mainWindow->showModalOverlay();
}

#ifdef ENABLE_WALLET
bool fBlockNextStakeCheckSignal = false;
void VeilStatusBar::setStakingText() {

    // Determine if staking is recently active. Note that this is not immediate effect. Staking could be disabled and it could take some time (activeStakingMaxTime) update state.
    int64_t nTimeLastHashing = 0;
    if (!mapHashedBlocks.empty()) {
        auto pindexBest = chainActive.Tip();
        if (mapHashedBlocks.count(pindexBest->GetBlockHash())) {
            nTimeLastHashing = mapHashedBlocks.at(pindexBest->GetBlockHash());
        } else if (mapHashedBlocks.count(pindexBest->pprev->GetBlockHash())) {
            nTimeLastHashing = mapHashedBlocks.at(pindexBest->pprev->GetBlockHash());
        }
    }
    bool fStakingActive = false;
    if (nTimeLastHashing)
        fStakingActive = GetAdjustedTime() + MAX_FUTURE_BLOCK_TIME - nTimeLastHashing < ACTIVE_STAKING_MAX_TIME;
	
    WalletModel::EncryptionStatus eStatus = this->walletModel->getEncryptionStatus();

    if (syncFlag){
	ui->checkStaking->setText("Staking Disabled while Syncing");
    }else if (WalletModel::Locked == eStatus) {
        ui->checkStaking->setText("Unlock wallet for Staking");
    }else if (this->walletModel->isStakingEnabled()) {
	if (fStakingActive) {
            ui->checkStaking->setText("Staking Enabled");
	}else{

            interfaces::Wallet& wallet = walletModel->wallet();
            interfaces::WalletBalances balances = wallet.getBalances();
            int64_t zerocoin_balance = balances.zerocoin_balance;

	    if (0.0 < zerocoin_balance) {
                ui->checkStaking->setText("Enabling...");
            }else{
                ui->checkStaking->setText("You need some zerocoin");
            }
	}
    }else{
	if (fStakingActive) {
	    ui->checkStaking->setText("Disabling...");
	}else{
	    ui->checkStaking->setText("Staking Disabled");
	}
    }
}

void VeilStatusBar::onCheckStakingClicked(bool res) {
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        return;

    // When our own dialog internally changes the checkstate, block signal from executing
    if (fBlockNextStakeCheckSignal) {
        fBlockNextStakeCheckSignal = false;
        return;
    }

    // Miner thread starts in init.cpp, but staking enabled flag is checked each iteration of the miner, so can be enabled or disabled here
    WalletModel::EncryptionStatus lockState = walletModel->getEncryptionStatus();

	if (gArgs.GetBoolArg("-exchangesandservicesmode", false)) {
		QString dialogMsg = "Staking is disabled in exchange mode";
		openToastDialog(dialogMsg, mainWindow);
		fBlockNextStakeCheckSignal = true;
		ui->checkStaking->setChecked(false);
		setStakingText();
		return;
	} else if (lockState == WalletModel::Locked) {
		QString dialogMsg = "Must unlock wallet before staking can be enabled";
		openToastDialog(dialogMsg, mainWindow);
		fBlockNextStakeCheckSignal = true;
		ui->checkStaking->setChecked(false);
		setStakingText();

		mainWindow->showHide(true);
		UnlockPasswordDialog *dialog = new UnlockPasswordDialog(/*fUnlockForStakingOnly*/true, this->walletModel, mainWindow);
		if(openDialogWithOpaqueBackground(dialog, mainWindow, 4)){
			openToastDialog("Wallet unlocked for staking", mainWindow);
		}else{
			openToastDialog("Wallet not unlocked for staking", mainWindow);
		}
	}else{
		if(!this->walletModel->isStakingEnabled()){
			this->walletModel->setStakingEnabled(true);
			mainWindow->updateWalletStatus();
			openToastDialog("Enabling staking - this may take a few minutes", mainWindow);
			setStakingText();
		}else {
			this->walletModel->setStakingEnabled(false);
			mainWindow->updateWalletStatus();
			openToastDialog("Disabling staking - this may take a few minutes", mainWindow);
			setStakingText();
		}
	}
}

bool fBlockNextBtnLockSignal = false;
void VeilStatusBar::onBtnLockClicked()
{
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        return;

    if(walletModel->getEncryptionStatus() == WalletModel::Unlocked || walletModel->getEncryptionStatus() == WalletModel::UnlockedForStakingOnly){
        if (walletModel->setWalletLocked(true, false)){
            ui->btnLock->setIcon(QIcon(":/icons/ic-locked-png"));
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
                ui->btnLock->setIcon(QIcon(":/icons/ic-unlocked-png"));
                openToastDialog("Wallet unlocked", mainWindow);
            } else {
                openToastDialog("Wallet failed to unlock", mainWindow);
            }
        } else {
            if(mainWindow->encryptWallet(true)){
                ui->btnLock->setIcon(QIcon(":/icons/ic-locked-png"));
                openToastDialog("Wallet locked", mainWindow);
            }
        }

    }

    updateStakingCheckbox();
}

void VeilStatusBar::setClientModel(ClientModel *model){
    this->clientModel = model;
}

void VeilStatusBar::setNumBlocks(const QDateTime& blockDate)
{
    if (!clientModel)
        return;
    
    // Set the informative last block time label.
    ui->labelLastBlockTime_Content->setText(blockDate.toString());
}    

void VeilStatusBar::setWalletModel(WalletModel *model)
{
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        return;

    this->walletModel = model;
    connect(ui->checkStaking, SIGNAL(toggled(bool)), this, SLOT(onCheckStakingClicked(bool)));
    connect(walletModel, SIGNAL(encryptionStatusChanged()), this, SLOT(updateLockCheckbox()));

    WalletModel::EncryptionStatus lockState = walletModel->getEncryptionStatus();
    bool lockStatus = lockState == WalletModel::Locked || lockState == WalletModel::UnlockedForStakingOnly;
    ui->btnLock->setChecked(lockStatus);
    ui->btnLock->setIcon(QIcon( (lockStatus) ? ":/icons/ic-locked-png" : ":/icons/ic-unlocked-png"));

    updateStakingCheckbox();
    updateLockCheckbox();
}

void VeilStatusBar::updateLockCheckbox(){
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        return;

    if(walletModel) {
        WalletModel::EncryptionStatus lockState = walletModel->getEncryptionStatus();
        bool lockStatus = lockState == WalletModel::Locked || lockState == WalletModel::UnlockedForStakingOnly;
        if (ui->btnLock->isChecked() != lockStatus) {
            ui->btnLock->setChecked(lockStatus);
            ui->btnLock->setIcon(QIcon( (lockStatus) ? ":/icons/ic-locked-png" : ":/icons/ic-unlocked-png"));
        }

        QString strToolTip;
        if (lockState == WalletModel::Locked)
            strToolTip = tr("Wallet is locked for all transaction types.");
        else if (lockState == WalletModel::UnlockedForStakingOnly)
            strToolTip = tr("Wallet is unlocked for staking transactions only.");
        else
            strToolTip = tr("Wallet is unlocked.");

        ui->btnLock->setStatusTip(strToolTip);
        ui->btnLock->setToolTip(strToolTip);
    }
}

void VeilStatusBar::updateStakingCheckbox()
{
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        return;

    if(walletModel) {
        WalletModel::EncryptionStatus lockState = walletModel->getEncryptionStatus();

	ui->checkStaking->setEnabled(!syncFlag);
        setStakingText();

	bool stakingStatus = walletModel->isStakingEnabled() && lockState != WalletModel::Locked && !syncFlag;
        if (ui->checkStaking->isChecked() != stakingStatus) {
            fBlockNextStakeCheckSignal = true;
            ui->checkStaking->setChecked(stakingStatus);
	    return;
        }
    }
}

#endif

VeilStatusBar::~VeilStatusBar()
{
    delete ui;
}

