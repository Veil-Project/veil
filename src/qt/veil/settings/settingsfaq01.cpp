#include <qt/veil/settings/settingsfaq01.h>
#include <qt/veil/forms/ui_settingsfaq01.h>

SettingsFaq01::SettingsFaq01(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq01)
{
    ui->setupUi(this);
}

SettingsFaq01::~SettingsFaq01()
{
    delete ui;
}
