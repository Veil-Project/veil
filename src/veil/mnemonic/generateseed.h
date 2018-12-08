#ifndef VEIL_GENERATESEED_H
#define VEIL_GENERATESEED_H

#include <uint256.h>

namespace veil {
uint512 GenerateNewMnemonicSeed(std::string& mnemonic, const std::string& strLanguage);
}

#endif //VEIL_GENERATESEED_H
