// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tag_manager_startup.hpp"
#include "biglybtc/saved_download_tag_edits.hpp"
#include "profile.hpp"
#include "captured_removal_tags.hpp"
#include <stop_token>
#include <thread>
#include <span>

namespace biglybtc::app {class SessionBridge;}
namespace biglybtc::storage {
class ManualTagStore;
class TagAssignmentOwner;
class PreparedFileOperation;
using ManualTagMembershipEdit=std::variant<core::SavedTagAddMember,core::SavedTagRemoveMember>;
class ManualTagSnapshot final {
public:
  ManualTagSnapshot() = default;
  bool valid() const noexcept { return bool(data_); }
  std::uint64_t generation() const noexcept;
  bool missing() const noexcept;
  bool writable() const noexcept;
  // Exact captured candidate bytes/metadata; excludes logical reload generation.
  bool sameSource(const ManualTagSnapshot&) const noexcept; // Passive captured-candidate hint, never an edit grant.
  std::optional<RecoverySource> recoverySource() const noexcept;
  const core::SavedDownloadTags* tags() const noexcept;
  // Missing has no physical raw preimage; tags() owns a declared empty "de".
  std::optional<std::string_view> rawBytes() const noexcept;
private:
  struct Data;
  friend class ManualTagStore;
  explicit ManualTagSnapshot(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
  std::shared_ptr<const Data> data_;
};
// Immutable staged membership data, tied to one actual store snapshot. It
// retains bytes only, no profile lease, writable snapshot or publication grant.
class ManualTagMembershipPlan final {
public:
  ManualTagMembershipPlan()=default;
  bool valid()const noexcept{return bool(data_);}
  const core::SavedDownloadTags* tags()const noexcept;
  std::optional<std::string_view> rawBytes()const noexcept;
  std::size_t edits()const noexcept;
  bool sameView(const ManualTagMembershipPlan& other)const noexcept{return data_==other.data_;}
private:
  struct Data;friend class ManualTagStore;
  explicit ManualTagMembershipPlan(std::shared_ptr<const Data> data):data_(std::move(data)){}
  std::shared_ptr<const Data> data_;
};
struct ManualTagCommit {
  ManualTagSnapshot snapshot;
  bool changed = false;
  std::optional<std::int32_t> createdId;
  std::optional<std::int64_t> createdUid;
};
// All methods run synchronously on the construction worker. No QObject, pool,
// network, live manager, clock, callback or automatic tag action is created.
// One retained actual OFD alias is released by close/destruction; snapshots own
// bytes only. Caller provides the external worker and serializes its commands.
class ManualTagStore final {
public:
  static std::expected<std::unique_ptr<ManualTagStore>, Error> open(
      const ProfileLease&, std::stop_token stop = {});
  ~ManualTagStore();
  ManualTagStore(const ManualTagStore&) = delete;
  ManualTagStore& operator=(const ManualTagStore&) = delete;
  // Passive worker hint only. An explicit reader may close/open a poisoned or
  // closed instance; this never retries the failed edit or repairs candidates.
  bool recoveryRequired() const noexcept;
  // Passive same-OFD identity only; never validates a source or grants an edit.
  bool sharesOwnership(const ProfileLease& lease) const noexcept {
    return lease_ && lease_->sharesOwnership(lease);
  }
  // Passive cached lineage only: no filesystem/source validation, edit or
  // registry authority. Held-turn reads and actual precommit CAS stay required.
  bool matchesSnapshotLineage(const ManualTagSnapshot& snapshot,const ProfileLease& lease) const noexcept {
    return std::this_thread::get_id()==worker_&&!poisoned_&&sharesOwnership(lease)
        &&snapshot.data_&&snapshot.data_==latest_&&snapshot.generation()==generation_;
  }
  std::expected<ManualTagSnapshot, Error> snapshot() const;
  std::expected<ManualTagSnapshot, Error> reload(std::stop_token stop = {});
  // expected must be this store's latest owning snapshot. Caller-supplied
  // SavedTagCreate.counter is refused: only a successful store commit advances
  // the retained logical-session counter. An uncertain save permanently poisons
  // this instance; close/reopen explicitly before another attempt.
  std::expected<ManualTagCommit, Error> apply(const ManualTagSnapshot& expected,
      const core::SavedTagEdit&, std::stop_token stop = {});
  // At most32 ordered membership edits across tags, one source preimage and
  // one durable publication. All edits/policies are validated before saving;
  // failure cannot publish an earlier prefix. No tag actions are executed.
  std::expected<ManualTagCommit, Error> applyMembershipBatch(const ManualTagSnapshot&,
      std::span<const ManualTagMembershipEdit>,std::stop_token={});
  // Preparation/extension are immutable value operations,including qualified
  // lifecycle configuration data. Public publication still refuses its active
  // effects;only the actual assignment owner may save those and issue receipts.
  // Publication repeats
  // real source/CAS checks and compares replay bytes with the sealed plan view.
  std::expected<ManualTagMembershipPlan,Error> planMembership(const ManualTagSnapshot&)const;
  std::expected<ManualTagMembershipPlan,Error> extendMembershipPlan(const ManualTagMembershipPlan&,
      const ManualTagMembershipEdit&,std::stop_token={})const;
  bool matchesMembershipPlan(const ManualTagMembershipPlan&,const ProfileLease&,const ManualTagSnapshot&)const noexcept;
  std::expected<ManualTagCommit,Error> publishMembershipPlan(const ManualTagMembershipPlan&,std::stop_token={});
  std::expected<void, Error> close();
private:
  // Trusted Host worker only. The mode is the actual immutable startup config
  // fact, not a UI permission. Every positive still checks the real namespace.
  static std::expected<bool,Error> historyRemovalModeAtOpen(const ProfileLease&,std::stop_token);
  std::expected<CapturedRemovalTags,Error> captureHistoryRemovalTags(
      const ProfileLease&,const PreparedFileOperation&,bool enabledAtOpen,std::stop_token);
  friend class biglybtc::app::SessionBridge;
  explicit ManualTagStore(std::shared_ptr<const ProfileLease> lease)
      : lease_(std::move(lease)), worker_(std::this_thread::get_id()) {}
  std::expected<void, Error> current() const;
  friend class TagAssignmentOwner;
  friend class PreparedTagRateSource;
  std::expected<ManualTagCommit,Error> applyLifecycleMembership(const ManualTagSnapshot&,
      const ManualTagMembershipEdit&,std::stop_token);
  std::expected<ManualTagCommit,Error> applyLifecycleBatch(const ManualTagSnapshot&,
      std::span<const ManualTagMembershipEdit>,std::optional<std::string_view>,std::stop_token);
  std::span<const ManualTagMembershipEdit> membershipPlanOperations(const ManualTagMembershipPlan&)const noexcept;
  std::expected<ManualTagCommit,Error> applySequence(const ManualTagSnapshot&,
      std::span<const core::SavedTagEdit>,std::stop_token,std::optional<std::string_view> prepared=std::nullopt,bool lifecycleExecutor=false);
  // Opaque store lifetime only;receipt weak references retain no profile lease.
  std::shared_ptr<const std::byte> assignmentIdentity_=std::make_shared<const std::byte>(std::byte{});
  std::shared_ptr<const ProfileLease> lease_;
  std::thread::id worker_;
  std::shared_ptr<const ManualTagSnapshot::Data> latest_;
  std::optional<std::int32_t> counter_;
  std::uint64_t generation_ = 0;
  bool poisoned_ = false;
};
} // namespace biglybtc::storage
