// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/saved_download_history.hpp"
#include "profile.hpp"
#include <array>
#include <chrono>
#include <stop_token>

namespace biglybtc::storage {
enum class DownloadHistorySide {Active,Dead};
enum class DownloadHistoryReadOnly {None,PendingCandidate,PendingIntent,DuplicatePairUid};
enum class DownloadHistoryWriteStage {IntentDurable,FilePublished};
using DownloadHistoryObserver=void(*)(DownloadHistoryWriteStage,std::size_t);
class DurableIntakeReceipt;
class DurableCompletionReceipt;
class DurableRemovalReceipt;
class HistoryReconstructionReceipt;
class DownloadHistoryStore;
class DownloadHistorySnapshot final {
public:
  DownloadHistorySnapshot()=default;
  bool valid()const noexcept{return bool(data_);}
  std::uint64_t generation()const noexcept;
  bool enabled()const noexcept; // Original true default; captured hint, not a grant.
  bool writable()const noexcept;
  bool missing(DownloadHistorySide)const noexcept;
  DownloadHistoryReadOnly review()const noexcept;
  const core::SavedDownloadHistory* history(DownloadHistorySide)const noexcept;
  bool sameSource(const DownloadHistorySnapshot&)const noexcept;
private:
  struct Data;friend class DownloadHistoryStore;
  explicit DownloadHistorySnapshot(std::shared_ptr<const Data> data):data_(std::move(data)){}
  std::shared_ptr<const Data> data_;
};
struct DownloadHistoryCommit {DownloadHistorySnapshot snapshot;bool changed=false;};
// ONE existing external construction worker, retained SAME OFD. No timer,
// QObject, manager, provider, payload or filesystem worker is started here.
class DownloadHistoryStore final {
public:
  using Deadline=std::chrono::steady_clock::time_point;
  static std::expected<std::unique_ptr<DownloadHistoryStore>,Error> open(
      const ProfileLease&,std::stop_token={},Deadline=Deadline::max());
  ~DownloadHistoryStore();DownloadHistoryStore(const DownloadHistoryStore&)=delete;
  DownloadHistoryStore& operator=(const DownloadHistoryStore&)=delete;
  bool recoveryRequired()const noexcept;
  bool sharesOwnership(const ProfileLease&)const noexcept;
  std::expected<DownloadHistorySnapshot,Error> snapshot()const;
  std::expected<DownloadHistorySnapshot,Error> reload(std::stop_token={},Deadline=Deadline::max());
  // Explicit existing-history edit, never a new-download event. Append is
  // deliberately refused here; only appendIntake accepts a fresh sealed intake.
  // Disabled config permits explicit erase/clear, but refuses lifecycle updates.
  std::expected<DownloadHistoryCommit,Error> apply(const DownloadHistorySnapshot&,
      DownloadHistorySide,const core::SavedDownloadHistoryEdit&,std::stop_token={},
      Deadline=Deadline::max(),DownloadHistoryObserver=nullptr);
  // Consumes an actual fresh import receipt even on refusal/disabled/no-op.
  // This is the only Append entry point; no arbitrary record/hash overload.
  std::expected<DownloadHistoryCommit,Error> appendIntake(const DownloadHistorySnapshot&,
      DurableIntakeReceipt,std::stop_token={},Deadline=Deadline::max());
  // Consumes a real retired-owner completion once. Updates only the existing
  // matching active row; disabled/missing is a no-op, never append/backfill.
  std::expected<DownloadHistoryCommit,Error> complete(const DownloadHistorySnapshot&,
      DurableCompletionReceipt,std::stop_token={},Deadline=Deadline::max());
  // Actual fresh Keep Data commit only: consumes once; moves an existing exact
  // active UID/full identity/add-time record to dead, never append/backfill.
  std::expected<DownloadHistoryCommit,Error> remove(const DownloadHistorySnapshot&,
      DurableRemovalReceipt,std::stop_token={},Deadline=Deadline::max());
  // Actual user clear of BOTH documents, dead first. Durable intent
  // before any write; interrupted prefixes remain read-only review, no replay.
  // This is not atomic across Java files and does not toggle/reset enabled.
  std::expected<DownloadHistoryCommit,Error> clear(const DownloadHistorySnapshot&,
      std::stop_token={},Deadline=Deadline::max(),DownloadHistoryObserver=nullptr);
  // Explicit user Reset only, not startup/intake or an arbitrary row setter.
  // Actual complete idle owner source supplies all records. Clears dead first,
  // replaces active second; interruption keeps the existing review intent.
  std::expected<DownloadHistoryCommit,Error> reset(const DownloadHistorySnapshot&,
      HistoryReconstructionReceipt,std::stop_token={},Deadline=Deadline::max());
  std::expected<void,Error> close();
private:
  struct Impl;explicit DownloadHistoryStore(std::unique_ptr<Impl>);
  std::expected<DownloadHistoryCommit,Error> commit(const DownloadHistorySnapshot&,
      std::array<std::optional<std::string>,2>,std::stop_token,Deadline,DownloadHistoryObserver,const DurableIntakeReceipt* = nullptr,const DurableCompletionReceipt* = nullptr,const HistoryReconstructionReceipt* = nullptr,const DurableRemovalReceipt* = nullptr);
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::storage
