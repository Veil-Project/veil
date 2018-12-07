#ifndef SETTINGSRESTORESEED_H
#define SETTINGSRESTORESEED_H

#include <QWidget>

namespace Ui {
class SettingsRestoreSeed;
}

class SettingsRestoreSeed : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsRestoreSeed(QStringList wordList, QWidget *parent = nullptr);
    ~SettingsRestoreSeed();

private:
    Ui::SettingsRestoreSeed *ui;
};

#endif // SETTINGSRESTORESEED_H
