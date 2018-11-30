#ifndef VEILSTATUSBAR_H
#define VEILSTATUSBAR_H

#include <QWidget>

class BitcoinGUI;

namespace Ui {
class VeilStatusBar;
}

class VeilStatusBar : public QWidget
{
    Q_OBJECT

public:
    explicit VeilStatusBar(QWidget *parent = 0, BitcoinGUI* gui = 0);
    ~VeilStatusBar();

    void updateSyncStatus(QString status);

private Q_SLOTS:
    void onBtnSyncClicked();

private:
    Ui::VeilStatusBar *ui;
    BitcoinGUI* mainWindow;
};

#endif // VEILSTATUSBAR_H
