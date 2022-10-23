// Copyright (c) 2019-2022 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockvalidation.h"

#include "chainparams.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "util/system.h"
#include "validation.h"
#include "veil/ringct/rctindex.h"
#include "veil/zerocoin/zchain.h"

namespace veil {

bool ValidateBlockSignature(const CBlock& block)
{
    if (block.IsProofOfWork())
        return true;

    if (block.vtx.size() < 2 || block.vtx[1]->vin.empty())
        return error("%s: Block transaction structure is not compatible with Veil's Proof of Stake validation", __func__);

    //Get the coin that was staked
    if (block.vtx[1]->vin[0].IsZerocoinSpend()) {
        auto spend = TxInToZerocoinSpend(block.vtx[1]->vin[0]);
        if (!spend)
            return error("%s: failed to get spend from txin", __func__);
        CPubKey pubkey = spend->getPubKey();

        if (!pubkey.IsValid())
            return error("%s: Public Key from zerocoin stake is not valid", __func__);

        return pubkey.Verify(block.GetHash(), block.vchBlockSig);
    } else if (block.vtx[1]->vin[0].IsAnonInput()) {
        if (block.nHeight < Params().HeightRingCTStaking()) {
            return error("%s: RingCT staking not accepted before height %d",
                         __func__, Params().HeightRingCTStaking());
        }
        CTxIn txin = block.vtx[1]->vin[0];
        const std::vector<uint8_t> &vKeyImages = txin.scriptData.stack[0];
        const std::vector<uint8_t> vMI = txin.scriptWitness.stack[0];

        uint32_t nInputs, nRingSize;
        txin.GetAnonInfo(nInputs, nRingSize);
        size_t nCols = nRingSize;
        size_t nRows = nInputs + 1;

        if (vKeyImages.size() != nInputs * 33)
            return error("%s: bad keyimage size", __func__);

        const CCmpPubKey &ki = *((CCmpPubKey *) &vKeyImages[0]);

        size_t ofs = 0, nB = 0;
        for (size_t i = 0; i < nCols; ++i) {
            int64_t nIndex = 0;

            if (0 != GetVarInt(vMI, ofs, (uint64_t &) nIndex, nB))
                return false;
            ofs += nB;

            CAnonOutput ao;
            if (!pblocktree->ReadRCTOutput(nIndex, ao))
                return false;

            CPubKey pubkey;
            pubkey.Set(ao.pubkey.begin(), ao.pubkey.end());
            if (!pubkey.IsValid())
                return error("%s: public key from ringct stake is not valid");

            if (pubkey.Verify(block.GetHash(), block.vchBlockSig))
                return true;
        }
        return error("%s: No pubkeys verified for RingCT stake", __func__);
    }

    return error("%s: Stake transaction is not zerocoin or ringct spend", __func__);
}

}
