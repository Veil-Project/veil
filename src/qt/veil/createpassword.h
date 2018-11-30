#ifndef CREATEPASSWORD_H
#define CREATEPASSWORD_H

#include <QWidget>

namespace Ui {
class CreatePassword;
}

class CreatePassword : public QWidget
{
    Q_OBJECT

public:
    explicit CreatePassword(QWidget *parent = nullptr);
    ~CreatePassword();

private:
    Ui::CreatePassword *ui;
};

#endif // CREATEPASSWORD_H
