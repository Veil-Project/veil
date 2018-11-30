#include <qt/veil/createpassword.h>
#include <qt/veil/forms/ui_createpassword.h>

CreatePassword::CreatePassword(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::CreatePassword)
{
    ui->setupUi(this);
    ui->editPassword->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editPasswordRepeat->setAttribute(Qt::WA_MacShowFocusRect, 0);



    // Error Message
    ui->errorMessage->setVisible(true);
    ui->separatorPassword->setStyleSheet("background-color:#fd1337");
    ui->separatorPasswordRepeat->setStyleSheet("background-color:#fd1337");
}

CreatePassword::~CreatePassword()
{
    delete ui;
}
