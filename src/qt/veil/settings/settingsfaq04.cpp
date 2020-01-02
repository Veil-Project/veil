#include <qt/veil/settings/settingsfaq04.h>
#include <qt/veil/forms/ui_settingsfaq04.h>

SettingsFaq04::SettingsFaq04(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq04)
{
    ui->setupUi(this);
    connect(ui->label_2,SIGNAL(linkActivated(QString)),parent, SLOT(onRadioButton06Clicked()));
}

SettingsFaq04::~SettingsFaq04()
{
    delete ui;
}
