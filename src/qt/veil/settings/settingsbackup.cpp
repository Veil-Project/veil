#include <qt/veil/settings/settingsbackup.h>
#include <qt/veil/forms/ui_settingsbackup.h>

SettingsBackup::SettingsBackup(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsBackup)
{
    ui->setupUi(this);

    ui->btnFolder->setText("Select destination folder");

    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");

    ui->editPassword->setPlaceholderText("Enter password ");
    ui->editPassword->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editPassword->setProperty("cssClass" , "edit-primary");

    ui->editConfirm->setPlaceholderText("Confirm password");
    ui->editConfirm->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editConfirm->setProperty("cssClass" , "edit-primary");

    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));
}

void SettingsBackup::onEscapeClicked(){
    close();
}
SettingsBackup::~SettingsBackup()
{
    delete ui;
}
