#include <veil/mnemonicwalletinit.h>

#include <init.h>
#include <ui_interface.h>
#include <util.h>
#include <wallet/wallet.h>
#include <wallet/walletutil.h>
#include <veil/hdwallet.h>

const WalletInitInterface& g_wallet_init_interface = MnemonicWalletInit();

bool MnemonicWalletInit::Open() const
{
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
        LogPrintf("Wallet disabled!\n");
        return true;
    }

    for (const std::string& walletFile : gArgs.GetArgs("-wallet")) {
        fs::path walletPath = fs::absolute(walletFile, GetWalletDir());
        if ((walletFile == "" && !fs::exists(walletPath / "wallet.dat")) || !fs::exists(walletPath)) {
            unsigned int initOption = MnemonicWalletInitFlags::INVALID_MNEMONIC;
            //Have to check startup args because daemon is not interactive
            if (gArgs.GetBoolArg("-generateseed", false))
                initOption = MnemonicWalletInitFlags::NEW_MNEMONIC;
            std::string strSeedPhraseArg = gArgs.GetArg("-importseed", "");
            if (!strSeedPhraseArg.empty())
                initOption = MnemonicWalletInitFlags::IMPORT_MNEMONIC;

            if (initOption == MnemonicWalletInitFlags::INVALID_MNEMONIC && !InitNewWalletPrompt(initOption))
                return false;
            if (initOption == MnemonicWalletInitFlags::NEW_MNEMONIC) {
                std::string mnemonic;
                if (!CWallet::CreateNewHDWallet(walletFile, walletPath, mnemonic))
                    return false;
                if (!DisplayWalletMnemonic(mnemonic))
                    return false;
            } else if (initOption == MnemonicWalletInitFlags::IMPORT_MNEMONIC) {
                std::string importMnemonic = strSeedPhraseArg;
                bool ret, fBadSeed;
                if (importMnemonic.empty() && !GetWalletMnemonic(importMnemonic))
                    return false;
                do {
                    ret = CWallet::CreateHDWalletFromMnemonic(walletFile, walletPath, importMnemonic, fBadSeed);
                    if (!ret || (fBadSeed && !RetryWalletMnemonic(importMnemonic)))
                        return false;
                } while (fBadSeed);

                LogPrintf("Successfully imported wallet seed\n");
            }
        }

        std::shared_ptr<CWallet> pwallet = CWallet::CreateWalletFromFile(walletFile, walletPath);
        if (!pwallet) {
            return false;
        }

        if (!((CHDWallet*)pwallet.get())->Initialise())
            return false;

        AddWallet(pwallet);
    }

    return true;
}
