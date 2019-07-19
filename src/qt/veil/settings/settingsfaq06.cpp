#include <qt/veil/settings/settingsfaq06.h>
#include <qt/veil/forms/ui_settingsfaq06.h>

SettingsFaq06::SettingsFaq06(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq06)
{
    ui->setupUi(this);
}

SettingsFaq06::~SettingsFaq06()
{
    delete ui;
}
