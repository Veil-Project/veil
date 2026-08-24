// Copyright (c) 2019-2022 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <veil/mnemonic/mnemonicwalletinit.h>

#include <init.h>
#include <ui_interface.h>
#include <util/system.h>
#include <wallet/wallet.h>
#include <wallet/walletutil.h>
#include <veil/ringct/anonwallet.h>
#include <support/cleanse.h>
#include "mnemonic.h"
#include "generateseed.h"

const WalletInitInterface& g_wallet_init_interface = MnemonicWalletInit();

bool MnemonicWalletInit::Open() const
{
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
        LogPrintf("Wallet disabled!\n");
        return true;
    }

    if(gArgs.GetArg("-lightwallet", false)) {
        LogPrintf("Light wallet enabled, wallet creation bypassed!\n");
        return true;
    }

    for (const std::string& walletFile : gArgs.GetArgs("-wallet")) {
        bool fNewSeed = false;
        bool fRestoredSeed = false;
        uint512 hashMasterKey;
        fs::path walletPath = fs::absolute(walletFile, GetWalletDir());
        if ((walletFile == "" && !fs::exists(walletPath / "wallet.dat")) || !fs::exists(walletPath)) {
            fNewSeed = true;
            unsigned int initOption = MnemonicWalletInitFlags::INVALID_MNEMONIC;
            /** check startup args because daemon is not interactive **/
            if (gArgs.GetBoolArg("-generateseed", false))
                initOption = MnemonicWalletInitFlags::NEW_MNEMONIC;

            std::string strSeedPhraseArg = gArgs.GetArg("-importseed", "");
            if (!strSeedPhraseArg.empty()) {
                initOption = MnemonicWalletInitFlags::IMPORT_MNEMONIC;
                fRestoredSeed = true;
            }

            /**If no startup args, then launch prompt **/
            std::string strMessage = "english";
            if (initOption == MnemonicWalletInitFlags::INVALID_MNEMONIC) {
                // Language only routes to GUI. It returns with the filled out mnemonic in strMessage
                if (!GetWalletMnemonicLanguage(strMessage, initOption))
                    return false;
                // The GUI reports IMPORT_MNEMONIC only when the user typed in an
                // existing phrase, meaning the seed may have on-chain history
                fRestoredSeed = (initOption == MnemonicWalletInitFlags::IMPORT_MNEMONIC);
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
                memory_cleanse(&mnemonic[0], mnemonic.size());
                initOption = MnemonicWalletInitFlags::IMPORT_MNEMONIC;
            }

            /** Convert the mnemonic phrase to the final seed used for the wallet **/
            if (initOption == MnemonicWalletInitFlags::IMPORT_MNEMONIC) {
                // Convert the BIP39 mnemonic phrase into the final 512bit wallet seed
                auto hashRet = decode_mnemonic(strSeedPhraseArg);
                memcpy(hashMasterKey.begin(), hashRet.begin(), hashRet.size());
                memory_cleanse(hashRet.data(), hashRet.size());
                //LogPrintf("%s: Staging for loading seed %s\n", __func__, hashMasterKey.GetHex());
            }
            // Wipe the plaintext mnemonic phrase now that the 512-bit seed has been derived from it.
            memory_cleanse(&strSeedPhraseArg[0], strSeedPhraseArg.size());
            memory_cleanse(&strMessage[0], strMessage.size());
        }

        std::shared_ptr<CWallet> pwallet = CWallet::CreateWalletFromFile(walletFile, walletPath, 0, fNewSeed ? &hashMasterKey : nullptr, fNewSeed && fRestoredSeed);        
        // The wallet has taken the seed; wipe our copy so the root seed does not linger in memory.
        memory_cleanse(hashMasterKey.begin(), hashMasterKey.size());
        if (!pwallet) {
            return false;
        }

        if (gArgs.GetBoolArg("-exchangesandservicesmode", false) || !gArgs.GetBoolArg("-staking",true))
            pwallet->SetStakingEnabled(false);

        AddWallet(pwallet);
    }

    return true;
}
