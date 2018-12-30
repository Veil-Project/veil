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
#include <QPainter>

#define DECORATION_SIZE 54
#define NUM_ITEMS 5

::(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::)
{
    ui->setupUi(this);

    ui->title->setProperty("cssClass" , "title");
    ui->checkStacking->setProperty("cssClass" , "switch");
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


enum ColumnIndex {
        Status = 0,
        Watchonly = 1,
        Date = 2,
        Type = 3,
        ToAddress = 4,
        Amount = 5
    };

enum ColumnWidths {
       STATUS_COLUMN_WIDTH = 23,
       WATCHONLY_COLUMN_WIDTH = 430,
       DATE_COLUMN_WIDTH = 170,
       TYPE_COLUMN_WIDTH = 240,
       AMOUNT_MINIMUM_COLUMN_WIDTH = 120,
       MINIMUM_COLUMN_WIDTH = 23
   };

//class CustomDelegate : public QStyledItemDelegate
//{
//public:
//    CustomDelegate(QTableView* tableView);
//protected:
//    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;
//private:
//    QPen _gridPen;
//};

//CustomDelegate::CustomDelegate(QTableView* tableView)
//{
//    // create grid pen
//    int gridHint = tableView->style()->styleHint(QStyle::SH_Table_GridLineColor, new QStyleOptionViewItemV4());
//    QColor gridColor = static_cast<QRgb>(gridHint);
//    _gridPen = QPen(gridColor, 0, tableView->gridStyle());
//}

//void CustomDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
//{
//    QStyledItemDelegate::paint(painter, option, index);

//    QPen oldPen = painter->pen();
//    painter->setPen(_gridPen);

//    // paint vertical lines
//    //painter->drawLine(option.rect.topRight(), option.rect.bottomRight());
//    // paint horizontal lines
//    bool isLine = index.row()  % 2 == 1;
//    bool isMergedRow = index.column() == 0 || index.column() == 1;
//    if (isLine || isMergedRow) //<-- check if column need horizontal grid lines
//        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());

//    painter->setPen(oldPen);
//}

void ::initTxesView(){

//    myModel = new TransactionsModel(centralWidget());

//    tableView = new QTableView(centralWidget());
//    tableView->setObjectName(QStringLiteral("tableView"));

//    tableView->setModel(myModel);
//    //tableView->setAlternatingRowColors(true);
//    tableView->horizontalHeader()->hide();
//    tableView->verticalHeader()->hide();

//    tableView->setColumnWidth(Status, STATUS_COLUMN_WIDTH);
//    tableView->setColumnWidth(Watchonly, WATCHONLY_COLUMN_WIDTH);
//    tableView->setColumnWidth(Date, DATE_COLUMN_WIDTH);
//    tableView->setColumnWidth(Type, TYPE_COLUMN_WIDTH);
//    tableView->setColumnWidth(Amount, AMOUNT_MINIMUM_COLUMN_WIDTH);

//    tableView->setSelectionMode(QAbstractItemView::NoSelection);

//    tableView->setShowGrid(false);
//    //tableView->setSelectionMode(QAbstractItemView::NoSelection);
//    for(int i=0;i < myModel->rowCount(); i=i+2){
//        tableView->setSpan(i, 0, 2, 1); //sets the 1st row 1st column cell to span over all the columns
//        tableView->setSpan(i, 1, 2, 1);
//        tableView->setRowHeight(i+1, 10);
//    }

//    //tableView->setItemDelegate(new TxViewDelegate());
//    tableView->setItemDelegate(new CustomDelegate(tableView));

//    ui->vLayoutTxes->insertWidget(1,tableView);

//    tableView->setWordWrap(true);
//    tableView->setTextElideMode(Qt::ElideMiddle);
//    tableView->resizeRowsToContents();

//    for(int i=0;i < myModel->rowCount(); i=i+2){
//        tableView->setSpan(i+1, 0, 1, myModel->columnCount());
//        //tableView->setRowHeight(i+2, 1);
//    }

//    tableView->horizontalHeader()->setStretchLastSection(true);

//    connect(
//      tableView->selectionModel(),
//      SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)),
//      this, SLOT(on_txes_row_selected(const QItemSelection &, const QItemSelection &))
//     );

}


