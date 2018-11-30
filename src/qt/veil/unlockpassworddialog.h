#ifndef UNLOCKPASSWORDDIALOG_H
#define UNLOCKPASSWORDDIALOG_H

#include <QDialog>

namespace Ui {
class UnlockPasswordDialog;
}

class UnlockPasswordDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UnlockPasswordDialog(QWidget *parent = nullptr);
    ~UnlockPasswordDialog();
private Q_SLOTS:
    void onEscapeClicked();
private:
    Ui::UnlockPasswordDialog *ui;
};

#endif // UNLOCKPASSWORDDIALOG_H
