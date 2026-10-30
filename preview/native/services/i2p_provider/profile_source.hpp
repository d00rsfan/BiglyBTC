// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owned_i2p_policy.hpp"
#include "managed_storage.hpp"
#include "identity.hpp"
#include "effect_authority.hpp"
#include <atomic>
#include <memory>
#include <functional>

namespace biglybtc::services::i2p::provider {
class OwnedTorrent;
class OwnedProfile;
// Filesystem methods belong to an external lifetime/I/O worker, never the
// socket thread. Public observations contain no key bytes or ephemeral auth.
class ProfileSource final:public std::enable_shared_from_this<ProfileSource> {
public:
  static std::expected<std::shared_ptr<ProfileSource>,storage::Error> open(
      const storage::ProfileLease&,const core::TorrentHash&,std::string pluginDirectory="azneti2phelper");
  // Explicit shared identity/session anchor. No torrent or payload permission.
  static std::expected<std::shared_ptr<ProfileSource>,storage::Error> createSession(
      const storage::ProfileLease&,std::string pluginDirectory="azneti2phelper");
  // Worker-only child admission; prepareStorage() performs its own publication.
  std::expected<std::shared_ptr<ProfileSource>,storage::Error> prepareChild(
      const core::TorrentHash&,std::span<const engine::detail::ManagedPayloadClaim> claimed={},
      std::span<const storage::OriginalTorrentIdentity> retainedIdentities={});
  // Trusted owner I/O only, never a user/reentrant callback or observed writer.
  // Holds SAME-OFD source gate across validation, actual CAS and post-validation.
  std::expected<void,storage::Error> publish(const core::TorrentHash&,
      const std::function<std::expected<void,storage::Error>()>& trustedWorkerOperation);
  // Original queue adapters only: positions may change, every other source
  // row/pause/state field remains exact. No completion classification is minted.
  std::expected<void,storage::Error> publishPositions(
      const std::function<std::expected<void,storage::Error>()>& trustedWorkerOperation);
  // Caller has removed the actual handle, expired its storage weak reference
  // and completed (or explicitly failed) checkpoint. Revokes only this child.
  std::expected<void,storage::Error> releaseChild(const core::TorrentHash&);
  bool groupMode()const noexcept;
  // Inotify is a dirty hint only; no fresh authority is granted here.
  void invalidate()noexcept;
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
  friend class OwnedTorrent;
  friend class OwnedProfile;
  // Read-only final CAS preflight after the friend's actual flush/remove/join.
  // Accepts intentionally revoked effects ONLY when every held named source
  // and exact prepared snapshot still matches; never restores effect authority.
  std::expected<void,storage::Error> validateRetiredSnapshot()const;
  friend class Bridge;
  std::vector<int> watchDescriptors()const;
  std::vector<int> criticalWatchDescriptors()const;
  static std::expected<std::shared_ptr<ProfileSource>,storage::Error> openImpl(
      const storage::ProfileLease&,const core::TorrentHash&,std::string,bool session);
  std::expected<engine::detail::ManagedStorage,storage::Error> prepareStorageDirect();
  std::expected<void,storage::Error> publishImpl(const std::optional<core::TorrentHash>&,
      const std::function<std::expected<void,storage::Error>()>&);
  struct Impl;
  explicit ProfileSource(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}
