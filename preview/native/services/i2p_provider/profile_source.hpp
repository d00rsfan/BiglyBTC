// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owned_i2p_policy.hpp"
#include "managed_storage.hpp"
#include "identity.hpp"
#include "effect_authority.hpp"
#include "subscription_source.hpp"
#include "biglybtc/saved_download_tags.hpp"
#include <atomic>
#include <memory>
#include <functional>
namespace libtorrent {struct session_handle;struct info_hash_t;}

namespace biglybtc::storage {class TagManagerStartupMode;}
namespace biglybtc::engine {class PrivateTagRateSource;class ProfileTagRates;class MixedManagedSource;class PendingMetadataSource;struct PendingMagnet;}
namespace biglybtc::services::i2p::provider {
class OwnedTorrent;
class PendingMagnetSource;
class OwnedProfile;
struct TagSourceRefresh;
// Filesystem methods belong to an external lifetime/I/O worker, never the
// socket thread. Public observations contain no key bytes or ephemeral auth.
class ProfileSource final:public std::enable_shared_from_this<ProfileSource> {
public:
  static std::expected<std::shared_ptr<ProfileSource>,storage::Error> open(
      const storage::ProfileLease&,const core::TorrentHash&,std::string pluginDirectory="azneti2phelper");
  // Explicit shared identity/session anchor. No torrent or payload permission.
  static std::expected<std::shared_ptr<ProfileSource>,storage::Error> createSession(
      const storage::ProfileLease&,std::string pluginDirectory="azneti2phelper",
      const storage::TagManagerStartupMode* startup=nullptr);
  // Explicit original mixed index0/key0; pure factories retain index1/key1.
  static std::expected<std::shared_ptr<ProfileSource>,storage::Error> createMixedSession(
      const storage::ProfileLease&,std::string pluginDirectory="azneti2phelper");
  // Read-only acceptance: no ManagedStorage preparation or effect authority.
  std::expected<std::shared_ptr<ProfileSource>,storage::Error> prepareMixedChild(
      const core::TorrentHash&);
  // One-shot, inside this anchor's target publication, after the sole engine
  // storage preparation. Only the actual engine can construct the binding.
  std::expected<void,storage::Error> bindMixedManaged(
      std::shared_ptr<const engine::MixedManagedSource>);
  // Source/transport retirement only; never removes/checkpoints engine storage.
  std::expected<void,storage::Error> releaseMixedChild(const core::TorrentHash&);
  std::expected<std::shared_ptr<PendingMagnetSource>,storage::Error> preparePendingMagnet(const engine::PendingMagnet&);
  // Only the selected original pending row and target import may change. All
  // other config, identity, inventory rows and child sources remain exact.
  std::expected<void,storage::Error> publishPendingMagnet(const core::TorrentHash&,
      const std::function<std::expected<void,storage::Error>()>&);
  std::expected<void,storage::Error> releasePendingChild(const core::TorrentHash&);
  bool discoveryAllowed()const noexcept;
  bool mixedMode()const noexcept;
  // Worker-only read after intentional transport withdrawal. Checks exact held
  // profile/key/metadata snapshots even when effects are revoked. Does not
  // clear dirty/revoked flags, refresh an epoch or restore any permission.
  std::expected<void,storage::Error> validateTransportSnapshot()const;
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
  // A scheduler publication may only change its saved options or global rate settings.
  std::expected<void,storage::Error> publishSchedulerSettings(
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
  bool tagManagerEnabled() const noexcept;
  void revoke()noexcept;
  void revokeTransportEffects() noexcept;
  std::shared_ptr<i2cp::EffectAuthority> effectAuthority()const noexcept;
private:
  friend class PendingMagnetSource;
  friend class OwnedTorrent;
  friend class OwnedProfile;
  friend class SourcePermit;
  // Absent until the I/O worker has observed this profile revision.
  std::optional<std::shared_ptr<const core::SavedTagQueueRules>> queueTagRules() const noexcept;
  std::expected<void,storage::Error> validate(const storage::ProfileSourceAccess&) const;
  // Read-only final CAS preflight after the friend's actual flush/remove/join.
  // Accepts intentionally revoked effects ONLY when every held named source
  // and exact prepared snapshot still matches; never restores effect authority.
  std::expected<void,storage::Error> validateRetiredSnapshot()const;
  friend class Bridge;
  using WatchHandle = decltype(std::declval<const storage::ProfileLease&>().directoryHandle());
#ifdef _WIN32
  struct WatchDirectory { WatchHandle handle; std::vector<std::wstring> criticalNames; };
  std::vector<WatchDirectory> watchDirectories() const;
#else
  std::vector<WatchHandle> watchDescriptors()const;
  std::vector<WatchHandle> criticalWatchDescriptors()const;
#endif
  static std::expected<std::shared_ptr<ProfileSource>,storage::Error> openImpl(
      const storage::ProfileLease&,const core::TorrentHash&,std::string,bool session,bool mixed=false,
      const storage::TagManagerStartupMode* startup=nullptr);
  // Trusted actual owner only. Capture raw full identity under its source gate,
  // release before SDK preparation,then revalidate immediately before storage.
  std::expected<libtorrent::info_hash_t,storage::Error> previewTagTarget()const;
  std::expected<engine::detail::ManagedStorage,storage::Error> prepareStorageWithTagSource(
      const engine::PrivateTagRateSource&,const libtorrent::info_hash_t&,libtorrent::session_handle);
  bool matchesTagOwner(const engine::ProfileTagRates&)const noexcept;
  bool matchesTagControllerProfile(const engine::ProfileTagRates&)const noexcept;
  std::expected<void,storage::Error> bindPendingTagSource(const engine::PrivateTagRateSource&,
      const libtorrent::info_hash_t&,libtorrent::session_handle);
  // Trusted owner only,after both real source workers have joined. FS-only;
  // all unrelated snapshots remain exact and every retained child participates.
  std::expected<void,storage::Error> acceptTagSources(std::span<const TagSourceRefresh>);
  std::expected<engine::detail::ManagedStorage,storage::Error> prepareStorageDirect();
  enum class Publication { Library, PendingMagnet, SchedulerSettings };
  std::expected<void,storage::Error> publishImpl(const std::optional<core::TorrentHash>&,
      const std::function<std::expected<void,storage::Error>()>&,Publication = Publication::Library);
  std::expected<void,storage::Error> bindPendingMetadata(std::shared_ptr<const engine::PendingMetadataSource>);
  std::expected<std::shared_ptr<SubscriptionSource>,storage::Error> prepareSubscriptionSource(
      const core::TorrentHash&,engine::HeldSubscriptionSeedResolver);
  struct Impl;
  explicit ProfileSource(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}
