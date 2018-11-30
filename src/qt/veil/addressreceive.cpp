#include <qt/veil/addressreceive.h>
#include <qt/veil/forms/ui_addressreceive.h>
#include <QPixmap>

AddressReceive::AddressReceive(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AddressReceive)
{
    ui->setupUi(this);
    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));

    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");

    // Description

    ui->editDescription->setPlaceholderText("Description (optional)");
    ui->editDescription->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editDescription->setProperty("cssClass" , "edit-primary");

    // QR Image

    QPixmap qrImg(200,
                  200
                  );

    qrImg.load(":/icons/qr");
    ui->imgQR->setPixmap(
                qrImg.scaled(
                    200,
                    200,
                    Qt::KeepAspectRatio)
                );

    // Address

    ui->labelAddress->setText("VN6i46dytMPVhV1JMGZFuQBh7BZZ6nNLox");
}

void AddressReceive::onEscapeClicked(){
    close();

}

AddressReceive::~AddressReceive()
{
    delete ui;
}
