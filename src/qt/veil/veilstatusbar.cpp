#include <qt/veil/veilstatusbar.h>
#include <qt/veil/forms/ui_veilstatusbar.h>

#include <qt/bitcoingui.h>
#include <qt/walletmodel.h>
#include <qt/veil/qtutils.h>
#include <iostream>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h> // For DEFAULT_DISABLE_WALLET
#endif

VeilStatusBar::VeilStatusBar(QWidget *parent, BitcoinGUI* gui) :
    QWidget(parent),
    mainWindow(gui),
    ui(new Ui::VeilStatusBar)
{
    ui->setupUi(this);

    connect(ui->btnSync, SIGNAL(clicked()), this, SLOT(onBtnSyncClicked()));

#ifdef ENABLE_WALLET
    if (!gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
        connect(ui->btnLock, SIGNAL(clicked()), this, SLOT(onBtnLockClicked()));
    }
    else {
#endif
        ui->btnLock->setVisible(false);
        ui->checkStaking->setVisible(false);
        ui->checkPrecompute->setVisible(false);
#ifdef ENABLE_WALLET
    }
#endif

    int days = 7, hours = 23;

    ui->daySelector->setProperty("cssClass" , "btn-text-primary-inactive");
    ui->daySelector->addItem(tr("Days"));
    ui->daySelector->setItemData(0, Qt::AlignRight, Qt::TextAlignmentRole);
    for (int i = 0 ; i <= days ; ++i) {
        ui->daySelector->addItem( QString::number(i));
        ui->daySelector->setItemData(i+1, Qt::AlignRight, Qt::TextAlignmentRole);
    }

    ui->hourSelector->setProperty("cssClass" , "btn-text-primary-inactive");
    ui->hourSelector->addItem(tr("Hours"));
    ui->hourSelector->setItemData(0, Qt::AlignRight, Qt::TextAlignmentRole);
    for (int i = 0 ; i <= hours ; ++i) {
        ui->hourSelector->addItem( QString::number(i));
        ui->hourSelector->setItemData(i+1, Qt::AlignRight, Qt::TextAlignmentRole);
    }

    connect(ui->daySelector,SIGNAL(currentIndexChanged(const QString&)),this,SLOT(daySelected(const QString&)));
    connect(ui->hourSelector,SIGNAL(currentIndexChanged(const QString&)),this,SLOT(hourSelected(const QString&)));

    // Update status every 5 min
    timerId = startTimer(5*60*1000);

}

bool VeilStatusBar::getSyncStatusVisible() {
    return ui->btnSync->isVisible();
}

void VeilStatusBar::updateSyncStatus(QString status){
    ui->btnSync->setText(status);
}

void VeilStatusBar::setSyncStatusVisible(bool fVisible) {
    this->setStakeCounterVisible(!fVisible);
    ui->btnSync->setVisible(fVisible);
}

void VeilStatusBar::onBtnSyncClicked(){
    mainWindow->showModalOverlay();
}

void VeilStatusBar::setStakeCounterVisible(bool fVisible) {
    ui->daySelector->setVisible(fVisible);
    ui->hourSelector->setVisible(fVisible);
    ui->counterLabel->setVisible(fVisible);
    ui->stakeLabel->setVisible(fVisible);
}

void VeilStatusBar::updateCounter(uint64_t numberOfStakes) {
    ui->counterLabel->setText(QString::number(numberOfStakes));
}

void VeilStatusBar::daySelected(const QString& selectedStr) {

    int days = 0, hrs = 0;

    days = selectedStr.toInt();
    hrs = (ui->hourSelector->currentText()).toInt();
    this->updateCounter(this->calculateStakes(days, hrs));
}

void VeilStatusBar::hourSelected(const QString& selectedStr) {
    int days = 0, hrs = 0;

    hrs = selectedStr.toInt();
    days = (ui->daySelector->currentText()).toInt();
    this->updateCounter(this->calculateStakes(days, hrs));
}

uint64_t VeilStatusBar::calculateStakes(int days, int hrs) {

    interfaces::Wallet& wall = walletModel->wallet();
    int64_t cutOff = (days*24 + hrs) * 60;

    return wall.getCountOfStakes(GetTime() - cutOff);
}

void VeilStatusBar::timerEvent(QTimerEvent *event) {
    QMetaObject::invokeMethod(this, "refreshCounter", Qt::QueuedConnection);
}

void VeilStatusBar::refreshCounter() {
    int days = 0, hrs = 0;
    days = (ui->daySelector->currentText()).toInt();
    hrs = (ui->hourSelector->currentText()).toInt();
    this->updateCounter(this->calculateStakes(days, hrs) + 90);
}

#ifdef ENABLE_WALLET

bool fBlockNextStakeCheckSignal = false;
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

bool fBlockNextPrecomputeCheckSignal = false;
void VeilStatusBar::onCheckPrecomputeClicked(bool res) {
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        return;

    // When our own dialog internally changes the checkstate, block signal from executing
    if (fBlockNextPrecomputeCheckSignal) {
        fBlockNextPrecomputeCheckSignal = false;
        return;
    }

    WalletModel::EncryptionStatus lockState = walletModel->getEncryptionStatus();

    if (res){
        if (gArgs.GetBoolArg("-exchangesandservicesmode", false) || lockState == WalletModel::Locked) {
            QString dialogMsg = gArgs.GetBoolArg("-exchangesandservicesmode", false) ? "Precomputing is disabled in exchange mode" : "Must unlock wallet before precomputing can be enabled";
            openToastDialog(dialogMsg, mainWindow);
            fBlockNextPrecomputeCheckSignal = true;
            ui->checkPrecompute->setChecked(false);
            return;
        } else {
            std::string strStatus;
            if(!this->walletModel->StartPrecomputing(strStatus)) {
                openToastDialog("Failed to start precomputing", mainWindow);
                this->walletModel->setPrecomputingEnabled(false);
                return;
            }
            this->walletModel->setPrecomputingEnabled(true);
            mainWindow->updateWalletStatus();
            openToastDialog("Precomputing enabled", mainWindow);
        }
    } else {
        this->walletModel->setPrecomputingEnabled(false);
        this->walletModel->StopPrecomputing();
        mainWindow->updateWalletStatus();
        openToastDialog("Precomputing disabled", mainWindow);
    }

}

bool fBlockNextBtnLockSignal = false;
void VeilStatusBar::onBtnLockClicked()
{
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        return;

    // When our own dialog internally changes the checkstate, block signal from executing
    if (fBlockNextBtnLockSignal) {
        fBlockNextBtnLockSignal = false;
        return;
    }

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
    updatePrecomputeCheckbox();
}

void VeilStatusBar::setWalletModel(WalletModel *model)
{
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        return;

    this->walletModel = model;
    connect(ui->checkStaking, SIGNAL(toggled(bool)), this, SLOT(onCheckStakingClicked(bool)));
    connect(ui->checkPrecompute, SIGNAL(toggled(bool)), this, SLOT(onCheckPrecomputeClicked(bool)));
    connect(walletModel, SIGNAL(encryptionStatusChanged()), this, SLOT(updateLockCheckbox()));

    WalletModel::EncryptionStatus lockState = walletModel->getEncryptionStatus();
    bool lockStatus = lockState == WalletModel::Locked || lockState == WalletModel::UnlockedForStakingOnly;
    ui->btnLock->setChecked(lockStatus);
    ui->btnLock->setIcon(QIcon( (lockStatus) ? ":/icons/ic-locked-png" : ":/icons/ic-unlocked-png"));

    updateStakingCheckbox();
    updatePrecomputeCheckbox();
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
            fBlockNextBtnLockSignal = true;
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
        bool stakingStatus = walletModel->isStakingEnabled() && lockState != WalletModel::Locked;
        if (ui->checkStaking->isChecked() != stakingStatus) {
            fBlockNextStakeCheckSignal = true;
            ui->checkStaking->setChecked(stakingStatus);
            return;
        }
    }
}

void VeilStatusBar::updatePrecomputeCheckbox()
{
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        return;

    if(walletModel) {
        WalletModel::EncryptionStatus lockState = walletModel->getEncryptionStatus();
        bool precomputeStatus = walletModel->isPrecomputingEnabled() && lockState != WalletModel::Locked;
        if (ui->checkPrecompute->isChecked() != precomputeStatus) {
            fBlockNextPrecomputeCheckSignal = true;
            ui->checkPrecompute->setChecked(precomputeStatus);
            return;
        }
    }
}
#endif
VeilStatusBar::~VeilStatusBar()
{
    killTimer(timerId);
    delete ui;
}
