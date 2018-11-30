#include <qt/veil/unlockpassworddialog.h>
#include <qt/veil/forms/ui_unlockpassworddialog.h>

UnlockPasswordDialog::UnlockPasswordDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UnlockPasswordDialog)
{
    ui->setupUi(this);
    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnCancel->setProperty("cssClass" , "btn-text-primary-inactive");
    ui->editPassword->setPlaceholderText("Enter password ");
    ui->editPassword->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editPassword->setProperty("cssClass" , "edit-primary");
    connect(ui->btnCancel,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));

}

void UnlockPasswordDialog::onEscapeClicked(){
    close();
}

UnlockPasswordDialog::~UnlockPasswordDialog()
{
    delete ui;
}
