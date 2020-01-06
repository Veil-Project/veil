// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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
    ui(new Ui::AddressNewContact),
    walletModel(_walletModel)
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

    ui->errorMessage->setVisible(false);

    connect(ui->btnSave,SIGNAL(clicked()),this, SLOT(onBtnSaveClicked()));
}

void AddressNewContact::onEscapeClicked(){
    close();
}

void AddressNewContact::onBtnSaveClicked(){
    std::string label = ui->editContactName->text().toUtf8().constData();
    std::string qAddress = ui->editAddress->text().toUtf8().constData();

    CTxDestination dest = DecodeDestination(qAddress);
    if (!IsValidDestination(dest)) {
        ui->errorMessage->setText("Invalid address");
        ui->errorMessage->setVisible(true);
        return;
    }

    interfaces::Wallet& wallet = walletModel->wallet();
    if(wallet.setAddressBook(dest, label, "send")){
        accept();
    }else close();
}

AddressNewContact::~AddressNewContact()
{
    delete ui;
}
