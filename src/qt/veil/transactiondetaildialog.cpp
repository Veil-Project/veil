#include <qt/veil/transactiondetaildialog.h>
#include <qt/veil/forms/ui_transactiondetaildialog.h>

#include <iostream>
#include <qt/transactionrecord.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/bitcoinunits.h>
#include <qt/walletmodel.h>
#include <qt/optionsmodel.h>

TransactionDetailDialog::TransactionDetailDialog(QWidget *parent, TransactionRecord *rec, WalletModel *walletModel) :
    QDialog(parent),
    ui(new Ui::TransactionDetailDialog)
{
    ui->setupUi(this);
    // Titles
    ui->title->setProperty("cssClass" , "title");
    ui->labelAmount->setProperty("cssClass" , "label-detail");
    ui->labelId->setProperty("cssClass" , "label-detail");
    ui->labelConfirmations->setProperty("cssClass" , "label-detail");
    ui->labelDate->setProperty("cssClass" , "label-detail");
    ui->labelFee->setProperty("cssClass" , "label-detail");
    ui->labelInputs->setProperty("cssClass" , "label-detail");
    ui->labelSend->setProperty("cssClass" , "label-detail");
    ui->labelSize->setProperty("cssClass" , "label-detail");

    // Information

    ui->textAmount->setProperty("cssClass" , "text-detail");
    ui->textConfirmations->setProperty("cssClass" , "text-detail");
    ui->textId->setProperty("cssClass" , "text-detail");
    ui->textDate->setProperty("cssClass" , "text-detail");
    ui->textFee->setProperty("cssClass" , "text-detail");
    ui->textInputs->setProperty("cssClass" , "text-detail");
    ui->textSend->setProperty("cssClass" , "text-detail");
    ui->textSize->setProperty("cssClass" , "text-detail");
    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));

    if(rec) {
        // TODO: Complete this..
        int unit = walletModel->getOptionsModel()->getDisplayUnit();
        ui->textId->setText(rec->getTxHash());
        ui->textId->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->textAmount->setText(BitcoinUnits::formatWithUnit(unit, rec->getAmount(), false, BitcoinUnits::separatorAlways));
        ui->textFee->setText(BitcoinUnits::formatWithUnit(unit, rec->getFee(), false, BitcoinUnits::separatorAlways));
        ui->textInputs->setText(QString::fromStdString(std::to_string(rec->getInputsSize())));
        ui->textConfirmations->setText(QString::fromStdString(std::to_string(rec->getConfirmations())));
        ui->textSend->setText(QString::fromStdString((rec->getAddress())));
        ui->textSize->setText("n/a Kb");
        ui->textDate->setText("n/a");
    }
}

void TransactionDetailDialog::onEscapeClicked(){
    close();
}


TransactionDetailDialog::~TransactionDetailDialog()
{
    delete ui;
}
