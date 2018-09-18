#ifndef MNEMONIC_H
#define MNEMONIC_H

#include <cstddef>
#include <string>
#include <vector>
#include <array>
#include "veil/arrayslice.h"
#include "veil/dictionary.h"

/**
* A valid mnemonic word count is evenly divisible by this number.
*/
static size_t mnemonic_word_multiple = 3;

/**
* A valid seed byte count is evenly divisible by this number.
*/
static size_t mnemonic_seed_multiple = 4;

template <size_t Size>
using byte_array = std::array<uint8_t, Size>;

/**
* Helpful type definitions
*/
typedef std::vector<std::string> word_list;
typedef array_slice<uint8_t> data_slice;
typedef std::vector<uint8_t> data_chunk;
typedef byte_array<64> long_hash;
typedef byte_array<32> hash_digest;
typedef std::initializer_list<data_slice> loaf;

/**
* Create a new mnenomic (list of words) from provided entropy and a dictionary
* selection. The mnemonic can later be converted to a seed for use in wallet
* creation. Entropy byte count must be evenly divisible by 4.
*/
word_list create_mnemonic(data_slice entropy, const dictionary &lexicon=language::en);

/**
* Checks a mnemonic against a dictionary to determine if the
* words are spelled correctly and the checksum matches.
* The words must have been created using mnemonic encoding.
*/
bool validate_mnemonic(const word_list& mnemonic, const dictionary &lexicon);

/**
* Checks that a mnemonic is valid in at least one of the provided languages.
*/
bool validate_mnemonic(const word_list& mnemonic, const dictionary_list& lexicons=language::all);

/**
* Convert a mnemonic with no passphrase to a wallet-generation seed.
*/
long_hash decode_mnemonic(const word_list& mnemonic);

/**
* Convert a mnemonic and passphrase to a wallet-generation seed.
* Any passphrase can be used and will change the resulting seed.
*/
long_hash decode_mnemonic(const word_list& mnemonic,
    const std::string& passphrase);

#endif
