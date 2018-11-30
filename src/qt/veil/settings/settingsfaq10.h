#ifndef SETTINGSFAQ10_H
#define SETTINGSFAQ10_H

#include <QWidget>

namespace Ui {
class SettingsFaq10;
}

class SettingsFaq10 : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsFaq10(QWidget *parent = nullptr);
    ~SettingsFaq10();

private:
    Ui::SettingsFaq10 *ui;
};

#endif // SETTINGSFAQ10_H
