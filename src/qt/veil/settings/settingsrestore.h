#ifndef SETTINGSRESTORE_H
#define SETTINGSRESTORE_H
#include <qt/veil/settings/settingsrestoreseed.h>
#include <qt/veil/settings/settingsrestorefile.h>

#include <QWidget>
#include <QDialog>

class SettingsRestoreFile;

namespace Ui {
class SettingsRestore;
}

class SettingsRestore : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsRestore(QStringList _wordList, QWidget *parent = nullptr);
    ~SettingsRestore();

    void acceptFile();
private Q_SLOTS:
    void onEscapeClicked();
    void onFileClicked();
    void onSeedClicked();
    void changeScreen(QWidget *widget);
private:
    Ui::SettingsRestore *ui;
    SettingsRestoreFile *restoreFile;
    SettingsRestoreSeed *restoreSeed;
    QStringList wordList;
};

#endif // SETTINGSRESTORE_H
