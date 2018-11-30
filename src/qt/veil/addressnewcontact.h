#ifndef ADDRESSNEWCONTACT_H
#define ADDRESSNEWCONTACT_H

#include <QWidget>
#include <QDialog>

namespace Ui {
class AddressNewContact;
}

class AddressNewContact : public QDialog
{
    Q_OBJECT

public:
    explicit AddressNewContact(QWidget *parent = nullptr);
    ~AddressNewContact();
private Q_SLOTS:
    void onEscapeClicked();
private:
    Ui::AddressNewContact *ui;
};

#endif // ADDRESSNEWCONTACT_H
