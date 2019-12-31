// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/veil/transactiondetaildialog.h>
#include <qt/veil/forms/ui_transactiondetaildialog.h>

#include <iostream>
#include <qt/transactionrecord.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/bitcoinunits.h>
#include <qt/walletmodel.h>
#include <qt/optionsmodel.h>
#include <QDateTime>
#include <QSettings>
#include <QDesktopServices>
#include <chainparams.h>

TransactionDetailDialog::TransactionDetailDialog(QWidget *parent, TransactionRecord *rec, WalletModel *walletModel) :
    QDialog(parent),
    ui(new Ui::TransactionDetailDialog)
{
    ui->setupUi(this);
    this->setStyleSheet(GUIUtil::loadStyleSheet());
    
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
    ui->labelStatus->setProperty("cssClass" , "label-detail");
    ui->labelComputeTime->setProperty("cssClass" , "label-detail");

    // Information

    ui->textAmount->setProperty("cssClass" , "text-detail");
    ui->textConfirmations->setProperty("cssClass" , "text-detail");
    ui->textId->setProperty("cssClass" , "text-detail");
    ui->textDate->setProperty("cssClass" , "text-detail");
    ui->textFee->setProperty("cssClass" , "text-detail");
    ui->textInputs->setProperty("cssClass" , "text-detail");
    ui->textSend->setProperty("cssClass" , "text-detail");
    ui->textSize->setProperty("cssClass" , "text-detail");
    ui->textStatus->setProperty("cssClass" , "text-detail");
    ui->textComputeTime->setProperty("cssClass" , "text-detail");
    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));
    connect(ui->explorerButton,SIGNAL(clicked()),this, SLOT(onExplorerClicked()));


    if(rec) {
        std::string chain = gArgs.GetChainName();
        QString baseStr = "";
        if (chain == CBaseChainParams::MAIN) {
            baseStr = "https://explorer.veil-project.com/tx/";
        } else if (chain == CBaseChainParams::TESTNET) {
            baseStr = "https://testnet.veil-project.com/tx/";
        } else if (chain == CBaseChainParams::DEVNET) {
            baseStr = "https://devnet.veil-project.com/tx/";
        }

        if (baseStr != "") {
            baseStr.append(rec->getTxHash());
            explorerLink = QUrl(baseStr);
        }
        int unit = walletModel->getOptionsModel()->getDisplayUnit();
        ui->textId->setText(rec->getTxHash());
        ui->textId->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ui->textAmount->setText(BitcoinUnits::formatWithUnit(unit, rec->getAmount(), false, BitcoinUnits::separatorAlways));
        ui->textFee->setText(BitcoinUnits::formatWithUnit(unit, rec->getFee(), false, BitcoinUnits::separatorAlways));
        ui->textInputs->setText(QString::fromStdString(std::to_string(rec->getInputsSize())));
        ui->textConfirmations->setText(QString::fromStdString(std::to_string(rec->getConfirmations())));
        if (rec->getAddress().empty()){
            ui->textSend->setText("Unknown");
        } else{
            ui->textSend->setText(QString::fromStdString((rec->getAddress())));
        }
        ui->textSize->setText(tr("%1 Bytes").arg(rec->size));
        ui->textDate->setText(GUIUtil::dateTimeStr(QDateTime::fromTime_t(static_cast<uint>(rec->time))));
        ui->textStatus->setText(QString::fromStdString(rec->statusToString()));
        if (rec->getComputeTime()) {
            if ( rec->getComputeTime() < 1000 ){
                ui->textComputeTime->setText(tr("%1 milliseconds").arg(rec->getComputeTime()));
            }
            else {
                ui->textComputeTime->setText(tr("%1 seconds").arg(QString::number(0.001 * rec->getComputeTime(), 'f', 2)));
            }
        }
        else {
            ui->textComputeTime->setText(tr("N/A"));
        }
    }

    QSettings settings;
    bool bShowComputeTime = settings.value("bShowComputeTime", false).toBool();
    ui->separatorComputeTime->setVisible(bShowComputeTime);
    ui->labelComputeTime->setVisible(bShowComputeTime);
    ui->textComputeTime->setVisible(bShowComputeTime);

//    ui->frameComputeTime->setVisible(settings.value("bShowComputeTime", false).toBool());

}

void TransactionDetailDialog::onEscapeClicked(){
    close();
}

void TransactionDetailDialog::onExplorerClicked(){
    if (explorerLink.isValid()) {
        QDesktopServices::openUrl(explorerLink);
    }
}

TransactionDetailDialog::~TransactionDetailDialog()
{
    delete ui;
}
