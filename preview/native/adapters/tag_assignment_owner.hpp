// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "manual_tag_store.hpp"
#include <biglybtc/tag_assignment_actions.hpp>
namespace biglybtc::storage {
struct TagAssignmentDomain;
class TagAssignmentOwner;
// Current values captured by the existing owner before acquiring its FS gate.
// These observations do not carry a command or profile permission.
struct TagAssignmentObservation {
  core::TorrentHash hash;
  std::uint64_t session=0,sourceRevision=0;
  std::int32_t originalState=0;
  std::uint64_t admission=0,generation=0; // Optional current provider binding,0/0 for a saved-only owner.
};
class TagAssignmentReceipt final {
public:
  TagAssignmentReceipt()=default;
  TagAssignmentReceipt(TagAssignmentReceipt&&)noexcept=default;
  TagAssignmentReceipt& operator=(TagAssignmentReceipt&&)noexcept=default;
  TagAssignmentReceipt(const TagAssignmentReceipt&)=delete;
  TagAssignmentReceipt& operator=(const TagAssignmentReceipt&)=delete;
  bool done()const noexcept;
private:
  struct Data;std::shared_ptr<Data> data_;
  friend class TagAssignmentOwner;
  explicit TagAssignmentReceipt(std::shared_ptr<Data> value):data_(std::move(value)){}
};
struct TagAssignmentCommit {ManualTagCommit commit;std::optional<TagAssignmentReceipt> receipt;};
struct TagAssignmentCommands {core::TorrentHash hash;core::tags::assignment::Effects commands;};
// Actual source/writer owner,not a network executor. It retains one profile
// lease until close. Receipts retain none. SDK commands must be consumed by the
// existing normal Host after these synchronous source sections have returned.
class TagAssignmentOwner final {
public:
  static std::expected<std::unique_ptr<TagAssignmentOwner>,Error> activate(
      const ProfileLease&,std::uint64_t session,const TagManagerStartupMode&);
  ~TagAssignmentOwner();
  TagAssignmentOwner(const TagAssignmentOwner&)=delete;
  TagAssignmentOwner& operator=(const TagAssignmentOwner&)=delete;
  std::expected<TagAssignmentCommit,Error> change(ManualTagStore&,const ManualTagSnapshot&,
      std::int32_t tag,const TagAssignmentObservation&,bool add,std::int64_t addedSeconds,std::stop_token={});
  // One subject,up to32 distinct ordered tags,one CAS and one bounded receipt.
  std::expected<TagAssignmentCommit,Error> changeBatch(ManualTagStore&,const ManualTagSnapshot&,
      std::span<const ManualTagMembershipEdit>,const TagAssignmentObservation&,std::stop_token={},std::optional<std::string_view> prepared=std::nullopt);
  std::expected<TagAssignmentCommit,Error> publishPlan(ManualTagStore&,const ManualTagSnapshot&,
      const ManualTagMembershipPlan&,const TagAssignmentObservation&,std::stop_token={});
  std::expected<TagAssignmentCommands,Error> next(ManualTagStore&,const ManualTagSnapshot&,
      TagAssignmentReceipt&,std::stop_token={});
  void close()noexcept;
private:
  TagAssignmentOwner(std::shared_ptr<const ProfileLease>,std::shared_ptr<TagAssignmentDomain>);
  std::expected<void,Error> current(const ManualTagStore&,const ManualTagSnapshot&)const;
  std::shared_ptr<const ProfileLease> lease_;
  std::shared_ptr<TagAssignmentDomain> domain_;
  std::vector<std::weak_ptr<TagAssignmentReceipt::Data>> pending_;
  std::uint64_t sequence_=0;
};
} // namespace biglybtc::storage
