#include <qt/veil/settings/settingsfaq11.h>
#include <qt/veil/forms/ui_settingsfaq11.h>

SettingsFaq11::SettingsFaq11(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq11)
{
    ui->setupUi(this);
}

SettingsFaq11::~SettingsFaq11()
{
    delete ui;
}
