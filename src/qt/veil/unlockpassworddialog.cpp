#include <qt/veil/unlockpassworddialog.h>
#include <qt/veil/forms/ui_unlockpassworddialog.h>
#include <qt/walletmodel.h>
#include <qt/guiconstants.h>

UnlockPasswordDialog::UnlockPasswordDialog(WalletModel* model, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UnlockPasswordDialog)
{
    ui->setupUi(this);
    this->walletModel = model;
    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnCancel->setProperty("cssClass" , "btn-text-primary-inactive");
    ui->editPassword->setPlaceholderText("Enter password ");
    ui->editPassword->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editPassword->setProperty("cssClass" , "edit-primary");
    connect(ui->btnCancel,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));
    connect(ui->btnSave,SIGNAL(clicked()),this, SLOT(onUnlockClicked()));

}

void UnlockPasswordDialog::onEscapeClicked(){
    close();
}

void UnlockPasswordDialog::onUnlockClicked()
{
    SecureString secureString;
    secureString.reserve(MAX_PASSPHRASE_SIZE);
    secureString.assign(ui->editPassword->text().toStdString().c_str());
    if (!walletModel->setWalletLocked(false, /*fUnlockForStakingOnly*/true, secureString)) {
        //todo: give some type of error message to try again
    } else {
        //Remove password text for form
        ui->editPassword->setText("");
        close();
    }
}

UnlockPasswordDialog::~UnlockPasswordDialog()
{
    delete ui;
}
