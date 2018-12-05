#include <qt/veil/tutorialmnemonicrevealed.h>
#include <qt/veil/forms/ui_tutorialmnemonicrevealed.h>


#include <QCompleter>
#include <QAbstractItemView>

TutorialMnemonicRevealed::TutorialMnemonicRevealed(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TutorialMnemonicRevealed)
{
    ui->setupUi(this);
    for(int i=0; i<6; i++){
        for(int j=0; j<4; j++){
            QLineEdit* label = new QLineEdit(this);
            label->setAttribute(Qt::WA_MacShowFocusRect, 0);


            label->setStyleSheet(
                        "QLineEdit{border-bottom:1px solid #707070;background-color:#fff;margin-right:6px;margin-left:6px;padding-left:2px;padding-right:2px;padding-top:7px;padding-bottom:7px;margin:8px;}");

            // TODO: Complete this with the available words..
            QStringList wordList;
            wordList << "alpha" << "omega" << "omicron" << "zeta" << "alca" << "armando" << "auch";
            QCompleter *completer = new QCompleter(wordList, this);
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            label->setCompleter(completer);

            QAbstractItemView *popup = completer->popup();
            popup->setStyleSheet("QListView"
                                 "{"
                                     "border-style: none;"
                                     "background-color: #ffffff;"
                                     "padding:0px;"
                                     "font-size:14px;"
                                     "border-radius:6px;"
                                 "}"
                                 ""
                                 "QListView::item"
                                 "{"
                                 "    border-style: none;"
                                 "    background-color: #ffffff;"
                                 "font-size:14px;"
                                 "    border-radius:3px;"
                                 "}"
                                 ""
                                 "QListView::item:checked"
                                 "{"
                                 "    border-style: none;"
                                 "    background-color: #105aef;"
                                 "    border-radius:2px;"
                                 "}"
                                 ""
                                 "QListView::item:hover"
                                 "{"
                                 "    border-style: none;"
                                 "    background-color: #bababa;"
                                 "}"
            );

            ui->gridLayoutMnemonic->addWidget(label, i, j);
            editList.push_back(label);
        }
    }

}

std::list<QString> TutorialMnemonicRevealed::getOrderedStrings(){
    std::list<QString> list;
    for(QLineEdit* label : editList){
        list.push_back(label->text());
    }
    return list;
}

TutorialMnemonicRevealed::~TutorialMnemonicRevealed()
{
    delete ui;
}
