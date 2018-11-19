// Copyright (c) 2018 The VEIL developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockvalidation.h"

#include "primitives/block.h"
#include "primitives/transaction.h"
#include "util.h"
#include "zchain.h"

namespace veil {

bool ValidateBlockSignature(const CBlock& block)
{
    if (block.IsProofOfWork())
        return true;

    if (block.vtx.size() < 2 || block.vtx[1]->vin.empty() || !block.vtx[1]->vin[0].scriptSig.IsZerocoinSpend())
        return error("%s: Block transaction structure is not compatible with Veil's Proof of Stake validation", __func__);

    //Get the zerocoin that was staked
    auto spend = TxInToZerocoinSpend(block.vtx[1]->vin[0]);
    if (!spend)
        return error("%s: failed to get spend from txin", __func__);
    auto pubkey = spend->getPubKey();

    if (!pubkey.IsValid())
        return error("%s: Public Key from zerocoin stake is not valid", __func__);

    return pubkey.Verify(block.GetHash(), block.vchBlockSig);
}

}