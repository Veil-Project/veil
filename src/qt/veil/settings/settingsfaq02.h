#ifndef SETTINGSFAQ02_H
#define SETTINGSFAQ02_H

#include <QWidget>

namespace Ui {
class SettingsFaq02;
}

class SettingsFaq02 : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsFaq02(QWidget *parent = nullptr);
    ~SettingsFaq02();

private:
    Ui::SettingsFaq02 *ui;
};

#endif // SETTINGSFAQ02_H
