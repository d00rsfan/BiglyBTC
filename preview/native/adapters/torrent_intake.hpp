#pragma once

#include "biglybtc/library.hpp"
#include "profile.hpp"

#include <expected>
#include <filesystem>
#include <optional>

namespace biglybtc::storage {

enum class TorrentIntakeStage { JournalSynced, StatePublished, ArchivePublished, InventoryPublished };

// Linux, serialized profile-owner worker only. The caller borrows one held
// lease; no extra lease/engine/payload allocation is performed. destination is
// the parent save directory; metadata's original name becomes save_file.
// Fixed owned paths: active/<UPPERCASE_HASH>.dat, torrents/<HASH>.torrent.
// Full validation precedes the first journal write. An I/O failure can leave a
// durable pending transaction, never a partial downloads.config row. Do not
// retry blindly: run explicit recovery before further inventory mutations.
// forceStart commits forceStart=1 + Waiting atomically (takes precedence over
// stopped); the optional trailing argument preserves existing source call sites.
std::expected<core::TorrentHash, Error> importTorrent(ProfileLease&,
    const std::filesystem::path& metadata, const std::filesystem::path& destination,
    bool stopped, void (*observer)(TorrentIntakeStage) = nullptr, bool forceStart = false);

// Startup recovery before loading library/admitting operations. No pending
// transaction => nullopt. Rebuilds the exact append from journal input and
// publishes inventory last. Conflicting user edits/artifacts are preserved and
// reported. No generic primary/backup recovery promotion or payload effects.
std::expected<std::optional<core::TorrentHash>, Error> recoverTorrentIntake(
    ProfileLease&, void (*observer)(TorrentIntakeStage) = nullptr);

} // namespace biglybtc::storage
