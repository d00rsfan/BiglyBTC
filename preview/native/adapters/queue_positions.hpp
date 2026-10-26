#pragma once
#include "profile.hpp"
#include "biglybtc/file_policy.hpp"
#include "original_torrent_identity.hpp"
#include <span>
#include <string>
#include <vector>
namespace biglybtc::storage {
struct LibraryPositionObservation {
  core::TorrentHash hash;
  OriginalTorrentIdentity identity;
  core::SavedDownload admitted;
  std::vector<compat::FilePolicy> policy;
  bool complete=false;
  // Exact post-prepare physical mapping scope, not unrelated state counters.
  std::string mappingScope;
};
struct LibraryGroupTransition {core::TorrentHash hash;bool complete=false;};
struct LibraryNormalizedPosition {core::TorrentHash hash;std::uint32_t position=0;};
struct LibraryPositionNormalization {
  std::vector<LibraryNormalizedPosition> positions;
  bool changed=false;
  RecoverySource source=RecoverySource::Primary;
};
// Captures only mapping-affecting attributes, preserving absent/present values.
// Source may be an authoritative cache projection; it is never edited here.
std::expected<std::string,Error> originalQueueMappingScope(const core::Document&);

// Requires complete current same-owner classification, exact fresh original
// raw/full state identity and whole downloads.config CAS. No state/payload IO
// writes or force/pause/intent changes. Unsupported providers remain refused.
// saveAttempted is reset on entry and set immediately before ConfigFile::save.
// False proves this invocation performed no publication attempt; true makes
// any error publication-uncertain, without classifying generic IO text.
std::expected<LibraryPositionNormalization,Error> normalizeLibraryPositions(
    const ProfileLease&,std::span<const LibraryPositionObservation>,
    std::span<const LibraryGroupTransition> = {},bool newSeedsTop=true,
    bool* saveAttempted=nullptr);
} // namespace biglybtc::storage
