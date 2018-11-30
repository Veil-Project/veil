#ifndef SETTINGSFAQ08_H
#define SETTINGSFAQ08_H

#include <QWidget>

namespace Ui {
class SettingsFaq08;
}

class SettingsFaq08 : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsFaq08(QWidget *parent = nullptr);
    ~SettingsFaq08();

private:
    Ui::SettingsFaq08 *ui;
};

#endif // SETTINGSFAQ08_H
