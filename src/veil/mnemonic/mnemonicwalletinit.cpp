// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <veil/mnemonic/mnemonicwalletinit.h>

#include <init.h>
#include <ui_interface.h>
#include <util.h>
#include <wallet/wallet.h>
#include <wallet/walletutil.h>
#include <veil/ringct/anonwallet.h>
#include "mnemonic.h"
#include "generateseed.h"

const WalletInitInterface& g_wallet_init_interface = MnemonicWalletInit();

bool MnemonicWalletInit::Open() const
{
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
        LogPrintf("Wallet disabled!\n");
        return true;
    }

    for (const std::string& walletFile : gArgs.GetArgs("-wallet")) {
        bool fNewSeed = false;
        uint512 hashMasterKey;
        fs::path walletPath = fs::absolute(walletFile, GetWalletDir());
        if ((walletFile == "" && !fs::exists(walletPath / "wallet.dat")) || !fs::exists(walletPath)) {
            fNewSeed = true;
            unsigned int initOption = MnemonicWalletInitFlags::INVALID_MNEMONIC;
            /** check startup args because daemon is not interactive **/
            if (gArgs.GetBoolArg("-generateseed", false))
                initOption = MnemonicWalletInitFlags::NEW_MNEMONIC;

            std::string strSeedPhraseArg = gArgs.GetArg("-importseed", "");
            if (!strSeedPhraseArg.empty())
                initOption = MnemonicWalletInitFlags::IMPORT_MNEMONIC;

            /**If no startup args, then launch prompt **/
            std::string strMessage = "english";
            if (initOption == MnemonicWalletInitFlags::INVALID_MNEMONIC) {
                // Language only routes to GUI. It returns with the filled out mnemonic in strMessage
                if (!GetWalletMnemonicLanguage(strMessage, initOption))
                    return false;
                // The mnemonic phrase now needs to be converted to the final wallet seed (note: different than the phrase seed)
                strSeedPhraseArg = strMessage;
                //LogPrintf("%s: mnemonic phrase: %s\n", __func__, strSeedPhraseArg);
                initOption = MnemonicWalletInitFlags::IMPORT_MNEMONIC;
            }

            /** Create a new mnemonic - this should only be triggered via daemon **/
            if (initOption == MnemonicWalletInitFlags::NEW_MNEMONIC) {
                std::string mnemonic;
                veil::GenerateNewMnemonicSeed(mnemonic, strMessage);
                if (!DisplayWalletMnemonic(mnemonic))
                    return false;
                strSeedPhraseArg = mnemonic;
                initOption = MnemonicWalletInitFlags::IMPORT_MNEMONIC;
            }

            /** Convert the mnemonic phrase to the final seed used for the wallet **/
            if (initOption == MnemonicWalletInitFlags::IMPORT_MNEMONIC) {
                // Convert the BIP39 mnemonic phrase into the final 512bit wallet seed
                auto hashRet = decode_mnemonic(strSeedPhraseArg);
                memcpy(hashMasterKey.begin(), hashRet.begin(), hashRet.size());
                //LogPrintf("%s: Staging for loading seed %s\n", __func__, hashMasterKey.GetHex());
            }
        }

        std::shared_ptr<CWallet> pwallet = CWallet::CreateWalletFromFile(walletFile, walletPath, 0, fNewSeed ? &hashMasterKey : nullptr);
        if (!pwallet) {
            return false;
        }

        if (gArgs.GetBoolArg("-exchangesandservicesmode", false))
            pwallet->SetStakingEnabled(false);

        AddWallet(pwallet);
    }

    return true;
}
