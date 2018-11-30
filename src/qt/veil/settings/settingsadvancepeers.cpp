#include <qt/veil/settings/settingsadvancepeers.h>
#include <qt/veil/forms/ui_settingsadvancepeers.h>

SettingsAdvancePeers::SettingsAdvancePeers(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsAdvancePeers)
{
    ui->setupUi(this);
}

SettingsAdvancePeers::~SettingsAdvancePeers()
{
    delete ui;
}
