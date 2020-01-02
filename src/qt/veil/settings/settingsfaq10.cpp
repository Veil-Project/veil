#include <qt/veil/settings/settingsfaq10.h>
#include <qt/veil/forms/ui_settingsfaq10.h>

SettingsFaq10::SettingsFaq10(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq10)
{
    ui->setupUi(this);
}

SettingsFaq10::~SettingsFaq10()
{
    delete ui;
}
