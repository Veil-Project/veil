#include <qt/veil/addressnewcontact.h>
#include <qt/veil/forms/ui_addressnewcontact.h>

AddressNewContact::AddressNewContact(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AddressNewContact)
{
    ui->setupUi(this);
    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));

    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");

    ui->editContactName->setPlaceholderText("Set name");
    ui->editContactName->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editContactName->setProperty("cssClass" , "edit-primary");

    ui->editAddress->setPlaceholderText("Enter address");
    ui->editAddress->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editAddress->setProperty("cssClass" , "edit-primary");
}

void AddressNewContact::onEscapeClicked(){
    close();
}

AddressNewContact::~AddressNewContact()
{
    delete ui;
}
