#include <qt/veil/settings/settingschangepassword.h>
#include <qt/veil/forms/ui_settingschangepassword.h>

SettingsChangePassword::SettingsChangePassword(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsChangePassword)
{
    ui->setupUi(this);
    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");

    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));
    ui->editCurrent->setPlaceholderText("Enter current password ");
    ui->editCurrent->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editCurrent->setProperty("cssClass" , "edit-primary");

    ui->editNew->setPlaceholderText("Enter new password ");
    ui->editNew->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editNew->setProperty("cssClass" , "edit-primary");

    ui->editConfirm->setPlaceholderText("Confirm password");
    ui->editConfirm->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editConfirm->setProperty("cssClass" , "edit-primary");
}
void SettingsChangePassword::onEscapeClicked(){
    close();
}
SettingsChangePassword::~SettingsChangePassword()
{
    delete ui;
}
