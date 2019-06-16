#include <qt/veil/settings/settingsfaq04.h>
#include <qt/veil/forms/ui_settingsfaq04.h>

SettingsFaq04::SettingsFaq04(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq04)
{
    ui->setupUi(this);
}

SettingsFaq04::~SettingsFaq04()
{
    delete ui;
}
