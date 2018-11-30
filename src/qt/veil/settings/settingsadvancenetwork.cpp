#include <qt/veil/settings/settingsadvancenetwork.h>
#include <qt/veil/forms/ui_settingsadvancenetwork.h>

SettingsAdvanceNetwork::SettingsAdvanceNetwork(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsAdvanceNetwork)
{
    ui->setupUi(this);
    ui->btnClear->setProperty("cssClass" , "btn-text-primary");
}

SettingsAdvanceNetwork::~SettingsAdvanceNetwork()
{
    delete ui;
}
