#include <qt/veil/settings/settingswidget.h>

//#include "transactiondetaildialog.h"
#include <qt/veil/settings/settingspreferences.h>
#include <qt/veil/settings/settingsnetwork.h>
#include <qt/veil/settings/settingsminting.h>
#include <qt/veil/settings/settingsbackup.h>
#include <qt/veil/settings/settingsrestore.h>
#include <qt/veil/forms/ui_settingswidget.h>
#include <qt/veil/settings/settingschangepassword.h>
#include <qt/veil/settings/settingsfaq.h>
#include <qt/veil/settings/settingsadvance.h>
#include <qt/guiutil.h>
#include <qt/veil/unlockpassworddialog.h>
#include <veil/mnemonic/mnemonic.h>
#include <qt/veil/qtutils.h>
#include <QDebug>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QCompleter>
#include <QAbstractItemView>



SettingsWidget::SettingsWidget(WalletView *parent) :
    QWidget(parent),
    mainWindow(parent),
    ui(new Ui::SettingsWidget)
{

    ui->setupUi(this);

    this->setStyleSheet(GUIUtil::loadStyleSheet());

    ui->title->setProperty("cssClass" , "title");
    ui->titleOptions->setProperty("cssClass" , "title");
    ui->labelStacking->setProperty("cssClass" , "btn-text-settings");
    ui->checkBoxStaking->setProperty("cssClass" , "btn-check");

    //Backup
    ui->btnBackup->setProperty("cssClass" , "btn-text-settings");
    connect(ui->btnBackup,SIGNAL(clicked()),this,SLOT(onBackupClicked()));

    //Restore
    // No restore implemented on the backend for now for now.
    //ui->btnRestore->setProperty("cssClass" , "btn-text-settings");
    //connect(ui->btnRestore,SIGNAL(clicked()),this,SLOT(onRestoreClicked()));

    //Password
    //ui->btnPassword
    ui->btnPassword->setProperty("cssClass" , "btn-text-settings");
    connect(ui->btnPassword,SIGNAL(clicked()),this,SLOT(onChangePasswordClicked()));

    //Preferences
    //ui->btnPreference
    ui->btnPreferences->setProperty("cssClass" , "btn-text-settings");
    connect(ui->btnPreferences,SIGNAL(clicked()),this,SLOT(onPreferenceClicked()));

    ui->btnMinting->setProperty("cssClass" , "btn-text-settings");
    connect(ui->btnMinting,SIGNAL(clicked()),this,SLOT(onMintingClicked()));

    //FAQ
    ui->btnFaq->setProperty("cssClass" , "btn-text-settings");
    connect(ui->btnFaq,SIGNAL(clicked()),this,SLOT(onFaqClicked()));

    //Advance
    ui->btnAdvance->setProperty("cssClass" , "btn-text-settings");
    connect(ui->btnAdvance,SIGNAL(clicked()),this,SLOT(onAdvanceClicked()));

}

void SettingsWidget::openDialog(QDialog *dialog){
    openDialogWithOpaqueBackground(dialog, mainWindow->getGUI());
}

void SettingsWidget::onLabelStakingClicked(){
    this->onCheckStakingClicked(false);
}

bool checkChangedManually = false;
void SettingsWidget::onCheckStakingClicked(bool res) {
    if(checkChangedManually){
        checkChangedManually = false;
        return;
    }
    bool error = false;
    try {
        if(!res){
            if(walletModel->getEncryptionStatus() == WalletModel::Unencrypted){
                if (mainWindow->getGUI()->encryptWallet(true)){
                    // This closes the wallet..
                    openToastDialog("Wallet encrypted", mainWindow->getGUI());
                }else{
                    openToastDialog("Wallet not encrypted", mainWindow->getGUI());
                    error = true;
                }
            }else if (walletModel->getEncryptionStatus() != WalletModel::Locked){
                if (walletModel->setWalletLocked(true, false)){
                    openToastDialog("Wallet locked", mainWindow->getGUI());
                }else{
                    openToastDialog("Wallet not locked", mainWindow->getGUI());
                    error = true;
                }
            }else{
                error = true;
            }
        }else{
            mainWindow->getGUI()->showHide(true);
            UnlockPasswordDialog *dialog = new UnlockPasswordDialog(/*fUnlockForStakingOnly*/true, mainWindow->getWalletModel(), mainWindow->getGUI());
            if(openDialogWithOpaqueBackground(dialog, mainWindow->getGUI(), 4)){
                openToastDialog("Wallet unlocked for staking", mainWindow->getGUI());
            }else{
                openToastDialog("Wallet not unlocked for staking", mainWindow->getGUI());
                error = true;
            }
        }
    } catch (std::exception e) {
        qDebug() << e.what();
        error = true;
    }

    WalletModel::EncryptionStatus status = walletModel->getEncryptionStatus();
    if(status != WalletModel::Unencrypted) {
        bool isChecked = walletModel->isStakingEnabled() && status != WalletModel::Locked;
        if(ui->checkBoxStaking->isChecked() != isChecked){
            checkChangedManually = true;
            ui->checkBoxStaking->setChecked(isChecked);
        }
        updateStakingCheckboxStatus();
    }
}


void SettingsWidget::onLockWalletClicked(){
    try {
        mainWindow->getGUI()->showHide(true);
        UnlockPasswordDialog *dialog = new UnlockPasswordDialog(/*fUnlockForStakingOnly*/false, mainWindow->getWalletModel(), mainWindow->getGUI());
        openDialogWithOpaqueBackground(dialog, mainWindow->getGUI(), 4);
    } catch (std::exception e) {
        qDebug() << e.what();
    }
}

void SettingsWidget::onFaqClicked(){
    try {
        mainWindow->getGUI()->showHide(true);
        SettingsFaq *dialog = new SettingsFaq(mainWindow->getGUI());
        openDialogWithOpaqueBackgroundFullScreen(dialog, mainWindow->getGUI());
    } catch (std::exception e) {
        qDebug() << e.what();
    }

}

void SettingsWidget::onAdvanceClicked(){
    try {
        mainWindow->getGUI()->showDebugWindow();
    } catch (std::exception e) {
        qDebug() << e.what();
    }

}

void SettingsWidget::onResetOptionClicked(){
    try {

        //SendControlDialog *dialog = new SendControlDialog(mainWindow);
        //openDialogFullScreen(mainWindow, dialog);
    } catch (std::exception e) {
        qDebug() << e.what();
    }

}

void SettingsWidget::onPreferenceClicked(){
    try {
        mainWindow->getGUI()->optionsClicked();
        //mainWindow->getGUI()->showHide(true);
        //SettingsPreferences *dialog = new SettingsPreferences();
        //openDialogWithOpaqueBackground(dialog, mainWindow->getGUI());
    } catch (std::exception e) {
        qDebug() << e.what();
    }
}

void SettingsWidget::onBackupClicked(){
    try {
        mainWindow->backupWallet();
        //mainWindow->getGUI()->showHide(true);
        //SettingsBackup *dialog = new SettingsBackup(mainWindow->getGUI());
        //openDialogWithOpaqueBackground(dialog, mainWindow->getGUI());
    } catch (std::exception e) {
        qDebug() << e.what();
    }
}

void SettingsWidget::onRestoreClicked(){
    try {
        mainWindow->getGUI()->showHide(true);
        // TODO: Complete this with the specific language words
        dictionary dic = string_to_lexicon("english");
        QStringList wordList;
        for(unsigned long i=0; i< dic.size() ; i++){
            wordList << dic[i];
        }
        SettingsRestore *dialog = new SettingsRestore(wordList, mainWindow->getGUI());
        openDialogWithOpaqueBackgroundFullScreen(dialog, mainWindow->getGUI());
    } catch (std::exception e) {
        qDebug() << e.what();
    }
}


void SettingsWidget::onMintingClicked(){
    try {
        mainWindow->getGUI()->showHide(true);
        SettingsMinting *dialog = new SettingsMinting(mainWindow->getGUI(), mainWindow , this->walletModel);
        openDialogWithOpaqueBackgroundFullScreen(dialog, mainWindow->getGUI());
    } catch (std::exception e) {
        qDebug() << e.what();
    }
}

void SettingsWidget::onNetworkClicked(){
    try {
        mainWindow->getGUI()->showHide(true);
        SettingsNetwork *dialog = new SettingsNetwork(mainWindow->getGUI());
        openDialogWithOpaqueBackground(dialog, mainWindow->getGUI());
    } catch (std::exception e) {
        qDebug() << e.what();
    }
}

void SettingsWidget::onChangePasswordClicked(){
    try {
        mainWindow->changePassphrase();
    } catch (std::exception e) {
        qDebug() << e.what();
    }
}


void SettingsWidget::showEvent(QShowEvent *event){

    if(isViewInitiated) {
        WalletModel::EncryptionStatus status = walletModel->getEncryptionStatus();
        if(status != WalletModel::Unencrypted) {
            bool isChecked = walletModel->isStakingEnabled() && status != WalletModel::Locked;
            if(ui->checkBoxStaking->isChecked() != isChecked){
                checkChangedManually = true;
                ui->checkBoxStaking->setChecked(isChecked);
            }
        }
        updateStakingCheckboxStatus();
    }

    QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(this);
    this->setGraphicsEffect(eff);
    QPropertyAnimation *a = new QPropertyAnimation(eff,"opacity");
    a->setDuration(100);
    a->setStartValue(0.25);
    a->setEndValue(1);
    a->setEasingCurve(QEasingCurve::InBack);
    a->start(QPropertyAnimation::DeleteWhenStopped);
}

void SettingsWidget::hideEvent(QHideEvent *event){
    QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(this);
    this->setGraphicsEffect(eff);
    QPropertyAnimation *a = new QPropertyAnimation(eff,"opacity");
    a->setDuration(100);
    a->setStartValue(1);
    a->setEndValue(0);
    a->setEasingCurve(QEasingCurve::OutBack);
    a->start(QPropertyAnimation::DeleteWhenStopped);
    connect(a,SIGNAL(finished()),this,SLOT(hideThisWidget()));
}

void SettingsWidget::setWalletModel(WalletModel *model){
    this->walletModel = model;

    // Update unlock staking / encrypt wallet btn
    this->updateStakingCheckboxStatus();

    ui->checkBoxStaking->setChecked(walletModel->isStakingEnabled() && walletModel->getEncryptionStatus() != WalletModel::Locked);
    connect(ui->checkBoxStaking, SIGNAL(toggled(bool)), this, SLOT(onCheckStakingClicked(bool)));
    connect(ui->labelStacking, SIGNAL(clicked()), this, SLOT(onLabelStakingClicked()));

    isViewInitiated = true;
}

void SettingsWidget::updateStakingCheckboxStatus(){
    if(walletModel->getEncryptionStatus() == WalletModel::Unencrypted){
        ui->labelStacking->setText("Encrypt Wallet");
        ui->labelStacking->setProperty("cssClass" , "btn-text-settings");
        ui->checkBoxStaking->setVisible(false);
    }else{
        ui->labelStacking->setText("Unlock Wallet for Staking");
        ui->checkBoxStaking->setVisible(true);
    }
}

void SettingsWidget::refreshWalletStatus() {
    if(walletModel) {
        WalletModel::EncryptionStatus lockState = walletModel->getEncryptionStatus();
        bool stakingStatus = walletModel->isStakingEnabled() && lockState != WalletModel::Locked;
        if (ui->checkBoxStaking->isChecked() != stakingStatus) {
            checkChangedManually = true;
            ui->checkBoxStaking->setChecked(stakingStatus);
        }
        updateStakingCheckboxStatus();
    }
}

SettingsWidget::~SettingsWidget()
{
    delete ui;
}

