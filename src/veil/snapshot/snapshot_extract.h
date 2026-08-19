// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_SNAPSHOT_SNAPSHOT_EXTRACT_H
#define VEIL_SNAPSHOT_SNAPSHOT_EXTRACT_H

#include <fs.h>

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace snapshot {

//! Streams a tar.zst archive, supplied as an ordered list of part files that
//! logically concatenate into one zstd stream, into destDir.
//!
//! Safety properties, because this parses data downloaded from the network:
//!  - entry paths may not be absolute and may not contain ".." components
//!  - only entries whose top level directory is in allowedDirs are written,
//!    anything else fails the whole extraction
//!  - unknown tar entry kinds that carry data are skipped, except GNU long
//!    name entries which fail extraction (they could remap paths)
//!
//! progress is called with the running total of decompressed bytes written;
//! returning false from it cancels the extraction.
//!
//! Returns false and fills error on any failure. destDir contents from a
//! failed run should be treated as garbage and removed by the caller.
bool ExtractTarZst(const std::vector<fs::path>& parts,
                   const fs::path& destDir,
                   const std::set<std::string>& allowedDirs,
                   std::string& error,
                   const std::function<bool(uint64_t)>& progress = nullptr);

} // namespace snapshot

#endif // VEIL_SNAPSHOT_SNAPSHOT_EXTRACT_H
