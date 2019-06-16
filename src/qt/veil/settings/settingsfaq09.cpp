#include <qt/veil/settings/settingsfaq09.h>
#include <qt/veil/forms/ui_settingsfaq09.h>

SettingsFaq09::SettingsFaq09(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq09)
{
    ui->setupUi(this);
}

SettingsFaq09::~SettingsFaq09()
{
    delete ui;
}
