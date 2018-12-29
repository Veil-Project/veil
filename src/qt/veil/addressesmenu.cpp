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
    mainWindow(_mainWindow),
    type(_type),
    index(_index),
    model(_model),
    ui(new Ui::AddressesMenu)
{
    ui->setupUi(this);

    connect(ui->btnCopy,SIGNAL(clicked()),this,SLOT(on_btnCopyAddress_clicked()));
    connect(ui->btnDelete,SIGNAL(clicked()),this,SLOT(on_btnDeleteAddress_clicked()));
    connect(ui->btnEdit,SIGNAL(clicked()),this,SLOT(on_btnEditAddress_clicked()));

    if(AddressTableModel::Receive == type){
        ui->btnDelete->setVisible(false);
    }
}

void AddressesMenu::on_btnDeleteAddress_clicked(){
    if(this->model->removeRows(index.row(), 1, index)){
        openToastDialog("Address deleted", this->mainWindow);
    }
    hide();
}

void AddressesMenu::on_btnEditAddress_clicked(){
    auto address = this->model->index(index.row(), AddressTableModel::Address, index);
    QString addressStr = this->model->data(address, Qt::DisplayRole).toString();

    this->mainWindow->getGUI()->showHide(true);
    std::string purpose;
    if(AddressTableModel::Receive == type){
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

void AddressesMenu::on_btnCopyAddress_clicked(){
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
