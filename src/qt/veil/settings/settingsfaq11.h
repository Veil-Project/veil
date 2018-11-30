#ifndef SETTINGSFAQ11_H
#define SETTINGSFAQ11_H

#include <QWidget>

namespace Ui {
class SettingsFaq11;
}

class SettingsFaq11 : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsFaq11(QWidget *parent = nullptr);
    ~SettingsFaq11();

private:
    Ui::SettingsFaq11 *ui;
};

#endif // SETTINGSFAQ11_H
