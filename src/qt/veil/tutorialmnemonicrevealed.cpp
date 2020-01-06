// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/veil/tutorialmnemonicrevealed.h>
#include <qt/veil/forms/ui_tutorialmnemonicrevealed.h>

#include <veil/mnemonic/mnemonic.h>
#include <QCompleter>
#include <QAbstractItemView>
#include <iostream>

QString editLineCorrectCss = "QLineEdit{border-bottom:1px solid #707070;background-color:#fff;margin-right:6px;margin-left:6px;padding-left:1px;padding-right:1px;padding-top:7px;padding-bottom:7px;margin:8px;}";
QString editLineInvalidCss = "QLineEdit{border-bottom:1px solid red;background-color:#fff;margin-right:6px;margin-left:6px;padding-left:1px;padding-right:1px;padding-top:7px;padding-bottom:7px;margin:8px;}";



TutorialMnemonicRevealed::TutorialMnemonicRevealed(QStringList _wordList, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TutorialMnemonicRevealed),
    wordList(_wordList)
{
    ui->setupUi(this);

    for(int i=0; i<6; i++){
        for(int j=0; j<4; j++){
            QLineEdit* label = new QLineEdit(this);
            label->setAttribute(Qt::WA_MacShowFocusRect, 0);

            label->setStyleSheet(editLineCorrectCss);


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

            connect(label, SIGNAL(textChanged(const QString &)), this, SLOT(textChanged(const QString &)));

            ui->gridLayoutMnemonic->addWidget(label, i, j);
            editList.push_back(label);
        }
    }

}

void TutorialMnemonicRevealed::textChanged(const QString &text){

    QObject *senderObj = sender();
    QLineEdit* label = static_cast<QLineEdit*>(senderObj);
    if(wordList.contains(text)){
        label->setStyleSheet(editLineCorrectCss);
    }else{
        label->setStyleSheet(editLineInvalidCss);
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
