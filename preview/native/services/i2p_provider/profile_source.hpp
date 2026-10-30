// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owned_i2p_policy.hpp"
#include "managed_storage.hpp"
#include "identity.hpp"
#include "effect_authority.hpp"
#include <atomic>
#include <memory>

namespace biglybtc::services::i2p::provider {
// Filesystem methods belong to an external lifetime/I/O worker, never the
// socket thread. Public observations contain no key bytes or ephemeral auth.
class ProfileSource final:public std::enable_shared_from_this<ProfileSource> {
public:
  static std::expected<std::shared_ptr<ProfileSource>,storage::Error> open(
      const storage::ProfileLease&,const core::TorrentHash&,std::string pluginDirectory="azneti2phelper");
  ~ProfileSource();
  ProfileSource(const ProfileSource&)=delete;
  const std::shared_ptr<const storage::ProfileLease>& lease()const noexcept;
  const Destination& destination()const noexcept;
  const engine::OwnedI2pPolicy& policy()const noexcept;
  const core::TorrentHash& hash()const noexcept;
  // Consumed once by actual createLeaseSession. No implicit identity creation.
  std::expected<ClientIdentity,storage::Error> takeIdentity();
  // All original storage preparation is reused, including real payload grants,
  // full raw-info hashes and recheck markers. Capture AFTER its own writes.
  std::expected<engine::detail::ManagedStorage,storage::Error> prepareStorage();
  std::expected<void,storage::Error> validate()const;
  bool revisionCurrent(std::uint64_t revision)const noexcept;
  bool trackerAllowed(const Hash&,std::uint16_t)const noexcept;
  bool prepared()const noexcept;
  void revoke()noexcept;
  std::shared_ptr<i2cp::EffectAuthority> effectAuthority()const noexcept;
private:
  friend class Bridge;
  std::vector<int> watchDescriptors()const;
  struct Impl;
  explicit ProfileSource(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}
