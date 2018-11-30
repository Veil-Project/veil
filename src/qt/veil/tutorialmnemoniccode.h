#ifndef TUTORIALMNEMONICCODE_H
#define TUTORIALMNEMONICCODE_H

#include <QLabel>
#include <QWidget>

namespace Ui {
class TutorialMnemonicCode;
}

class TutorialMnemonicCode : public QWidget
{
    Q_OBJECT

public:
    explicit TutorialMnemonicCode(QWidget *parent = nullptr);
    ~TutorialMnemonicCode();

private Q_SLOTS:
    void onRevealClicked();
private:
    Ui::TutorialMnemonicCode *ui;

    std::list<QLabel*> labelsList;
};

#endif // TUTORIALMNEMONICCODE_H
