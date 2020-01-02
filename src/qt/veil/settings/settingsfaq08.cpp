#include <qt/veil/settings/settingsfaq08.h>
#include <qt/veil/forms/ui_settingsfaq08.h>

SettingsFaq08::SettingsFaq08(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq08)
{
    ui->setupUi(this);
}

SettingsFaq08::~SettingsFaq08()
{
    delete ui;
}
