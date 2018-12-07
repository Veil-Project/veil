#ifndef TRANSACTIONDETAILDIALOG_H
#define TRANSACTIONDETAILDIALOG_H

#include <QDialog>
#include <QWidget>

class WalletModel;
class TransactionRecord;
class OptionsModel;

namespace Ui {
class TransactionDetailDialog;
}

class TransactionDetailDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TransactionDetailDialog(QWidget *parent = nullptr, TransactionRecord *rec = nullptr, WalletModel *walletModel = nullptr);
    ~TransactionDetailDialog();

private Q_SLOTS:
    void onEscapeClicked();
private:
    Ui::TransactionDetailDialog *ui;
};

#endif // TRANSACTIONDETAILDIALOG_H
