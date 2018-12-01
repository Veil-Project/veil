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
        CPubKey pubkeySeed;
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

          //  if (initOption == MnemonicWalletInitFlags::INVALID_MNEMONIC && !InitNewWalletPrompt(initOption))
            std::string strLanguage = "english";
            if (initOption == MnemonicWalletInitFlags::INVALID_MNEMONIC) {
                if (!GetWalletMnemonicLanguage(strLanguage, initOption))
                    return false;
                std::stringstream ss(strLanguage);
                std::istream_iterator<std::string> begin(ss);
                std::istream_iterator<std::string> end;
                std::vector<std::string> mnemonicWordList(begin, end);
                std::vector<unsigned char> keyData = key_from_mnemonic(mnemonicWordList);

                CKey key;
                key.Set(keyData.data(), keyData.data() + keyData.size(), true);
                if (!key.IsValid())
                    return error("%s: Key not valid\n", __func__);
                pubkeySeed = key.GetPubKey();
                LogPrintf("%s: GUI loaded seed %s\n", __func__, HexStr(pubkeySeed));
            } else if (initOption == MnemonicWalletInitFlags::NEW_MNEMONIC) {
                std::string mnemonic;
                if (!CWallet::CreateNewHDWallet(walletFile, walletPath, mnemonic, strLanguage, &pubkeySeed))
                    return false;
                if (!DisplayWalletMnemonic(mnemonic))
                    return false;
            } else if (initOption == MnemonicWalletInitFlags::IMPORT_MNEMONIC) {
                std::string importMnemonic = strSeedPhraseArg;
                bool ret, fBadSeed;
                if (importMnemonic.empty() && !GetWalletMnemonic(importMnemonic))
                    return false;
                do {
                    ret = CWallet::CreateHDWalletFromMnemonic(walletFile, walletPath, importMnemonic, fBadSeed, pubkeySeed);
                    if (!ret || (fBadSeed && !RetryWalletMnemonic(importMnemonic)))
                        return false;
                } while (fBadSeed);

                LogPrintf("Successfully imported wallet seed\n");
            }
        }

        std::shared_ptr<CWallet> pwallet = CWallet::CreateWalletFromFile(walletFile, walletPath, 0, fNewSeed ? &pubkeySeed : nullptr);
        if (!pwallet) {
            return false;
        }

        //Extract masterkey for HD wallet from account 1 m/0/0/1
        CExtKey extKey;
        if (fNewSeed) {
            WalletBatch walletdb(pwallet->GetDBHandle());
            CKeyMetadata metadata;
            pwallet->DeriveNewExtKey(walletdb, metadata, extKey, false, 1);
            LogPrintf("%s: Loading key %s %s for hdwallet\n", __func__, metadata.hdKeypath, HexStr(extKey.key.GetPubKey()));
        }

        CHDWallet *phdwallet = (CHDWallet *) pwallet.get();
        if (!phdwallet->Initialise(fNewSeed ? &extKey : nullptr))
            return false;
        AddWallet(pwallet);
    }

    return true;
}
