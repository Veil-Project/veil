#include <qt/veil/settings/settingsfaq07.h>
#include <qt/veil/forms/ui_settingsfaq07.h>

SettingsFaq07::SettingsFaq07(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq07)
{
    ui->setupUi(this);
}

SettingsFaq07::~SettingsFaq07()
{
    delete ui;
}
