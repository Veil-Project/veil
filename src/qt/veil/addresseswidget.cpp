// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/veil/addresseswidget.h>
#include <qt/veil/forms/ui_addresseswidget.h>

#include <qt/veil/addressreceive.h>
#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>

#include <QDebug>
#include <QGraphicsOpacityEffect>
#include <QStyledItemDelegate>
#include <QPropertyAnimation>
#include <QPen>
#include <QDialog>
#include <QPainter>
#include <QAbstractItemDelegate>
#include <QPropertyAnimation>
#include <QPoint>
#include <QMenu>
#include <QSortFilterProxyModel>

#include <iostream>

#define DECORATION_SIZE 54
#define NUM_ITEMS 5

class AddressViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit AddressViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr, AddressTableModel *_model = nullptr):
        QAbstractItemDelegate(parent),
	unit(BitcoinUnits::VEIL),
        platformStyle(_platformStyle),
	model(_model)
	{
	}

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const {
        painter->save();

        QRect mainRect = option.rect;
        int xspace = 6;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+(halfheight - ypad), mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace + ypad, mainRect.top()+(halfheight - ypad), mainRect.width() - xspace, halfheight);

        // Get the address
        QString address = index.data(Qt::DisplayRole).toString();
        QModelIndex header = index.sibling(index.row(), AddressTableModel::Address_dot);
        QString addressData = header.data(Qt::DisplayRole).toString();

        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);

        if(value.canConvert<QBrush>()) {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        if (option.state & QStyle::State_Selected) {
            QRect selectedRect = option.rect;
            selectedRect.setLeft(0);
            painter->fillRect(selectedRect, QColor("#CEDDFB"));
            foreground = QColor("#575756");
        }else if(option.state & QStyle::State_MouseOver){
            QRect selectedRect = option.rect;
            selectedRect.setLeft(0);
            painter->fillRect(selectedRect, QColor("#F4F4F4"));
            foreground = QColor("#575756");
            QIcon icon(":/icons/ic-menu-png");
            QRect mainRect2 = option.rect;
            QRect decorationRect(mainRect2.topRight(), QSize(42, 42));
            QRect iconRect = decorationRect;
            int halfheightIcon = ( mainRect2.height()) / 4.1;
            iconRect.setTop(iconRect.top() + halfheightIcon);
            iconRect.setLeft(iconRect.left() - 92);
            icon.paint(painter, iconRect);
            amountRect.setRight(amountRect.right() - 42);
        } else{
            foreground = option.palette.color(QPalette::Text);
        }             

        // Text size.
        QFont fontTemp = painter->font();
        QFont font = painter->font() ;
        if(addressData.size() > 40){
            font.setPointSize(12);
            painter->setFont(font);
        }else{
            font.setPointSize(14);
            painter->setFont(font);
        }

        painter->setPen(foreground);   

        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address, &boundingRect);

        amountRect.setRight(amountRect.right() - 16);
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, addressData);

        painter->setPen(option.palette.color(QPalette::Text));

        // Separator
        QPen _gridPen = QPen(COLOR_UNCONFIRMED, 0, Qt::SolidLine);
        QPen oldPen = painter->pen();
        painter->setPen(_gridPen);
        QRect linerect(mainRect.left() + 15, mainRect.top(), mainRect.width() - + 15, mainRect.height());
        linerect.setRight(linerect.right() - 15);
        painter->drawLine(linerect.bottomLeft(), linerect.bottomRight());
        painter->setPen(oldPen);

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    void setModel(AddressTableModel *_model){
        this->model = _model;
    }

    int unit;
    const PlatformStyle *platformStyle;
    AddressTableModel *model = nullptr;

};

#include <qt/veil/addresseswidget.moc>
#include "tooltipbalance.h"
#include "addressnewcontact.h"


class AddressSortFilterProxyModel final : public QSortFilterProxyModel
{
    const QString m_type;

public:
    AddressSortFilterProxyModel(const QString& type, QObject* parent)
            : QSortFilterProxyModel(parent)
            , m_type(type)
    {
        setDynamicSortFilter(true);
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setSortCaseSensitivity(Qt::CaseInsensitive);
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const
    {
        auto model = sourceModel();
        auto label = model->index(row, AddressTableModel::Label, parent);

        if (model->data(label, AddressTableModel::TypeRole).toString() != m_type) {
            return false;
        }

        auto address = model->index(row, AddressTableModel::Address, parent);

        if (filterRegExp().indexIn(model->data(address).toString()) < 0 &&
            filterRegExp().indexIn(model->data(label).toString()) < 0) {
            return false;
        }

        return true;
    }
};

AddressesWidget::AddressesWidget(const PlatformStyle *platformStyle, WalletView *parent) :
    QWidget(parent),
    ui(new Ui::AddressesWidget),
    mainWindow(parent),
    clientModel(0),
    walletModel(0),
    addressViewDelegate(new AddressViewDelegate(platformStyle, this))
{
    ui->setupUi(this);

    // Addresses
    ui->listAddresses->setItemDelegate(addressViewDelegate);
    ui->listAddresses->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listAddresses->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listAddresses->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->listAddresses->setSelectionBehavior(QAbstractItemView::SelectRows);

    ui->listContacts->setItemDelegate(addressViewDelegate);
    ui->listContacts->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listContacts->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listContacts->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->listContacts->setSelectionBehavior(QAbstractItemView::SelectRows);


    connect(ui->listAddresses, SIGNAL(clicked(QModelIndex)), this, SLOT(handleAddressClicked(QModelIndex)));
    connect(ui->listContacts, SIGNAL(clicked(QModelIndex)), this, SLOT(handleAddressClicked(QModelIndex)));

    ui->btnMyAddresses->setChecked(true);

    isOnMyAddresses = true;
    showList(true);
    onButtonChanged();

    connect(ui->btnMyAddresses,SIGNAL(clicked()),this,SLOT(onMyAddressClicked()));
    connect(ui->btnContacts,SIGNAL(clicked()),this,SLOT(onContactsClicked()));
    connect(ui->btnAdd,SIGNAL(clicked()),this,SLOT(onNewAddressClicked()));
    connect(ui->btnAddIcon,SIGNAL(clicked()),this,SLOT(onNewAddressClicked()));

    ui->btnMiningAddress->setVisible(false);
    ui->btnMiningAddress2->setVisible(false);
    ui->lblSplit->setVisible(false);
    //connect(ui->btnMiningAddress,SIGNAL(clicked()),this,SLOT(onNewMinerAddressClicked()));
    //connect(ui->btnMiningAddress2,SIGNAL(clicked()),this,SLOT(onNewMinerAddressClicked()));
}

void AddressesWidget::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel()) {
        // nothing yet
    }
}

void AddressesWidget::handleAddressClicked(const QModelIndex &index){
    QListView *listView;
    QModelIndex updatedIndex;
    QString type;
    if (isOnMyAddresses){
        listView = ui->listAddresses;
        type = AddressTableModel::Receive;
        updatedIndex = proxyModel->mapToSource(index);
    }else{
        listView = ui->listContacts;
        type = AddressTableModel::Send;
        updatedIndex = proxyModelSend->mapToSource(index);
    }

    listView->setCurrentIndex(updatedIndex);
    QRect rect = listView->visualRect(index);
    QPoint pos = rect.topRight();
    pos.setX(pos.x() - (DECORATION_SIZE * 2));
    pos.setY(pos.y() + (DECORATION_SIZE));
    const QString constType = type;
    if(!this->menu) this->menu = new AddressesMenu(constType , updatedIndex, this, this->mainWindow, this->model);
    else {
        this->menu->hide();
        this->menu->setInitData(updatedIndex, this->model, constType);
    }
    menu->move(pos);
    menu->show();
}

void AddressesWidget::initAddressesView(){
    // Complete me..
}

void AddressesWidget::showEvent(QShowEvent *event){
    QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(this);
    this->setGraphicsEffect(eff);
    QPropertyAnimation *a = new QPropertyAnimation(eff,"opacity");
    a->setDuration(100);
    a->setStartValue(0.25);
    a->setEndValue(1);
    a->setEasingCurve(QEasingCurve::InBack);
    a->start(QPropertyAnimation::DeleteWhenStopped);
    reloadTab(isOnMyAddresses);
}

void AddressesWidget::hideEvent(QHideEvent *event){
    QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(this);
    this->setGraphicsEffect(eff);
    QPropertyAnimation *a = new QPropertyAnimation(eff,"opacity");
    a->setDuration(100);
    a->setStartValue(1);
    a->setEndValue(0);
    a->setEasingCurve(QEasingCurve::OutBack);
    a->start(QPropertyAnimation::DeleteWhenStopped);
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void AddressesWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    //columnResizingFixer->stretchColumnWidth(0);
}

void AddressesWidget::onMyAddressClicked(){
    reloadTab(true);
}

void AddressesWidget::onContactsClicked(){
    reloadTab(false);
}

void AddressesWidget::reloadTab(bool _isOnMyAddresses) {
    isOnMyAddresses = _isOnMyAddresses;
    onForeground();
    onButtonChanged();
}

void AddressesWidget::onButtonChanged() {
    if(isOnMyAddresses){
        ui->btnAdd->setText("New Address");
        showHideMineAddressBtn(true);
    }else{
        ui->btnAdd->setText("New Contact");
        showHideMineAddressBtn(false);
    }
    if(this->menu){
        this->menu->hide();
    }
}

void AddressesWidget::showHideMineAddressBtn(bool show){
    //ui->btnMiningAddress2->setVisible(show);
    //ui->btnMiningAddress->setVisible(show);
    //ui->lblSplit->setVisible(show);
}

void AddressesWidget::onNewMinerAddressClicked(){
    mainWindow->showHide(true);
    QDialog *widget = new AddressReceive(mainWindow->getGUI(), this->walletModel, true);
    std::string toast = "Mining Address";
    widget->setWindowFlags(Qt::CustomizeWindowHint);
    widget->setAttribute(Qt::WA_TranslucentBackground, true);

    if(openDialogWithOpaqueBackground(widget, mainWindow->getGUI())){
        openToastDialog(QString::fromStdString(toast + " Created"), mainWindow->getGUI());
    }else{
        openToastDialog(QString::fromStdString(toast + " Creation Failed"), mainWindow->getGUI());
    }
    // if it's the first one created, display it without having to reload
    onForeground();
}

void AddressesWidget::onNewAddressClicked(){
    mainWindow->showHide(true);
    QDialog *widget;
    std::string toast;
    if(ui->btnContacts->isChecked()){
        widget = new AddressNewContact(mainWindow->getGUI(), this->walletModel);
        toast = "Contact";
    } else {
        bool isLocked = walletModel->getEncryptionStatus() == WalletModel::Locked;
        if(isLocked){
            openToastDialog(QString::fromStdString("Wallet Locked, please unlock it to continue"), mainWindow->getGUI());
            mainWindow->showHide(false);
            return;
        }
        widget = new AddressReceive(mainWindow->getGUI(), this->walletModel);
        toast = "Address";
    }
    widget->setWindowFlags(Qt::CustomizeWindowHint);
    widget->setAttribute(Qt::WA_TranslucentBackground, true);

    if(openDialogWithOpaqueBackground(widget, mainWindow->getGUI())){
        openToastDialog(QString::fromStdString(toast + " Created"), mainWindow->getGUI());
    }else{
        openToastDialog(QString::fromStdString(toast + " Creation Failed"), mainWindow->getGUI());
    }
    // if it's the first one created, display it without having to reload
    onForeground();
}

void AddressesWidget::setModel(AddressTableModel *_model)
{
    this->model = _model;
    if(!_model)
        return;
    this->addressViewDelegate->setModel(_model);
    proxyModel = new AddressSortFilterProxyModel(AddressTableModel::Receive, this);
    proxyModel->setSourceModel(_model);

    proxyModelSend = new AddressSortFilterProxyModel(AddressTableModel::Send, this);
    proxyModelSend->setSourceModel(_model);

    //connect(ui->searchLineEdit, SIGNAL(textChanged(QString)), proxyModel, SLOT(setFilterWildcard(QString)));

    ui->listAddresses->setModel(proxyModel);
    ui->listContacts->setModel(proxyModelSend);

    // update ui
    onForeground();

    //ui->tableView->sortByColumn(0, Qt::AscendingOrder);
    //connect(ui->tableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
    //    this, SLOT(selectionChanged()));

    // Select row for newly created address
    //connect(_model, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(selectNewAddress(QModelIndex,int,int)))
}

void AddressesWidget::onForeground(){
    if(walletModel){
        interfaces::Wallet& wallet = walletModel->wallet();
        auto listsize = model->rowCount(QModelIndex());
        ui->empty->setVisible(listsize == 0);
        showList(listsize > 0);
    }
}

void AddressesWidget::showList(bool show){
    if(show){
        if(isOnMyAddresses){
            ui->listAddresses->setVisible(true);
            ui->listContacts->setVisible(false);
        }else{
            ui->listAddresses->setVisible(false);
            ui->listContacts->setVisible(true);
        }
    }else{
        ui->listAddresses->setVisible(false);
        ui->listContacts->setVisible(false);
    }
}

AddressesWidget::~AddressesWidget() {
    delete ui;
}

void AddressesWidget::refreshWalletStatus() {
    model->refresh();
}
