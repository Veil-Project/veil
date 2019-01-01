// Copyright (c) 2010-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <ui_interface.h>
#include <util.h>

CClientUIInterface uiInterface;

bool InitError(const std::string& str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR);
    return false;
}

void InitWarning(const std::string& str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING);
}

std::string AmountHighWarn(const std::string& optname)
{
    return strprintf(_("%s is set very high!"), optname);
}

std::string AmountErrMsg(const char* const optname, const std::string& strValue)
{
    return strprintf(_("Invalid amount for -%s=<amount>: '%s'"), optname, strValue);
}

bool InitNewWalletPrompt(unsigned int& initOption)
{
    std::string message;
    initOption = MnemonicWalletInitFlags::PROMPT_MNEMONIC;
    return *uiInterface.InitWallet(message, initOption);
}

bool DisplayWalletMnemonic(std::string& message)
{
    unsigned int initOption = MnemonicWalletInitFlags::NEW_MNEMONIC;
    return *uiInterface.InitWallet(message, initOption);
}

bool GetWalletMnemonic(std::string& message)
{
    unsigned int initOption = MnemonicWalletInitFlags::IMPORT_MNEMONIC;
    return *uiInterface.InitWallet(message, initOption);
}

bool GetWalletMnemonicLanguage(std::string& message, unsigned int& initOption)
{
    initOption = MnemonicWalletInitFlags::SELECT_LANGUAGE;
    return *uiInterface.InitWallet(message, initOption);
}

bool RetryWalletMnemonic(std::string& message)
{
    unsigned int initOption = MnemonicWalletInitFlags::INVALID_MNEMONIC;
    return *uiInterface.InitWallet(message, initOption);
}
