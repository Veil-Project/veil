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

UpdateAddress::UpdateAddress(const QModelIndex &_index, QWidget *parent,  WalletModel* _walletModel,
                             AddressTableModel *_model) :
    QDialog(parent),
    walletModel(_walletModel),
    index(_index),
    model(_model),
    ui(new Ui::UpdateAddress)
{
    ui->setupUi(this);
    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));

    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");
    ui->errorMessage->setVisible(false);


    ui->editLabel->setPlaceholderText("Enter address");
    ui->editLabel->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editLabel->setProperty("cssClass" , "edit-primary");

    //
    auto address = this->model->index(index.row(), AddressTableModel::Address, index);
    addressStr = this->model->data(address, Qt::DisplayRole).toString();
    ui->lblAddress->setText(addressStr);

    connect(ui->btnSave,SIGNAL(clicked()),this, SLOT(onBtnSaveClicked()));
}

void UpdateAddress::onEscapeClicked(){
    close();
}

void UpdateAddress::onBtnSaveClicked(){
    std::string label = ui->editLabel->text().toUtf8().constData();
    interfaces::Wallet& wallet = walletModel->wallet();
    CTxDestination dest = DecodeDestination(addressStr.toUtf8().constData());
    if(wallet.setAddressBook(dest, label, "receive")){
        accept();
    }else close();
}

UpdateAddress::~UpdateAddress() {
    delete ui;
}
