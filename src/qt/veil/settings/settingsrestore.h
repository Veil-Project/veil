#ifndef SETTINGSRESTORE_H
#define SETTINGSRESTORE_H
#include <qt/veil/settings/settingsrestoreseed.h>
#include <qt/veil/settings/settingsrestorefile.h>

#include <QWidget>
#include <QDialog>

namespace Ui {
class SettingsRestore;
}

class SettingsRestore : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsRestore(QWidget *parent = nullptr);
    ~SettingsRestore();
private Q_SLOTS:
    void onEscapeClicked();
    void onFileClicked();
    void onSeedClicked();
    void changeScreen(QWidget *widget);
private:
    Ui::SettingsRestore *ui;
    SettingsRestoreFile *restoreFile;
    SettingsRestoreSeed *restoreSeed;
};

#endif // SETTINGSRESTORE_H
