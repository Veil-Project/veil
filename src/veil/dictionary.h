#ifndef VEIL_DICTIONARY_H
#define VEIL_DICTIONARY_H

#include <array>
#include <vector>

/**
 * A valid mnemonic dictionary has exactly this many words.
 */
static constexpr size_t dictionary_size = 2048;

/**
 * Dictionary definitions for creating mnemonics.
 * The bip39 spec calls this a "wordlist".
 * This is a POD type, which means the compiler can write it directly
 * to static memory with no run-time overhead.
 */
typedef std::array<const char*, dictionary_size> dictionary;

/**
 * A collection of candidate dictionaries for mnemonic metadata.
 */
typedef std::vector<const dictionary*> dictionary_list;

namespace language {

// Individual built-in languages:
extern const dictionary en;
extern const dictionary es;
extern const dictionary ja;
extern const dictionary it;
extern const dictionary fr;
extern const dictionary cs;
extern const dictionary ru;
extern const dictionary uk;
extern const dictionary zh_Hans;
extern const dictionary zh_Hant;

// All built-in languages:
extern const dictionary_list all;

} // namespace language

#endif //VEIL_DICTIONARY_H
