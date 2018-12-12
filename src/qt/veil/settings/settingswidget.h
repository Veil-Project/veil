#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include <QWidget>
#include <qt/walletview.h>
#include <qt/walletmodel.h>

class WalletModel;
class WalletView;

namespace Ui {
class SettingsWidget;
}

class SettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsWidget(WalletView *parent = nullptr);
    ~SettingsWidget();

    virtual void showEvent(QShowEvent *event) override;
    virtual void hideEvent(QHideEvent *event) override;

    void setWalletModel(WalletModel *model);

private Q_SLOTS:
    void onResetOptionClicked();
    void onLockWalletClicked();
    void onPreferenceClicked();
    void onBackupClicked();
    void onRestoreClicked();
    void onChangePasswordClicked();
    void onMintingClicked();
    void onNetworkClicked();
    void onFaqClicked();
    void onAdvanceClicked();
private:
    Ui::SettingsWidget *ui;
    WalletView *mainWindow;
    WalletModel *walletModel;

    void openDialog(QDialog *dialog);
};

#endif // SETTINGSWIDGET_H
