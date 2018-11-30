#include <qt/veil/settings/settingsrestorefile.h>
#include <qt/veil/forms/ui_settingsrestorefile.h>

SettingsRestoreFile::SettingsRestoreFile(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsRestoreFile)
{
    ui->setupUi(this);
    ui->editPassword->setPlaceholderText("Enter password");
    ui->editPassword->setAttribute(Qt::WA_MacShowFocusRect, 0);
}

SettingsRestoreFile::~SettingsRestoreFile()
{
    delete ui;
}
