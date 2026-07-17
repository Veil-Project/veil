Veil Core 1.5.0 Release Notes
=============================

This release modernizes Veil's build system, RNG, P2P DoS resistance, toolchain,
and Tor support on top of the existing Bitcoin Core 0.17 base. There are **no
changes to consensus rules, wallet key storage, RingCT/Zerocoin validation,
Dandelion, or stealth addresses**. Legacy peers, existing wallets, and
`peers.dat` remain fully compatible.

Security
--------

- **Modern RNG, OpenSSL removed from the daemon.** The OpenSSL 1.0.1k-backed
  0.17 RNG is replaced with Bitcoin Core's self-contained ChaCha20 `RNGState`
  plus environment-entropy gathering (`randomenv`). `veild`/`veil-cli`/`veil-tx`
  link without libssl/libcrypto. Zerocoin deterministic minting was used as a
  regression gate to prove wallet/key randomness is unchanged.
- **P2P DoS hardening:**
  - Per-peer token-bucket rate limiting of incoming address processing
    (whitelisted peers exempt).
  - Compact-block index deserialization rejects oversized/overflowing input.
  - A large `inv` now triggers a single `getheaders` instead of one per
    announced block, removing an amplification vector.
- **UPnP is disabled by default.**

Privacy: Tor v3 / BIP155 (addrv2)
---------------------------------

- **Tor v3 support.** Veil now speaks the BIP155 `addrv2` address format and
  stores/relays 32-byte Tor v3 addresses, creates `ED25519-V3` onion services
  via the Tor controller, and handles v3 `.onion` strings using SHA3-256.
- **Backward compatible.** Nodes negotiate `sendaddrv2` during the handshake;
  peers that predate it keep receiving legacy `addr`. The legacy 16-byte wire
  and `peers.dat` encodings are byte-identical to previous releases.
- Each Tor v3 address forms its own address-group for eclipse-attack resistance.

Toolchain & CI
--------------

- Builds cleanly with C++17, C23-safe crypto prototypes, and Boost >= 1.81
  (version-portable filesystem shim); native macOS arm64 supported.
- CI now publishes **both** macOS binary artifacts: `macosx-x86_64-binaries`
  (cross-compiled, includes veil-qt) and `macosx-arm64-binaries` (native
  Apple Silicon: veild/veil-cli/veil-tx/veil-qt, plus a `Veil-Qt.app` `.dmg`).
- The native macOS GUI is configured with `--with-qrencode` explicitly, so a
  missing libqrencode fails the build instead of silently shipping a GUI
  whose receive screen cannot render QR codes.
- New CI gates: a **Linux ASan + UBSan** unit-test job (UBSan is a hard gate,
  `halt_on_error=1`) and a **native macOS arm64** build+test job (the
  project's first automated sanitizer/unit coverage).

Notes
-----

- **Pruning** works as in prior releases. Because Veil enables `-txindex` by
  default, run `-prune=<MiB> -txindex=0` to prune; `-prune` alone is ignored
  (a warning is logged) because a transaction index cannot be pruned.
- I2P and CJDNS `addrv2` networks are recognized but not routed; unknown
  network ids are safely ignored.

Verification status
-------------------

- Unit suites for the touched subsystems are green; serialization round-trips
  and BIP155 wire vectors are locked by tests; SHA3-256 is validated against a
  NIST known-answer vector; native macOS arm64 binaries verified (`file` reports
  `arm64`).
- Live validation: a node connected to **mainnet**, learned real addresses, and
  relayed them to a second node as a **populated addrv2 message** (decoded
  cleanly); a node created a **live Tor v3 onion service** (56-char `.onion`)
  against a real Tor daemon; a partial mainnet shadow-sync ran without issue.
- A **full testnet IBD from genesis to tip** completed on this build, with
  bidirectional addrv2 exchange and a live Tor v3 onion service on testnet.

Recommended before a tagged mainnet release
-------------------------------------------

- Full mainnet resync-from-genesis on this build (in progress).
- A longer multi-peer Tor v3 soak.
