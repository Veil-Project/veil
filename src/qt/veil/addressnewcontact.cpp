#include <qt/veil/addressnewcontact.h>
#include <qt/veil/forms/ui_addressnewcontact.h>

#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <interfaces/node.h>
#include <qt/walletview.h>
#include <qt/walletmodel.h>
#include <key_io.h>
#include <wallet/wallet.h>

#include <QString>

AddressNewContact::AddressNewContact(QWidget *parent, WalletModel* _walletModel) :
    QDialog(parent),
    walletModel(_walletModel),
    ui(new Ui::AddressNewContact)
{
    ui->setupUi(this);
    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));

    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");

    ui->editContactName->setPlaceholderText("Set name");
    ui->editContactName->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editContactName->setProperty("cssClass" , "edit-primary");

    ui->editAddress->setPlaceholderText("Enter address");
    ui->editAddress->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editAddress->setProperty("cssClass" , "edit-primary");

    connect(ui->btnSave,SIGNAL(clicked()),this, SLOT(onBtnSaveClicked()));
}

void AddressNewContact::onEscapeClicked(){
    close();
}

void AddressNewContact::onBtnSaveClicked(){
    std::string label = ui->editContactName->text().toUtf8().constData();
    std::string qAddress = ui->editAddress->text().toUtf8().constData();

    // TODO: Validate address

    interfaces::Wallet& wallet = walletModel->wallet();
    CTxDestination dest = DecodeDestination(qAddress);
    if(wallet.setAddressBook(dest, label, "send")){
        accept();
    }else close();
}

AddressNewContact::~AddressNewContact()
{
    delete ui;
}
