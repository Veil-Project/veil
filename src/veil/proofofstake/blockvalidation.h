// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_BLOCKVALIDATION_H
#define VEIL_BLOCKVALIDATION_H

class CBlock;

namespace veil {

bool ValidateBlockSignature(const CBlock& block);

}

#endif //VEIL_BLOCKVALIDATION_H
