#pragma once

#include "biglybtc/download_action.hpp"
#include "biglybtc/library.hpp"
#include "profile.hpp"

#include <optional>
#include <span>
#include <vector>

namespace biglybtc::storage {

// Qualified owner classification, never inferred from saved positions/counters.
// Moves require exactly one classification for every saved library download.
struct LibraryCompletionPool {
  core::TorrentHash hash;
  bool complete;
};
struct LibraryPositionChange {
  core::TorrentHash hash;
  std::int64_t before;
  std::int64_t after;
};
struct LibraryCommandResult {
  core::DownloadAction action;
  core::SavedDownload before;
  core::SavedDownload after;
  std::optional<core::SavedPause> pauseBefore;
  std::optional<core::SavedPause> pauseAfter;
  std::vector<LibraryPositionChange> positions;
  bool changed = false;
  RecoverySource source = RecoverySource::Primary;
};

// Synchronous saved intent only: no torrent/payload/engine IO. Uses the held
// lease and fixed downloads.config resilient-load/CAS-save policy. A no-op
// returns changed=false without replacing any file. A publication/sync error is
// returned as-is; it must not be described as proof that no replacement occurred.
// ForceStart sets true (not a toggle). No completion pool => moves Unsupported.
std::expected<LibraryCommandResult, Error> applyLibraryCommand(const ProfileLease&,
    const core::TorrentHash&, core::DownloadAction,
    std::span<const LibraryCompletionPool> completionPools = {});

// Automatic completion transition: clear only original force fields, retaining
// saved startup, pause ownership, position and every unknown value.
std::expected<LibraryCommandResult, Error> clearLibraryForceStart(const ProfileLease&, const core::TorrentHash&);

} // namespace biglybtc::storage
