#ifndef SETTINGSMINTING_H
#define SETTINGSMINTING_H
#include <QWidget>
#include <QDialog>
#include <QString>
#include <interfaces/wallet.h>
#include <qt/walletmodel.h>
#include <qt/walletview.h>

class WalletModel;
class WalletView;


namespace Ui {
class SettingsMinting;
}

class SettingsMinting : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsMinting(QWidget *parent = nullptr, WalletView *mainWindow = nullptr, WalletModel *_walletModel = nullptr);
    ~SettingsMinting();
private Q_SLOTS:
    void onCheck10Clicked(bool res);
    void onCheck100Clicked(bool res);
    void onCheck1000Clicked(bool res);
    void onCheck100000Clicked(bool res);
    void mintAmountChange(const QString &amount);
    void btnMint();
    void onCheckFullMintClicked(bool res);
private:
    Ui::SettingsMinting *ui;
    WalletModel *walletModel;
    WalletView *mainWindow;

    void mintzerocoins();
    CAmount parseAmount(const QString &text, bool& valid_out, std::string& strError) const;

    void saveSettings(int prefDenom);
};

#endif // SETTINGSMINTING_H
