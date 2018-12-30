#include <qt/veil/receivewidget.h>
#include <qt/veil/forms/ui_receivewidget.h>

#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <interfaces/node.h>
#include <qt/walletview.h>
#include <qt/walletmodel.h>
#include <key_io.h>
#include <wallet/wallet.h>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPixmap>
#include <QIcon>
#include <QClipboard>
#include <QMimeData>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <qt/veil/qtutils.h>

#ifdef USE_QRCODE
#include <qrencode.h>
#endif

ReceiveWidget::ReceiveWidget(QWidget *parent, WalletView* walletView) :
    QWidget(parent),
    mainWindow(walletView),
    ui(new Ui::ReceiveWidget)
{
    ui->setupUi(this);
    ui->title->setProperty("cssClass" , "title");

    QPixmap qrImg(275,
                  275
                  );

    qrImg.load(":/icons/qr");
    /*ui->lblQRCode->setPixmap(
                qrImg.scaled(
                    275,
                    275,
                    Qt::KeepAspectRatio)
                );

                */

    // Copy Address
    QPixmap imgCopy(24,24);
    imgCopy.load(":/icons/ic-copy-dark");
    QIcon ButtonIconCopy(imgCopy);
    ui->btnCopy->setIcon(ButtonIconCopy);
    ui->btnCopy->setIconSize(imgCopy.rect().size());

    // Button Create new address
    QPixmap imgCreate(24,24);
    imgCreate.load(":/icons/ic-update-address");
    QIcon ButtonIconCreate(imgCreate);
    ui->btnCreate->setIcon(ButtonIconCreate);
    ui->btnCreate->setIconSize(imgCreate.rect().size());

    connect(ui->btnCopy, SIGNAL(clicked()), this, SLOT(on_btnCopyAddress_clicked()));
    connect(ui->btnCreate, SIGNAL(clicked()), this, SLOT(generateNewAddressClicked()));

}

void ReceiveWidget::on_btnCopyAddress_clicked() {
    GUIUtil::setClipboard(qAddress);
    openToastDialog("Address copied", mainWindow->getGUI());
}

void ReceiveWidget::generateNewAddressClicked(){
    generateNewAddress();
    openToastDialog("Address generated", mainWindow->getGUI());
}

void ReceiveWidget::generateNewAddress(){
    // Address
    interfaces::Wallet& wallet = walletModel->wallet();
    // Generate a new address to associate with given label
    CStealthAddress address;
    if (!walletModel->wallet().getNewStealthAddress(address))
        return;
    bool fBech32 = true;
    std::string strAddress = address.ToString(fBech32);
    qAddress =  QString::fromStdString(strAddress);

    // set address
    ui->labelAddress->setText(qAddress);

    SendCoinsRecipient info;
    info.address = qAddress;

    QString uri = GUIUtil::formatBitcoinURI(info);
    ui->labelQr->setText("No address");
#ifdef USE_QRCODE
    ui->labelQr->setText("");
    if(!uri.isEmpty())
    {
        // limit URI length
        if (uri.length() > MAX_URI_LENGTH)
        {
            ui->labelQr->setText(tr("Resulting URI too long, try to reduce the text for label / message."));
        } else {
            QRcode *code = QRcode_encodeString(uri.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);
            if (!code)
            {
                ui->labelQr->setText(tr("Error encoding URI into QR Code."));
                return;
            }
            QImage qrImage = QImage(code->width + 8, code->width + 8, QImage::Format_RGB32);
            qrImage.fill(0xffffff);
            unsigned char *p = code->data;
            for (int y = 0; y < code->width; y++)
            {
                for (int x = 0; x < code->width; x++)
                {
                    qrImage.setPixel(x + 4, y + 4, ((*p & 1) ? 0x004377 : 0xffffff));
                    p++;
                }
            }
            QRcode_free(code);

            int qrImageSize = 275;

            QImage qrAddrImage = QImage(qrImageSize, qrImageSize,  QImage::Format_RGB32);//QR_IMAGE_SIZE, QR_IMAGE_SIZE+20, QImage::Format_RGB32);
            qrAddrImage.fill(0xffffff);
            QPainter painter(&qrAddrImage);
            painter.drawImage(0, 0, qrImage.scaled(qrImageSize,qrImageSize));//QR_IMAGE_SIZE, QR_IMAGE_SIZE));
            QFont font = GUIUtil::fixedPitchFont();
            QRect paddedRect = qrAddrImage.rect();

            // calculate ideal font size
            qreal font_size = GUIUtil::calculateIdealFontSize(paddedRect.width() - 20, info.address, font);
            font.setPointSizeF(font_size);

            painter.setFont(font);
            paddedRect.setHeight(qrImageSize);//QR_IMAGE_SIZE+12);
            //painter.drawText(paddedRect, Qt::AlignBottom|Qt::AlignCenter, info.address);
            painter.end();

            ui->labelQr->setPixmap(QPixmap::fromImage(qrAddrImage));
            //ui->btnSaveAs->setEnabled(true);
        }
    }
#endif
}

void ReceiveWidget::setWalletModel(WalletModel *model){
    // TODO: Load view here..
    this->walletModel = model;

    // Label Address
    generateNewAddress();
}

void ReceiveWidget::showEvent(QShowEvent *event){
    QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(this);
    this->setGraphicsEffect(eff);
    QPropertyAnimation *a = new QPropertyAnimation(eff,"opacity");
    a->setDuration(100);
    a->setStartValue(0.25);
    a->setEndValue(1);
    a->setEasingCurve(QEasingCurve::InBack);
    a->start(QPropertyAnimation::DeleteWhenStopped);
}

void ReceiveWidget::hideEvent(QHideEvent *event){
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

ReceiveWidget::~ReceiveWidget()
{
    delete ui;
}
