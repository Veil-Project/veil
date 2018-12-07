#ifndef SETTINGSRESTOREFILE_H
#define SETTINGSRESTOREFILE_H

#include <qt/veil/settings/settingsrestore.h>
#include <QWidget>

class SettingsRestore;

namespace Ui {
class SettingsRestoreFile;
}

class SettingsRestoreFile : public QWidget
{
    Q_OBJECT

private Q_SLOTS:
    void onRestoreClicked();
public:
    explicit SettingsRestoreFile(SettingsRestore* _parent = nullptr, QWidget* widget = nullptr);
    ~SettingsRestoreFile();

private:
    Ui::SettingsRestoreFile *ui;
    SettingsRestore* parent;
};

#endif // SETTINGSRESTOREFILE_H
