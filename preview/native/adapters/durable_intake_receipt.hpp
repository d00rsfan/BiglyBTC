// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "torrent_intake.hpp"
#include "biglybtc/saved_download_history.hpp"
#include <chrono>

namespace biglybtc::storage {
class DownloadHistoryStore;
// A single-use owning witness of a NEW completed import, never a hash/UI grant.
// Only fresh imports or the private fresh stopped-plan commit can issue one. Recovery and
// read/start/reopen paths have no issuer. No provider/network capability lives here.
class DurableIntakeReceipt final {
public:
  DurableIntakeReceipt(DurableIntakeReceipt&&)noexcept;
  DurableIntakeReceipt& operator=(DurableIntakeReceipt&&)noexcept;
  ~DurableIntakeReceipt();
  DurableIntakeReceipt(const DurableIntakeReceipt&)=delete;
  DurableIntakeReceipt& operator=(const DurableIntakeReceipt&)=delete;
private:
  struct Data;std::unique_ptr<Data> data_;
  explicit DurableIntakeReceipt(std::unique_ptr<Data>);
  static std::expected<DurableIntakeReceipt,Error> issue(const ProfileLease&,const core::TorrentHash&,std::string_view inventory,std::string_view state,std::string_view archive);
  std::expected<void,Error> current(const ProfileLease&,bool exactEpoch)const;
  const core::DownloadHistoryNewRecord& record()const;
  bool monitored()const;
  friend class DownloadHistoryStore;
  friend class PreparedStoppedPublicIntake;
  friend std::expected<core::TorrentHash,Error> importTorrent(ProfileLease&,const std::filesystem::path&,const std::filesystem::path&,bool,void(*)(TorrentIntakeStage),bool,TorrentIntakeNetwork,IntakeHistoryOutcome*);
  friend std::expected<core::TorrentHash,Error> importTorrentWithFileOptions(ProfileLease&,const TorrentIntakeMetadata&,const std::filesystem::path&,bool,std::span<const compat::FilePolicy>,void(*)(TorrentIntakeStage),bool,std::stop_token,core::TorrentIntakeLimits,bool,bool,bool,std::optional<core::TorrentIntakeTrackerTiers>,std::optional<std::string>,bool,std::optional<core::TorrentIntakeFileDestinations>,core::ExistingVerificationGoal,core::TorrentAllocationMode,TorrentIntakeNetwork,IntakeHistoryOutcome*);
  friend std::expected<core::TorrentHash,Error> importHybridAlias(ProfileLease&,const core::TorrentHash&,OriginalSelectedHash,std::span<const OriginalTorrentIdentity>,void(*)(TorrentIntakeStage),std::stop_token,IntakeHistoryOutcome*);
};
// Post-import auxiliary outcome: issuing/history failure can NEVER change the
// already committed import result. Default missing outcome grants nothing.
struct IntakeHistoryOutcome {
  std::optional<DurableIntakeReceipt> receipt;
  std::optional<Error> warning;
};
} // namespace biglybtc::storage
