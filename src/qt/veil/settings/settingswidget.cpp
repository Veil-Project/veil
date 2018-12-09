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

//#include "sendcontroldialog.h"
//#include "addressreceive.h"
//#include "addressnewcontact.h"

//#include "sendconfirmation.h"

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
    ui->labelUnlock->setProperty("cssClass" , "btn-text-settings");
    ui->labelStacking->setProperty("cssClass" , "btn-text-settings");
    ui->checkBoxLock->setProperty("cssClass" , "btn-check");
    ui->checkBoxStaking->setProperty("cssClass" , "btn-check");

    //Backup
    ui->btnBackup->setProperty("cssClass" , "btn-text-settings");
    connect(ui->btnBackup,SIGNAL(clicked()),this,SLOT(onBackupClicked()));

    //Restore
    //ui->btnRestore
    ui->btnRestore->setProperty("cssClass" , "btn-text-settings");
     connect(ui->btnRestore,SIGNAL(clicked()),this,SLOT(onRestoreClicked()));

    //Password
    //ui->btnPassword
    ui->btnPassword->setProperty("cssClass" , "btn-text-settings");
    connect(ui->btnPassword,SIGNAL(clicked()),this,SLOT(onChangePasswordClicked()));

    //Preferences
    //ui->btnPreference
    ui->btnPreferences->setProperty("cssClass" , "btn-text-settings");
     connect(ui->btnPreferences,SIGNAL(clicked()),this,SLOT(onPreferenceClicked()));

    //Minting
    //ui->btnMinting
     ui->btnMinting->setProperty("cssClass" , "btn-text-settings");
    connect(ui->btnMinting,SIGNAL(clicked()),this,SLOT(onMintingClicked()));

    //Network
    //ui->btnNetwork
    //ui->btnNetwork->setProperty("cssClass" , "btn-text-settings");
    //connect(ui->btnNetwork,SIGNAL(clicked()),this,SLOT(onNetworkClicked()));

    //Display
    //ui->btnDisplay
    //ui->btnDisplay->setProperty("cssClass" , "btn-text-settings");
    //connect(ui->btnBackup,SIGNAL(clicked()),this,SLOT(onBackupClicked()));

    //Reset Options
    //ui->btnResetOptions

    //FAQ
    ui->btnFaq->setProperty("cssClass" , "btn-text-settings");
    connect(ui->btnFaq,SIGNAL(clicked()),this,SLOT(onFaqClicked()));

    //Advance
    ui->btnAdvance->setProperty("cssClass" , "btn-text-settings");
    connect(ui->btnAdvance,SIGNAL(clicked()),this,SLOT(onAdvanceClicked()));

    connect(ui->btnResetOptions,SIGNAL(clicked()),this,SLOT(onResetOptionClicked()));
    connect(ui->checkBoxStaking, SIGNAL(clicked(bool)), this, SLOT(onLockWalletClicked()));

}

void SettingsWidget::openDialog(QDialog *dialog){
    openDialogWithOpaqueBackground(dialog, mainWindow->getGUI());
}

void SettingsWidget::onLockWalletClicked(){
    try {
        mainWindow->getGUI()->showHide(true);
        UnlockPasswordDialog *dialog = new UnlockPasswordDialog(mainWindow->getGUI());
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
        SettingsMinting *dialog = new SettingsMinting(mainWindow->getGUI());
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

SettingsWidget::~SettingsWidget()
{
    delete ui;
}
