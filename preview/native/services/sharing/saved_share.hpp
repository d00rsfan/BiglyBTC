// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "torrent_intake.hpp"
#include "sharing_values.hpp"
#include <cstdint>
#include <memory>
#include <stop_token>
namespace biglybtc::app {class SessionBridge;}
namespace biglybtc::tracker {class EmbeddedTrackerService;}
namespace biglybtc::sharing {
class SavedSharingService;
struct SavedShareDescription {
  SavedShareSelector selected;
  std::filesystem::path source,cacheFile,intakeDestination;
  core::SavedShareKind kind=core::SavedShareKind::File;
  core::TorrentHash hash;
  std::string announceUrl;
  std::uint64_t catalogGeneration=0;
};
class SavedShareHandle final {
public:
  SavedShareHandle(SavedShareHandle&&)noexcept;
  SavedShareHandle& operator=(SavedShareHandle&&)noexcept;
  ~SavedShareHandle();
  const SavedShareDescription& description()const noexcept;
  // Consumers must first prove current(); close/revocation retires this value.
  const storage::TorrentIntakeMetadata& metadata()const noexcept;
  std::expected<std::uint64_t,storage::Error> current(const storage::ProfileLease&,std::uint64_t)const;
private:
  friend class SavedSharingService;friend class ::biglybtc::app::SessionBridge;
  friend class ::biglybtc::tracker::EmbeddedTrackerService;
  static std::expected<SavedShareHandle,storage::Error> read(const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,const SavedShareSelector&,std::uint64_t,std::stop_token);
  void revoke()noexcept;
  std::size_t retainedBytes()const noexcept;
  struct Impl;explicit SavedShareHandle(std::shared_ptr<Impl>)noexcept;
  std::expected<SavedShareHandle,storage::Error> retainedCopy(const storage::ProfileLease&,std::uint64_t)const;
  std::shared_ptr<Impl> impl_;
};
} // namespace biglybtc::sharing
