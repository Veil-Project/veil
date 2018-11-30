#include <qt/veil/settings/settingsnetwork.h>
#include <qt/veil/forms/ui_settingsnetwork.h>

SettingsNetwork::SettingsNetwork(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsNetwork)
{
    ui->setupUi(this);
    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");
    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));
    ui->editProxy->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editProxy->setProperty("cssClass" , "edit-primary");
    ui->editPort->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editPort->setProperty("cssClass" , "edit-primary");
}
void SettingsNetwork::onEscapeClicked(){
    close();
}

SettingsNetwork::~SettingsNetwork()
{
    delete ui;
}
