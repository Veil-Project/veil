Veil Core 1.5.0 Release Notes
=============================

This release modernizes Veil's build system, RNG, P2P DoS resistance, toolchain,
and Tor support on top of the existing Bitcoin Core 0.17 base. There are **no
changes to consensus rules, wallet key storage, RingCT/Zerocoin validation,
Dandelion, or stealth addresses**. Legacy peers, existing wallets, and
`peers.dat` remain fully compatible.

Two defensive correctness fixes touch consensus-adjacent code and are called
out under "Correctness fixes" below: both are argued bit-identical (DGW
arithmetic) or no-behavior-change (key-image binding) on mainnet and testnet,
with the reasoning documented in their commit messages.

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
- **OpenSSL is fully removed.** The BIP70 payment protocol - the GUI's last
  OpenSSL and protobuf consumer, dead on the network and deleted upstream -
  is removed; only plain BIP21 `veil:` URIs are handled. No Veil binary links
  libssl/libcrypto or protobuf any more (enforced by CI on every build), and
  both leave the depends tree (Qt now builds `-no-openssl`).

Privacy: Tor v3 / BIP155 (addrv2)
---------------------------------

- **Tor v3 support.** Veil now speaks the BIP155 `addrv2` address format and
  stores/relays 32-byte Tor v3 addresses, creates `ED25519-V3` onion services
  via the Tor controller, and handles v3 `.onion` strings using SHA3-256.
- **Backward compatible.** Nodes negotiate `sendaddrv2` during the handshake;
  peers that predate it keep receiving legacy `addr`. The legacy 16-byte wire
  and `peers.dat` encodings are byte-identical to previous releases.
- All onion addresses (v2 and v3) share a small fixed set of address groups,
  matching upstream Bitcoin Core: onion keys are free to generate, so giving
  each address its own group would let one attacker occupy arbitrarily many
  address-manager buckets.

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
- Every build now **asserts that no binary links OpenSSL** — `veild`,
  `veil-cli`, `veil-tx`, and (with BIP70 gone) `veil-qt` too — via `ldd`+`nm`
  on Linux and `otool -L` on native arm64.
- The upstream **`validation_block_tests` suite is revived** for Veil consensus
  rules (coinbase height/reward schedule, post-PoW-update header formats,
  in-memory zerocoin DB) and runs in CI, alongside newly added `pow_tests` and
  `netbase_tests`.

Correctness fixes
-----------------

- **DGW difficulty average computed at 512-bit width.** The DarkGravityWave
  running average of past targets could wrap 2^256 once a few near-pow-limit
  targets accumulate. Only regtest targets reach that range (difficulty jumped
  ~3x erratically there); mainnet and testnet targets sit far below it, and
  the new arithmetic is bit-identical wherever the old one did not wrap, so
  sync is unaffected. Locked by new `pow_tests` cases.
- **Key-image stack binding moved inside the anon-input branch** in
  `CheckTxInputs`. Non-anon inputs carry an empty stack, so the old
  unconditional binding was undefined behavior (caught by UBSan once
  `validation_block_tests` connected real blocks); the reference was only
  ever used for anon inputs. No behavior change.
- **Regtest chainparams initialize all version-bits deployments.** The
  `POS_WEIGHT` and `ZC_LIMP` entries were never set on regtest and read
  indeterminate values whenever the deployment set was iterated; both are now
  always-active with their canonical bits, matching testnet/devnet.

Notes
-----

- I2P and CJDNS `addrv2` networks are recognized but not routed; unknown
  network ids are safely ignored.
- Upgrading nodes that use Tor: an `onion_private_key` file written by an
  earlier release holds a v2 (RSA1024) key that modern Tor daemons reject.
  Delete the file from the data directory and restart; a new `ED25519-V3`
  v3 onion service is created automatically.
- **Tor v3 peers now persist across restarts.** `peers.dat` moves to format 2
  (addresses stored in the BIP155 encoding). Old files load unchanged;
  downgrading to a previous release resets `peers.dat` (the old code cannot
  read the new format and recreates it, losing no other state).
- The RNG event hasher now receives entropy from P2P message and RPC request
  arrival timing (the upstream 0.20 call sites for `RandAddEvent`).

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
- A **full mainnet resync from genesis** with `-assumevalid=0` (full script
  verification) completed on Apple Silicon and remained synced at tip for
  several days.

Recommended before a tagged mainnet release
-------------------------------------------

- A longer multi-peer Tor v3 soak.
- Wider community testing on additional platforms and network conditions.
