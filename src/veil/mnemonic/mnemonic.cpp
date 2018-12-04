
#include <veil/mnemonic/mnemonic.h>
#include <crypto/external/pkcs5_pbkdf2.h>
#include <crypto/external/hmac_sha256.h>

//! Boost Locale is its own library that requires an additional flag to link it "-lboost_locale"
// TODO: If we really need this we should check for it in the build-aux boost files
//#include <boost/locale.hpp>

#include <boost/algorithm/string.hpp>

// BIP-39 private constants.
static constexpr size_t bits_per_word = 11;
static constexpr size_t entropy_bit_divisor = 32;
static constexpr size_t hmac_iterations = 2048;
static constexpr uint8_t byte_bits = 8;
static const auto passphrase_prefix = "mnemonic";

/////////////////////////
// local helper methods//
/////////////////////////
long_hash pkcs5_pbkdf2_hmac_sha512(data_slice passphrase, data_slice salt, size_t iterations)
{
    long_hash hash;
    const auto result = pkcs5_pbkdf2(passphrase.data(), passphrase.size(), salt.data(), salt.size(), hash.data(),
                                     hash.size(), iterations);

    if (result != 0)
        throw std::bad_alloc();

    return hash;
}

hash_digest sha256_hash(data_slice data)
{
    hash_digest hash;
    SHA256_(data.data(), data.size(), hash.data());
    return hash;
}

template <typename Source>
data_chunk to_chunk(const Source& bytes)
{
    return data_chunk(std::begin(bytes), std::end(bytes));
}

template <typename Element, typename Container>
int find_position(const Container& list, const Element& value)
{
    const auto it = std::find(std::begin(list), std::end(list), value);

    if (it == std::end(list))
        return -1;

    return static_cast<int>(std::distance(list.begin(), it));
}

inline data_chunk build_chunk(loaf slices)
{
    size_t size = 0;
    for (const auto slice: slices)
        size += slice.size();

    data_chunk out;
    out.reserve(size);
    for (const auto slice: slices)
        out.insert(out.end(), slice.begin(), slice.end());

    return out;
}

inline uint8_t bip39_shift(size_t bit)
{
    return (1 << (byte_bits - (bit % byte_bits) - 1));
}
/////////////////////////

bool validate_mnemonic(const word_list& words, const dictionary& lexicon)
{
    const auto word_count = words.size();
    if ((word_count % mnemonic_word_multiple) != 0)
        return false;

    const auto total_bits = bits_per_word * word_count;
    const auto check_bits = total_bits / (entropy_bit_divisor + 1);
    const auto entropy_bits = total_bits - check_bits;

    size_t bit = 0;
    data_chunk data((total_bits + byte_bits - 1) / byte_bits, 0);

    for (const auto& word: words)
    {
        const auto position = find_position(lexicon, word);
        if (position == -1)
            return false;

        for (size_t loop = 0; loop < bits_per_word; loop++, bit++)
        {
            if (position & (1 << (bits_per_word - loop - 1)))
            {
                const auto byte = bit / byte_bits;
                data[byte] |= bip39_shift(bit);
            }
        }
    }

    data.resize(entropy_bits / byte_bits);
    const auto mnemonic = create_mnemonic(data, lexicon);
    return std::equal(mnemonic.begin(), mnemonic.end(), words.begin());
}

data_chunk key_from_mnemonic(const word_list& words, const dictionary& lexicon)
{
    const auto word_count = words.size();
    if ((word_count % mnemonic_word_multiple) != 0)
        return {};

    const auto total_bits = bits_per_word * word_count;
    const auto check_bits = total_bits / (entropy_bit_divisor + 1);
    const auto entropy_bits = total_bits - check_bits;

    size_t bit = 0;
    data_chunk data((total_bits + byte_bits - 1) / byte_bits, 0);

    for (const auto& word: words)
    {
        const auto position = find_position(lexicon, word);
        if (position == -1)
            return {};

        for (size_t loop = 0; loop < bits_per_word; loop++, bit++)
        {
            if (position & (1 << (bits_per_word - loop - 1)))
            {
                const auto byte = bit / byte_bits;
                data[byte] |= bip39_shift(bit);
            }
        }
    }

    data.resize(entropy_bits / byte_bits);
    const auto mnemonic = create_mnemonic(data, lexicon);
    if (!std::equal(mnemonic.begin(), mnemonic.end(), words.begin()))
        return {};
    else
        return data;
}

dictionary string_to_lexicon(const std::string& strLanguage)
{
    if (strLanguage == "english")
        return language::en;
    else if (strLanguage == "spanish")
        return language::es;
    else if (strLanguage == "italian")
        return language::it;
    else if (strLanguage == "japanese")
        return language::ja;
    else if (strLanguage == "french")
        return language::fr;

    return language::en;
}

word_list create_mnemonic(data_slice entropy, const dictionary &lexicon)
{
    if ((entropy.size() % mnemonic_seed_multiple) != 0)
        return {};

    const size_t entropy_bits = (entropy.size() * byte_bits);
    const size_t check_bits = (entropy_bits / entropy_bit_divisor);
    const size_t total_bits = (entropy_bits + check_bits);
    const size_t word_count = (total_bits / bits_per_word);

    const auto data = build_chunk({entropy, sha256_hash(entropy)});

    size_t bit = 0;
    word_list words;

    for (size_t word = 0; word < word_count; word++)
    {
        size_t position = 0;
        for (size_t loop = 0; loop < bits_per_word; loop++)
        {
            bit = (word * bits_per_word + loop);
            position <<= 1;

            const auto byte = bit / byte_bits;

            if ((data[byte] & bip39_shift(bit)) > 0)
                position++;
        }

        words.push_back(lexicon[position]);
    }

    return words;
}

bool validate_mnemonic(const word_list& mnemonic, const dictionary_list& lexicons)
{
    for (const auto& lexicon: lexicons)
        if (validate_mnemonic(mnemonic, *lexicon))
            return true;

    return false;
}

long_hash decode_mnemonic(const word_list& mnemonic)
{
    const auto sentence = boost::join(mnemonic, " ");
    return decode_mnemonic(sentence);
}

long_hash decode_mnemonic(const std::string& mnemonic)
{
    const std::string salt(passphrase_prefix);
    return pkcs5_pbkdf2_hmac_sha512(to_chunk(mnemonic), to_chunk(salt),
                                    hmac_iterations);
}

//long_hash decode_mnemonic(const word_list& mnemonic,
//    const std::string& passphrase)
//{
//    const auto sentence = boost::join(mnemonic, " ");
//    const std::string prefix(passphrase_prefix);
//    const auto salt = boost::locale::normalize(prefix + passphrase, boost::locale::norm_nfkd);
//    return pkcs5_pbkdf2_hmac_sha512(to_chunk(sentence), to_chunk(salt), hmac_iterations);
//}