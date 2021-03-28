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

QString toggleOnBlue  = "QCheckBox {background-image: url(':/icons/ic-switch-on-png'); background-repeat:no-repeat; background-position:left center; color:#105aef; margin-right:5px;}";
QString toggleOnGrey  = "QCheckBox {background-image: url(':/icons/ic-switch-on-grey-png'); background-repeat:no-repeat; background-position:left center; color:#bababa; margin-right:5px;}";
QString toggleOffGrey = "QCheckBox {background-image: url(':/icons/ic-switch-off-png'); background-repeat:no-repeat; background-position:left center; color:#bababa; margin-right:5px;}";
QString toggleOffBlue = "QCheckBox {background-image: url(':/icons/ic-switch-off-blue-png'); background-repeat:no-repeat; background-position:left center; color:#105aef; margin-right:5px;}";


VeilStatusBar::VeilStatusBar(QWidget *parent, BitcoinGUI* gui) :
    QWidget(parent),
    ui(new Ui::VeilStatusBar),
    mainWindow(gui)
{
    ui->setupUi(this);

    stakingTextUpdateTimer = new QTimer(this);
    connect(stakingTextUpdateTimer, SIGNAL(timeout()), this, SLOT(setStakingText()));
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
	stakingTextUpdateTimer->stop();

	auto pwallet = GetMainWallet();

	bool fStakingActive = pwallet->IsStakingActive();

    WalletModel::EncryptionStatus eStatus = this->walletModel->getEncryptionStatus();

    if (syncFlag){
    	ui->checkStaking->setText("Staking Disabled while Syncing");
    	ui->checkStaking->setStyleSheet(toggleOffGrey);
    }else if (WalletModel::Locked == eStatus) {
        ui->checkStaking->setText("Unlock wallet for Staking");
        ui->checkStaking->setStyleSheet(toggleOffGrey);
    }else if (this->walletModel->isStakingEnabled()) {
		if (fStakingActive) {
				ui->checkStaking->setText("Staking Enabled");
				ui->checkStaking->setStyleSheet(toggleOnBlue);
		}else{
			CAmount confirmationsRemaining = pwallet->GetZTrackerPointer()->GetConfirmationsRemainingForStaking();
			if (confirmationsRemaining >=0) {
					if(confirmationsRemaining == 0){
						stakingTextUpdateTimer->start(5000);
						ui->checkStaking->setText("Enabling...");
					}
					else{
						QString str;
						LogPrintf("%s: Waiting on %d confirmations for staking\n", __func__, confirmationsRemaining);
						ui->checkStaking->setText("Staking requires " + str.setNum(confirmationsRemaining) + " more confirmations");
					}
			}else{
					if(confirmationsRemaining == -1){
						ui->checkStaking->setText("You need some zerocoin in order to stake");
					}
					else if(confirmationsRemaining == -2){
						ui->checkStaking->setText("Fetching zerocoin status...");
					}
			}
			ui->checkStaking->setStyleSheet(toggleOnGrey);
		}
    }else{
		if (fStakingActive) {
			stakingTextUpdateTimer->start(5000);
			ui->checkStaking->setText("Disabling...");
			ui->checkStaking->setStyleSheet(toggleOffBlue);
		}else{
			ui->checkStaking->setText("Staking Disabled");
			ui->checkStaking->setStyleSheet(toggleOffGrey);
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
	if (gArgs.GetBoolArg("-exchangesandservicesmode", false) || lockState == WalletModel::Locked) {
		QString dialogMsg = gArgs.GetBoolArg("-exchangesandservicesmode", false) ? "Staking is disabled in exchange mode" : "Must unlock wallet before staking can be enabled";
		openToastDialog(dialogMsg, mainWindow);
		fBlockNextStakeCheckSignal = true;
		ui->checkStaking->setChecked(false);
		setStakingText();
		return;
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

    auto pwallet = GetMainWallet();
    CAmount confirmationsRemaining = pwallet->GetZTrackerPointer()->GetConfirmationsRemainingForStaking();

	bool stakingStatus = !mapHashedBlocks.empty() && confirmationsRemaining == 0 && lockState != WalletModel::Locked && !syncFlag;
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

