#include <qt/veil/sendconfirmation.h>
#include <qt/veil/forms/ui_sendconfirmation.h>

SendConfirmation::SendConfirmation(QWidget *parent, const QString &addresses, const QString &amount, const QString &fee) :
    QDialog(parent),
    ui(new Ui::SendConfirmation)
{
    ui->setupUi(this);
    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSend->setProperty("cssClass" , "btn-text-primary");
    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");

    ui->labelAmount->setProperty("cssClass" , "label-detail");
    ui->labelSend->setProperty("cssClass" , "label-detail");
    ui->labelFee->setProperty("cssClass" , "label-detail");

    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(close()));
    connect(ui->btnSend,SIGNAL(clicked()),this, SLOT(onSendClicked()));

    // Information
    ui->textAmount->setProperty("cssClass" , "text-detail");
    ui->textAddress->setProperty("cssClass" , "text-detail");
    ui->textFee->setProperty("cssClass" , "text-detail");

    ui->textAmount->setText(amount);
    ui->textFee->setText(fee);
    ui->textAddress->setText(addresses);



}

void SendConfirmation::onSendClicked(){
    res = true;
    accept();
}

SendConfirmation::~SendConfirmation()
{
    delete ui;
}
