#include <veil/mnemonic/mnemonicwalletinit.h>

#include <init.h>
#include <ui_interface.h>
#include <util.h>
#include <wallet/wallet.h>
#include <wallet/walletutil.h>
#include <veil/ringct/hdwallet.h>
#include "mnemonic.h"

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
            //Have to check startup args because daemon is not interactive
            if (gArgs.GetBoolArg("-generateseed", false))
                initOption = MnemonicWalletInitFlags::NEW_MNEMONIC;
            std::string strSeedPhraseArg = gArgs.GetArg("-importseed", "");
            if (!strSeedPhraseArg.empty())
                initOption = MnemonicWalletInitFlags::IMPORT_MNEMONIC;

            std::string strMessage = "english";
            if (initOption == MnemonicWalletInitFlags::INVALID_MNEMONIC) {
                // Language only routes to GUI. It returns with the filled out mnemonic in strMessage
                if (!GetWalletMnemonicLanguage(strMessage, initOption))
                    return false;

                // The mnemonic phrase now needs to be converted to the final wallet seed (note: different than the phrase seed)
                std::string strMnemonic = strMessage;
                auto hashRet = decode_mnemonic(strMnemonic);
                memcpy(hashMasterKey.begin(), hashRet.begin(), hashRet.size());
                //LogPrintf("%s: GUI loaded seed %s\n", __func__, hashMasterKey.GetHex());
            } else if (initOption == MnemonicWalletInitFlags::NEW_MNEMONIC) {
                std::string mnemonic;
                if (!CWallet::CreateNewHDWallet(walletFile, walletPath, mnemonic, strMessage, &hashMasterKey))
                    return false;
                if (!DisplayWalletMnemonic(mnemonic))
                    return false;
            } else if (initOption == MnemonicWalletInitFlags::IMPORT_MNEMONIC) {
                std::string importMnemonic = strSeedPhraseArg;
                bool ret, fBadSeed;
                if (importMnemonic.empty() && !GetWalletMnemonic(importMnemonic))
                    return false;
                do {
                    ret = CWallet::CreateHDWalletFromMnemonic(walletFile, walletPath, importMnemonic, fBadSeed, hashMasterKey);
                    if (!ret || (fBadSeed && !RetryWalletMnemonic(importMnemonic)))
                        return false;
                } while (fBadSeed);

                LogPrintf("Successfully imported wallet seed\n");
            }
        }

        std::shared_ptr<CWallet> pwallet = CWallet::CreateWalletFromFile(walletFile, walletPath, 0, nullptr);
        if (!pwallet) {
            return false;
        }

        //Extract masterkey for HD wallet from account m/44'/slip44id'/0'/x
        CExtKey extKey;
        if (fNewSeed)
            extKey = pwallet->DeriveBIP32Path({{0, true}, {Params().BIP32_RingCT_Account(), true}});

        CHDWallet *phdwallet = (CHDWallet *) pwallet.get();
        if (!phdwallet->Initialise(fNewSeed ? &extKey : nullptr))
            return false;
        AddWallet(pwallet);
    }

    return true;
}
