#ifndef SETTINGSFAQ05_H
#define SETTINGSFAQ05_H

#include <QWidget>

namespace Ui {
class SettingsFaq05;
}

class SettingsFaq05 : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsFaq05(QWidget *parent = nullptr);
    ~SettingsFaq05();

private:
    Ui::SettingsFaq05 *ui;
};

#endif // SETTINGSFAQ05_H
