#include <qt/veil/settings/settingsadvanceconsole.h>
#include <qt/veil/forms/ui_settingsadvanceconsole.h>

SettingsAdvanceConsole::SettingsAdvanceConsole(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsAdvanceConsole)
{
    ui->setupUi(this);
}

SettingsAdvanceConsole::~SettingsAdvanceConsole()
{
    delete ui;
}
