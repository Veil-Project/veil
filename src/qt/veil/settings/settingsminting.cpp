#include <qt/veil/settings/settingsminting.h>
#include <qt/veil/forms/ui_settingsminting.h>

SettingsMinting::SettingsMinting(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsMinting)
{
    ui->setupUi(this);
    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");

    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnSendMint->setProperty("cssClass" , "btn-text-primary");
    ui->btnSendMint->setText("MINT");
    ui->btnSave->setText("SAVE");


    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");

    ui->editAmount->setPlaceholderText("Enter amount here");
    ui->editAmount->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editAmount->setProperty("cssClass" , "edit-primary");

    ui->labelZVeilBalance->setText("0 zVeil");

    ui->labelConvertable->setText("50.738 Veil");

    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(close()));
}
void SettingsMinting::onEscapeClicked(){
}

SettingsMinting::~SettingsMinting()
{
    delete ui;
}
