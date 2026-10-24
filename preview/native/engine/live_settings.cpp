#include "live_settings.hpp"

#include <new>
#include <utility>

namespace biglybtc::engine {
namespace {
using Settings = libtorrent::settings_pack;
storage::Error invalid(std::string message) { return {storage::ErrorCode::InvalidFormat, std::move(message)}; }
bool validQueues(const std::optional<LiveQueueLimits>& queues) {
  return !queues || (queues->maximumDownloads >= 0 && queues->maximumDownloads <= LiveQueueLimits::Maximum
      && queues->maximumActive >= 0 && queues->maximumActive <= LiveQueueLimits::Maximum);
}
bool sameProxy(const NetworkProxy& a, const NetworkProxy& b) {
  return a.kind == b.kind && a.host == b.host && a.port == b.port && a.username == b.username
      && a.password == b.password && a.peers == b.peers && a.trackers == b.trackers && a.remoteDns == b.remoteDns;
}
storage::Error policyError(const NetworkPolicyError& error) {
  return {error.code == NetworkPolicyErrorCode::Memory ? storage::ErrorCode::Io
      : error.code == NetworkPolicyErrorCode::Unsupported ? storage::ErrorCode::Unsupported : storage::ErrorCode::InvalidFormat,
      "Invalid live settings: " + error.field};
}
}
std::expected<LiveSettingsPlan, storage::Error> prepareLiveSettings(const NetworkPolicy& active,
    const NetworkPolicy& requested, std::optional<LiveQueueLimits> queues) {
  try {
    if (!validQueues(queues)) { return std::unexpected(invalid("Native queue caps must be between 0 and 1,000,000")); }
    if (auto valid = makeNetworkSettings(active); !valid) { return std::unexpected(policyError(valid.error())); }
    if (auto valid = makeNetworkSettings(requested); !valid) { return std::unexpected(policyError(valid.error())); }
    LiveSettingsPlan result{active, queues, 0};
    const auto difference = [&](bool changed, LiveRestartField field) {
      if (changed) { result.restartFields |= static_cast<std::uint32_t>(field); }
    };
    difference(active.mode != requested.mode, LiveRestartField::Mode);
    difference(active.incomingTcp != requested.incomingTcp || active.outgoingTcp != requested.outgoingTcp
        || active.incomingUtp != requested.incomingUtp || active.outgoingUtp != requested.outgoingUtp
        || active.ipv6 != requested.ipv6, LiveRestartField::Transports);
    difference(active.listenPort != requested.listenPort || active.bindAddresses != requested.bindAddresses, LiveRestartField::Binding);
    difference(!sameProxy(active.proxy, requested.proxy), LiveRestartField::Proxy);
    difference(active.dht != requested.dht || active.lsd != requested.lsd || active.upnp != requested.upnp
        || active.natpmp != requested.natpmp, LiveRestartField::Discovery);
    difference(active.requireEncryption != requested.requireEncryption, LiveRestartField::Encryption);
    difference(active.httpTrackers != requested.httpTrackers || active.udpTrackers != requested.udpTrackers
        || active.pex != requested.pex || active.webSeeds != requested.webSeeds, LiveRestartField::TorrentSources);
    result.effectivePolicy.uploadBytesPerSecond = requested.uploadBytesPerSecond;
    result.effectivePolicy.downloadBytesPerSecond = requested.downloadBytesPerSecond;
    result.effectivePolicy.seedingUploadBytesPerSecond = requested.seedingUploadBytesPerSecond;
    result.effectivePolicy.seedingOnlyUpload = requested.seedingOnlyUpload;
    return result;
  } catch (const std::bad_alloc&) {
    return std::unexpected(storage::Error{storage::ErrorCode::Io, "Cannot prepare live settings"});
  }
}
std::expected<Settings, storage::Error> makeLiveSettingsPack(const NetworkPolicy& effective,
    std::optional<LiveQueueLimits> queues, bool seedingOnly) {
  try {
    if (!validQueues(queues)) { return std::unexpected(invalid("Native queue caps must be between 0 and 1,000,000")); }
    if (auto valid = makeNetworkSettings(effective); !valid) { return std::unexpected(policyError(valid.error())); }
    Settings result;
    result.set_int(Settings::upload_rate_limit, seedingOnly && effective.seedingOnlyUpload
        ? effective.seedingUploadBytesPerSecond : effective.uploadBytesPerSecond);
    result.set_int(Settings::download_rate_limit, effective.downloadBytesPerSecond);
    if (queues) {
      result.set_int(Settings::active_downloads, queues->maximumDownloads == 0 ? -1 : queues->maximumDownloads);
      result.set_int(Settings::active_limit, queues->maximumActive == 0 ? -1 : queues->maximumActive);
      result.set_int(Settings::active_seeds, -1); // Combined cap, not LT's unrelated default seed cap8.
      result.set_bool(Settings::dont_count_slow_torrents, false); // Native upper caps include idle auto-managed torrents.
    }
    return result;
  } catch (const std::bad_alloc&) {
    return std::unexpected(storage::Error{storage::ErrorCode::Io, "Cannot prepare live settings pack"});
  }
}
} // namespace biglybtc::engine
