#ifndef SETTINGSMINTING_H
#define SETTINGSMINTING_H
#include <QWidget>
#include <QDialog>
#include <QString>
#include <interfaces/wallet.h>
#include <qt/walletmodel.h>

class WalletModel;


namespace Ui {
class SettingsMinting;
}

class SettingsMinting : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsMinting(QWidget *parent = nullptr, WalletModel *walletModel = nullptr);
    ~SettingsMinting();
private Q_SLOTS:
    void onEscapeClicked();
    void onCheck10Clicked(bool res);
    void onCheck100Clicked(bool res);
    void onCheck1000Clicked(bool res);
    void onCheck100000Clicked(bool res);
    void mintAmountChange(const QString &amount);
private:
    Ui::SettingsMinting *ui;
};

#endif // SETTINGSMINTING_H
