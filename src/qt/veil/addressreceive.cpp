// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/veil/addressreceive.h>
#include <qt/veil/forms/ui_addressreceive.h>

#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <interfaces/node.h>
#include <qt/walletview.h>
#include <qt/walletmodel.h>
#include <key_io.h>
#include <wallet/wallet.h>

#include <QString>
#include <QPixmap>
#include <QIcon>
#include <QClipboard>
#include <QMimeData>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QString>
#include <qt/veil/qtutils.h>

#ifdef USE_QRCODE
#include <qrencode.h>
#endif

AddressReceive::AddressReceive(QWidget *parent, WalletModel* _walletModel, bool isMinerAddress) :
    QDialog(parent),
    ui(new Ui::AddressReceive),
    walletModel(_walletModel),
    isMiner(isMinerAddress)
{
    ui->setupUi(this);

    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSave->setProperty("cssClass" , "btn-text-primary");

    // Description

    ui->editDescription->setPlaceholderText("Description (optional)");
    ui->editDescription->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editDescription->setProperty("cssClass" , "edit-primary");

    ui->labelTitle->setText(isMinerAddress ? "New Miner Address" : "New Receiving Address");
    generateNewAddress(isMinerAddress);

    connect(ui->btnCopy, SIGNAL(clicked()),this, SLOT(onBtnCopyAddressClicked()));
    connect(ui->btnSave, SIGNAL(clicked()),this, SLOT(onBtnSaveClicked()));

}

void AddressReceive::onBtnSaveClicked(){
    std::string label = ui->editDescription->text().toUtf8().constData();
    interfaces::Wallet& wallet = walletModel->wallet();
    if(isMiner){
        wallet.setAddressBook(dest, label, "receive_miner", false);
    }else{
        wallet.setAddressBook(dest, label, "receive", true);
    }

    accept();
}

void AddressReceive::onBtnCopyAddressClicked() {
    if(!qAddress.isEmpty()) {
        GUIUtil::setClipboard(qAddress);
        openToastDialog("Address copied", this);
    }else openToastDialog("Wallet Encrypted, please unlock it first", this);
}

void AddressReceive::generateNewAddress(bool isMinerAddress){
    // Address
    interfaces::Wallet& wallet = walletModel->wallet();

    std::string strAddress;
    if (!isMinerAddress) {
        // Generate a new address to associate with given label
        CStealthAddress address;
        if (!walletModel->wallet().getNewStealthAddress(address))
            return;

        bool fBech32 = true;
        strAddress = address.ToString(fBech32);
        dest = DecodeDestination(strAddress);
        wallet.setAddressBook(dest, "", "receive", true);
    }else{
        CPubKey newKey;
        if (!wallet.getKeyFromPool(false, newKey)) {
            WalletModel::UnlockContext ctx(walletModel->requestUnlock());
            if(!ctx.isValid()) {
                // TODO: Check if the wallet is locked or not
                openToastDialog(QString::fromUtf8("Address generation failed"), this);
                return;
            }
            if(!wallet.getKeyFromPool(false /* internal */, newKey)) {
                openToastDialog(QString::fromUtf8("Address generation failed"), this);
                return;
            }
        }else {
            wallet.learnRelatedScripts(newKey, OutputType::LEGACY);
            dest = newKey.GetID();
            wallet.setAddressBook(dest, "", "receive_miner");
            bool fBech32 = false;
            strAddress = EncodeDestination(dest, fBech32);
        }
    }
    qAddress =  QString::fromStdString(strAddress);

    // set address
    if(!isMinerAddress){
        ui->labelAddress->setText(qAddress.left(12) + "..." + qAddress.right(12));
    }else{
        ui->labelAddress->setText(qAddress);
    }

    SendCoinsRecipient info;
    info.address = qAddress;

    QString uri = GUIUtil::formatBitcoinURI(info);
    ui->imgQR->setText("No address");
#ifdef USE_QRCODE
    ui->imgQR->setText("");
    if(!uri.isEmpty())
    {
        // limit URI length
        if (uri.length() > MAX_URI_LENGTH)
        {
            ui->imgQR->setText(tr("Resulting URI too long, try to reduce the text for label / message."));
        } else {
            QRcode *code = QRcode_encodeString(uri.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);
            if (!code)
            {
                ui->imgQR->setText(tr("Error encoding URI into QR Code."));
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

            int qrImageSize = 200;

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

            ui->imgQR->setPixmap(QPixmap::fromImage(qrAddrImage));
            //ui->btnSaveAs->setEnabled(true);
        }
    }
#endif
}

void AddressReceive::onEscapeClicked(){
    close();

}

AddressReceive::~AddressReceive()
{
    delete ui;
}
