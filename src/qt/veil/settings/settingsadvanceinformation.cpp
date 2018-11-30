#include <qt/veil/settings/settingsadvanceinformation.h>
#include <qt/veil/forms/ui_settingsadvanceinformation.h>

SettingsAdvanceInformation::SettingsAdvanceInformation(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsAdvanceInformation)
{
    ui->setupUi(this);
    ui->btnDebug->setProperty("cssClass" , "btn-secundary-blue");


}

SettingsAdvanceInformation::~SettingsAdvanceInformation()
{
    delete ui;
}
