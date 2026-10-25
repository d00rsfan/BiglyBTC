#pragma once

#include "profile.hpp"
#include "original_file_io.hpp"
#include "biglybtc/library.hpp"
#include "biglybtc/torrent_layout.hpp"

namespace biglybtc::storage {

// Copies only the interpreted save root. The original inventory remains intact.
// Existing components are inspected without following symbolic links; no mkdir.
std::expected<core::SavedDownload, Error> resolveOriginalSaveRoot(
    const core::SavedDownload&, const compat::OriginalLayout&);
// Recovery uses the captured, authenticated root rather than new existence
// observations after relocation. It must be an original source-defined choice.
std::expected<core::SavedDownload, Error> restoreOriginalSaveRoot(
    const core::SavedDownload&, const compat::OriginalLayout&,
    const std::filesystem::path& approvedRoot);

struct OriginalStorageMarker {
  std::size_t index = 0;
  std::string leaf;
  std::uint64_t device = 0, inode = 0, bytes = 0;
  std::int64_t modifiedSeconds = 0, modifiedNanoseconds = 0;
  std::int64_t changedSeconds = 0, changedNanoseconds = 0;
  bool operator==(const OriginalStorageMarker&) const = default;
};
struct ResolvedOriginalStorageTypes {
  std::vector<OriginalStorageKind> requested, actual;
  std::vector<OriginalStorageMarker> markers;
  bool synthesized = false;
  // Set only when original getStorageTypes would capture the global threshold.
  std::optional<std::int64_t> reorderMinimumMiB;
  bool unsupportedConversion = false;
  bool operator==(const ResolvedOriginalStorageTypes&) const = default;
};

// Explicit types bypass global defaults. Missing/empty types use the held
// biglybt.config and source defaults. Fixed control leaves identify physical
// C/R data independently of the desired policy; they are never treated as L.
std::expected<ResolvedOriginalStorageTypes, Error> resolveOriginalStorageTypes(
    const ProfileLease&, const core::TorrentHash&, const compat::OriginalLayout&,
    const core::Node& attributes);
std::expected<void, Error> applyOriginalStorageDefaults(core::Document& attributes,
    const ResolvedOriginalStorageTypes&, bool physicalKinds = false);

} // namespace biglybtc::storage
