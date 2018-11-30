#include <qt/veil/transactiondetaildialog.h>
#include <qt/veil/forms/ui_transactiondetaildialog.h>

TransactionDetailDialog::TransactionDetailDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TransactionDetailDialog)
{
    ui->setupUi(this);
    // Titles
    ui->title->setProperty("cssClass" , "title");
    ui->labelAmount->setProperty("cssClass" , "label-detail");
    ui->labelConfirmations->setProperty("cssClass" , "label-detail");
    ui->labelDate->setProperty("cssClass" , "label-detail");
    ui->labelDescription->setProperty("cssClass" , "label-detail");
    ui->labelFee->setProperty("cssClass" , "label-detail");
    //ui->labelFrom->setProperty("cssClass" , "label-detail");
    ui->labelInputs->setProperty("cssClass" , "label-detail");
    ui->labelSend->setProperty("cssClass" , "label-detail");
    ui->labelSize->setProperty("cssClass" , "label-detail");

    // Information

    ui->textAmount->setProperty("cssClass" , "text-detail");
    ui->textConfirmations->setProperty("cssClass" , "text-detail");
    ui->textDate->setProperty("cssClass" , "text-detail");
    ui->textDescription->setProperty("cssClass" , "text-detail");
    ui->textFee->setProperty("cssClass" , "text-detail");
    //ui->textFrom->setProperty("cssClass" , "text-detail");
    ui->textInputs->setProperty("cssClass" , "text-detail");
    ui->textSend->setProperty("cssClass" , "text-detail");
    ui->textSize->setProperty("cssClass" , "text-detail");
    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));
}

void TransactionDetailDialog::onEscapeClicked(){
    close();
}


TransactionDetailDialog::~TransactionDetailDialog()
{
    delete ui;
}
