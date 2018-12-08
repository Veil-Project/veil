// Copyright (c) 2018 The VEIL developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <veil/mnemonic/generateseed.h>
#include <key.h>
#include <veil/mnemonic/mnemonic.h>

namespace veil {

uint512 GenerateNewMnemonicSeed(std::string& mnemonic, const std::string& strLanguage)
{
    CKey key;
    key.MakeNewKey(true);

    std::vector<unsigned char> keyData;
    const unsigned char* ptrKeyData = key.begin();
    for (int i = 0; ptrKeyData != key.end(); i++) {
        unsigned char byte = *ptrKeyData;
        keyData.push_back(byte);
        ptrKeyData++;
    }

    word_list mnemonic_words = create_mnemonic(keyData, string_to_lexicon(strLanguage));
    for (auto it = mnemonic_words.begin(); it != mnemonic_words.end();) {
        const auto word = *it;
        mnemonic += word;
        ++it;
        if (it == mnemonic_words.end())
            break;
        mnemonic += " ";
    }

    auto blob_512 = decode_mnemonic(mnemonic_words);
    uint512 seed512;
    memcpy(seed512.begin(), blob_512.begin(), blob_512.size());

    return seed512;
}

}