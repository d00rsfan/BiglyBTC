#pragma once

#include "network_policy.hpp"
#include "profile.hpp"
#include "biglybtc/effective_rate_limits.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace biglybtc::engine {

// Original0 means unlimited. These are native auto-managed upper caps only:
// force-start/manual pause keep their existing bypass/hold behavior. Original
// minimum downloads, seed ranks/first priority and stalled rules are not mapped.
struct LiveQueueLimits {
  static constexpr int Maximum = 1000000;
  int maximumDownloads = 4, maximumActive = 4;
  bool operator==(const LiveQueueLimits&) const = default;
};
enum class LiveRestartField : std::uint32_t {
  Mode = 1, Transports = 2, Binding = 4, Proxy = 8,
  Discovery = 16, Encryption = 32, TorrentSources = 64, LanClassification = 128
};
struct LiveSettingsPlan {
  // Active routes plus ONLY the desired safe rate fields. Worker-private value;
  // never publish this policy (it may contain current proxy credentials).
  NetworkPolicy effectivePolicy;
  std::optional<LiveQueueLimits> queues;
  std::uint32_t restartFields = 0;
};
// Pure validation/allocation. Call BEFORE committing original configuration.
// Both policies must be valid. Requested routing is reported, never copied into
// effectivePolicy. No session, filesystem, settings change or provider startup.
std::expected<LiveSettingsPlan, storage::Error> prepareLiveSettings(
    const NetworkPolicy& active, const NetworkPolicy& requested,
    std::optional<LiveQueueLimits> queues = {});

// Pure, narrow LT pack: two rate keys and (when supplied) native queue caps.
// No bind/listen/proxy/discovery/encryption/source key can enter this pack.
std::expected<libtorrent::settings_pack, storage::Error> makeLiveSettingsPack(
    const NetworkPolicy& effective, std::optional<LiveQueueLimits> queues = {}, bool seedingOnly = false);

struct LiveSettingsResult {
  std::uint32_t restartFields = 0;
  // false means accepted for next owner startup without creating a session.
  bool appliedToLiveSession = false;
};
struct LiveSettingsSnapshot {
  core::SessionRateLimits rateLimits;
  int activeDownloads = 0, activeSeeds = 0, activeLimit = 0;
  bool countSlowTorrents = false;
  core::EffectiveRateLimit lanUpload, lanDownload;
  bool separateLanClass = false;
  std::string listenInterfaces, outgoingInterfaces;
  int proxyType = 0;
  bool dht = false, localDiscovery = false, upnp = false, natpmp = false;
  bool operator==(const LiveSettingsSnapshot&) const = default;
};

} // namespace biglybtc::engine
