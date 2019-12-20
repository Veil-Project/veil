// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/veil/updateaddress.h>
#include <qt/veil/forms/ui_updateaddress.h>

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

UpdateAddress::UpdateAddress(const QModelIndex &_index, QString addressStr, std::string _addressPurpose, QWidget *parent, WalletModel* _walletModel, AddressTableModel *_model) :
    QDialog(parent),
    ui(new Ui::UpdateAddress),
    walletModel(_walletModel),
    index(_index),
    addressPurpose(_addressPurpose),
    address(addressStr),
    model(_model)
{
    ui->setupUi(this);
    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));

    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");
    ui->errorMessage->setVisible(false);


    ui->editLabel->setPlaceholderText("Enter address label");
    ui->editLabel->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editLabel->setProperty("cssClass" , "edit-primary");

    ui->lblAddress->setText(addressStr);
    connect(ui->btnSave,SIGNAL(clicked()),this, SLOT(onBtnSaveClicked()));
}

void UpdateAddress::onEscapeClicked(){
    close();
}

void UpdateAddress::onBtnSaveClicked(){
    std::string label = ui->editLabel->text().toUtf8().constData();
    interfaces::Wallet& wallet = walletModel->wallet();
    CTxDestination dest = DecodeDestination(address.toUtf8().constData());
    if(wallet.setAddressBook(dest, label, addressPurpose)){
        accept();
    }else close();
}

UpdateAddress::~UpdateAddress() {
    delete ui;
}
