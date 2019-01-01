// Copyright (c) 2019 The VEIL developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_GENERATESEED_H
#define VEIL_GENERATESEED_H

#include <uint256.h>

namespace veil {
uint512 GenerateNewMnemonicSeed(std::string& mnemonic, const std::string& strLanguage);
}

#endif //VEIL_GENERATESEED_H
