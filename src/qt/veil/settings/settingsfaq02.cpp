#include <qt/veil/settings/settingsfaq02.h>
#include <qt/veil/forms/ui_settingsfaq02.h>

SettingsFaq02::SettingsFaq02(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq02)
{
       ui->setupUi(this);
}

SettingsFaq02::~SettingsFaq02()
{
    delete ui;
}
