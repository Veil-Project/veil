# Snapshot support

Code behind the "use a verified snapshot instead of a full sync" offer in the
GUI. `snapshot_extract.*` unpacks a downloaded snapshot; the download, manifest
handling and checksum verification live in `src/qt/snapshotdownloader.*`.

## Vendored zstd

`zstddeclib.c`, `zstd.h` and `zstd_errors.h` are Zstandard **1.5.7**, taken
unmodified from upstream.

They are the official single file amalgamation, not a hand assembled copy. The
upstream tree generates `zstddeclib.c` with the script in
`build/single_file_libs`, and the header of the file records the exact command:

```
python combine.py -r ../../lib -x legacy/zstd_legacy.h -o zstddeclib.c zstddeclib-in.c
```

**Decompressor only.** The amalgamation deliberately leaves out the compressor
and the legacy format decoders, so the surface reachable from a downloaded
snapshot is only what is needed to read one.

Licensed under BSD-3-Clause or GPLv2 at your option, per the header in each
file. Both are compatible with this project's MIT licence.

### Updating

Take the generated files from the matching upstream release rather than editing
them here. If they are ever patched locally, say so in this file, because
nothing else distinguishes a modified copy from the upstream one.

### Why vendored

The wallet has to unpack a snapshot on every platform we ship, including the
Windows and macOS cross builds, and this avoids adding a zstd dependency to
`depends/` for all of them. The cost is a large file in the tree, which is why
its provenance is written down here instead of leaving a reviewer to work out
where 22,000 lines of C came from.
