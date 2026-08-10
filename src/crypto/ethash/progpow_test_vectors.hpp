// ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
// Copyright 2018-2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.

/// @file
/// ProgPoW test vectors.
///
/// Veil: regenerated 2026-07 for Veil's ProgPoW configuration (0.9.4,
/// ETHASH_EPOCH_LENGTH 5525). The previous values were upstream ethash
/// vectors for Ethereum's 30000-block epochs and never matched Veil's
/// tuned algorithm. Generated with the in-tree library, which is
/// byte-identical to libethash-veil in the Veil ProgPOW miner that
/// produces mainnet-accepted blocks; mainnet sync-from-genesis is the
/// independent cross-check.

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
                "4b201dfe81d337a037b2533266a2e62d14f6f6f30242aa4d3c5186259ba1f667",
                "10ebe85e03e26ffc49f3520fea29c39a3644cd8edb77c317be783d487a873ef5"},
        {49, "63155f732f2bf556967f906155b510c917e48e99685ead76ea83f4eca03ab12b", "0000000006ff2c47",
                "8f9d64ba0da9e401ef7d2eb98db40cf6f1b7d72da9ede4ed5b1c7450e0b81aed",
                "9039bdf616dae117943ea09104b97a829bd47c4f8f56f50196f0014e44e7c2b6"},
        {50, "9e7248f20914913a73d80a70174c331b1d34f260535ac3631d770e656b5dd922", "00000000076e482e",
                "8d213b74d2601283f20bd680f91fe51fc36203b8822308c9c14ab803a052eea6",
                "504bc9ec4b8e93e650510991658956743b14cffd40ee21b7c754760fa46caf7d"},
        {99, "de37e1824c86d35d154cf65a88de6d9286aec4f7f10c3fc9f0fa1bcc2687188d", "000000003917afab",
                "150277ca54e1c719dc7232d538c056112057e92ce927486db5e2f1f725c58f8d",
                "867270d863599135b655ab77c37fd831a16b729998ebeac2c4faddc993e71669"},
        {29950, "ac7b55e801511b77e11d52e9599206101550144525b5679f2dab19386f23dcce", "005d409dbc23a62a",
                "cb285563542c8860ed2ac6e84ba321642e3d8b5cd2ea0f342e197961b375776c",
                "856d9b4ffb8ea4bace59321a5332b07400ef8195944d3ccff5ddde04c51c8457"},
        {29999, "e43d7e0bdc8a4a3f6e291a5ed790b9fa1a0948a2b9e33c844888690847de19f5", "005db5fa4c2a3d03",
                "e0c29a35ab5153af593103982a35cf801a143b42c7520969851462bcffb9a9c1",
                "a50cdc339b6031818e1302adb361fcfa2b37e4576b827878ac824e80e88d143b"},
        {30000, "d34519f72c97cae8892c277776259db3320820cb5279a299d0ef1e155e5c6454", "005db8607994ff30",
                "1ad82c933d66c9f5ed37d4abcd8aa97286e41cb0f2f04ae6d86435e05b6beb77",
                "d14bab8ea4a44dffa13aac957afbf06ec98769b7191d78f47058484ea20b0d2b"},
        {30049, "8b6ce5da0b06d18db7bd8492d9e5717f8b53e7e098d9fef7886d58a6e913ef64", "005e2e215a8ca2e7",
                "ec82fd7410ffe98bc448d06aa8e3e8819d65b578142c97e9259c7c4f3e5102d1",
                "ab5b45a33480e4c0c309acad46d070933cf24e2f80209d6f0e61b02f81926fbd"},
        {30050, "c2c46173481b9ced61123d2e293b42ede5a1b323210eb2a684df0874ffe09047", "005e30899481055e",
                "abbff83d14ff92daa77d108f7a6f656f857197b10987fa5021277e0116369754",
                "2e466c8f1c0e1d11d5f00db06b8eda71d7800740359b88d51eec920faf7e4094"},
        {30099, "ea42197eb2ba79c63cb5e655b8b1f612c5f08aae1a49ff236795a3516d87bc71", "005ea6aef136f88b",
                "0e4b3b7d780b35965e250ce7aadfd7834f6931bceca3f573304b7488d528854d",
                "d40197e499a4d6079bb34766de276c54fe68a0c3483541d283fbeea83b3f1dc4"},
        {59950, "49e15ba4bf501ce8fe8876101c808e24c69a859be15de554bf85dbc095491bd6", "02ebe0503bd7b1da",
                "91fa338d105e63f4c0a9fe66244a2b4c333288ec2fb4ffba4fbc8504ad79df1a",
                "1f53cf269278d129bebdc19739f7a332929b1501c5dbe78b56248072243049e6"},
        {59999, "f5c50ba5c0d6210ddb16250ec3efda178de857b2b1703d8d5403bd0f848e19cf", "02edb6275bd221e3",
                "e5a82db2eabf22c18ecdfe1ddbe2095dab43045dd4452aa3a4e73c05fe8446a8",
                "883d50d9a5535d008baf949d0ef953b129f1196df28f6d35f3a539f4af2a66a8"},
        {170915, "5b3e8dfa1aafd3924a51f33e2d672d8dae32fa528d8b1d378d6e4db0ec5d665d", "0000000044975727",
                "61a29dc43e35f41f66ea8a8990ce45708258bd95fa87da0e7634cef312d29a2e",
                "fdabae4cce76c2e4904d604139d3f7d76526d7dc9475e22beedfbf99ef1ebdd0"},
        {170915, "5b3e8dfa1aafd3924a51f33e2d672d8dae32fa528d8b1d378d6e4db0ec5d665d", "00000000502F578A",
                "d687deb72cd20d5bea985c4a938396b1a4d80ae8dd7a8db8757a24a936e79046",
                "00a45c17f3633fbe13c320df4bdc577e81288c20c214a11b09786188b9d5f840"},

};
}  // namespace
