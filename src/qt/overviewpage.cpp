// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/overviewpage.h>
#include <qt/forms/ui_overviewpage.h>

#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/veil/qtutils.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>
#include <qt/veil/settings/settingsfaq.h>
#include <qt/veil/transactiondetaildialog.h>
#include <qt/transactionrecord.h>

#include <QAbstractItemDelegate>
#include <QPainter>

#define DECORATION_SIZE 54
#define NUM_ITEMS 3

Q_DECLARE_METATYPE(interfaces::WalletBalances)

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::VEIL),
        platformStyle(_platformStyle)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::RawDecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));


        QColor foreground;
        // Special treatment for the selected state
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
        } else{
            foreground = option.palette.color(QPalette::Text);
        }

        int decorationSize = DECORATION_SIZE - 36;
        int halfheightIcon = ( mainRect.height()) / 3.1;
        QRect iconRect = decorationRect;
        iconRect.setTop(iconRect.top() + halfheightIcon);
        iconRect.setLeft(iconRect.left() + 16);
        QRect decorationRect1(iconRect.topLeft(), QSize(decorationSize, decorationSize));

        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace - 16, halfheight);
        QRect dateRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace - 16, halfheight);
        QRect addressRect(mainRect.left() + decorationSize + ypad  + 16, mainRect.top()+(halfheight - ypad), mainRect.width() - xspace, halfheight);
        //icon = platformStyle->SingleColorIcon(icon);
        icon.paint(painter, decorationRect1);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();

        // Tx type
        QModelIndex header = index.sibling(index.row(), 3);
        QString message = header.data(Qt::DisplayRole).toString();

        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);

        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;

        if (message == "Mined" || message == "Basecoin mined" || message == "CT mined") {
            address = "  " + message + " on " + address;
        }else if (address == ""){
            // Tx type
            QModelIndex header2 = index.sibling(index.row(), 4);
            QString message2 = header2.data(Qt::DisplayRole).toString();
            if(message2 == ""){
                // TODO: no address on the tx..
                message2 = "anonymous";
            }
            address = "  " + message + " " + message2;
        } else{
            address = "  " + message + " " + address;
        }

        QFont fontTemp = painter->font() ;
        QFont font = painter->font() ;
        /* twice the size than the current font size */
        font.setPointSize(14);

        /* set the modified font to the painter */
        painter->setFont(font);

        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address, &boundingRect);


        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top()+ypad+halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        // TODO: Change this balance calculation
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
        if(amount < 0) {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed) {
            foreground = COLOR_UNCONFIRMED;
        }
        else {
            foreground = COLOR_CONFIRMED; option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);

        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        // Draw the date
        painter->setPen(QColor("#707070"));//option.palette.color(QPalette::Text));
        painter->drawText(dateRect, Qt::AlignRight|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        // Separator
        QPen _gridPen = QPen(COLOR_UNCONFIRMED, 0, Qt::SolidLine);
        QPen oldPen = painter->pen();
        painter->setPen(_gridPen);
        QRect linerect(mainRect.left() + 12, mainRect.top(), mainRect.width() - + 12, mainRect.height());
        linerect.setRight(linerect.right() - 12);
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
#include <qt/overviewpage.moc>

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, WalletView *parent) :
    QWidget(parent),
    mainWindow(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    txdelegate(new TxViewDelegate(platformStyle, this))
{
    ui->setupUi(this);
    ui->title->setProperty("cssClass" , "title");
    setStyleSheet(GUIUtil::loadStyleSheet());

    // Sort
    ui->comboSort->setProperty("cssClass" , "btn-text-primary-inactive");
    ui->comboSort->addItem(tr("Sort By"));
    ui->comboSort->addItem(tr("Date"));
    ui->comboSort->addItem(tr("Amount"));
    for (int i = 0 ; i < ui->comboSort->count() ; ++i) {
       ui->comboSort->setItemData(i, Qt::AlignRight, Qt::TextAlignmentRole);
    }
    // combo selection:
    connect(ui->comboSort,SIGNAL(currentIndexChanged(const QString&)),this,SLOT(sortTxes(const QString&)));

    this->setContentsMargins(0,0,0,0);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->listTransactions->setSelectionBehavior(QAbstractItemView::SelectRows);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    connect(ui->btnGoFaq,SIGNAL(clicked()),this,SLOT(onFaqClicked()));

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    //connect(ui->labelWalletStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
    //connect(ui->labelTransactionsStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{

    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));

    QModelIndex selectedIndex = filter->mapToSource(index);

    ui->listTransactions->setCurrentIndex(selectedIndex);

    TransactionRecord *rec = static_cast<TransactionRecord*>(selectedIndex.internalPointer());

    mainWindow->getGUI()->showHide(true);
    TransactionDetailDialog *dialog = new TransactionDetailDialog(mainWindow->getGUI(), rec, this->walletModel);
    openDialogWithOpaqueBackgroundY(dialog, mainWindow->getGUI(), 4.5, 5);

    // Back to regular status
    QModelIndex rselectedIndex = filter->mapToSource(index);
    ui->listTransactions->scrollTo(rselectedIndex);
    ui->listTransactions->setCurrentIndex(rselectedIndex);
    ui->listTransactions->setFocus();

}

void OverviewPage::sortTxes(const QString& selectedStr){
    if(selectedStr == "Date") {
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);
    }else if(selectedStr == "Amount"){
        filter->sort(TransactionTableModel::Amount, Qt::DescendingOrder);
    }
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    //ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    //ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
    //ui->lineWatchBalance->setVisible(showWatchOnly);    // show watch-only balance separator line
    //ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    //ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    //ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    //if (!showWatchOnly)
    //    ui->labelWatchImmature->hide();
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        //filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);



        interfaces::Wallet& wallet = model->wallet();

        if(wallet.getWalletTxs().size() == 0 ){
            // show empty view
            ui->containerEmpty->setVisible(true);
            ui->listTransactions->setVisible(false);

            connect(model, SIGNAL(balanceChanged(interfaces::WalletBalances)), this, SLOT(updateTxesView()));
        }else{
            ui->containerEmpty->setVisible(false);
        }

        // Keep up to date with wallet
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        updateWatchOnlyLabels(wallet.haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
    }

    // update the display unit, to not use the default ("VEIL")
    updateDisplayUnit();
}

void OverviewPage::updateTxesView(){
    if(ui->containerEmpty->isVisible()){
        ui->containerEmpty->setVisible(false);
        ui->listTransactions->setVisible(true);
        disconnect(this, SLOT(updateTxesView()));
    }
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    // TODO: Out of sync icon
    //ui->labelWalletStatus->setVisible(fShow);
    //ui->labelTransactionsStatus->setVisible(fShow);
}


void OverviewPage::onFaqClicked(){
    mainWindow->getGUI()->showHide(true);
    SettingsFaq *dialog = new SettingsFaq(mainWindow->getGUI());
    openDialogWithOpaqueBackgroundFullScreen(dialog, mainWindow->getGUI());
}