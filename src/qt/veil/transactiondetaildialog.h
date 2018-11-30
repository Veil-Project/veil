#ifndef TRANSACTIONDETAILDIALOG_H
#define TRANSACTIONDETAILDIALOG_H

#include <QDialog>
#include <QWidget>

namespace Ui {
class TransactionDetailDialog;
}

class TransactionDetailDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TransactionDetailDialog(QWidget *parent = nullptr);
    ~TransactionDetailDialog();

private Q_SLOTS:
    void onEscapeClicked();
private:
    Ui::TransactionDetailDialog *ui;
};

#endif // TRANSACTIONDETAILDIALOG_H
