#include <qt/veil/addresseswidget.h>
//#include "addressesmodel.h"
#include <qt/veil/forms/ui_addresseswidget.h>

#include <qt/veil/addressreceive.h>
#include <qt/veil/addressnewcontact.h>
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
#include <iostream>
#include <QSortFilterProxyModel>

#define DECORATION_SIZE 54
#define NUM_ITEMS 3

class AddressViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit AddressViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::VEIL),
        platformStyle(_platformStyle)
    {

    }

    // TODO: Complete it...
    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        //
        QRect mainRect = option.rect;
        int xspace = 6;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+(halfheight - ypad), mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace + ypad, mainRect.top()+(halfheight - ypad), mainRect.width() - xspace, halfheight);

        // Get the address
        QString address = index.data(Qt::DisplayRole).toString();

        QModelIndex header = index.sibling(index.row(), index.column() + 1);
        QString addressData = header.data(Qt::DisplayRole).toString();

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


        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);        

        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address, &boundingRect);

        painter->setPen(foreground);

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

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;

};

#include <qt/veil/addresseswidget.moc>


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

    ui->btnMyAddresses->setChecked(true);

    isOnMyAddresses = true;
    showList(true);
    onButtonChanged();

    connect(ui->btnMyAddresses,SIGNAL(clicked()),this,SLOT(onMyAddressClicked()));
    connect(ui->btnContacts,SIGNAL(clicked()),this,SLOT(onContactsClicked()));
    connect(ui->btnAdd,SIGNAL(clicked()),this,SLOT(onNewAddressClicked()));
}

class CustomDelegateAddresses : public QStyledItemDelegate
{
public:
    CustomDelegateAddresses(QTableView* tableView);
protected:
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;
private:
    QPen _gridPen;
};

CustomDelegateAddresses::CustomDelegateAddresses(QTableView* tableView)
{
    // create grid pen
    int gridHint = tableView->style()->styleHint(QStyle::SH_Table_GridLineColor, new QStyleOptionViewItemV4());
    QColor gridColor = static_cast<QRgb>(gridHint);
    _gridPen = QPen(gridColor, 0, tableView->gridStyle());
}

void CustomDelegateAddresses::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    QPen oldPen = painter->pen();
    painter->setPen(_gridPen);

    // paint horizontal lines
    bool isLine = index.row() % 2 == 1;
    if (isLine || index.column() == 2) //<-- check if column need horizontal grid lines
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());

    painter->setPen(oldPen);
}

void AddressesWidget::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        //filter.reset(new TransactionFilterProxy());
        //filter->setSourceModel(model->getTransactionTableModel());
        //filter->setLimit(NUM_ITEMS);
        //filter->setDynamicSortFilter(true);
        //filter->setSortRole(Qt::EditRole);
        //filter->setShowInactive(false);
        //filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        //ui->listTransactions->setModel(filter.get());
        //ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);


    }
}


void AddressesWidget::initAddressesView(){

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
    connect(a,SIGNAL(finished()),this,SLOT(hideThisWidget()));
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void AddressesWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    //columnResizingFixer->stretchColumnWidth(0);
}

void AddressesWidget::onMyAddressClicked(){
    isOnMyAddresses = true;
    if(this->model->rowCount(QModelIndex()) == 0){
        onForeground();
        this->ui->empty->setVisible(true);
    } else{
        onForeground();
    }
    onButtonChanged();
}

void AddressesWidget::onContactsClicked(){
    isOnMyAddresses = false;
    if(this->model->rowCount(QModelIndex()) == 0){
        onForeground();
        this->ui->empty->setVisible(true);
    } else{
        onForeground();
    }
    onButtonChanged();
}

void AddressesWidget::onButtonChanged() {
    if(isOnMyAddresses){
        ui->btnAdd->setText("New Address");
    }else{
        ui->btnAdd->setText("New Contact");
    }
}


void AddressesWidget::onNewAddressClicked(){
    mainWindow->showHide(true);
    QDialog *widget;
    if(ui->btnContacts->isChecked()){
        widget = new AddressNewContact(mainWindow->getGUI());
    } else {
        widget = new AddressReceive(mainWindow->getGUI());
    }
    widget->setWindowFlags(Qt::CustomizeWindowHint);
    widget->setAttribute(Qt::WA_TranslucentBackground, true);

    openDialogWithOpaqueBackground(widget, mainWindow->getGUI());
}

void AddressesWidget::setModel(AddressTableModel *_model)
{
    this->model = _model;
    if(!_model)
        return;
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

    // Set column widths
    //ui->tableView->horizontalHeader()->setSectionResizeMode(AddressTableModel::Label, QHeaderView::Stretch);
    //ui->tableView->horizontalHeader()->setSectionResizeMode(AddressTableModel::Address, QHeaderView::ResizeToContents);

    //connect(ui->tableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
    //    this, SLOT(selectionChanged()));

    // Select row for newly created address
    //connect(_model, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(selectNewAddress(QModelIndex,int,int)));

    //selectionChanged();
}

void AddressesWidget::onForeground(){
    if(walletModel){
        interfaces::Wallet& wallet = walletModel->wallet();
        if(wallet.getAddresses().size() > 0){
            ui->empty->setVisible(false);
            showList(true);
        }else{
            ui->empty->setVisible(true);
            showList(false);
        }
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

AddressesWidget::~AddressesWidget()
{
    delete ui;
}
