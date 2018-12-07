#ifndef TUTORIALMNEMONICREVEALED_H
#define TUTORIALMNEMONICREVEALED_H

#include <QWidget>
#include <list>
#include <QString>
#include <QLineEdit>

namespace Ui {
class TutorialMnemonicRevealed;
}

class TutorialMnemonicRevealed : public QWidget
{
    Q_OBJECT

public:
    explicit TutorialMnemonicRevealed(QStringList wordList, QWidget *parent = nullptr);
    ~TutorialMnemonicRevealed();

    std::list<QString> getOrderedStrings();
private:
    Ui::TutorialMnemonicRevealed *ui;
    std::list<QLineEdit*> editList;
};

#endif // TUTORIALMNEMONICREVEALED_H
