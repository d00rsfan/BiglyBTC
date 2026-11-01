// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "magnet_intake.hpp"
#include "mixed_i2p_source.hpp"
namespace biglybtc::services::i2p::provider {class OwnedProfile;}
namespace biglybtc::engine {
namespace detail {struct PendingMetadataLifetime final {std::atomic_bool retained{true},authorized{false},attached{false},retiring{false};std::atomic<std::uint64_t> generation{0},route{0};};}
// A real reserved metadata-only entry. No payload storage, completion or peer
// permission is represented here. Only its owning session can mint the seal.
class PendingMetadataSource final {
public:
  const storage::ProfileLease& lease()const noexcept{return *lease_;}
  const PendingMagnet& intent()const noexcept{return intent_;}
  const core::TorrentHash& hash()const noexcept{return intent_.hash;}
  const libtorrent::info_hash_t& requested()const noexcept{return intent_.requested;}
  std::uint64_t admission()const noexcept{return admission_;}
  bool current()const noexcept{return lease_&&lease_->valid()&&lifetime_&&lifetime_->retained.load(std::memory_order_acquire);}
  // Retirement suppresses new network effects immediately. Retained remains
  // true until the real handle/weak-reference fence (or never-installed end).
  bool effectsCurrent()const noexcept{return current()&&!lifetime_->retiring.load(std::memory_order_acquire);}
private:
  friend class PersistentSession;
  friend class MixedI2pTransport;
  friend class services::i2p::provider::OwnedProfile;
  PendingMetadataSource(std::shared_ptr<const storage::ProfileLease> lease,PendingMagnet intent,
      std::uint64_t admission,std::shared_ptr<detail::PendingMetadataLifetime> lifetime,
      std::shared_ptr<const detail::MixedManagedOrigin> origin)
      :lease_(std::move(lease)),intent_(std::move(intent)),admission_(admission),lifetime_(std::move(lifetime)),origin_(std::move(origin)){}
  std::shared_ptr<const storage::ProfileLease> lease_;
  PendingMagnet intent_;
  std::uint64_t admission_=0;
  std::shared_ptr<detail::PendingMetadataLifetime> lifetime_;
  std::shared_ptr<const detail::MixedManagedOrigin> origin_;
};
}
