#include <qt/veil/balance.h>
#include <qt/veil/forms/ui_balance.h>

#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>
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
    mainWindow(gui),
    ui(new Ui::Balance)
{
    ui->setupUi(this);
    m_balances.balance = -1;
    this->ui->containerBalance->setContentsMargins(0,0,0,0);
    this->ui->vLayoutBalance->setContentsMargins(10,10,10,10);

    // Address
//    QPixmap pixAddress(ui->copyAddress->width(),ui->copyAddress->height());
//    pixAddress.load(":/icons/ic-copy-blue-svg");
//    QIcon ButtonIcon(pixAddress);
//    ui->copyAddress->setIcon(ButtonIcon);
//    ui->copyAddress->setIconSize(QSize(20, 20));

    // Load address qr..
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
    //ui->labelAddress->setStyleSheet("font-size:9px;");
    connect(ui->btnBalance, SIGNAL(clicked()), this, SLOT(onBtnBalanceClicked()));
    connect(ui->copyAddress, SIGNAL(clicked()), this, SLOT(on_btnCopyAddress_clicked()));

}

Balance::~Balance()
{
    delete ui;
}


void Balance::on_btnCopyAddress_clicked() {
    GUIUtil::setClipboard(qAddress);
    openToastDialog("Address copied", mainWindow);
}

void Balance::onBtnBalanceClicked(){
    interfaces::Wallet& wallet = walletModel->wallet();
    interfaces::WalletBalances balances = wallet.getBalances();
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    tooltip = new TooltipBalance(parentWidget(), unit, balances.zerocoin_balance + balances.zerocoin_immature_balance, balances.ring_ct_balance + balances.ring_ct_immature_balance);
    tooltip->move(ui->btnBalance->pos().rx()+150,0);
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


    // Address

    // Generate a new address to associate with given label
    if(!walletModel->wallet().getKeyFromPool(false /* internal */, newKey))
    {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());
        if(!ctx.isValid())
        {
            // TODO: Check if the wallet is locked or not
            // Unlock wallet failed or was cancelled
            //editStatus = WALLET_UNLOCK_FAILURE;
            //return QString();
        }
        if(!wallet.getKeyFromPool(false /* internal */, newKey))
        {
            // TODO: Generation fail
            //editStatus = KEY_GENERATION_FAILURE;
            //return QString();
        }
    }
    wallet.learnRelatedScripts(newKey, OutputType::BECH32);
    std::string strAddress;
    strAddress = EncodeDestination(GetDestinationForKey(newKey, OutputType::BECH32));

    qAddress =  QString::fromStdString(strAddress);

    // set address
    ui->labelAddress->setText(qAddress);

    SendCoinsRecipient info;
    info.address = qAddress;

    // TODO: QR.. add transparent background..

    QString uri = GUIUtil::formatBitcoinURI(info);
//    ui->labelQr->setText("No address");
//#ifdef USE_QRCODE
//    ui->labelQr->setText("");
//    if(!uri.isEmpty())
//    {
//        // limit URI length
//        if (uri.length() > MAX_URI_LENGTH)
//        {
//            ui->labelQr->setText(tr("Resulting URI too long, try to reduce the text for label / message."));
//        } else {
//            QRcode *code = QRcode_encodeString(uri.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);
//            if (!code)
//            {
//                ui->labelQr->setText(tr("Error encoding URI into QR Code."));
//                return;
//            }
//            QImage qrImage = QImage(code->width + 8, code->width + 8, QImage::Format_RGB32);
//            qrImage.fill(0x00ffffff);
//            unsigned char *p = code->data;
//            for (int y = 0; y < code->width; y++)
//            {
//                for (int x = 0; x < code->width; x++)
//                {
//                    qrImage.setPixel(x + 4, y + 4, ((*p & 1) ? 0x004377 : 0x00ffffff));
//                    p++;
//                }
//            }
//            QRcode_free(code);
//
//            int qrImageSize = 150;
//
//            QImage qrAddrImage = QImage(qrImageSize, qrImageSize,  QImage::Format_RGB32);//QR_IMAGE_SIZE, QR_IMAGE_SIZE+20, QImage::Format_RGB32);
//            qrAddrImage.fill(0x00ffffff);
//            QPainter painter(&qrAddrImage);
//            painter.drawImage(0, 0, qrImage.scaled(qrImageSize,qrImageSize));//QR_IMAGE_SIZE, QR_IMAGE_SIZE));w
//            QFont font = GUIUtil::fixedPitchFont();
//            QRect paddedRect = qrAddrImage.rect();
//
//            // calculate ideal font size
//            qreal font_size = GUIUtil::calculateIdealFontSize(paddedRect.width() - 20, info.address, font);
//            font.setPointSizeF(font_size);
//
//            painter.setFont(font);
//            paddedRect.setHeight(qrImageSize);//QR_IMAGE_SIZE+12);
//            //painter.drawText(paddedRect, Qt::AlignBottom|Qt::AlignCenter, info.address);
//            painter.end();
//
//            ui->labelQr->setPixmap(QPixmap::fromImage(qrAddrImage));
//            //ui->btnSaveAs->setEnabled(true);
//        }
//    }
//#endif
}

void Balance::setBalance(const interfaces::WalletBalances& balances){
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    m_balances = balances;
    // TODO: Change this balance calculation
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balances.balance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, balances.unconfirmed_balance, false, BitcoinUnits::separatorAlways));
    ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, balances.immature_balance, false, BitcoinUnits::separatorAlways));
    //ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balances.balance + balances.unconfirmed_balance + balances.immature_balance, false, BitcoinUnits::separatorAlways));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = balances.immature_balance != 0;
    //bool showWatchOnlyImmature = balances.immature_watch_only_balance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    //ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    //ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
}


void Balance::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if (m_balances.balance != -1) {
            setBalance(m_balances);
        }
    }
}
