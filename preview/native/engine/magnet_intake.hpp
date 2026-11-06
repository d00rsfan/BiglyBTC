#pragma once

#include "network_policy.hpp"
#include "biglybtc/library.hpp"
#include "../adapters/profile.hpp"

#include <libtorrent/info_hash.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace biglybtc::storage {struct IntakeHistoryOutcome;}

namespace biglybtc::engine {

// Context PublicAndI2p is a capability ceiling, with missing net still Public.
// Explicit pure I2p context alone defaults missing net to I2P.
enum class MagnetNetwork { Public, I2p, PublicAndI2p };

struct PendingMagnet {
  MagnetNetwork network=MagnetNetwork::Public;
  // Exact explicit original choices, or the caller's explicit import context.
  std::vector<std::string> networks{"Public"};
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
    const std::filesystem::path& destination, bool stopped, const NetworkPolicy&, MagnetNetwork context=MagnetNetwork::Public);

// Serialized held-profile worker. Uses original global config map
// MagnetPlugin.active.magnets, with exact Java hash/args/sources/timeout/ss fields.
// Native destination/version live in additive other_metadata entries. No normal
// download/state/payload is created while metadata is missing.
std::expected<PendingMagnet, storage::Error> queuePendingMagnet(
    storage::ProfileLease&, const PendingMagnet&, const NetworkPolicy&, MagnetNetwork context=MagnetNetwork::Public);
// Worker must exclude retained metadata handles before changing destination.
// Exact sourceEntry CAS; preserves original intent and opaque row metadata.
// Same destination is a no-op; a different existing destination is a conflict.
std::expected<PendingMagnet, storage::Error> attachPendingMagnetDestination(
    storage::ProfileLease&, const PendingMagnet&, const std::filesystem::path&, const NetworkPolicy&, MagnetNetwork context=MagnetNetwork::Public);
std::expected<std::vector<PendingMagnet>, storage::Error> readPendingMagnets(
    const storage::ProfileLease&, const NetworkPolicy&, MagnetNetwork context=MagnetNetwork::Public);
// Read-only exact original row CAS, returns reparsed saved values rather than
// trusting caller copies of URI/full hashes/network/destination/timeout.
std::expected<PendingMagnet,storage::Error> validatePendingMagnet(
    const storage::ProfileLease&,const PendingMagnet&,const NetworkPolicy&);
std::expected<void, storage::Error> cancelPendingMagnet(storage::ProfileLease&, const PendingMagnet&);

// Parameters only, no IO/engine effects. The owner must use its metadata-only
// storage boundary and one alert pump; upload_mode/stop_when_ready alone are not
// a general storage sandbox. Retire that actual handle before completion below.
std::expected<libtorrent::add_torrent_params, storage::Error> metadataOnlyParameters(
    const PendingMagnet&, const NetworkPolicy&, const std::filesystem::path& ownedMetadataDirectory);

// Trusted existing owner worker only: consume the sealed actual import outcome
// BEFORE pending-row cleanup changes its source epoch. The hook may perform
// synchronous Store filesystem work only, never SDK/Qt waits or owner reentry.
// A callback cannot issue a receipt; optional errors are auxiliary, not rollback.
using MagnetCompletionHook=std::function<std::optional<storage::Error>(storage::IntakeHistoryOutcome)>;

// Input is ACTUAL received complete metainfo. Verify every requested full hash,
// merge admitted magnet sources, then journaled ordinary import. Idempotently
// retires the pending row after a matching already-published import on restart.
// Legacy Java intents without native destination remain readable but require an
// explicit destination choice. New force-start is committed in the same import
// transaction; an already imported row keeps any later user's state/force edits.
std::expected<core::TorrentHash, storage::Error> completePendingMagnet(storage::ProfileLease&,
    const PendingMagnet&, const std::filesystem::path& actualMetadata, const NetworkPolicy&,
    bool* newlyImported=nullptr, const MagnetCompletionHook& onFreshImport={},
    std::optional<storage::Error>* historyWarning=nullptr); // Outputs are observations, never receipt authority.

} // namespace biglybtc::engine
