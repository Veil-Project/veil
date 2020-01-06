// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/veil/addressesmenu.h>
#include <qt/veil/forms/ui_addressesmenu.h>

#include <qt/veil/qtutils.h>

#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPropertyAnimation>
#include <QTimer>
#include <iostream>

AddressesMenu::AddressesMenu(const QString _type, const QModelIndex &_index, QWidget *parent, WalletView *_mainWindow, AddressTableModel *_model) :
    QWidget(parent),
    ui(new Ui::AddressesMenu),
    mainWindow(_mainWindow),
    index(_index),
    type(_type),
    model(_model)
{
    ui->setupUi(this);

    connect(ui->btnCopy,SIGNAL(clicked()),this,SLOT(onBtnCopyAddressClicked()));
    connect(ui->btnDelete,SIGNAL(clicked()),this,SLOT(onBtnDeleteAddressClicked()));
    connect(ui->btnEdit,SIGNAL(clicked()),this,SLOT(onBtnEditAddressClicked()));

    if(AddressTableModel::Receive == type){
        ui->btnDelete->setVisible(false);
    }
}

void AddressesMenu::onBtnDeleteAddressClicked(){
    if(this->model->removeRows(index.row(), 1, index)){
        openToastDialog("Address deleted", this->mainWindow);
    }
    hide();
}

void AddressesMenu::onBtnEditAddressClicked(){
    auto address = this->model->index(index.row(), AddressTableModel::Address, index);
    QString addressStr = this->model->data(address, Qt::DisplayRole).toString();


    auto isMinerAddress = this->model->index(index.row(), AddressTableModel::Is_Basecoin, index);
    bool isMiner = this->model->data(isMinerAddress, Qt::DisplayRole).toBool();

    this->mainWindow->getGUI()->showHide(true);
    std::string purpose;
    if(AddressTableModel::Receive == type){
        if(isMiner){
            purpose = "receive_miner";
        }else
            purpose = "receive";
    }else{
        purpose = "send";
    }
    UpdateAddress *updateAddress = new UpdateAddress(index, addressStr, purpose, this->mainWindow->getGUI(), mainWindow->getWalletModel(), model);
    if(openDialogWithOpaqueBackground(updateAddress, this->mainWindow->getGUI(), 3)){
        openToastDialog("Label Updated", this->mainWindow->getGUI());
    }
    hide();
}

void AddressesMenu::onBtnCopyAddressClicked(){
    auto address = this->model->index(index.row(), AddressTableModel::Address, index);
    QString addressStr = this->model->data(address, Qt::DisplayRole).toString();
    GUIUtil::setClipboard(addressStr);
    openToastDialog("Address copied", this->mainWindow);
    hide();
}

void AddressesMenu::setInitData(const QModelIndex &_index, AddressTableModel *_model, const QString _type){
    this->index = _index;
    this->model = _model;
    this->type = _type;

    ui->btnDelete->setVisible(AddressTableModel::Receive != type);
}

void AddressesMenu::showEvent(QShowEvent *event){
    QTimer::singleShot(3500, this, SLOT(hide()));
}

AddressesMenu::~AddressesMenu() {
    delete ui;
}
