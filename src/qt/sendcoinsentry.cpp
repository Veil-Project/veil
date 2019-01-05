// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/sendcoinsentry.h>
#include <qt/forms/ui_sendcoinsentry.h>

#include <qt/addressbookpage.h>
#include <qt/addresstablemodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>

#include <qt/bitcoinunits.h>
#include <qt/guiconstants.h>

#include <QApplication>
#include <QClipboard>
#include <QDoubleValidator>

SendCoinsEntry::SendCoinsEntry(const PlatformStyle *_platformStyle, QWidget *parent) :
    QStackedWidget(parent),
    ui(new Ui::SendCoinsEntry),
    model(0),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    setStyleSheet(GUIUtil::loadStyleSheet());

    ui->payTo->setPlaceholderText("Enter address");
    ui->payTo->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->payTo->setProperty("cssClass" , "edit-primary");

    ui->payAmount->setPlaceholderText("Amount to send");
    ui->payAmount->setValidator(new QDoubleValidator(0, 100000000000, 7, this) );
    ui->payAmount->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->payAmount->setProperty("cssClass" , "edit-primary");

    ui->addAsLabel->setPlaceholderText("Description (optional)");
    ui->addAsLabel->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->addAsLabel->setProperty("cssClass" , "edit-primary");

    //ui->useAvailableBalance->setProperty("cssClass" , "btn-check");
    //ui->useAvailableBalance->setAttribute(Qt::WA_MacShowFocusRect, 0);

    //setCurrentWidget(ui->SendCoins);
    setCurrentWidget(ui->SendCoins);

    if (platformStyle->getUseExtraSpacing())
        ui->payToLayout->setSpacing(4);
    ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));

    // normal veil address field
    GUIUtil::setupAddressWidget(ui->payTo, this);
    // just a label for displaying veil address(es)
    ui->payTo_is->setFont(GUIUtil::fixedPitchFont());

    // Connect signals
    //connect(ui->payAmount, SIGNAL(valueChanged()), this, SIGNAL(payAmountChanged()));
    //connect(ui->checkboxSubtractFeeFromAmount, SIGNAL(toggled(bool)), this, SIGNAL(subtractFeeFromAmountChanged()));
    connect(ui->btnRemove, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->deleteButton_is, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->deleteButton_s, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    //connect(ui->useAvailableBalanceButton, SIGNAL(clicked()), this, SLOT(useAvailableBalanceClicked()));
}

SendCoinsEntry::~SendCoinsEntry()
{
    delete ui;
}

//void SendCoinsEntry::on_addressBookButton_clicked()
//{
//    if(!model)
//        return;
//    AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
//    dlg.setModel(model->getAddressTableModel());
//    if(dlg.exec())
//    {
//        ui->payTo->setText(dlg.getReturnValue());
//        ui->payAmount->setFocus();
//    }
//}

void SendCoinsEntry::on_payTo_textChanged(const QString &address)
{
    updateLabel(address);
}

void SendCoinsEntry::setModel(WalletModel *_model)
{
    this->model = _model;

    if (_model && _model->getOptionsModel())
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    clear();
}

void SendCoinsEntry::clear()
{
    // clear UI elements for normal payment
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->payAmount->clear();
    //ui->checkboxSubtractFeeFromAmount->setCheckState(Qt::Unchecked);
    //ui->messageTextLabel->clear();
    //ui->messageTextLabel->hide();
    //ui->messageLabel->hide();
    // clear UI elements for unauthenticated payment request
    ui->payTo_is->clear();
    ui->memoTextLabel_is->clear();
    ui->payAmount_is->clear();
    // clear UI elements for authenticated payment request
    ui->payTo_s->clear();
    ui->memoTextLabel_s->clear();
    ui->payAmount_s->clear();

    // update the display unit, to not use the default ("VEIL")
    updateDisplayUnit();
}

void SendCoinsEntry::checkSubtractFeeFromAmount()
{
    //ui->checkboxSubtractFeeFromAmount->setChecked(true);
}

void SendCoinsEntry::deleteClicked()
{
    Q_EMIT removeEntry(this);
}

void SendCoinsEntry::useAvailableBalanceClicked()
{
    Q_EMIT useAvailableBalance(this);
}

bool SendCoinsEntry::validate(interfaces::Node& node, std::string& error)
{
    if (!model)
        return false;

    // Check input validity
    bool retval = true;

    // Skip checks for payment request
    if (recipient.paymentRequest.IsInitialized())
        return retval;

    // Only stealth addresses accepted.
    if(!model->isStealthAddress(ui->payTo->text())){
        validAmount(ui->payTo, false);
        error = "Only stealth addresses accepted";
        retval = false;
    }

    if (!validateEdit(ui->payAmount)) {
        error = "Amount out of range";
        retval = false;
    }

    // Amount
    CAmount amount = parseAmount(ui->payAmount->text());

    // Sending a zero amount is invalid
    if (amount <= 0)
    {
        validAmount(ui->payTo, false);
        error = "Amount cannot be zero";
        retval = false;
    }

    // Reject dust outputs:
    if (retval && GUIUtil::isDust(node, ui->payTo->text(), amount)) {
        validAmount(ui->payTo, false);
        error = "Dust output";
        retval = false;
    }

    return retval;
}

bool SendCoinsEntry::validAmount(QLineEdit* edit, bool valid){
    // TODO: Move this to a new css file..
    if (valid)
        edit->setStyleSheet(""
                            "padding:8;"
                            "   margin-left:12px;"
                            "font-size:16px;"
                            "margin-right:16px;"
                            "background:transparent;"
                            "   background-color:transparent;"
                            "border-top:none;"
                            "border-left:none;"
                            "border-right:none;"
                            "   border-bottom:1px solid #bababa;"
                            "}"
                            ""
                            "#payTo:focus {"
                            "    border-bottom:1px solid #105aef;"
                            "");
    else
        edit->setStyleSheet(""
                            "padding:8;"
                            "   margin-left:12px;"
                            "font-size:16px;"
                            "margin-right:16px;"
                            "background:transparent;"
                            "   background-color:transparent;"
                            "border-top:none;"
                            "border-left:none;"
                            "border-right:none;"
                            "   border-bottom:1px solid red;"
                            "}"
                            ""
                            "#payTo:focus {"
                            "    border-bottom:1px solid #105aef;"
                            "");
    return true;
}

bool SendCoinsEntry::validateEdit(QLineEdit* edit) {
    bool valid = false;
    parseAmount(edit->text(), &valid);
    validAmount(edit, valid);
    //value(&valid);
    //setValid(valid);
    return valid;
}

/**
 * Parse a string into a number of base monetary units and
 * return validity.
 * @note Must return 0 if !valid.
 */
CAmount SendCoinsEntry::parseAmount(const QString &text, bool *valid_out) const
{
    CAmount val = 0;
    bool valid = BitcoinUnits::parse(BitcoinUnits::VEIL, text, &val);
    if(valid)
    {
        if(val < 0 || val > BitcoinUnits::maxMoney())
            valid = false;
    }
    if(valid_out)
        *valid_out = valid;
    return valid ? val : 0;
}

SendCoinsRecipient SendCoinsEntry::getValue()
{
    // Payment request
    if (recipient.paymentRequest.IsInitialized())
        return recipient;

    // Normal payment
    recipient.address = ui->payTo->text();
    recipient.label = ui->addAsLabel->text();
    recipient.amount = parseAmount(ui->payAmount->text());
    //recipient.message = ui->messageTextLabel->text();
    //recipient.fSubtractFeeFromAmount = (ui->checkboxSubtractFeeFromAmount->checkState() == Qt::Checked);

    return recipient;
}

QWidget *SendCoinsEntry::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, ui->payTo);
    QWidget::setTabOrder(ui->payTo, ui->addAsLabel);
    //QWidget *w = ui->payAmount->setupTabChain(ui->addAsLabel);
    //QWidget::setTabOrder(w, ui->checkboxSubtractFeeFromAmount);
    //QWidget::setTabOrder(ui->checkboxSubtractFeeFromAmount, ui->addressBookButton);
   // QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
    //QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
    //return ui->deleteButton;
    return 0;
}

void SendCoinsEntry::setValue(const SendCoinsRecipient &value)
{
    recipient = value;

    if (recipient.paymentRequest.IsInitialized()) // payment request
    {
        if (recipient.authenticatedMerchant.isEmpty()) // unauthenticated
        {
            ui->payTo_is->setText(recipient.address);
            ui->memoTextLabel_is->setText(recipient.message);
            ui->payAmount_is->setValue(recipient.amount);
            ui->payAmount_is->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_UnauthenticatedPaymentRequest);
        }
        else // authenticated
        {
            ui->payTo_s->setText(recipient.authenticatedMerchant);
            ui->memoTextLabel_s->setText(recipient.message);
            ui->payAmount_s->setValue(recipient.amount);
            ui->payAmount_s->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_AuthenticatedPaymentRequest);
        }
    }
    else // normal payment
    {
        // message
        //ui->messageTextLabel->setText(recipient.message);
        //ui->messageTextLabel->setVisible(!recipient.message.isEmpty());
        //ui->messageLabel->setVisible(!recipient.message.isEmpty());

        ui->addAsLabel->clear();
        ui->payTo->setText(recipient.address); // this may set a label from addressbook
        if (!recipient.label.isEmpty()) // if a label had been set from the addressbook, don't overwrite with an empty label
            ui->addAsLabel->setText(recipient.label);

        //ui->payAmount->setValue(recipient.amount);
        // TODO: Add value here..
        ui->payAmount->setText(BitcoinUnits::format(BitcoinUnits::VEIL, recipient.amount, false, BitcoinUnits::separatorAlways));
        //ui->payAmount->setText(recipient.amount);
    }
}

void SendCoinsEntry::setAddress(const QString &address)
{
    ui->payTo->setText(address);
    ui->payAmount->setFocus();
}

void SendCoinsEntry::setAmount(const CAmount &amount)
{
    //ui->payAmount->setValue(amount);
    // TODO: add value here..
    ui->payAmount->setText(BitcoinUnits::format(BitcoinUnits::VEIL, amount, false, BitcoinUnits::separatorAlways));
}

bool SendCoinsEntry::isClear()
{
    return ui->payTo->text().isEmpty() && ui->payTo_is->text().isEmpty() && ui->payTo_s->text().isEmpty();
}

void SendCoinsEntry::setFocus()
{
    ui->payTo->setFocus();
}

void SendCoinsEntry::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        // Update payAmount with the current unit
        // TODO: Complete this..
        //ui->payAmount->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        //ui->payAmount_is->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
       // ui->payAmount_s->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    }
}

bool SendCoinsEntry::updateLabel(const QString &address)
{
    if(!model)
        return false;

    // Fill in label from address book, if address has an associated label
    QString associatedLabel = model->getAddressTableModel()->labelForAddress(address);
    if(!associatedLabel.isEmpty())
    {
        ui->addAsLabel->setText(associatedLabel);
        return true;
    }

    return false;
}
