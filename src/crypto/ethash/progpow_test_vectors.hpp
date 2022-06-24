// ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
// Copyright 2018-2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.

/// @file
/// ProgPoW test vectors.

#pragma once

namespace  // In anonymous namespace to allow including in multiple compilation units.
{
/// Defines a test case for ProgPoW hash() function.
struct progpow_hash_test_case
{
    int block_number;
    const char* header_hash_hex;
    const char* nonce_hex;
    const char* mix_hash_hex;
    const char* final_hash_hex;
};

progpow_hash_test_case progpow_hash_test_cases[] = {
        {0, "0000000000000000000000000000000000000000000000000000000000000000", "0000000000000000",
                "b1388e12e9898029a487f5534225c2ea8bd79c6ef6754db0405809f53d83c497",
                "3d2f6484ee555362e9d8e2ca54fb99741e755e849f9900ef84ad65aa3c935bd1"},
        {49, "63155f732f2bf556967f906155b510c917e48e99685ead76ea83f4eca03ab12b", "0000000006ff2c47",
                "a1574df5d1b9ddee0b357c2147422fed087308acd1a64e453f26a1c8c6a4c38c",
                "856b47c0683695810667b71056bb42696b6d67039f17dc49c72b0175fc1b96c4"},
        {50, "9e7248f20914913a73d80a70174c331b1d34f260535ac3631d770e656b5dd922", "00000000076e482e",
                "ae3a0f800b0228630e30d21a8e8fba67836997642af9de8179261a4a614fc182",
                "a56e213ff5e85bd2a9d13463b9b4965dfe9b133c8d857e78adb04121631b8b4d"},
        {99, "de37e1824c86d35d154cf65a88de6d9286aec4f7f10c3fc9f0fa1bcc2687188d", "000000003917afab",
                "e17cc5853e71e5dbbda14fc649f538a98f613767d05e5c4981b1e3c1d923de5c",
                "f1722578712b4e6068d1fc8f02a9b87c792bed0d478e6d4a1f12fb739dc5a731"},
        {29950, "ac7b55e801511b77e11d52e9599206101550144525b5679f2dab19386f23dcce", "005d409dbc23a62a",
                "a2ba820ea38d27f1735d8353e87334cb4991c0258c131f196399b0bf27653104",
                "cf275d05091cf4c90eebae01f918dc70aa7c31e2694a4b8ca4ff8008b9d39fbf"},
        {29999, "e43d7e0bdc8a4a3f6e291a5ed790b9fa1a0948a2b9e33c844888690847de19f5", "005db5fa4c2a3d03",
                "d9f1293eeb42f983938faa97fa996e307623d9e671ed112007bf0ef1edbc2ef3",
                "aeb8b2ad997bfb21253a6ce880fab07dadacb16c29d4da4f2cd3e8a93413932f"},
        {30000, "d34519f72c97cae8892c277776259db3320820cb5279a299d0ef1e155e5c6454", "005db8607994ff30",
                "b3e994c8a744613dfbffaa895e1050da5681fcc91fc702ada184967a34a2d664",
                "4ef01fc366313e0831fbccf7eb74d98f5e985fe0d9d355b58de5388732d5dfaf"},
        {30049, "8b6ce5da0b06d18db7bd8492d9e5717f8b53e7e098d9fef7886d58a6e913ef64", "005e2e215a8ca2e7",
                "513a44a075d2c5d0c1116f16396674a4a1f8c91024b33ba9d744a0999d223dbc",
                "1f2b1346cfb00500780f25903802e156cfbbc8576bb5832c1ddaf6ac7065ac3c"},
        {30050, "c2c46173481b9ced61123d2e293b42ede5a1b323210eb2a684df0874ffe09047", "005e30899481055e",
                "062acc9f4801ebbd8a288b5135560e42758c803d1c135d1b36e201c9d395304a",
                "146967512a1578e5dc0036c01a6076c6fa3f77396faba66f22e566be38cd041f"},
        {30099, "ea42197eb2ba79c63cb5e655b8b1f612c5f08aae1a49ff236795a3516d87bc71", "005ea6aef136f88b",
                "1afe6b2523d9cc9dc9a06cc35ff482fc6b4bdd0f3b0eab485b83cd8d13a97eef",
                "614a8042ad80f533a6c54980455c620e949dd360b01195f562092f3a48f33ace"},
        {59950, "49e15ba4bf501ce8fe8876101c808e24c69a859be15de554bf85dbc095491bd6", "02ebe0503bd7b1da",
                "aa820a696c147ea70b44488dcf0dbfc2f324fb93400ec0770e7a4f38bf2ab8e9",
                "f84a5009dadc3beb4a779a19683723a41f2fab604e0fa619c4809d3125aaf6b0"},
        {59999, "f5c50ba5c0d6210ddb16250ec3efda178de857b2b1703d8d5403bd0f848e19cf", "02edb6275bd221e3",
                "5992c94193adb30c185b341012958fd3b7884cfb0359bbdc48de5e8f88224edc",
                "92d3cbf19f06c4e5601fb6a319b867c1b6f9bf0fa1d6b03c4f96dfaab1197aa6"},
        {170915, "5b3e8dfa1aafd3924a51f33e2d672d8dae32fa528d8b1d378d6e4db0ec5d665d", "0000000044975727",
                "1025e17ce3c64142f049091aabf176a790c117748ded337e696092705b0640d6",
                "6e1c18e5d685928f0d575934846193e4922c04d49967bbd0a4cb62a2f88b811f"},
        {170915, "5b3e8dfa1aafd3924a51f33e2d672d8dae32fa528d8b1d378d6e4db0ec5d665d", "00000000502F578A",
                "841212781723bd474c4d72c5c5390a37663a9cb225028ab31e8120a5328dea0f",
                "1213dffe795baa555087798cbda49af07d04dcd524cf9a906da23ea6047656c0"},

};
}  // namespace
