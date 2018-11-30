#include <qt/veil/settings/settingsfaq03.h>
#include <qt/veil/forms/ui_settingsfaq03.h>

SettingsFaq03::SettingsFaq03(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq03)
{
    ui->setupUi(this);
}

SettingsFaq03::~SettingsFaq03()
{
    delete ui;
}
