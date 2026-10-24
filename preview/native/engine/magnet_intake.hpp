#pragma once

#include "network_policy.hpp"
#include "biglybtc/library.hpp"
#include "../adapters/profile.hpp"

#include <libtorrent/info_hash.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace biglybtc::engine {

struct PendingMagnet {
  core::TorrentHash hash; // Original recovery identity: v1, else truncated v2 (20 bytes).
  libtorrent::info_hash_t requested; // Full identities retained and checked at completion.
  std::string uri, displayName;
  std::optional<std::filesystem::path> destination;
  bool stopped = false, forceStart = false;
  std::int64_t timeoutMillis = 600000;
  std::optional<std::int64_t> addedMillis;
  std::vector<std::string> trackers, webSeeds;
  std::vector<libtorrent::tcp::endpoint> peers;
  std::vector<std::pair<std::string, int>> dhtNodes;
  // Owned raw original row for stale-intent detection. Empty before queue.
  std::string sourceEntry;
};

std::expected<PendingMagnet, storage::Error> parseMagnetIntent(std::string_view uri,
    const std::filesystem::path& destination, bool stopped, const NetworkPolicy&);

// Serialized held-profile worker. Uses original global config map
// MagnetPlugin.active.magnets, with exact Java hash/args/sources/timeout/ss fields.
// Native destination/version live in additive other_metadata entries. No normal
// download/state/payload is created while metadata is missing.
std::expected<PendingMagnet, storage::Error> queuePendingMagnet(
    storage::ProfileLease&, const PendingMagnet&, const NetworkPolicy&);
// Worker must exclude retained metadata handles before changing destination.
// Exact sourceEntry CAS; preserves original intent and opaque row metadata.
// Same destination is a no-op; a different existing destination is a conflict.
std::expected<PendingMagnet, storage::Error> attachPendingMagnetDestination(
    storage::ProfileLease&, const PendingMagnet&, const std::filesystem::path&, const NetworkPolicy&);
std::expected<std::vector<PendingMagnet>, storage::Error> readPendingMagnets(
    const storage::ProfileLease&, const NetworkPolicy&);
std::expected<void, storage::Error> cancelPendingMagnet(storage::ProfileLease&, const PendingMagnet&);

// Parameters only, no IO/engine effects. The owner must use its metadata-only
// storage boundary and one alert pump; upload_mode/stop_when_ready alone are not
// a general storage sandbox. Retire that actual handle before completion below.
std::expected<libtorrent::add_torrent_params, storage::Error> metadataOnlyParameters(
    const PendingMagnet&, const NetworkPolicy&, const std::filesystem::path& ownedMetadataDirectory);

// Input is ACTUAL received complete metainfo. Verify every requested full hash,
// merge admitted magnet sources, then journaled ordinary import. Idempotently
// retires the pending row after a matching already-published import on restart.
// Legacy Java intents without native destination remain readable but require an
// explicit destination choice. New force-start is committed in the same import
// transaction; an already imported row keeps any later user's state/force edits.
std::expected<core::TorrentHash, storage::Error> completePendingMagnet(storage::ProfileLease&,
    const PendingMagnet&, const std::filesystem::path& actualMetadata, const NetworkPolicy&);

} // namespace biglybtc::engine
