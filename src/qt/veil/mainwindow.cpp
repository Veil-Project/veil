// Copyright (c) 2019-2020 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/veil/.h>
#include <qt/veil/forms/ui_.h>

//#include "transactionsModel.h"
//#include "txviewdelegate.h"
//#include <QtSvg>
//#include "tooltipbalance.h"
#include <QDate>
#include <QFile>
#include <QMessageBox>
#include <QtDebug>
#include <QSplashScreen>
#include <QStyledItemDelegate>
#include <qt/guiutil.h>
#include <iostream>

#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>

#include <QAbstractItemDelegate>

#define DECORATION_SIZE 54
#define NUM_ITEMS 5

::(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::)
{
    ui->setupUi(this);

    ui->title->setProperty("cssClass" , "title");
    ui->checkStaking->setProperty("cssClass" , "switch");
    ui->checkPrecompute->setProperty("cssClass" , "switch");
    this->setStyleSheet(GUIUtil::loadStyleSheet());

    //ui->centralWidget->setContentsMargins(0,0,0,0);
    this->setContentsMargins(0,0,0,0);
    this->ui->containerBalance->setContentsMargins(0,0,0,0);
    this->ui->vLayoutBalance->setSpacing(0);
    this->ui->vLayoutBalance->setContentsMargins(10,10,10,10);

    centerStack = ui->stackedWidget;
    centerStack->setContentsMargins(0,0,0,0);
    ui->stackFirst->setContentsMargins(0,0,0,0);

    initBalance();
    initTxesView();

    ui->progressBar->setValue(67);
    ui->progressBar->setAlignment(Qt::AlignCenter);

     // Sort
    ui->comboSort->setProperty("cssClass" , "btn-text-primary-inactive");
    ui->comboSort->addItem(tr("Sort"));
    ui->comboSort->addItem(tr("Date"));
    ui->comboSort->addItem(tr("Amount"));
    for (int i = 0 ; i < ui->comboSort->count() ; ++i) {
        ui->comboSort->setItemData(i, Qt::AlignRight, Qt::TextAlignmentRole);
    }

    connect(ui->btnBalance, SIGNAL(pressed()), this, SLOT(onBtnBalanceClicked()));

}

::~()
{
    delete ui;
}

void ::onBtnBalanceClicked(){
    qDebug() << "Action touched! at date: ";
    //TooltipBalance *tooltip = new TooltipBalance(this);
    //tooltip->move(ui->btnBalance->pos().rx()+150,0);
    //tooltip->show();
}

void ::homeScreen(){
    //this->ui->mainToolBar->show();
    this->ui->containerBalance->show();
    centerStack->setCurrentWidget(ui->stackFirst);
}

void ::initBalance(){

    QPixmap veilImg(145,120);
    veilImg.load(":/icons/symbol3");
    this->ui->labelVeilImg->setFixedWidth(150);
    this->ui->labelVeilImg->setFixedHeight(120);
    this->ui->labelVeilImg->setMaximumWidth(150);
    this->ui->labelVeilImg->setMaximumHeight(120);
    ui->labelVeilImg->setPixmap(veilImg.scaled(
                                    145,
                                    120,
                                    Qt::KeepAspectRatio)
                                );



    QPixmap qrImg(ui->labelQr->width(),
                  ui->labelQr->height()
                  );

    qrImg.load(":/icons/qr");
    ui->labelQr->setPixmap(
                qrImg.scaled(
                    150,
                    150,
                    Qt::KeepAspectRatio)
                );

    // Copy Address

    QPixmap pixAddress(ui->copyAddress->width(),ui->copyAddress->height());
    pixAddress.load(":/icons/ic-copy-blue");
    QIcon ButtonIcon(pixAddress);
    ui->copyAddress->setIcon(ButtonIcon);
    ui->copyAddress->setIconSize(QSize(20, 20));

    // Balance

    ui->labelBalance->setText("0.00 VEIL");

    ui->labelUnspendable->setText("0.00 VEIL");

    ui->labelInmature->setText("0.00 VEIL");

}

void ::changeScreen(QWidget *widget){
    centerStack->setCurrentWidget(widget);
}

void ::on_txes_row_selected(const QItemSelection& prev, const QItemSelection& selected){
    qDebug() << "selection changed";
    QItemSelection selectedItems = selected;
    if (selectedItems.isEmpty()) return;

    QModelIndex sel = selectedItems.indexes().first();
    int row = sel.row();

    if(selectedRow == -1){
        selectedRow = row;
    }

    if(row != selectedRow){

        tableView->selectionModel()->clearSelection();

        //tableView->selectRow(row);


        selectedItems.merge(tableView->selectionModel()->selection(), QItemSelectionModel::Select);
        //tableView->selectColumn(2);
        //selectedItems.merge(tableView->selectionModel()->selection(), QItemSelectionModel::Select);

        tableView->selectionModel()->select(selectedItems, QItemSelectionModel::Select);

    }
}

void ::initTxesView(){

}


