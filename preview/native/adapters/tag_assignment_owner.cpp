// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_assignment_owner.hpp"
#include "prepared_tag_rate_source.hpp"
#include "manual_tag_identity.hpp"
#include "torrent_state.hpp"
#include "category_store.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
#include <new>
namespace biglybtc::storage {
namespace a=core::tags::assignment;
struct TagAssignmentDomain {
  std::uint64_t session=0;
  std::thread::id thread=std::this_thread::get_id();
  std::atomic_bool closed=false;
};
struct TagAssignmentReceipt::Data {
  std::weak_ptr<TagAssignmentDomain> owner;
  std::weak_ptr<const std::byte> store;
  std::uint64_t sequence=0;
  core::TorrentHash hash;
  struct Task {std::int32_t tag; a::Task command;};
  std::vector<Task> tasks; // <=64,allocated before the single CAS.
  unsigned index=0;
  bool terminal=false;
};
namespace {
Error conflict(){return {ErrorCode::Conflict,"Assignment actor or committed source is stale or foreign"};}
Error unavailable(){return {ErrorCode::Unsupported,"Configured assignment effects require another owner"};}
const core::SavedDownloadTag* definition(const ManualTagSnapshot& source,std::int32_t id){
  if(!source.tags()){return nullptr;}
  const core::SavedDownloadTag* found=nullptr;
  for(const auto& row:source.tags()->tags){if(row.id==id){if(found||row.duplicateId){return nullptr;}found=&row;}}
  return found;
}
const core::Node* record(const ManualTagSnapshot& source,std::int32_t id){
  const auto* type=source.tags()&&source.tags()->source?source.tags()->source->find("3"):nullptr;
  return type?type->find(std::to_string(id)):nullptr;
}
std::expected<bool,Error> metadata(const ProfileLease& lease,const core::TorrentHash& hash){
  auto store=TorrentStateStore::open(lease);if(!store){return std::unexpected(store.error());}
  auto state=store->load(lease,hash);if(!state||state->recoveredBackup()){return std::unexpected(conflict());}
  const auto* attributes=state->document().find("attributes");if(!attributes){return false;}
  if(attributes->type!=core::Node::Type::Dictionary){return std::unexpected(unavailable());}
  const auto* flags=attributes->find("flags");if(!flags){return false;}
  if(flags->type!=core::Node::Type::Integer){return std::unexpected(unavailable());}
  return (static_cast<std::uint64_t>(flags->integer)&0x200U)!=0;
}
}
bool TagAssignmentReceipt::done()const noexcept{
  if(!data_){return true;}const auto owner=data_->owner.lock();
  return !owner||data_->store.expired()||owner->closed.load()||owner->thread!=std::this_thread::get_id()
      ||data_->terminal||data_->index>=data_->tasks.size();
}
TagAssignmentOwner::TagAssignmentOwner(std::shared_ptr<const ProfileLease> lease,std::shared_ptr<TagAssignmentDomain> domain)
    :lease_(std::move(lease)),domain_(std::move(domain)){pending_.reserve(32);}
TagAssignmentOwner::~TagAssignmentOwner(){close();}
std::expected<std::unique_ptr<TagAssignmentOwner>,Error> TagAssignmentOwner::activate(
    const ProfileLease& lease,std::uint64_t session,const TagManagerStartupMode& mode){
  try{
    if(!session||!mode.enabled()||!mode.matches(lease)){return std::unexpected(unavailable());}
    auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
    auto held=lease.retain();if(!held){return std::unexpected(held.error());}
    auto domain=std::make_shared<TagAssignmentDomain>();domain->session=session;
    return std::unique_ptr<TagAssignmentOwner>(new TagAssignmentOwner(std::move(*held),std::move(domain)));
  }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Cannot activate assignment owner"});}
}
std::expected<void,Error> TagAssignmentOwner::current(const ManualTagStore& store,const ManualTagSnapshot& source)const{
  if(!lease_||!domain_||domain_->closed.load()||domain_->thread!=std::this_thread::get_id()
      ||!source.valid()||!source.writable()||!store.matchesSnapshotLineage(source,*lease_)){return std::unexpected(conflict());}
  if(hasPendingCategoryDeletion(*lease_)){return std::unexpected(conflict());}
  return {};
}
void TagAssignmentOwner::close()noexcept{
  if(domain_){domain_->closed.store(true);}pending_.clear();domain_.reset();lease_.reset();
}
std::expected<TagAssignmentCommit,Error> TagAssignmentOwner::change(ManualTagStore& store,const ManualTagSnapshot& source,
    std::int32_t id,const TagAssignmentObservation& observed,bool add,std::int64_t added,std::stop_token stop,const PreparedTagRateSource* rates){
  const ManualTagMembershipEdit edit=add?ManualTagMembershipEdit{core::SavedTagAddMember{id,observed.hash,added}}:ManualTagMembershipEdit{core::SavedTagRemoveMember{id,observed.hash}};
  return changeBatch(store,source,{&edit,1},observed,stop,std::nullopt,rates);
}
std::expected<TagAssignmentCommit,Error> TagAssignmentOwner::publishPlan(ManualTagStore& store,const ManualTagSnapshot& source,
    const ManualTagMembershipPlan& plan,const TagAssignmentObservation& observed,std::stop_token stop,const PreparedTagRateSource* rates){
  auto live=current(store,source);if(!live){return std::unexpected(live.error());}
  if(!plan.rawBytes()||!store.matchesMembershipPlan(plan,*lease_,source)){return std::unexpected(conflict());}
  return changeBatch(store,source,store.membershipPlanOperations(plan),observed,stop,*plan.rawBytes(),rates);
}
std::expected<TagAssignmentCommit,Error> TagAssignmentOwner::changeBatch(ManualTagStore& store,const ManualTagSnapshot& source,
    std::span<const ManualTagMembershipEdit> edits,const TagAssignmentObservation& observed,std::stop_token stop,std::optional<std::string_view> prepared,const PreparedTagRateSource* rates){
  try{
    auto live=current(store,source);if(!live){return std::unexpected(live.error());}
    if(edits.size()>32){return std::unexpected(unavailable());}
    if(observed.session!=domain_->session||!observed.sourceRevision||observed.sourceRevision!=lease_->sourceRevision()){
      return std::unexpected(conflict());
    }
    if(stop.stop_requested()){return std::unexpected(Error{ErrorCode::Cancelled,"Assignment cancelled before publication"});}
    auto access=lease_->sourceAccess();if(!access){return std::unexpected(access.error());}
    if(access->revision()!=observed.sourceRevision){return std::unexpected(conflict());}
    auto identity=bindManualTagIdentity(store,*lease_,source,observed.hash);if(!identity){return std::unexpected(identity.error());}
    auto md=metadata(*lease_,observed.hash);if(!md){return std::unexpected(md.error());}
    std::vector<TagAssignmentReceipt::Data::Task> tasks;tasks.reserve(edits.size()*2);
    std::vector<std::int32_t> seen;seen.reserve(edits.size());
    for(const auto& edit:edits){
      const auto id=std::visit([](const auto& value){return value.id;},edit);
      const auto hash=std::visit([](const auto& value){return value.hash;},edit);
      if(hash!=observed.hash||std::ranges::find(seen,id)!=seen.end()){return std::unexpected(unavailable());}seen.push_back(id);
      const auto* row=definition(source,id);const auto* saved=record(source,id);
      if(!row||!saved||!(core::lifecycleSavedTagMembershipEffects(*source.tags(),*saved)
          ||(rates&&rates->matchesAssignment(*lease_,store,source,id,domain_->session)&&core::rateReadyLifecycleSavedTagMembershipEffects(*source.tags(),*saved)))){return std::unexpected(unavailable());}
      const auto flags=a::readSaved(*saved,3);if(!flags.lifecycleOnly()){return std::unexpected(unavailable());}
      auto plan=a::plan(flags.enabled,observed.originalState,*md,false);if(!plan){return std::unexpected(unavailable());}
      const bool member=std::ranges::find(row->uniqueHashes,observed.hash)!=row->uniqueHashes.end();
      if(std::holds_alternative<core::SavedTagAddMember>(edit)&&!member){for(const auto task:plan->tasks()){tasks.push_back({id,task});}}
    }
    std::erase_if(pending_,[](const auto& entry){const auto data=entry.lock();return !data||data->terminal||data->index>=data->tasks.size();});
    std::shared_ptr<TagAssignmentReceipt::Data> receipt;
    if(!tasks.empty()){
      if(pending_.size()==32||sequence_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(Error{ErrorCode::Unsupported,"Assignment dispatch capacity exhausted"});}
      receipt=std::make_shared<TagAssignmentReceipt::Data>();receipt->owner=domain_;receipt->sequence=sequence_+1;
      receipt->store=store.assignmentIdentity_;receipt->hash=observed.hash;receipt->tasks=std::move(tasks);
      pending_.emplace_back(receipt);
    }
    auto changed=store.applyLifecycleBatch(source,edits,prepared,stop,rates);if(!changed){return std::unexpected(changed.error());}
    TagAssignmentCommit out{std::move(*changed),{}};
    if(out.commit.changed&&receipt){++sequence_;out.receipt=TagAssignmentReceipt(std::move(receipt));}
    return out;
  }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Cannot prepare bounded assignment receipt"});}
}
std::expected<TagAssignmentSubjectsCommit,Error> TagAssignmentOwner::changeSubjects(ManualTagStore& store,const ManualTagSnapshot& source,
    std::int32_t id,std::span<const TagAssignmentObservation> observations,bool add,std::int64_t added,std::stop_token stop,const PreparedTagRateSource* rates){
  try{
    auto live=current(store,source);if(!live){return std::unexpected(live.error());}
    if(observations.empty()||observations.size()>32){return std::unexpected(unavailable());}
    if(stop.stop_requested()){return std::unexpected(Error{ErrorCode::Cancelled,"Assignment selection cancelled before publication"});}
    auto access=lease_->sourceAccess();if(!access){return std::unexpected(access.error());}
    const auto* row=definition(source,id);const auto* saved=record(source,id);
    if(!row||!saved||!(core::lifecycleSavedTagMembershipEffects(*source.tags(),*saved)
          ||(rates&&rates->matchesAssignment(*lease_,store,source,id,domain_->session)&&core::rateReadyLifecycleSavedTagMembershipEffects(*source.tags(),*saved)))){return std::unexpected(unavailable());}
    const auto flags=a::readSaved(*saved,3);if(!flags.lifecycleOnly()){return std::unexpected(unavailable());}
    std::erase_if(pending_,[](const auto& entry){const auto value=entry.lock();return !value||value->terminal||value->index>=value->tasks.size();});
    std::vector<core::TorrentHash> hashes;hashes.reserve(observations.size());
    std::vector<std::shared_ptr<TagAssignmentReceipt::Data>> prepared;prepared.reserve(observations.size());
    TagAssignmentSubjectsCommit out;out.receipts.reserve(observations.size());
    for(const auto& observed:observations){
      if(observed.session!=domain_->session||!observed.sourceRevision||observed.sourceRevision!=access->revision()
          ||std::ranges::find(hashes,observed.hash)!=hashes.end()){return std::unexpected(conflict());}
      hashes.push_back(observed.hash);auto identity=bindManualTagIdentity(store,*lease_,source,observed.hash);if(!identity){return std::unexpected(identity.error());}
      auto md=metadata(*lease_,observed.hash);if(!md){return std::unexpected(md.error());}
      auto plan=a::plan(flags.enabled,observed.originalState,*md,false);if(!plan){return std::unexpected(unavailable());}
      const bool member=std::ranges::find(row->uniqueHashes,observed.hash)!=row->uniqueHashes.end();
      if(add&&!member&&plan->count){
        if(pending_.size()+prepared.size()==32||sequence_==std::numeric_limits<std::uint64_t>::max()-prepared.size()){return std::unexpected(Error{ErrorCode::Busy,"Assignment selection dispatch capacity exhausted"});}
        auto receipt=std::make_shared<TagAssignmentReceipt::Data>();receipt->owner=domain_;receipt->store=store.assignmentIdentity_;
        receipt->hash=observed.hash;receipt->sequence=sequence_+prepared.size()+1;receipt->tasks.reserve(plan->count);
        for(const auto task:plan->tasks()){receipt->tasks.push_back({id,task});}prepared.push_back(std::move(receipt));
      }
    }
    // Register weak queue storage before the real CAS. Failed writes retain no
    // receipt and these expired entries are pruned on the next actual request.
    for(const auto& receipt:prepared){pending_.push_back(receipt);}
    const core::SavedTagEdit edit=core::SavedTagMemberBatch{id,std::move(hashes),add,added};
    auto committed=store.applySequence(source,{&edit,1},stop,std::nullopt,true,rates);if(!committed){return std::unexpected(committed.error());}
    out.commit=std::move(*committed);
    if(out.commit.changed){sequence_+=prepared.size();for(auto& receipt:prepared){out.receipts.push_back(TagAssignmentReceipt(std::move(receipt)));}}
    return out;
  }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Cannot prepare bounded selection assignment receipts"});}
}
std::expected<TagAssignmentCommands,Error> TagAssignmentOwner::next(ManualTagStore& store,const ManualTagSnapshot& source,
    TagAssignmentReceipt& receipt,std::stop_token stop){
  auto live=current(store,source);if(!live){return std::unexpected(live.error());}
  if(!receipt.data_||receipt.data_->owner.lock()!=domain_||receipt.data_->store.lock()!=store.assignmentIdentity_||receipt.done()){return std::unexpected(conflict());}
  auto& data=*receipt.data_;
  try{
    if(stop.stop_requested()){data.terminal=true;return std::unexpected(Error{ErrorCode::Cancelled,"Queued assignment cancelled"});}
    auto access=lease_->sourceAccess();if(!access){data.terminal=true;return std::unexpected(access.error());}
    auto identity=bindManualTagIdentity(store,*lease_,source,data.hash);if(!identity){data.terminal=true;return std::unexpected(identity.error());}
    const auto* saved=record(source,data.tasks[data.index].tag);
    const auto enabled=saved?a::readSaved(*saved,3).enabled&a::LifecycleMask:0;
    auto commands=a::resolve(data.tasks[data.index].command,enabled);if(!commands){data.terminal=true;return std::unexpected(unavailable());}
    TagAssignmentCommands out{data.hash,*commands};++data.index;return out;
  }catch(const std::bad_alloc&){data.terminal=true;return std::unexpected(Error{ErrorCode::Io,"Cannot resolve queued assignment"});}
}
} // namespace biglybtc::storage
