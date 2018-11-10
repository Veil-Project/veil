// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <noui.h>

#include <ui_interface.h>
#include <util.h>

#include <cstdio>
#include <stdint.h>
#include <string>

static bool noui_ThreadSafeMessageBox(const std::string& message, const std::string& caption, unsigned int style)
{
    bool fSecure = style & CClientUIInterface::SECURE;
    style &= ~CClientUIInterface::SECURE;

    std::string strCaption;
    // Check for usage of predefined caption
    switch (style) {
    case CClientUIInterface::MSG_ERROR:
        strCaption += _("Error");
        break;
    case CClientUIInterface::MSG_WARNING:
        strCaption += _("Warning");
        break;
    case CClientUIInterface::MSG_INFORMATION:
        strCaption += _("Information");
        break;
    default:
        strCaption += caption; // Use supplied caption (can be empty)
    }

    if (!fSecure)
        LogPrintf("%s: %s\n", strCaption, message);
    fprintf(stderr, "%s: %s\n", strCaption.c_str(), message.c_str());
    return false;
}

static bool noui_ThreadSafeQuestion(const std::string& /* ignored interactive message */, const std::string& message, const std::string& caption, unsigned int style)
{
    return noui_ThreadSafeMessageBox(message, caption, style);
}

static void noui_InitMessage(const std::string& message)
{
    LogPrintf("init message: %s\n", message);
}

static bool noui_DisplayWalletMnemonic(std::string& mnemonic)
{
    noui_ThreadSafeMessageBox(mnemonic, "WARNING BACKUP THESE WORDS SECURELY ALL OF YOUR VEIL CRYPTOCURRENCY IS TIED TO THESE SEED WORDS!! Seed", -1);
    return true;
}

static bool noui_InitNewWallet(std::string& mnemonic, unsigned int& flag)
{
    switch (flag) {
        case MnemonicWalletInitFlags::PROMPT_MNEMONIC:
        {
            return noui_ThreadSafeQuestion("", "To import existing seed phrase restart wallet with -importseed= . To generate a new seed start wallet with -generateseed=1 ", "New Wallet Load Detected: ", -1);
        }
        case MnemonicWalletInitFlags::NEW_MNEMONIC:
        {
            noui_DisplayWalletMnemonic(mnemonic);
            return true;
        }
        case MnemonicWalletInitFlags::IMPORT_MNEMONIC:
        {
            //Should never reach this spot
            return noui_ThreadSafeMessageBox("Error with seed import.", "", CClientUIInterface::MSG_ERROR);
        }
        case MnemonicWalletInitFlags::INVALID_MNEMONIC:
        {
            return noui_ThreadSafeMessageBox("Failed to import mnemonic seed, format is invalid.", "", CClientUIInterface::MSG_ERROR);
        }
        default:
            return false;
    }
    return false;
}

void noui_connect()
{
    // Connect bitcoind signal handlers
    uiInterface.ThreadSafeMessageBox.connect(noui_ThreadSafeMessageBox);
    uiInterface.ThreadSafeQuestion.connect(noui_ThreadSafeQuestion);
    uiInterface.InitMessage.connect(noui_InitMessage);
    uiInterface.InitWallet.connect(noui_InitNewWallet);
}
