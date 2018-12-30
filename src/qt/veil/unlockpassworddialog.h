#ifndef UNLOCKPASSWORDDIALOG_H
#define UNLOCKPASSWORDDIALOG_H

#include <QDialog>

namespace Ui {
class UnlockPasswordDialog;
}

class WalletModel;

class UnlockPasswordDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UnlockPasswordDialog(bool fForStakingOnly, WalletModel* model, QWidget *parent = nullptr);
    ~UnlockPasswordDialog();
private Q_SLOTS:
    void onEscapeClicked();
    void onUnlockClicked();
private:
    Ui::UnlockPasswordDialog *ui;
    WalletModel* walletModel;
    bool fForStakingOnly;
};

#endif // UNLOCKPASSWORDDIALOG_H
