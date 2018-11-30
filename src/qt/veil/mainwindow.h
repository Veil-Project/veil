#ifndef _H
#define _H

//#include "transactionsModel.h"

#include <Q>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableView>
#include <QWidget>
#include <interfaces/wallet.h>

#include <QWidget>
#include <memory>

class ClientModel;
class TransactionFilterProxy;
class TxViewDelegate;
class PlatformStyle;
class WalletModel;

namespace Ui {
class ;
}

class  : public QWidget
{
    Q_OBJECT

public:
    explicit (QWidget *parent = nullptr);
    ~();

    void homeScreen();
private Q_SLOTS:
    void on_txes_row_selected(const QItemSelection &, const QItemSelection &);
    void onBtnBalanceClicked();
private:
    Ui:: *ui;


    QStackedWidget* centerStack;

    QTableView* tableView;
    //TransactionsModel* myModel;
    int selectedRow = -1;

    void initBalance();
    void initCenter();
    void initTxesView();

    void changeScreen(QWidget *widget);

};

#endif // _H
