#include <qt/test/wallettests.h>
#include <qt/test/util.h>

#include <interfaces/node.h>
#include <qt/bitcoinamountfield.h>
#include <qt/callback.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/sendcoinsdialog.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/walletmodel.h>
#include <key_io.h>
#include <test/test_veil.h>
#include <validation.h>
#include <veil/ringct/anonwallet.h>
#include <veil/zerocoin/spendreceipt.h>
#include <veil/zerocoin/zwallet.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>
#include <qt/overviewpage.h>
#include <qt/veil/balance.h>
#include <qt/receivecoinsdialog.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/receiverequestdialog.h>

#include <memory>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QTextEdit>
#include <QListView>
#include <QDialogButtonBox>

namespace
{
//! Press "Yes" or "Cancel" buttons in modal send confirmation dialog.
void ConfirmSend(QString* text = nullptr, bool cancel = false)
{
    QTimer::singleShot(0, makeCallback([text, cancel](Callback* callback) {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->inherits("SendConfirmationDialog")) {
                SendConfirmationDialog* dialog = qobject_cast<SendConfirmationDialog*>(widget);
                if (text) *text = dialog->text();
                QAbstractButton* button = dialog->button(cancel ? QMessageBox::Cancel : QMessageBox::Yes);
                button->setEnabled(true);
                button->click();
            }
        }
        delete callback;
    }), SLOT(call()));
}

//! Send coins to address and return txid.
//
// Veil's SendCoinsDialog cannot be driven headless: on_sendButton_clicked
// prepares the transaction on a QtConcurrent thread behind a modal message
// box and parents its confirmation dialog to the BitcoinGUI main window,
// which this harness does not construct. Drive WalletModel directly instead;
// this still exercises the prepare/send path and the transaction table
// notifications. The dialog also hardcodes m_signal_bip125_rbf to false, so
// replaceable transactions cannot be created from the GUI at all.
uint256 SendCoins(CWallet& wallet, WalletModel& walletModel, const CTxDestination& address, CAmount amount)
{
    SendCoinsRecipient rcp;
    rcp.address = QString::fromStdString(EncodeDestination(address));
    rcp.amount = amount;
    WalletModelTransaction transaction({rcp});
    WalletModelSpendType spendType;
    CZerocoinSpendReceipt receipt;
    std::vector<std::tuple<CWalletTx, std::vector<CDeterministicMint>, std::vector<CZerocoinMint>>> vCommitData;
    uint256 txid;
    boost::signals2::scoped_connection c(wallet.NotifyTransactionChanged.connect([&txid](CWallet*, const uint256& hash, ChangeType status) {
        if (status == CT_NEW) txid = hash;
    }));
    QMetaObject::Connection msgConn = QObject::connect(&walletModel, &WalletModel::message,
        [](const QString&, const QString& msg, unsigned int) { qWarning() << "SendCoins: wallet message:" << msg; });
    WalletModel::SendCoinsReturn prepared = walletModel.prepareTransaction(transaction, CCoinControl(), spendType, receipt, vCommitData, OUTPUT_STANDARD);
    QObject::disconnect(msgConn);
    if (prepared.status != WalletModel::OK) {
        qWarning() << "SendCoins: prepareTransaction failed with status" << prepared.status;
        return txid;
    }
    WalletModel::SendCoinsReturn sent = walletModel.sendCoins(transaction);
    if (sent.status != WalletModel::OK)
        qWarning() << "SendCoins: sendCoins failed with status" << sent.status << sent.reasonCommitFailed;
    return txid;
}

//! Find index of txid in transaction list.
QModelIndex FindTx(const QAbstractItemModel& model, const uint256& txid)
{
    QString hash = QString::fromStdString(txid.ToString());
    int rows = model.rowCount({});
    for (int row = 0; row < rows; ++row) {
        QModelIndex index = model.index(row, 0, {});
        if (model.data(index, TransactionTableModel::TxHashRole) == hash) {
            return index;
        }
    }
    return {};
}

//! Invoke bumpfee on txid and check results.
void BumpFee(TransactionView& view, const uint256& txid, bool expectDisabled, std::string expectError, bool cancel)
{
    QTableView* table = view.findChild<QTableView*>("transactionView");
    QModelIndex index = FindTx(*table->selectionModel()->model(), txid);
    QVERIFY2(index.isValid(), "Could not find BumpFee txid");

    // Select row in table, invoke context menu, and make sure bumpfee action is
    // enabled or disabled as expected.
    QAction* action = view.findChild<QAction*>("bumpFeeAction");
    table->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    action->setEnabled(expectDisabled);
    table->customContextMenuRequested({});
    QCOMPARE(action->isEnabled(), !expectDisabled);

    action->setEnabled(true);
    QString text;
    if (expectError.empty()) {
        ConfirmSend(&text, cancel);
    } else {
        ConfirmMessage(&text);
    }
    action->trigger();
    QVERIFY(text.indexOf(QString::fromStdString(expectError)) != -1);
}

//! Simple qt wallet tests.
//
// Test widgets can be debugged interactively calling show() on them and
// manually running the event loop, e.g.:
//
//     sendCoinsDialog.show();
//     QEventLoop().exec();
//
// This also requires overriding the default minimal Qt platform:
//
//     src/qt/test/test_veil-qt -platform xcb      # Linux
//     src/qt/test/test_veil-qt -platform windows  # Windows
//     src/qt/test/test_veil-qt -platform cocoa    # macOS
void TestGUI()
{
    // Set up wallet and chain with CoinbaseMaturity()+5 blocks (5 mature
    // blocks for spending). TestChain100Setup mines CoinbaseMaturity()
    // blocks, which on Veil regtest is 10, not 100.
    TestChain100Setup test;
    for (int i = 0; i < 5; ++i) {
        test.CreateAndProcessBlock({}, GetScriptForRawPubKey(test.coinbaseKey.GetPubKey()));
    }
    const int numBlocks = Params().CoinbaseMaturity() + 5;
    std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>("mock", WalletDatabase::CreateMock());
    bool firstRun;
    wallet->LoadWallet(firstRun);
    // Normal wallet load wires up the zerocoin wallet/tracker and the anon
    // wallet in CreateWalletFromFile; the harness must do the same or GUI
    // wallet flows dereference a null zTracker/pAnonWalletMain. Both derive
    // their master seeds from the wallet's HD chain, so seed it first.
    wallet->SetHDSeed(wallet->GenerateNewSeed());
    CzWallet zwallet(wallet.get());
    wallet->setZWallet(&zwallet);
    std::shared_ptr<WalletDatabase> anonDatabase = WalletDatabase::CreateMock();
    {
        // Batches only create the backing (in-memory) db file when opened
        // with 'c' in the mode; AnonWallet's batches open "r+".
        WalletBatch batch(*anonDatabase, "cr+");
    }
    AnonWallet anonwallet(wallet, "anonwallet", anonDatabase);
    CExtKey extMasterAnon;
    QVERIFY(wallet->GetAnonWalletSeed(extMasterAnon));
    QVERIFY(anonwallet.Initialise(&extMasterAnon));
    wallet->SetAnonWallet(&anonwallet);
    {
        LOCK(wallet->cs_wallet);
        wallet->SetAddressBook(GetDestinationForKey(test.coinbaseKey.GetPubKey(), wallet->m_default_address_type), "", "receive");
        wallet->AddKeyPubKey(test.coinbaseKey, test.coinbaseKey.GetPubKey());
    }
    {
        LOCK(cs_main);
        WalletRescanReserver reserver(wallet.get());
        reserver.reserve();
        wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver, true);
    }
    wallet->SetBroadcastTransactions(true);

    // Create widgets for sending coins and listing transactions.
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    SendCoinsDialog sendCoinsDialog(platformStyle.get());
    TransactionView transactionView(platformStyle.get());
    auto node = interfaces::MakeNode();
    OptionsModel optionsModel(*node);
    AddWallet(wallet);
    WalletModel walletModel(std::move(node->getWallets().back()), *node, platformStyle.get(), &optionsModel);
    RemoveWallet(wallet);
    sendCoinsDialog.setModel(&walletModel);
    transactionView.setModel(&walletModel);

    // Send two transactions, and verify they are added to transaction list.
    TransactionTableModel* transactionTableModel = walletModel.getTransactionTableModel();
    QCOMPARE(transactionTableModel->rowCount({}), numBlocks);
    // Veil spends basecoin into CT outputs, which need the recipient's public
    // key for ECDH, so the destination must be a stealth address rather than
    // a bare CKeyID.
    CStealthAddress stealthAddress;
    QVERIFY(anonwallet.NewStealthKey(stealthAddress, 0, nullptr));
    uint256 txid1 = SendCoins(*wallet.get(), walletModel, stealthAddress, 5 * COIN);
    uint256 txid2 = SendCoins(*wallet.get(), walletModel, stealthAddress, 10 * COIN);
    // The table model receives wallet notifications through queued
    // invocations, which only run once the event loop spins.
    qApp->processEvents();
    QCOMPARE(transactionTableModel->rowCount({}), numBlocks + 2);
    QVERIFY(FindTx(*transactionTableModel, txid1).isValid());
    QVERIFY(FindTx(*transactionTableModel, txid2).isValid());

    // Call bumpfee. GUI transactions never signal BIP 125 (see SendCoins
    // above), so only the non-replaceable path can be exercised.
    BumpFee(transactionView, txid1, true /* expect disabled */, "not BIP 125 replaceable" /* expected error */, false /* cancel */);

    // Exercise the transaction list on OverviewPage
    OverviewPage overviewPage(platformStyle.get());
    overviewPage.setWalletModel(&walletModel);

    // Check current balance in the Balance widget. Veil's GUI shows balances
    // in the top-bar Balance widget; OverviewPage has no balance label.
    Balance balanceWidget;
    balanceWidget.setWalletModel(&walletModel);
    QLabel* balanceLabel = balanceWidget.findChild<QLabel*>("labelBalance");
    QString balanceText = balanceLabel->text();
    int unit = walletModel.getOptionsModel()->getDisplayUnit();
    interfaces::WalletBalances balances = walletModel.wallet().getBalances();
    CAmount balance = balances.basecoin_balance + balances.ct_balance + balances.ring_ct_balance + balances.zerocoin_balance;
    QString balanceComparison = BitcoinUnits::formatWithUnit(unit, balance, false, BitcoinUnits::separatorAlways);
    QCOMPARE(balanceText, balanceComparison);

    // Check Request Payment button
    ReceiveCoinsDialog receiveCoinsDialog(platformStyle.get());
    receiveCoinsDialog.setModel(&walletModel);
    RecentRequestsTableModel* requestTableModel = walletModel.getRecentRequestsTableModel();

    // Label input
    QLineEdit* labelInput = receiveCoinsDialog.findChild<QLineEdit*>("reqLabel");
    labelInput->setText("TEST_LABEL_1");

    // Amount input
    BitcoinAmountField* amountInput = receiveCoinsDialog.findChild<BitcoinAmountField*>("reqAmount");
    amountInput->setValue(1);

    // Message input
    QLineEdit* messageInput = receiveCoinsDialog.findChild<QLineEdit*>("reqMessage");
    messageInput->setText("TEST_MESSAGE_1");
    int initialRowCount = requestTableModel->rowCount({});
    QPushButton* requestPaymentButton = receiveCoinsDialog.findChild<QPushButton*>("receiveButton");
    requestPaymentButton->click();
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->inherits("ReceiveRequestDialog")) {
            ReceiveRequestDialog* receiveRequestDialog = qobject_cast<ReceiveRequestDialog*>(widget);
            QTextEdit* rlist = receiveRequestDialog->QObject::findChild<QTextEdit*>("outUri");
            QString paymentText = rlist->toPlainText();
            QStringList paymentTextList = paymentText.split('\n');
            QCOMPARE(paymentTextList.at(0), QString("Payment information"));
            QVERIFY(paymentTextList.at(1).indexOf(QString("URI: veil:")) != -1);
            QVERIFY(paymentTextList.at(2).indexOf(QString("Address:")) != -1);
            QCOMPARE(paymentTextList.at(3), QString("Amount: 0.00000001 ") + QString::fromStdString(CURRENCY_UNIT));
            QCOMPARE(paymentTextList.at(4), QString("Label: TEST_LABEL_1"));
            QCOMPARE(paymentTextList.at(5), QString("Message: TEST_MESSAGE_1"));
        }
    }

    // Clear button
    QPushButton* clearButton = receiveCoinsDialog.findChild<QPushButton*>("clearButton");
    clearButton->click();
    QCOMPARE(labelInput->text(), QString(""));
    QCOMPARE(amountInput->value(), CAmount(0));
    QCOMPARE(messageInput->text(), QString(""));

    // Check addition to history
    int currentRowCount = requestTableModel->rowCount({});
    QCOMPARE(currentRowCount, initialRowCount+1);

    // Check Remove button
    QTableView* table = receiveCoinsDialog.findChild<QTableView*>("recentRequestsView");
    table->selectRow(currentRowCount-1);
    QPushButton* removeRequestButton = receiveCoinsDialog.findChild<QPushButton*>("removeRequestButton");
    removeRequestButton->click();
    QCOMPARE(requestTableModel->rowCount({}), currentRowCount-1);
}

} // namespace

void WalletTests::walletTests()
{
    TestGUI();
}
