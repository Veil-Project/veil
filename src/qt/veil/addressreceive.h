#ifndef ADDRESSRECEIVE_H
#define ADDRESSRECEIVE_H

#include <QWidget>
#include <QDialog>

namespace Ui {
class AddressReceive;
}

class AddressReceive : public QDialog
{
    Q_OBJECT

public:
    explicit AddressReceive(QWidget *parent = nullptr);
    ~AddressReceive();
private Q_SLOTS:
    void onEscapeClicked();
private:
    Ui::AddressReceive *ui;
};

#endif // ADDRESSRECEIVE_H
