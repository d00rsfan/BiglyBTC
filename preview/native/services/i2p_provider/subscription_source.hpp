// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "biglybtc/atomic_shared_ptr.hpp"
#include "biglybtc/library.hpp"
#include "subscription_seed_runtime.hpp"

#include <atomic>
#include <limits>

namespace biglybtc::services::i2p::provider {

class ProfileSource;
class OwnedProfile;
class Bridge;

// File authority for one signed private subscription in the existing profile.
// The I/O permit validates it alongside the original session identity. A
// withdrawn subscription does not invalidate unrelated private torrents.
class SubscriptionSource final {
public:
  const core::TorrentHash& hash() const noexcept { return hash_; }
  bool current(std::uint64_t revision) const noexcept {
    return revision != std::numeric_limits<std::uint64_t>::max()
        && revision_.load(std::memory_order_acquire) == revision && !revoked();
  }
  bool revoked() const noexcept { return revoked_.load(std::memory_order_acquire); }
  void revoke() noexcept { revoked_.store(true, std::memory_order_release); }
private:
  friend class ProfileSource;
  friend class OwnedProfile;
  friend class Bridge;
  SubscriptionSource(std::shared_ptr<const storage::ProfileLease> lease, core::TorrentHash hash,
      engine::SubscriptionSeedSource initial, engine::HeldSubscriptionSeedResolver resolver)
      : lease_(std::move(lease)), hash_(std::move(hash)), initial_(std::move(initial)), resolver_(std::move(resolver)) {}

  std::shared_ptr<const engine::SubscriptionSeedPresence> presence(std::uint64_t revision) const noexcept {
    const auto records=current(revision)?records_.load():nullptr;
    if (!records||!records->presence) { return {}; }
    return {records,&*records->presence};
  }
  std::shared_ptr<const engine::SubscriptionSeedMetainfo> metainfo(std::uint64_t revision) const noexcept {
    const auto records=current(revision)?records_.load():nullptr;
    if (!records||!records->metainfo) { return {}; }
    return {records,&*records->metainfo};
  }

  struct NetworkRecords {
    std::optional<engine::SubscriptionSeedPresence> presence;
    std::optional<engine::SubscriptionSeedMetainfo> metainfo;
    bool operator==(const NetworkRecords&) const=default;
  };

  void refresh(const storage::ProfileSourceAccess& access) noexcept {
    namespace io = storage::detail;
    if (revoked()) { return; }
    try {
      const auto raw = hash_.bytes();
      const std::string_view key(reinterpret_cast<const char*>(raw.data()), raw.size());
      auto current = resolver_(access, key, engine::SubscriptionSeedRead::Revalidate);
      if (current && current->sameVersion(initial_) && initial_.directoryHandle) {
        const auto named = io::openStatePathDirectory(current->directory);
        const auto original = io::stateStamp(initial_.directoryHandle->get());
        if (named && original) {
          const auto observed = io::stateStamp(named->get());
          if (observed && io::sameStateDirectory(*original, *observed) && !revoked()) {
            const auto previous=records_.load();
            NetworkRecords projected;
            if (!current->presence || current->presence->wire.empty() || current->presence->wire.size()>=512
                || current->presence->version<=0) {
              if (previous) { records_.store(nullptr); }
            } else {
              projected.presence=std::move(current->presence);
              if (current->ddbMetainfo&&!current->ddbMetainfo->encoded.empty()&&current->ddbMetainfo->encoded.size()<=4096) {
                projected.metainfo=std::move(current->ddbMetainfo);
              }
              if (!previous||*previous!=projected) {
                records_.store(std::make_shared<const NetworkRecords>(std::move(projected)));
              }
            }
            revision_.store(access.revision(), std::memory_order_release);
            return;
          }
        }
      }
    } catch (...) { /* An uncertain version is withdrawn below. */ }
    revoke();
  }

  std::shared_ptr<const storage::ProfileLease> lease_;
  core::TorrentHash hash_;
  engine::SubscriptionSeedSource initial_;
  engine::HeldSubscriptionSeedResolver resolver_;
  core::AtomicSharedPtr<const NetworkRecords> records_;
  std::atomic<std::uint64_t> revision_{std::numeric_limits<std::uint64_t>::max()};
  std::atomic_bool revoked_{false};
};

} // namespace biglybtc::services::i2p::provider
