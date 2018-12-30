#include <qt/veil/unlockpassworddialog.h>
#include <qt/veil/forms/ui_unlockpassworddialog.h>
#include <qt/walletmodel.h>
#include <qt/guiconstants.h>
#include <qt/veil/qtutils.h>

UnlockPasswordDialog::UnlockPasswordDialog(bool fForStakingOnly, WalletModel* model, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UnlockPasswordDialog)
{
    ui->setupUi(this);
    this->walletModel = model;
    this->fForStakingOnly = fForStakingOnly;

    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnCancel->setProperty("cssClass" , "btn-text-primary-inactive");
    ui->editPassword->setPlaceholderText("Enter password ");
    ui->editPassword->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editPassword->setProperty("cssClass" , "edit-primary");
    connect(ui->btnCancel,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));
    connect(ui->btnSave,SIGNAL(clicked()),this, SLOT(onUnlockClicked()));

    QString strUnlockTitle = "Unlock Wallet For Staking";
    QString strDescripton = "Enter password to unlock wallet for staking only";
    if (!fForStakingOnly) {
        strUnlockTitle = "Unlock Wallet";
        strDescripton = "Enter password to unlock wallet";
    }

    ui->labelTitle->setText(strUnlockTitle);
    ui->labelDescription->setText(strDescripton);
    ui->errorMessage->setText("");
}

void UnlockPasswordDialog::onEscapeClicked(){
    close();
}


void UnlockPasswordDialog::onUnlockClicked()
{
    SecureString secureString;
    secureString.reserve(MAX_PASSPHRASE_SIZE);
    secureString.assign(ui->editPassword->text().toStdString().c_str());
    if (!walletModel->setWalletLocked(false, fForStakingOnly, secureString)) {
        close();
    } else {
        //Remove password text for form
        ui->editPassword->setText("");
        accept();
    }
}

UnlockPasswordDialog::~UnlockPasswordDialog() {
    delete ui;
}
