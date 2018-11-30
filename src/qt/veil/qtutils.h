#ifndef QTUTILS_H
#define QTUTILS_H

#include <QWidget>
#include <QDialog>
#include <QPropertyAnimation>
#include <QAbstractAnimation>
#include <QPoint>
#include <QString>
#include <qt/bitcoingui.h>

class BitcoinGUI;


void openDialog(QWidget * dialog);
void openDialogFullScreen(QWidget *parent, QWidget * dialog);
void openDialogWithOpaqueBackground(QDialog *widget, BitcoinGUI *gui, int posX = 3);
void openDialogWithOpaqueBackgroundFullScreen(QDialog *widget, BitcoinGUI *gui);


void openToastDialog(QString text, QWidget *gui);

#endif // QTUTILS_H
