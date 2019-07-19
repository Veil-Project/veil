#include <qt/veil/settings/settingsfaq05.h>
#include <qt/veil/forms/ui_settingsfaq05.h>

SettingsFaq05::SettingsFaq05(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq05)
{
    ui->setupUi(this);
}

SettingsFaq05::~SettingsFaq05()
{
    delete ui;
}
