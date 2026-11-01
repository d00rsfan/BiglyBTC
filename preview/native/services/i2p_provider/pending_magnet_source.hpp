// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile_source.hpp"
#include "magnet_intake.hpp"
#include "pending_metadata_source.hpp"
namespace biglybtc::services::i2p::provider {
// Hash-only source authority for one ORIGINAL persisted metadata intent. It is
// deliberately distinct from a full torrent/storage source. No payload grant.
class PendingMagnetSource final {
public:
  const std::shared_ptr<const storage::ProfileLease>& lease()const noexcept;
  const core::TorrentHash& hash()const noexcept;
  const engine::PendingMagnet& intent()const noexcept;
  bool prepared()const noexcept;
  bool discoveryAllowed()const noexcept;
  bool revisionCurrent(std::uint64_t)const noexcept;
  std::shared_ptr<i2cp::EffectAuthority> effectAuthority()const noexcept;
  std::expected<void,storage::Error> validate()const;
  std::expected<void,storage::Error> validateTransportSnapshot()const;
  std::expected<void,storage::Error> bindMetadata(std::shared_ptr<const engine::PendingMetadataSource>);
  std::expected<void,storage::Error> release(); // Worker, only after actual metadata lifetime ended.
  void revoke()noexcept;
private:
  friend class ProfileSource;friend class SourcePermit;friend class Bridge;
  explicit PendingMagnetSource(std::shared_ptr<ProfileSource> source):source_(std::move(source)){}
  std::shared_ptr<ProfileSource> source_;
};
}
