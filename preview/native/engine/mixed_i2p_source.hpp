// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "original_torrent_identity.hpp"
#include "managed_payload_claim.hpp"
#include "network_policy.hpp"
#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace biglybtc::engine {
class PersistentSession;
class MixedI2pTransport;
namespace detail {struct MixedManagedOrigin final {};struct MixedManagedLifetime {std::atomic_bool retained{true},attached{false};};}
// Actual pending/live managed storage seal. It never creates another handle,
// writer, I2P identity or transport grant. Only the engine can construct it.
class MixedManagedSource final {
public:
  const storage::ProfileLease& lease()const noexcept{return *lease_;}
  const storage::OriginalTorrentIdentity& identity()const noexcept{return identity_;}
  std::uint64_t admission()const noexcept{return admission_;}
  std::string_view stateSnapshot()const noexcept{return state_;}
  std::span<const std::string> trackers()const noexcept{return trackers_;}
  std::span<const detail::ManagedPayloadClaim> claims()const noexcept{return claims_;}
  bool current()const noexcept{return lease_&&lease_->valid()&&alive_&&alive_->retained.load(std::memory_order_acquire);}
private:
  friend class PersistentSession;
  friend class MixedI2pTransport;
  bool backendCurrent()const noexcept{return current()&&alive_->attached.load(std::memory_order_acquire);}
  MixedManagedSource(std::shared_ptr<const storage::ProfileLease> lease,
      storage::OriginalTorrentIdentity identity,std::uint64_t admission,std::string state,
      std::vector<std::string> trackers,std::vector<detail::ManagedPayloadClaim> claims,
      std::shared_ptr<const detail::MixedManagedLifetime> alive,std::shared_ptr<const detail::MixedManagedOrigin> origin)
      :lease_(std::move(lease)),identity_(std::move(identity)),admission_(admission),
       state_(std::move(state)),trackers_(std::move(trackers)),claims_(std::move(claims)),alive_(std::move(alive)),origin_(std::move(origin)){}
  std::shared_ptr<const storage::ProfileLease> lease_;
  storage::OriginalTorrentIdentity identity_;
  std::uint64_t admission_;
  std::string state_;
  std::vector<std::string> trackers_;
  std::vector<detail::ManagedPayloadClaim> claims_;
  std::shared_ptr<const detail::MixedManagedLifetime> alive_;
  std::shared_ptr<const detail::MixedManagedOrigin> origin_;
};
// Public-side storage policy for an explicitly saved two-network selection.
// This does not grant I2P traffic; the source-bound mixed SDK route is separate.
class MixedPublicPolicy final {
public:
  bool matches(const storage::ProfileLease& lease,const core::TorrentHash& hash)const noexcept{
    return lease_&&lease_->sharesOwnership(lease)&&hash==hash_;
  }
  std::expected<void,NetworkPolicyError> apply(const NetworkPolicy&,const TorrentNetworkSelection&,
      libtorrent::add_torrent_params&)const;
private:
  friend class PersistentSession;
  MixedPublicPolicy(std::shared_ptr<const storage::ProfileLease> lease,core::TorrentHash hash)
      :lease_(std::move(lease)),hash_(std::move(hash)){}
  std::shared_ptr<const storage::ProfileLease> lease_;
  core::TorrentHash hash_;
};
}
