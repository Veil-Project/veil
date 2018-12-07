#include <qt/veil/settings/settingsrestoreseed.h>
#include <qt/veil/forms/ui_settingsrestoreseed.h>
#include <QLineEdit>
#include <QString>
#include <QCompleter>
#include <QAbstractItemView>

SettingsRestoreSeed::SettingsRestoreSeed(QStringList wordList, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsRestoreSeed)
{
    ui->setupUi(this);

    for(int i=0; i<6; i++){
        for(int j=0; j<4; j++){
            QLineEdit* editWord = new QLineEdit(this);
            editWord->setAttribute(Qt::WA_MacShowFocusRect, 0);
            editWord->setAlignment(Qt::AlignHCenter);
            editWord->setProperty("cssClass" , "edit-seed");
            QCompleter *completer = new QCompleter(wordList, this);
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            editWord->setCompleter(completer);

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

            ui->gridLayout->addWidget(editWord, i, j);
        }
    }

}

SettingsRestoreSeed::~SettingsRestoreSeed()
{
    delete ui;
}
