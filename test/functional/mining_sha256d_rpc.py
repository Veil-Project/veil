#!/usr/bin/env python3
# Copyright (c) 2026 The Veil developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test sha256d external mining via rpc

- getblocktemplate with algo sha256d returns sharpcheader work
- sharpcsb submits a solved nonce and the block is accepted and relayed
"""

from test_framework.messages import hash256, uint256_from_str
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, connect_nodes_bi

SHA256D_VERSION_BIT = 1 << 24


class Sha256dMiningRPCTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Restart node0 with -miningaddress so getblocktemplate hands out sha256d work")
        addr = node.getnewbasecoinaddress()
        self.restart_node(0, extra_args=["-miningaddress=" + addr])
        connect_nodes_bi(self.nodes, 0, 1)

        # Leave initial block download so getblocktemplate is willing to serve work
        node.generate(1)
        self.sync_all()

        self.log.info("getblocktemplate algo=sha256d returns an 80 byte header and a 64 bit noncerange")
        tmpl = node.getblocktemplate({"algo": "sha256d", "rules": ["segwit"]})
        assert "sharpcheader" in tmpl
        header_hex = tmpl["sharpcheader"]
        assert_equal(len(header_hex), 160)
        assert_equal(tmpl["noncerange"], "0000000000000000ffffffffffffffff")

        header = bytes.fromhex(header_hex)
        # The template is handed out with the 64 bit nonce zeroed
        assert_equal(header[72:80], bytes(8))

        self.log.info("Grind the 64 bit nonce over the header blob")
        target = int(tmpl["target"], 16)
        good_nonce = None
        good_hash = None
        bad_nonce = None
        nonce = 0
        while good_nonce is None or bad_nonce is None:
            powhash = hash256(header[:72] + nonce.to_bytes(8, "little"))
            if uint256_from_str(powhash) < target:
                if good_nonce is None:
                    good_nonce = nonce
                    good_hash = powhash
            elif bad_nonce is None:
                bad_nonce = nonce
            nonce += 1

        self.log.info("sharpcsb rejects a nonce that does not solve the boundary")
        assert_raises_rpc_error(-32600, "Block does not solve the boundary",
                                node.sharpcsb, header_hex, "00" * 32, "%016x" % bad_nonce)

        self.log.info("sharpcsb rejects an unknown header")
        assert_raises_rpc_error(-32602, "Block header not found in block data",
                                node.sharpcsb, "11" * 80, "00" * 32, "%016x" % good_nonce)

        self.log.info("sharpcsb accepts the solved block")
        height = node.getblockcount()
        # sha_hash is signature parity with pprpcsb/rxrpcsb; the node ignores
        # it, so a bogus value must still be accepted
        result = node.sharpcsb(header_hex, "ff" * 32, "%016x" % good_nonce)
        assert_equal(result, True)
        assert_equal(node.getblockcount(), height + 1)

        self.log.info("The sha256d block relays and carries the sha256d version bit")
        self.sync_all()
        tip = node.getbestblockhash()
        assert_equal(self.nodes[1].getbestblockhash(), tip)
        header_json = node.getblockheader(tip)
        assert header_json["version"] & SHA256D_VERSION_BIT

        self.log.info("A resubmit of the same block reports duplicate")
        assert_equal(node.sharpcsb(header_hex, good_hash[::-1].hex(), "%016x" % good_nonce), "duplicate")


if __name__ == '__main__':
    Sha256dMiningRPCTest().main()
