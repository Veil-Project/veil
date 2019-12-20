// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/veil/balance.h>
#include <qt/veil/forms/ui_balance.h>

#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <wallet/wallet.h>
#include <iostream>

#include <QPixmap>
#include <QClipboard>
#include <QMimeData>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <qt/veil/qtutils.h>
#include <qt/walletmodel.h>

#ifdef USE_QRCODE
#include <qrencode.h>
#endif

Q_DECLARE_METATYPE(interfaces::WalletBalances)

Balance::Balance(QWidget *parent, BitcoinGUI* gui) :
    QWidget(parent),
    ui(new Ui::Balance),
    mainWindow(gui)
{
    ui->setupUi(this);
    m_balances.total_balance = -1;
    this->ui->containerBalance->setContentsMargins(0,0,0,0);
    this->ui->vLayoutBalance->setContentsMargins(10,10,10,10);

//    Address
//    QPixmap pixAddress(ui->copyAddress->width(),ui->copyAddress->height());
//    pixAddress.load(":/icons/ic-copy-blue-png");
//    QIcon ButtonIcon(pixAddress);
//    ui->copyAddress->setIcon(ButtonIcon);
//    ui->copyAddress->setIconSize(QSize(20, 20));

    ui->btnBalance->installEventFilter(this);
    ui->btnUnconfirmed->installEventFilter(this);
    ui->btnImmature->installEventFilter(this);

    connect(ui->btnBalance, SIGNAL(clicked()), this, SLOT(onBtnBalanceClicked()));
    connect(ui->btnUnconfirmed, SIGNAL(clicked()), this, SLOT(onBtnUnconfirmedClicked()));
    connect(ui->btnImmature, SIGNAL(clicked()), this, SLOT(onBtnImmatureClicked()));
    connect(ui->copyAddress, SIGNAL(clicked()), this, SLOT(onBtnCopyAddressClicked()));

}

bool Balance::eventFilter(QObject *obj, QEvent *event) {
    if (obj == ui->btnBalance || obj == ui->btnUnconfirmed || obj == ui->btnImmature) {
        if (event->type() == QEvent::Leave) {
            if (tooltip && tooltip->isVisible()) {
                tooltip->hide();
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);;
}

Balance::~Balance()
{
    delete ui;
}

void Balance::onBtnBalanceClicked() {
    onBtnBalanceClicked(0);
}

void Balance::onBtnUnconfirmedClicked() {
    onBtnBalanceClicked(1);
}

void Balance::onBtnImmatureClicked() {
    onBtnBalanceClicked(2);
}


void Balance::onBtnCopyAddressClicked() {
    GUIUtil::setClipboard(qAddress);
    openToastDialog("Address copied", mainWindow);
}

void Balance::onBtnBalanceClicked(int type){
    interfaces::Wallet& wallet = walletModel->wallet();
    interfaces::WalletBalances balances = wallet.getBalances();
    int unit = walletModel->getOptionsModel()->getDisplayUnit();

    if(!tooltip)
        tooltip = new TooltipBalance(
                parentWidget(),
                unit,
                balances.zerocoin_balance,
                balances.ring_ct_balance + balances.ct_balance,
                balances.basecoin_balance
        );

    QString firstTitle;
    int64_t firstBalance;
    QString secondTitle;
    int64_t secondBalance;
    QString thirdTitle;
    int64_t thirdBalance;

    QPushButton* widget;
    int posy;
    int posx;

    switch (type){
        case 0:
            firstTitle = QString::fromStdString("Zerocoin");
            firstBalance = balances.zerocoin_balance;
            secondTitle = QString::fromStdString("RingCT & CT");
            secondBalance = balances.ring_ct_balance + balances.ct_balance;
            thirdTitle = QString::fromStdString("Basecoin");
            thirdBalance = balances.basecoin_balance;
            widget = ui->btnBalance;
            posy = 0;
            posx = widget->pos().rx()+150;
           break;
        case 1:
            firstTitle = QString::fromStdString("Zerocoin");
            firstBalance = balances.zerocoin_unconfirmed_balance;
            secondTitle = QString::fromStdString("RingCT & CT");
            secondBalance = balances.ring_ct_unconfirmed_balance + balances.ct_unconfirmed_balance;
            thirdTitle = QString::fromStdString("Basecoin");
            thirdBalance = balances.basecoin_unconfirmed_balance;
            widget = ui->btnUnconfirmed;
            posy = widget->pos().ry() - 140;
            posx = widget->pos().rx() + 130;
            break;
        case 2:
            firstTitle = QString::fromStdString("Zerocoin");
            firstBalance = balances.zerocoin_immature_balance;
            secondTitle = QString::fromStdString("RingCT & CT");
            secondBalance = balances.ring_ct_immature_balance + balances.ct_immature_balance;
            thirdTitle = QString::fromStdString("Basecoin");
            thirdBalance = balances.basecoin_immature_balance;
            widget = ui->btnImmature;
            posy = widget->pos().ry() - 140;
            posx = widget->pos().rx() + 130;
            break;
    }

    tooltip->update(
            firstTitle,
            firstBalance,
            secondTitle,
            secondBalance,
            thirdTitle,
            thirdBalance
    );

    tooltip->move(posx,posy);
    tooltip->show();
}

void Balance::setClientModel(ClientModel *model){
    this->clientModel = model;
}

void Balance::setWalletModel(WalletModel *model){
    this->walletModel = model;

    interfaces::Wallet& wallet = model->wallet();
    interfaces::WalletBalances balances = wallet.getBalances();
    setBalance(balances);
    connect(model, SIGNAL(balanceChanged(interfaces::WalletBalances)), this, SLOT(setBalance(interfaces::WalletBalances)));
    connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    // update the display unit, to not use the default ("VEIL")
    updateDisplayUnit();

    refreshWalletStatus();
}

void Balance::setBalance(const interfaces::WalletBalances& balances){
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    m_balances = balances;
    // TODO: Change this balance calculation
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(
            unit,
            balances.basecoin_balance + balances.ct_balance + balances.ring_ct_balance + balances.zerocoin_balance,
            false,
            BitcoinUnits::separatorAlways));

    ui->labelUnconfirmed->setText(
            BitcoinUnits::formatWithUnit(unit,
            balances.total_unconfirmed_balance,
            false,
            BitcoinUnits::separatorAlways
            )
    );
    ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, balances.total_immature_balance, false, BitcoinUnits::separatorAlways));
    //ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balances.balance + balances.unconfirmed_balance + balances.immature_balance, false, BitcoinUnits::separatorAlways));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    //bool showImmature = balances.total_immature_balance != 0;
    //bool showWatchOnlyImmature = balances.immature_watch_only_balance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    //ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    //ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
}


void Balance::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if (m_balances.total_balance != -1) {
            setBalance(m_balances);
        }
    }
}

void Balance::refreshWalletStatus() {
    // Check wallet status
    interfaces::Wallet& wallet = walletModel->wallet();
    std::string strAddress;
    std::vector<interfaces::WalletAddress> addresses = wallet.getLabelAddress("stealth");
    if(!addresses.empty()) {
        interfaces::WalletAddress address = addresses[0];
        if (address.dest.type() == typeid(CStealthAddress)){
            bool fBech32 = true;
            strAddress = EncodeDestination(address.dest,true);
        }
    }else {
        ui->copyAddress->setVisible(true);
        ui->labelReceive->setAlignment(Qt::AlignLeft);
        ui->labelReceive->setText("Receiving address");
        // Generate a new address to associate with given label
        // TODO: Use only one stealth address here.
        CStealthAddress address;
        if (!walletModel->wallet().getNewStealthAddress(address)) {
            ui->labelQr->setText("");
            ui->copyAddress->setVisible(false);
            ui->labelReceive->setText("Wallet Locked");
            ui->labelReceive->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
            ui->labelAddress->setText("");
            return;
        }
        bool fBech32 = true;
        strAddress = address.ToString(fBech32);
        wallet.setAddressBook(DecodeDestination(strAddress), "stealth", "receive", fBech32);
    }

    qAddress = QString::fromStdString(strAddress);

    // set address
    ui->labelAddress->setText(qAddress.left(12) + "..." + qAddress.right(12));

    SendCoinsRecipient info;
    info.address = qAddress;

    QString uri = GUIUtil::formatBitcoinURI(info);
    ui->labelQr->setText("No address");
#ifdef USE_QRCODE
    ui->labelQr->setText("");
    if(!uri.isEmpty()){
        // limit URI length
        if (uri.length() > MAX_URI_LENGTH){
            ui->labelQr->setText(tr("Resulting URI too long, try to reduce the text for label / message."));
        } else {
            QRcode *code = QRcode_encodeString(uri.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);
            if (!code){
                ui->labelQr->setText(tr("Error encoding URI into QR Code."));
                return;
            }

            int qrImageSize = 150;

            QPixmap transparent(qrImageSize, qrImageSize);
            transparent.fill(Qt::transparent);
            QPainter pain;
            pain.begin(&transparent);
            pain.setCompositionMode(QPainter::CompositionMode_Source);
            //pain.drawPixmap(0, 0, QPixmap::fromImage(image));
            pain.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            pain.fillRect(transparent.rect(), QColor(0, 0, 0, 50));
            pain.end();
            QImage image = transparent.toImage().convertToFormat(QImage::Format_ARGB32);

            QImage qrImage = QImage(code->width + 8, code->width + 8, QImage::Format_RGB32);
            qrImage.fill(0x00ffffff);
            unsigned char *p = code->data;
            for (int y = 0; y < code->width; y++){
                for (int x = 0; x < code->width; x++){
                    qrImage.setPixel(x + 4, y + 4, ((*p & 1) ? 0x004377 : 0x00ffffff));
                    p++;
                }
            }
            QRcode_free(code);

            QPainter painter(&image);
            painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            painter.fillRect(QPixmap::fromImage(image).rect(), QColor(0, 0, 0, 30));
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawPixmap(0, 0, QPixmap::fromImage(qrImage.scaled(qrImageSize,qrImageSize).convertToFormat(QImage::Format_ARGB32)));
            //QFont font = GUIUtil::fixedPitchFont();
            painter.end();
            ui->labelQr->setPixmap(QPixmap::fromImage(image));
            repaint();
        }
    }
#endif
}


