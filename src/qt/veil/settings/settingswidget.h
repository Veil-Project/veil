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

    void refreshWalletStatus();

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
    void onCheckStakingClicked(bool res);
    void onLabelStakingClicked();
private:
    Ui::SettingsWidget *ui;
    WalletView *mainWindow;
    WalletModel *walletModel = nullptr;

    void openDialog(QDialog *dialog);

    void updateStakingCheckboxStatus();

    bool isViewInitiated = false;
};

#endif // SETTINGSWIDGET_H
