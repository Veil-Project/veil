#ifndef SENDCONTROLDIALOG_H
#define SENDCONTROLDIALOG_H

#include <QWidget>
#include <QDialog>

namespace Ui {
class SendControlDialog;
}

class SendControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SendControlDialog(QWidget *parent = nullptr);
    ~SendControlDialog();
private slots:
    void onEscapeClicked();
private:
    Ui::SendControlDialog *ui;
};

#endif // SENDCONTROLDIALOG_H
