#include "tag_publication.hpp"
// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_constraint_worker.hpp"
#include "constraint_state_history.hpp"
#include <biglybtc/saved_peer_sets.hpp>
#include "new_download_events.hpp"
#include "tracker_tag_births.hpp"
#include "constraint_cadence.hpp"
#include "library_view.hpp"
#include "category_store.hpp"
#include "file_io.hpp"
#include "file_operations.hpp"
#include <biglybtc/category_tags.hpp>
#include <biglybtc/stock_tag_names.hpp>
#include <biglybtc/tracker_tag_rules.hpp>
#include <biglybtc/untagged_tag_policy.hpp>
#include <biglybtc/tag_dependency_apply.hpp>
#include <QCryptographicHash>
#include <cerrno>
#include <sys/stat.h>
#include <fcntl.h>
#include "local_session.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <deque>
#include <map>
#include <new>
#include <set>

namespace biglybtc::app {
namespace c=core::tags::constraints;
namespace {
storage::Error error(storage::ErrorCode code,const char* text){return {code,text};}
c::Problem unavailable(const char* text){return {c::ProblemKind::Unavailable,text};}
bool compatibleConfiguration(const core::Node& record,bool ordinaryGroup){
  const auto* config=record.find("c");if(!config){return true;}
  if(config->type!=core::Node::Type::Dictionary){return false;}
  for(std::size_t i=0;i<config->children.size();i+=2){
    const auto key=config->children[i].bytes;const auto& value=config->children[i+1];
    if(core::savedTagSupportedRateProperty(key,value)||core::savedTagLifecycleProperty(key,value)||core::savedTagDisplayMetadata(key,value)||key=="pp.constraint"||key=="pp.enabled.constraint"||key=="vis"){continue;}
    if(key=="gr"&&ordinaryGroup&&value.type==core::Node::Type::Bytes){continue;}
    if(key=="col.rgb"&&core::readSavedTagColour(record)){continue;}
    if(key=="pp.trackers"&&value.type==core::Node::Type::List&&value.children.empty()){continue;}
    if(key=="pp.untagged"&&value.type==core::Node::Type::Integer&&value.integer!=1){continue;}
    // Unimplemented tag actions/rates/properties must not be bypassed by a
    // value-only membership write. Unknown saved configuration remains intact.
    return false;
  }
  return true;
}
c::Result<c::Property> property(const core::Node& record){
  const auto* config=record.find("c");const auto* value=config?config->find("pp.constraint"):nullptr;
  if(!value){return c::Property{};}
  if(value->type!=core::Node::Type::List||value->children.size()>64){return std::unexpected(unavailable("Malformed saved constraint property"));}
  std::vector<std::optional<c::Text>> strings;
  for(const auto& item:value->children){
    if(item.type!=core::Node::Type::Bytes||item.bytes.size()>65536){return std::unexpected(unavailable("Malformed saved constraint string"));}
    const auto text=QString::fromUtf8(item.bytes.data(),static_cast<qsizetype>(item.bytes.size()));
    if(text.toUtf8().toStdString()!=item.bytes){return std::unexpected(unavailable("Unsupported malformed UTF-8 constraint"));}
    strings.push_back(text.toStdU16String());
  }
  bool enabled=true;
  if(const auto* flag=config->find("pp.enabled.constraint")){
    if(flag->type!=core::Node::Type::Integer){return std::unexpected(unavailable("Malformed constraint enable property"));}
    enabled=flag->integer==1;
  }
  return c::readProperty(strings,enabled);
}
std::optional<std::int64_t> addedTime(const core::Document& state){
  const auto* attrs=state.find("attributes");const auto* params=attrs?attrs->find("parameters"):nullptr;
  const auto* value=params?params->find("stats.download.added.time"):nullptr;
  if(!params||params->type!=core::Node::Type::Dictionary||!value||value->type!=core::Node::Type::Integer){return {};}
  return value->integer;
}
struct CategoryFactSource {
  std::string bytes;bool primary=false;
  bool operator==(const CategoryFactSource&)const=default;
};
std::expected<CategoryFactSource,storage::Error> categoryFactSource(const storage::ProfileLease& lease){
  struct stat info{};
  const auto absent=[&](const char* name){return ::fstatat(lease.directoryHandle(),name,&info,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT;};
  if(!absent("categories.config.saving")||storage::hasPendingCategoryDeletion(lease)){
    return std::unexpected(error(storage::ErrorCode::Conflict,"Category facts require settled source definitions"));
  }
  auto file=storage::ConfigFile::load(lease,"categories.config");
  if(file){
    if(file->recoverySource()!=storage::RecoverySource::Primary){return std::unexpected(error(storage::ErrorCode::Conflict,"Category facts require primary definitions"));}
    return CategoryFactSource{file->document().originalBytes(),true};
  }
  if(file.error().code==storage::ErrorCode::Missing&&absent("categories.config")&&absent("categories.config.bak")&&absent("categories.config.saving")){
    return CategoryFactSource{"de",false};
  }
  return std::unexpected(file.error());
}
core::tags::runtime::Type categoryFactType(const core::tags::category::Roster& roster,
    const core::SavedCategoryAssignment& assignment,const std::optional<std::string>& locale){
  namespace runtime=core::tags::runtime;namespace category=core::tags::category;
  const auto names=locale?core::tags::stockCategoryTagNames(*locale):std::nullopt;
  std::optional<runtime::Text> assigned;
  if(assignment.resolved&&assignment.declaredName){assigned=QString::fromUtf8(assignment.declaredName->data(),qsizetype(assignment.declaredName->size())).toStdU16String();}
  runtime::Type type;type.id=1;type.complete=true;
  // Declaration order identifies allocation only. The original HashMap's live
  // getTags order is not certified by this partial source census.
  for(const auto& entry:roster.entries()){
    runtime::Tag value;value.id=entry.id;value.taggableTypes=2;
    if(entry.kind==category::Kind::User){
      if(!entry.rawName.starts_with(u"tag.")){value.name=entry.rawName;}
      else if(locale){if(auto stock=core::tags::stockTagName(*locale,entry.rawName)){value.name=runtime::Text(*stock);}}
    }
    else if(names){value.name=runtime::Text((*names)[entry.kind==category::Kind::All?0:1]);}
    if(names){value.group=runtime::Text((*names)[2]);}else{value.groupKnown=false;}
    const auto assignedKind=assigned?std::optional{category::Kind::User}:std::nullopt;
    value.membership=category::member(entry.kind,true,assigned&&*assigned==entry.rawName,assignedKind)?runtime::Membership::Present:runtime::Membership::Absent;
    type.tags.push_back(std::move(value));
  }
  return type;
}

const core::SavedDownloadTag* tag(const core::SavedDownloadTags& tags,std::int32_t id){
  const core::SavedDownloadTag* found=nullptr;
  for(const auto& row:tags.tags){if(row.id==id){if(found||row.duplicateId){return nullptr;}found=&row;}}
  return found;
}
}
struct TagConstraintWorker::Impl {
  struct Rule {std::int32_t id=0;std::string configuration;c::Property property;std::optional<c::Expression> expression;std::optional<c::Problem> problem;bool timeDependent=false;};
  struct RecentAdd {std::int32_t id;core::TorrentHash hash;std::chrono::steady_clock::time_point at;};
  std::shared_ptr<const storage::ProfileLease> lease;
  std::uint64_t session=0,lastSequence=0;
  std::optional<std::string> stockTagLocale;
  core::tags::category::Roster categoryRoster;
  std::optional<std::string> categoryDefinitions;
  std::thread::id worker=std::this_thread::get_id();
  bool closed=false,started=false,settingsKnown=false,nextEventTurn=true,enabledAtOpen=false;
  detail::NewDownloadEvents newDownloads;
  std::shared_ptr<const detail::NewDownloadRules> dispatchRules;
  std::vector<std::weak_ptr<const detail::NewDownloadRules>> ruleVersions; // <=8 active owning rule snapshots.
  std::shared_ptr<const detail::NewDownloadRules> newRules(){
    if(dispatchRules){return dispatchRules;}
    std::erase_if(ruleVersions,[](const auto& v){return v.expired();});
    if(ruleVersions.size()==8){return {};}
    auto next=std::make_shared<detail::NewDownloadRules>();
    for(const auto& rule:rules){if(rule.property.configured&&rule.property.mode.newOnly&&!rule.problem){next->push_back({rule.id,rule.property});}}
    ruleVersions.push_back(next);dispatchRules=next;return next;
  }
  std::optional<InternalTagSnapshot> lastInternalNamespace;
  std::shared_ptr<const core::Document> cachedSource;
  std::vector<Rule> rules;
  std::deque<std::int32_t> compiled; // <=128 retained ASTs, lazy bounded cache.
  std::deque<RecentAdd> recentAdds; // Original one-second per-tag/DM add guard.
  std::vector<QString> rows;
  std::size_t rowCursor=0,ruleCursor=0;
  std::shared_ptr<const core::Document> membershipSource;
  detail::ConstraintCadence cadence;
  bool fullPending=false,timePending=false,nextTimedTurn=true;
  std::vector<QString> timeRows;
  std::vector<std::int32_t> timeRules;
  std::size_t timeRowCursor=0,timeRuleCursor=0;
  std::chrono::seconds reapply{30};
  using StateSignature=std::pair<std::array<std::uint64_t,7>,QString>;
  std::map<QString,StateSignature> observedStates;
  bool statesStarted=false;
  struct StateHistory {
    std::uint64_t run=0,epoch=0,admission=0;
    detail::ConstraintStateHistory history;
  };
  std::map<QString,StateHistory> stateHistories;
  std::expected<void,storage::Error> queueChangedRows(const std::set<QString>& affected,const ui::LibraryPresentation& library){
    std::set<QString> allowed;for(const auto& row:library.items){allowed.insert(row.hash);}
    rows.erase(rows.begin(),rows.begin()+static_cast<std::ptrdiff_t>(rowCursor));rowCursor=0;
    if(!rows.empty()&&!allowed.contains(rows.front())){ruleCursor=0;}
    std::erase_if(rows,[&](const auto& key){return !allowed.contains(key);});
    std::set<QString> queued;
    for(std::size_t i=0;i<rows.size();++i){if(i||!ruleCursor){queued.insert(rows[i]);}}
    for(const auto& key:affected){if(queued.insert(key).second){rows.push_back(key);}}
    // One partially visited front may also need one later complete revisit.
    if(rows.size()>ui::LibraryModel::MaximumRows+1){return std::unexpected(error(storage::ErrorCode::Unsupported,"Dependency row work exceeds the current library bound"));}
    return {};
  }
  std::expected<void,storage::Error> stateChanges(const ConstraintWorkerFrame& frame){
    if(frame.managed.size()>ui::LibraryModel::MaximumRows){return std::unexpected(error(storage::ErrorCode::Unsupported,"State-tag observations exceed the library bound"));}
    std::map<QString,const engine::ManagedDownloadStatus*> managed;
    for(const auto& value:frame.managed){const auto key=QString::fromStdString(value.hash.hex());auto [at,inserted]=managed.emplace(key,&value);if(!inserted){at->second=nullptr;}}
    std::map<QString,StateSignature> next;std::set<QString> affected;
    std::set<QString> historySeen;
    const auto now=std::chrono::steady_clock::now();
    const bool engineKnown=frame.peerEngineIdentity&&frame.peerEngineIdentity->applicationRun&&
        frame.peerEngineIdentity->engineEpoch&&frame.peerEngineIdentity->profileSession==session;
    for(const auto& row:frame.library->items){
      std::array<std::uint64_t,7> value{row.providerAdmission,row.providerGeneration,0,0,0,0,0};
      const auto at=managed.find(row.hash);
      if(row.providerAdmission&&row.providerGeneration==session&&at!=managed.end()&&at->second
          &&at->second->admission==row.providerAdmission&&at->second->retained){
        const auto& m=*at->second;value[2]=static_cast<std::uint64_t>(m.phase)+1;
        value[3]=m.checked;value[4]=m.checked&&m.selectedComplete;value[5]=m.manuallyPaused;value[6]=1;
        if(engineKnown){
          const auto& identity=*frame.peerEngineIdentity;
          auto& history=stateHistories[row.hash];
          const auto before=history.history.members();
          if(history.run!=identity.applicationRun||history.epoch!=identity.engineEpoch||history.admission!=m.admission){
            history=StateHistory{identity.applicationRun,identity.engineEpoch,m.admission,{}};
          }
          history.history.observe(m,now);historySeen.insert(row.hash);
          if(statesStarted&&before!=history.history.members()){affected.insert(row.hash);}
        }
      }
      const StateSignature observed{value,row.originalValues[5].toString()}; // Wakeup hint only, never category fact authority.
      if(!next.emplace(row.hash,observed).second){return std::unexpected(error(storage::ErrorCode::Conflict,"State-tag library identity is ambiguous"));}
      const auto old=observedStates.find(row.hash);if(statesStarted&&(old==observedStates.end()||old->second!=observed)){affected.insert(row.hash);}
    }
    for(const auto& [key,history]:stateHistories){(void)history;if(!historySeen.contains(key)){affected.insert(key);}}
    std::erase_if(stateHistories,[&](const auto& item){return !historySeen.contains(item.first);});
    std::erase_if(trackerBirthUncertain,[&](const auto& hash){return !next.contains(hash);});
    observedStates=std::move(next);statesStarted=true;auto untagged=queueUntaggedRows(affected,*frame.library);if(!untagged){return untagged;}return queueChangedRows(affected,*frame.library);
  }
  std::expected<void,storage::Error> membershipChanges(const core::SavedDownloadTags& tags,const ui::LibraryPresentation& library){
    if(!membershipSource){membershipSource=tags.source;return {};}
    if(membershipSource==tags.source){return {};}
    if(membershipSource->originalBytes()==tags.source->originalBytes()){membershipSource=tags.source;return {};}
    auto previous=core::readSavedDownloadTags(membershipSource->originalBytes());
    if(!previous){return std::unexpected(error(storage::ErrorCode::Conflict,"Previous dependency source cannot be revalidated"));}
    std::set<std::pair<std::int32_t,QString>> difference;
    const auto toggle=[&](const core::SavedDownloadTags& value){for(const auto& row:value.tags){
      if(!row.id||row.duplicateId||row.idKey!=std::to_string(*row.id)){continue;}
      for(const auto& hash:row.uniqueHashes){const auto item=std::pair{*row.id,QString::fromStdString(hash.hex())};
        if(!difference.erase(item)){difference.insert(item);}}
    }};
    toggle(*previous);toggle(tags);
    std::set<QString> allowed,affected;
    for(const auto& row:library.items){allowed.insert(row.hash);}
    for(const auto& item:difference){if(allowed.contains(item.second)){affected.insert(item.second);}}
    // Original membership callbacks run all rules for the changed DM only.
    // Reuse this worker's normal queue, not a new event/newness authority.
    auto untagged=queueUntaggedRows(affected,library);if(!untagged){return untagged;}
    auto queuedResult=queueChangedRows(affected,library);if(!queuedResult){return queuedResult;}
    membershipSource=tags.source;return {};
  }
  static const storage::PreparedTagRateSource* assignmentRates(const ConstraintWorkerFrame& frame){
    return frame.assignmentObservation&&frame.assignmentObservation->rates?&*frame.assignmentObservation->rates:nullptr;
  }
  static bool needsAssignment(const storage::ManualTagSnapshot& snapshot,std::int32_t id){
    const auto* type=snapshot.tags()->source->find("3");const auto* record=type?type->find(std::to_string(id)):nullptr;
    return record&&!core::ordinarySavedTagMembershipEffects(*snapshot.tags(),*record)&&core::rateReadyLifecycleSavedTagMembershipEffects(*snapshot.tags(),*record);
  }
  const storage::TagAssignmentObservation* assignmentFor(ConstraintWorkerUpdate& result,
      const ConstraintWorkerFrame& frame,const storage::ManualTagSnapshot& snapshot,const core::TorrentHash& hash,
      std::int32_t id,bool add,ConstraintAssignmentKind kind,std::span<const std::int32_t> cohort={}){
    std::vector<std::int32_t> rateTags;
    const auto capture=[&](std::int32_t tag){
      const auto* type=snapshot.tags()->source->find("3");const auto* record=type?type->find(std::to_string(tag)):nullptr;
      if(record){auto rate=core::readSavedTagRateLimits(*record);if(rate&&!rate->unlimited()){rateTags.push_back(tag);}}
    };
    if(cohort.empty()){capture(id);}else{for(auto tag:cohort){capture(tag);}}
    if(rateTags.size()>32){result.kept=unavailable("Assignment rate cohort exceeds32 tags");return nullptr;}

    const auto key=QString::fromStdString(hash.hex());const ui::LibraryItem* selected=nullptr;
    for(const auto& row:frame.library->items){if(row.hash==key){if(selected){result.kept=unavailable("Assignment library target is ambiguous");return nullptr;}selected=&row;}}
    if(!selected){result.kept=unavailable("Assignment awaits a current library row");return nullptr;}
    const ConstraintFactStamp stamp{{session,{key,frame.library->revision},selected->providerAdmission,selected->providerGeneration},frame.sequence};
    const auto& reply=frame.assignmentObservation;
    const bool match=reply&&reply->request.kind==kind&&reply->request.stamp.binding==stamp.binding
        &&reply->request.stamp.sequence<frame.sequence&&reply->request.stamp.sequence==frame.sequence-1
        &&reply->request.sourceRevision==frame.sourceRevision&&reply->request.tag==id&&reply->request.add==add&&reply->request.rateTags==rateTags;
    if(!match){result.assignmentRequest=ConstraintAssignmentRequest{stamp,frame.sourceRevision,id,add,kind,rateTags};return nullptr;}
    if(!reply->value||!frame.assignmentOwner||(!rateTags.empty()&&!reply->rates)){
      result.kept=unavailable("Assignment actions await a supported normal download owner");
      if(reply->retry){result.assignmentRequest=ConstraintAssignmentRequest{stamp,frame.sourceRevision,id,add,kind,rateTags};}
      return nullptr;
    }
    const auto& value=*reply->value;
    if(value.hash!=hash||value.session!=session||value.sourceRevision!=frame.sourceRevision
        ||value.admission!=stamp.binding.providerAdmission||value.generation!=stamp.binding.providerGeneration){
      result.kept=unavailable("Assignment observation belongs to a different current download/source/provider");return nullptr;
    }
    return &value;
  }
#include "tracker_tag_owner.inc"
#include "tracker_tag_birth_owner.inc"
#include "untagged_tag_owner.inc"
#include "newonly_tag_dependencies.inc"
#include "peer_set_tag_source.inc"
  bool refresh(const core::SavedDownloadTags& tags){
    if(cachedSource==tags.source){return false;}
    const auto* manual=tags.source->find("3");std::vector<Rule> next;bool changed=false;
    for(const auto& row:tags.tags){
      if(!row.id||row.duplicateId||row.membersMalformed||!manual){continue;}
      const auto& record=manual->children[row.sourceIndex*2+1];const auto* config=record.find("c");
      if(!config||!config->find("pp.constraint")){continue;}
      const bool ordinaryGroup=core::ordinarySavedTagGroup(tags,record);
      // Audited display metadata fires checkStuff(), not apply/recompile.
      // Neither is a rule version or a new-download event boundary in cb8.
      const bool ordinaryMetadata=ordinaryGroup&&bool(core::readSavedTagColour(record))&&compatibleConfiguration(record,ordinaryGroup);
      std::string raw=ordinaryMetadata?"0d":"1d";
      for(std::size_t i=0;i<config->children.size();i+=2){if(config->children[i].bytes!="gr"&&config->children[i].bytes!="col.rgb"&&!core::savedTagSupportedRateProperty(config->children[i].bytes,config->children[i+1])&&!core::savedTagLifecycleProperty(config->children[i].bytes,config->children[i+1])&&!core::savedTagDisplayMetadata(config->children[i].bytes,config->children[i+1])){
        raw+=tags.source->originalBytes().substr(config->children[i].begin,config->children[i+1].end-config->children[i].begin);
      }}
      raw+='e';
      const auto old=std::find_if(rules.begin(),rules.end(),[&](const auto& value){return value.id==*row.id&&value.configuration==raw;});
      if(old!=rules.end()){next.push_back(*old);continue;}
      Rule value;value.id=*row.id;value.configuration=raw;
      auto parsed=property(record);
      if(!compatibleConfiguration(record,ordinaryGroup)){value.problem=unavailable("Tag has unsupported configured actions or properties");}
      else if(!parsed){value.problem=parsed.error();}
      else{value.property=std::move(*parsed);
      }
      next.push_back(std::move(value));changed=true;
    }
    if(next.size()!=rules.size()){changed=true;}
    else{for(std::size_t i=0;i<next.size();++i){if(next[i].id!=rules[i].id){changed=true;break;}}}
    if(changed){dispatchRules.reset();timeRows.clear();timeRules.clear();timeRowCursor=timeRuleCursor=0;timePending=false;}
    rules=std::move(next);cachedSource=tags.source;
    std::erase_if(compiled,[&](auto id){return std::none_of(rules.begin(),rules.end(),[&](const auto& value){return value.id==id&&value.expression.has_value();});});
    return changed;
  }
};
TagConstraintWorker::TagConstraintWorker(std::unique_ptr<Impl> value):impl_(std::move(value)){}
TagConstraintWorker::~TagConstraintWorker()=default;
std::expected<std::unique_ptr<TagConstraintWorker>,storage::Error> TagConstraintWorker::activate(
    const storage::ProfileLease& lease,std::uint64_t session){
  if(!session){return std::unexpected(error(storage::ErrorCode::Conflict,"Normal profile session required"));}
  auto mode=storage::TagManagerStartupMode::capture(lease);if(!mode){return std::unexpected(mode.error());}
  return activate(lease,session,*mode);
}
std::expected<std::unique_ptr<TagConstraintWorker>,storage::Error> TagConstraintWorker::activate(
    const storage::ProfileLease& lease,std::uint64_t session,const storage::TagManagerStartupMode& mode){
  try{
    if(!mode.matches(lease)){return std::unexpected(error(storage::ErrorCode::Conflict,"Tag startup setting belongs to another profile owner"));}
    if(!session){return std::unexpected(error(storage::ErrorCode::Conflict,"Normal profile session required"));}
    auto held=lease.retain();if(!held){return std::unexpected(held.error());}
    auto impl=std::make_unique<Impl>();impl->lease=std::move(*held);impl->session=session;impl->enabledAtOpen=mode.enabled();impl->stockTagLocale=mode.stockTagLocale();
    return std::unique_ptr<TagConstraintWorker>(new TagConstraintWorker(std::move(impl)));
  }catch(const std::bad_alloc&){return std::unexpected(error(storage::ErrorCode::Io,"Insufficient memory activating constraint worker"));}
}
void TagConstraintWorker::close() noexcept{if(impl_){impl_->closed=true;impl_->untaggedPending.clear();impl_->untaggedUncertain.clear();impl_->untaggedFlags.clear();impl_->untaggedEnabled.clear();impl_->untaggedDisabled.clear();impl_->untaggedSource.reset();impl_->trackerBirths.clear();impl_->trackerBirthUncertain.clear();impl_->trackerWork.clear();impl_->trackerProperties.clear();impl_->trackerUncertain.clear();impl_->trackerSource.reset();impl_->newDownloads.clear();impl_->dispatchRules.reset();impl_->ruleVersions.clear();impl_->lastInternalNamespace.reset();impl_->lease.reset();}}
std::expected<bool,storage::Error> TagConstraintWorker::recordTrackerPropertyEvent(storage::ManualTagStore& store,
    const storage::ManualTagSnapshot& snapshot,std::int32_t id,ui::Presentation library,TrackerPropertyEvent event,std::stop_token stop){
  try{
    auto& self=*impl_;
    if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Tracker property event cancelled"));}
    if(self.closed||self.worker!=std::this_thread::get_id()||!self.lease||!store.sharesOwnership(*self.lease)
        ||!library||!library->revision||library->items.size()>ui::LibraryModel::MaximumRows){return std::unexpected(error(storage::ErrorCode::Conflict,"Tracker property event has no current owning session"));}
    auto latest=store.snapshot();if(!latest){return std::unexpected(latest.error());}
    if(!snapshot.valid()||!snapshot.writable()||!snapshot.tags()||latest->generation()!=snapshot.generation()||!latest->sameSource(snapshot)){
      return std::unexpected(error(storage::ErrorCode::Conflict,"Tracker property saved source is stale"));}
    if(!self.enabledAtOpen){return false;}
    auto refreshed=self.refreshTrackers(*snapshot.tags(),*library);if(!refreshed){return std::unexpected(refreshed.error());}
    const auto* definition=tag(*snapshot.tags(),id);auto names=definition?self.trackerNames(*snapshot.tags(),*definition):std::nullopt;
    if(!names){if(event==TrackerPropertyEvent::Sync){return std::unexpected(error(storage::ErrorCode::Unsupported,"Saved tracker filters are unavailable or unsupported"));}return false;}
    // Original explicit sync does nothing for an empty filter. A changed save
    // that cleared a nonempty filter must still reconcile/removal-pass.
    if(event==TrackerPropertyEvent::Sync&&names->empty()){return false;}
    auto targets=std::make_shared<std::vector<QString>>();std::set<QString> unique;
    for(const auto& row:library->items){if(!unique.insert(row.hash).second){return std::unexpected(error(storage::ErrorCode::Conflict,"Tracker property library identities are ambiguous"));}targets->push_back(row.hash);}
    auto pending=self.trackerWork;std::erase_if(pending,[&](const auto& work){return work.id==id;});
    if(pending.size()==64){return std::unexpected(error(storage::ErrorCode::Unsupported,"Tracker property event bound reached"));}
    const auto first=definition->uniqueHashes.empty()?targets->size():0;
    pending.push_back({id,*names,std::move(targets),first});
    if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Tracker synchronisation cancelled before queue acceptance"));}
    self.trackerWork=std::move(pending);self.trackerUncertain.erase(id);return true;
  }catch(const std::bad_alloc&){return std::unexpected(error(storage::ErrorCode::Io,"Insufficient memory recording a changed tracker property"));}
}
std::expected<bool,storage::Error> TagConstraintWorker::recordUntaggedPropertyChange(storage::ManualTagStore& store,
    const storage::ManualTagSnapshot& before,const storage::ManualTagSnapshot& after,ui::Presentation library){
  try{auto& self=*impl_;
    if(self.closed||self.worker!=std::this_thread::get_id()||!self.lease||!store.sharesOwnership(*self.lease)
        ||!library||library->items.size()>ui::LibraryModel::MaximumRows||!before.valid()||!before.tags()){
      return std::unexpected(error(storage::ErrorCode::Conflict,"Untagged property requires its actual worker/preimage"));}
    auto latest=store.snapshot();if(!latest){return std::unexpected(latest.error());}
    if(!after.valid()||!after.writable()||!after.tags()||!after.sameSource(*latest)||after.generation()!=latest->generation()
        ||before.generation()==std::numeric_limits<std::uint64_t>::max()||after.generation()!=before.generation()+1){
      return std::unexpected(error(storage::ErrorCode::Conflict,"Untagged changed-save lineage is stale"));}
    if(!self.enabledAtOpen){return false;}
    // Actual preimage prevents a quick true->false save from losing its disable
    // transition before the first timer turn. No noop can call this producer.
    auto old=self.refreshUntagged(*before.tags(),*library);if(!old){return std::unexpected(old.error());}
    auto next=self.refreshUntagged(*after.tags(),*library);if(!next){return std::unexpected(next.error());}return true;
  }catch(const std::bad_alloc&){return std::unexpected(error(storage::ErrorCode::Io,"Insufficient memory recording changed Untagged property"));}
}
std::expected<bool,storage::Error> TagConstraintWorker::recordIntake(storage::ManualTagStore& store,
    const storage::ManualTagSnapshot& snapshot,const core::TorrentHash& hash,std::uint64_t event){
  try{
    auto& self=*impl_;
    if(self.closed||self.worker!=std::this_thread::get_id()||!self.lease||!store.sharesOwnership(*self.lease)){
      return std::unexpected(error(storage::ErrorCode::Conflict,"New-download event requires its actual worker/profile"));
    }
    auto latest=store.snapshot();if(!latest){return std::unexpected(latest.error());}
    if(!snapshot.valid()||!snapshot.writable()||!snapshot.tags()||snapshot.generation()!=latest->generation()||!snapshot.sameSource(*latest)){
      return std::unexpected(error(storage::ErrorCode::Conflict,"New-download tag source is stale or read-only"));
    }
    if(!self.enabledAtOpen){return false;}
    if(self.refresh(*snapshot.tags())){self.rows.clear();self.rowCursor=self.ruleCursor=0;self.started=false;}
    auto birthRules=self.trackerBirthRules(*snapshot.tags());if(!birthRules){return std::unexpected(birthRules.error());}
    const bool hasConstraints=std::any_of(self.rules.begin(),self.rules.end(),[](const auto& rule){return rule.property.configured;});
    if(!hasConstraints&&(*birthRules)->empty()){return false;}
    auto gate=self.lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}
    auto inventory=storage::ConfigFile::load(*self.lease,"downloads.config");if(!inventory){return std::unexpected(inventory.error());}
    auto library=core::readLibrary(inventory->document());
    if(inventory->recoverySource()!=storage::RecoverySource::Primary||!library||!library->issues.empty()
        ||std::count_if(library->downloads.begin(),library->downloads.end(),[&](const auto& row){return row.hash&&*row.hash==hash&&row.persistent;})!=1){
      return std::unexpected(error(storage::ErrorCode::Conflict,"New-download event has no unique durable saved row"));
    }
    auto owner=storage::TorrentStateStore::open(*self.lease);if(!owner){return std::unexpected(owner.error());}
    auto state=owner->load(*self.lease,hash);if(!state){return std::unexpected(state.error());}
    const auto added=addedTime(state->document());if(state->recoveredBackup()||!added){return std::unexpected(error(storage::ErrorCode::Conflict,"Original saved added time is unavailable"));}
    bool recordedBirth=false;
    if(!(*birthRules)->empty()){
      const auto birth=self.trackerBirths.record(hash,*added,event,*birthRules);
      if(!birth){return std::unexpected(error(storage::ErrorCode::Unsupported,"Tracker birth queue is unavailable or exceeds its bound"));}
      recordedBirth=*birth==detail::TrackerTagBirths::Recorded::Added;
      if(recordedBirth){self.trackerBirthUncertain.erase(QString::fromStdString(hash.hex()));}
    }
    if(!hasConstraints){return recordedBirth;}
    const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto result=self.newDownloads.record(hash,*added,wall,std::chrono::steady_clock::now(),event);
    if(result==detail::NewDownloadEvents::Added::Full||result==detail::NewDownloadEvents::Added::Unavailable){return std::unexpected(error(storage::ErrorCode::Unsupported,"New-download event work bound reached"));}
    if(result==detail::NewDownloadEvents::Added::Recorded){
      // Queue only this real creation for ordinary rules too. Do not turn a
      // new-only event into a full periodic reapply when that is disabled.
      self.rows.erase(self.rows.begin(),self.rows.begin()+static_cast<std::ptrdiff_t>(self.rowCursor));self.rowCursor=0;
      const auto key=QString::fromStdString(hash.hex());
      if(!self.rows.empty()&&self.rows.front()==key){self.ruleCursor=0;}
      std::erase(self.rows,key);self.rows.push_back(key);
    }
    return recordedBirth||result==detail::NewDownloadEvents::Added::Recorded;
  }catch(const std::bad_alloc&){return std::unexpected(error(storage::ErrorCode::Io,"Insufficient memory recording actual new download"));}
}
std::expected<ConstraintWorkerUpdate,storage::Error> TagConstraintWorker::step(
    storage::ManualTagStore& store,const storage::ManualTagSnapshot& snapshot,
    const ConstraintWorkerFrame& frame,std::stop_token stop){
  try{
    auto& self=*impl_;ConstraintWorkerUpdate result;result.snapshot=snapshot;
    if(self.worker!=std::this_thread::get_id()||self.closed||!self.lease||frame.session!=self.session
        ||!frame.sequence||frame.sequence<=self.lastSequence||!frame.library||!frame.library->revision
        ||frame.library->items.size()>ui::LibraryModel::MaximumRows||frame.peerObservations.size()>32){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint worker lifetime/turn is stale"));}
    self.lastSequence=frame.sequence;
    if(self.peerDefinitionsRevision!=frame.sourceRevision){self.peerSets.reset();self.peerDefinitionsDigest.clear();self.peerDefinitionsRevision=0;}
    if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Constraint turn cancelled"));}
    if(!store.sharesOwnership(*self.lease)){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint store belongs to another profile OFD"));}
    auto latest=store.snapshot();if(!latest){return std::unexpected(latest.error());}
    if(!snapshot.valid()||latest->generation()!=snapshot.generation()||latest->rawBytes()!=snapshot.rawBytes()
        ||!snapshot.writable()||!snapshot.tags()){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint tag preimage is stale or read-only"));}
    if(!self.enabledAtOpen){result.kept=unavailable("Original tag manager is disabled at startup");return result;}
    if(storage::hasPendingCategoryDeletion(*self.lease)||storage::hasPendingFileOperation(*self.lease)){
      result.kept=unavailable("Automatic tags require settled category/file journals");return result;}
    if(frame.internalNamespace&&!frame.internalNamespace->current(*self.lease,self.session)){result.kept=unavailable("Internal tag namespace frame is stale/foreign");return result;}
    const bool internalChanged=bool(frame.internalNamespace)!=bool(self.lastInternalNamespace)||(frame.internalNamespace&&self.lastInternalNamespace&&!frame.internalNamespace->sameRevision(*self.lastInternalNamespace));
    self.lastInternalNamespace=frame.internalNamespace;if(internalChanged){self.fullPending=true;}
    const bool rulesChanged=self.refresh(*snapshot.tags());const auto now=std::chrono::steady_clock::now();
    if(rulesChanged){self.rows.clear();self.rowCursor=self.ruleCursor=0;}
    if(rulesChanged){self.membershipSource=snapshot.tags()->source;}
    else{auto membership=self.membershipChanges(*snapshot.tags(),*frame.library);
      if(!membership){return std::unexpected(membership.error());}}
    auto stateUpdates=self.stateChanges(frame);if(!stateUpdates){return std::unexpected(stateUpdates.error());}
    self.newDownloads.expire(now);
    auto trackerSource=self.refreshTrackers(*snapshot.tags(),*frame.library);if(!trackerSource){return std::unexpected(trackerSource.error());}
    auto untaggedSource=self.refreshUntagged(*snapshot.tags(),*frame.library);if(!untaggedSource){return std::unexpected(untaggedSource.error());}
    const std::array<bool,3> automationReady{!self.trackerWork.empty(),self.trackerBirths.size()!=0,!self.untaggedPending.empty()};
    const bool pendingAutomation=std::ranges::any_of(automationReady,[](bool value){return value;});
    const auto echoKind=frame.assignmentObservation?std::optional{frame.assignmentObservation->request.kind}:std::nullopt;
    const bool echoAutomation=echoKind&&*echoKind!=ConstraintAssignmentKind::Constraint;
    const bool automationTurn=pendingAutomation&&(echoAutomation||(!echoKind&&(self.nextTrackerTurn||self.rules.empty())));
    if(pendingAutomation&&!echoKind){self.nextTrackerTurn=!automationTurn;}
    if(automationTurn){for(unsigned offset=0;offset<3;++offset){
      const auto lane=echoAutomation?(*echoKind==ConstraintAssignmentKind::Tracker?0U:*echoKind==ConstraintAssignmentKind::TrackerBirth?1U:2U):(self.nextTagAutomation+offset)%3;
      if(!automationReady[lane]){if(echoAutomation){break;}continue;}self.nextTagAutomation=(lane+1)%3;
      auto update=lane==0?self.trackerStep(store,snapshot,frame,stop):lane==1?self.trackerBirthStep(store,snapshot,frame,stop):self.untaggedStep(store,snapshot,frame,stop);
      if(!update){return std::unexpected(update.error());}if(update->evaluated||update->changed||update->assignmentRequest){return update;}break;
    }}
    if(self.rules.empty()){self.newDownloads.clear();self.rowCursor=self.rows.size();self.started=false;self.cadence.reset();self.fullPending=false;return result;}
    self.cadence.start(now);
    const bool pendingNormal=!self.started||rulesChanged||self.rowCursor<self.rows.size()||self.fullPending;
    const bool pendingTime=self.timePending||self.timeRowCursor<self.timeRows.size();
    if(!pendingNormal&&!pendingTime&&self.newDownloads.empty()&&!self.cadence.ready(now)&&!frame.refreshSettings){return result;}
    // All provider values are already owning. From here until apply returns,
    // only synchronous FS, pure parser/evaluator/editor and CAS operations run.
    auto gate=self.lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}
    if(!frame.sourceRevision||gate->revision()!=frame.sourceRevision){
      result.kept=unavailable("Constraint source revision changed before its held turn");return result;
    }
    if(frame.refreshSettings||!self.settingsKnown){
      auto config=storage::ConfigFile::load(*self.lease,"biglybt.config");
      if(!config&&config.error().code!=storage::ErrorCode::Missing){return std::unexpected(config.error());}
      if(config&&config->recoverySource()!=storage::RecoverySource::Primary){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint settings recovery requires explicit review"));}
      std::int32_t seconds=30;
      if(config){if(const auto* value=config->document().find("Tag Auto Full Reapply Period Secs")){
        if(value->type!=core::Node::Type::Integer){return std::unexpected(error(storage::ErrorCode::InvalidFormat,"Malformed automatic-tag reapply period"));}
        seconds=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value->integer));
      }}
      self.reapply=std::chrono::seconds(seconds<=0?0:std::max(10,seconds));self.settingsKnown=true;
    }
    const auto due=self.cadence.poll(std::chrono::steady_clock::now(),static_cast<std::int32_t>(self.reapply.count()));
    self.fullPending=self.fullPending||due.full;self.timePending=self.timePending||due.time;
    const bool normalWork=!self.started||rulesChanged||self.rowCursor<self.rows.size()||self.fullPending;
    if(normalWork&&self.rowCursor>=self.rows.size()){
      self.rows.clear();self.rowCursor=self.ruleCursor=0;
      std::set<QString> unique;
      for(const auto& row:frame.library->items){if(!unique.insert(row.hash).second){return std::unexpected(error(storage::ErrorCode::Conflict,"Ambiguous constraint library identity"));}self.rows.push_back(row.hash);}
      // The original timer's apply_done is separate from startup assignment.
      if(self.fullPending){self.cadence.fullQueued();}
      self.started=true;self.fullPending=false;
    }
    if(self.timePending&&self.timeRowCursor>=self.timeRows.size()){
      self.timeRows.clear();self.timeRules.clear();self.timeRowCursor=self.timeRuleCursor=0;self.timePending=false;
      for(const auto& rule:self.rules){if(rule.timeDependent&&rule.property.configured&&rule.property.enabled&&!rule.problem){self.timeRules.push_back(rule.id);}}
      if(!self.timeRules.empty()){
        std::set<QString> unique;
        for(const auto& row:frame.library->items){if(!unique.insert(row.hash).second){return std::unexpected(error(storage::ErrorCode::Conflict,"Ambiguous timed constraint library identity"));}self.timeRows.push_back(row.hash);}
      }
    }
    const bool timeWork=self.timeRowCursor<self.timeRows.size();
    const bool eventTurn=!self.newDownloads.empty()&&(self.nextEventTurn||!(normalWork||timeWork));
    self.nextEventTurn=!eventTurn;
    const bool timedTurn=!eventTurn&&timeWork&&(self.nextTimedTurn||!normalWork);
    if(!eventTurn){self.nextTimedTurn=!timedTurn;}
    if(!eventTurn&&!timedTurn&&self.rowCursor>=self.rows.size()){return result;}
    auto inventory=storage::ConfigFile::load(*self.lease,"downloads.config");if(!inventory){return std::unexpected(inventory.error());}
    if(inventory->recoverySource()!=storage::RecoverySource::Primary){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint inventory recovery requires explicit review"));}
    const auto library=core::readLibrary(inventory->document());if(!library||!library->issues.empty()){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint inventory is ambiguous"));}
    std::optional<CategoryFactSource> categorySource;
    if(auto source=categoryFactSource(*self.lease);source){
      auto definitions=core::readSavedCategories(source->bytes);
      if(definitions){
        bool ready=self.categoryDefinitions==source->bytes;
        if(!ready){std::vector<core::tags::runtime::Text> names;
          for(const auto& row:definitions->categories){if(row.kind==core::SavedCategoryKind::User){names.push_back(QString::fromUtf8(row.name.data(),qsizetype(row.name.size())).toStdU16String());}}
          ready=bool(self.categoryRoster.observe(names));if(ready){self.categoryDefinitions=source->bytes;}
        }
        if(ready){categorySource=std::move(*source);}
      }
    }
    std::optional<ConstraintNamedMetricCensus> namedCensus;bool namedCensusAttempted=false;
    const auto captureNamedCensus=[&]{
      if(namedCensusAttempted){return;}namedCensusAttempted=true;
      if(library->downloads.size()>ui::LibraryModel::MaximumRows){return;}
      auto data=std::make_shared<ConstraintNamedMetricCensus::Data>();
      data->session=self.session;data->sequence=frame.sequence;data->libraryRevision=frame.library->revision;
      data->sourceRevision=frame.sourceRevision;data->source=snapshot;
      std::set<QString> visible;for(const auto& row:frame.library->items){if(!visible.insert(row.hash).second){return;}}
      std::set<std::string> identities;
      for(const auto& row:library->downloads){if(!row.persistent){continue;}
        if(!row.hash){return;}const auto key=row.hash->hex();
        if(!identities.insert(key).second||!visible.contains(QString::fromStdString(key))){return;}
        if(row.hash->bytes().size()==20){data->primaryV1.push_back(key);}
      }
      std::ranges::sort(data->primaryV1);
      // Same-turn values from the actual Host-owned engine,not saved/display
      // positions. Missing/failed/foreign inputs cannot be inferred as zero.
      if(frame.peerEngineIdentity&&frame.peerEngineIdentity->applicationRun&&frame.peerEngineIdentity->engineEpoch&&frame.peerEngineIdentity->profileSession==self.session){
        std::map<QString,const engine::ManagedDownloadStatus*> observed;
        for(const auto& value:frame.managed){auto [at,unique]=observed.emplace(QString::fromStdString(value.hash.hex()),&value);if(!unique){at->second=nullptr;}}
        for(const auto& row:frame.library->items){const auto found=observed.find(row.hash);
          if(!row.providerAdmission||row.providerGeneration!=self.session||found==observed.end()||!found->second){continue;}
          const auto& value=*found->second;
          if(value.admission!=row.providerAdmission||!value.retained||value.failure||value.queuePositionFailure
              ||(value.unavailableQueueInputs&(1U<<5))||!value.queuePosition||!*value.queuePosition
              ||*value.queuePosition>std::uint32_t(std::numeric_limits<std::int32_t>::max())){continue;}
          data->positions.emplace_back(row.hash.toStdString(),static_cast<std::int32_t>(*value.queuePosition));
        }
        std::ranges::sort(data->positions);
      }
      namedCensus.emplace();namedCensus->data_=std::move(data);
    };
    bool capturedPeerObserved=false;
    std::optional<QString> captured;
    std::optional<ConstraintFactSnapshot> facts;
    std::optional<ConstraintFactSnapshot> tagScope;
    std::optional<ConstraintMembershipCohort> membershipCohort;
    std::optional<c::Problem> factProblem;
    std::optional<core::TorrentHash> selectedHash;
    std::string capturedStateBytes;std::optional<std::int64_t> capturedAdded;
    for(unsigned budget=0;budget<32&&result.evaluated<32&&(eventTurn?!self.newDownloads.empty():timedTurn?self.timeRowCursor<self.timeRows.size():self.rowCursor<self.rows.size());++budget){
      if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Constraint turn cancelled before publication"));}
      QString hashText;Impl::Rule* selectedRule=nullptr;std::optional<std::int64_t> eventAdded;
      std::optional<std::chrono::steady_clock::time_point> eventEnd;
      const auto rowBefore=self.rowCursor,ruleBefore=self.ruleCursor,timeRowBefore=self.timeRowCursor,timeRuleBefore=self.timeRuleCursor;
      const auto eventBefore=eventTurn?self.newDownloads.front().cursor:0;
      if(eventTurn){
        auto& event=self.newDownloads.front();
        if(!event.rules){event.rules=self.newRules();if(!event.rules){result.kept=unavailable("New-download rule versions are busy");break;}}
        if(event.cursor==event.rules->size()){self.newDownloads.finishTurn();continue;}
        const auto& key=(*event.rules)[event.cursor++];hashText=QString::fromStdString(event.hash.hex());eventAdded=event.addedMillis;eventEnd=event.end;
        const auto found=std::find_if(self.rules.begin(),self.rules.end(),[&](const auto& r){return r.id==key.id&&detail::sameNewDownloadRule(r.property,key.property);});
        if(found==self.rules.end()){continue;}selectedRule=&*found;
      }else if(timedTurn){
        hashText=self.timeRows[self.timeRowCursor];const auto id=self.timeRules[self.timeRuleCursor];
        if(++self.timeRuleCursor==self.timeRules.size()){self.timeRuleCursor=0;++self.timeRowCursor;}
        const auto found=std::find_if(self.rules.begin(),self.rules.end(),[&](const auto& rule){return rule.id==id;});
        if(found==self.rules.end()){continue;}selectedRule=&*found;
      }else{
        hashText=self.rows[self.rowCursor];selectedRule=&self.rules[self.ruleCursor];
        if(++self.ruleCursor==self.rules.size()){self.ruleCursor=0;++self.rowCursor;}
      }
      auto& rule=*selectedRule;
      ++result.evaluated;result.tag=rule.id;result.kept=rule.problem;
      if(!rule.expression&&!rule.problem&&rule.property.configured&&rule.property.enabled){
        auto expression=c::compile(rule.property.expression);
        if(!expression){rule.problem=expression.error();result.kept=rule.problem;}
        else{
          if(self.compiled.size()==128){const auto evicted=self.compiled.front();self.compiled.pop_front();
            for(auto& value:self.rules){if(value.id==evicted){value.expression.reset();break;}}
          }
          rule.timeDependent=expression->dependsOnTime();rule.expression=std::move(*expression);self.compiled.push_back(rule.id);
        }
      }
      if(!rule.expression){continue;} // Includes disabled, malformed and am=3.
      if(!rule.expression->requiredTagNames().empty()){self.loadPeerDefinitions(frame.sourceRevision);}

      if(!captured||*captured!=hashText||(self.needsPeerEndpoints(*rule.expression)&&facts&&!capturedPeerObserved)){
        captured=hashText;facts.reset();tagScope.reset();membershipCohort.reset();selectedHash.reset();capturedStateBytes.clear();capturedAdded.reset();
        factProblem=unavailable("Current constraint row facts could not be captured");result.kept=factProblem;
        const auto hash=constraintFactHash(hashText);if(!hash){factProblem=hash.error();result.kept=factProblem;continue;}
        const core::SavedDownload* saved=nullptr;bool duplicate=false;
        for(const auto& row:library->downloads){if(row.hash&&*row.hash==*hash){if(saved){duplicate=true;}saved=&row;}}
        if(duplicate||!saved||!saved->persistent){continue;}
        auto source=std::find_if(frame.library->items.begin(),frame.library->items.end(),[&](const auto& row){return row.hash==hashText;});
        if(source==frame.library->items.end()){continue;}
        // Capture only this actual selected row; duplicate identities were
        // checked in the complete supplied library and again in saved source.
        auto owning=std::make_shared<ui::LibraryPresentation>();owning->revision=frame.library->revision;
        owning->items.push_back(*source);auto& selected=owning->items.front();
        selected.savedPosition=saved->position;selected.savedSelected.reset();
        if(!selected.providerAdmission){selected.sent=saved->uploaded;}
        const ConstraintFactStamp stamp{{self.session,{hashText,owning->revision},selected.providerAdmission,selected.providerGeneration},frame.sequence};
        ConstraintFactInputs input;input.stockTagLocale=self.stockTagLocale;input.stamp=stamp;input.library=std::move(owning);
        if(frame.internalNamespace){input.internalTags=ConstraintInternalTagSource{stamp,*frame.internalNamespace};}
        if(selected.providerAdmission){input.queue=frame.queue;}
        bool duplicateProvider=false;
        for(const auto& value:frame.managed){if(value.hash==*hash){if(input.managed){duplicateProvider=true;}input.managed=value;}}
        for(const auto& value:frame.i2p){if(value.hash==*hash){if(input.i2p){duplicateProvider=true;}input.i2p=value;}}
        if(duplicateProvider){factProblem=unavailable("Ambiguous current constraint provider observations");result.kept=factProblem;continue;}
        if(input.managed){const auto history=self.stateHistories.find(hashText);
          if(history!=self.stateHistories.end()&&history->second.admission==stamp.binding.providerAdmission){
            input.downloadStates=ConstraintDownloadStateSource(stamp,history->second.history.members());
          }
        }
        auto stateStore=storage::TorrentStateStore::open(*self.lease);if(!stateStore){continue;}
        auto state=stateStore->load(*self.lease,*hash);if(!state||state->recoveredBackup()){continue;}
        capturedStateBytes=state->document().originalBytes();capturedAdded=addedTime(state->document());
        auto original=captureConstraintOriginalState(stamp,*state);if(!original){factProblem=original.error();result.kept=factProblem;continue;}input.original=std::move(*original);
        if(categorySource){auto assignment=core::readSavedCategoryAssignment(categorySource->bytes,capturedStateBytes);
          if(assignment){input.categories=ConstraintCategorySource{stamp,categoryFactType(self.categoryRoster,*assignment,self.stockTagLocale)};}}
        auto detail=engine::queryTorrent(*self.lease,*hash);if(!detail){continue;}
        auto files=std::make_shared<ui::FileDetails>();files->target=stamp.binding.target;
        for(const auto& value:detail->entries){files->items.push_back({value.index,QString::fromStdString(value.path),value.bytes,value.priority,value.skipped,value.padding});}
        // All name, geometry and selected file policy come from this held
        // original state, not a translated table state or a hash fallback.
        const auto name=detail->displayName&&!detail->displayName->empty()?*detail->displayName:detail->name;
        input.details=ui::Details{stamp.binding.target,QString::fromStdString(name),detail->bytes,std::move(files)};
        bool peerObserved=false;
        if(self.peerSets){input.peerSets=self.peerSource(stamp,frame,peerObserved);}
        const bool activePeer=input.managed&&input.managed->retained&&input.managed->checked&&!input.managed->failure
            &&(input.managed->phase==engine::ManagedPhase::Downloading||input.managed->phase==engine::ManagedPhase::Seeding);
        if(activePeer&&stamp.binding.providerGeneration==self.session&&self.needsPeerEndpoints(*rule.expression)&&!peerObserved){
          if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Peer-set observation cancelled"));}
          result.peerRequest=ConstraintPeerRequest{stamp,frame.sourceRevision,self.peerDefinitionsDigest};
          self.rowCursor=rowBefore;self.ruleCursor=ruleBefore;self.timeRowCursor=timeRowBefore;self.timeRuleCursor=timeRuleBefore;
          if(eventTurn){self.newDownloads.front().cursor=eventBefore;}
          return result;
        }
        capturedPeerObserved=peerObserved;
        auto fresh=captureConstraintFacts(stamp,input);if(!fresh){factProblem=fresh.error();result.kept=factProblem;continue;}
        facts=std::move(*fresh);selectedHash=*hash;factProblem.reset();
        auto cohortData=std::make_shared<ConstraintMembershipCohort::Data>();cohortData->stamp=stamp;cohortData->source=snapshot;
        const auto* sourceType=snapshot.tags()->source->find("3");
        for(const auto& candidate:self.rules){
          const auto* definition=tag(*snapshot.tags(),candidate.id);
          const auto* record=definition&&sourceType?sourceType->find(definition->idKey):nullptr;
          if(!record||!candidate.property.configured||!compatibleConfiguration(*record,core::ordinarySavedTagGroup(*snapshot.tags(),*record))
              ||!core::rateReadyLifecycleSavedTagMembershipEffects(*snapshot.tags(),*record)){continue;}
          cohortData->known.push_back(candidate.id);
          if(candidate.property.enabled&&!candidate.property.mode.newOnly){cohortData->roots.push_back(candidate.id);}
        }
        const bool birthPending=self.trackerBirths.contains(*selectedHash)||self.trackerBirthUncertain.contains(hashText);
        if(!birthPending){for(const auto& candidate:snapshot.tags()->tags){if(candidate.id&&self.trackerOwned(*candidate.id)){cohortData->known.push_back(*candidate.id);}}}
        for(const auto& candidate:snapshot.tags()->tags){if(candidate.id&&self.untaggedOwned(*candidate.id,hashText)){cohortData->known.push_back(*candidate.id);}}
        // These are lookup sets only; execution/registry order is untouched.
        std::ranges::sort(cohortData->known);std::ranges::sort(cohortData->roots);
        membershipCohort.emplace();membershipCohort->data_=std::move(cohortData);
      }
      if(!facts||!selectedHash){if(factProblem){result.kept=factProblem;}continue;}
      const auto* selected=tag(*snapshot.tags(),rule.id);if(!selected){continue;}
      const bool member=std::find(selected->uniqueHashes.begin(),selected->uniqueHashes.end(),*selectedHash)!=selected->uniqueHashes.end();
      // Base row facts are cached per hash; tag_age and selected-rule identity
      // belong to THIS rule and must never be inherited from another rule.
      const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      bool isNew=false;
      if(eventAdded){
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        isNew=capturedAdded==eventAdded&&eventEnd&&std::chrono::steady_clock::now()<*eventEnd&&detail::NewDownloadEvents::recent(*eventAdded,now);
        if(!isNew){result.kept=unavailable("New-download saved time changed or expired");self.newDownloads.front().cursor=self.newDownloads.front().rules->size();continue;}
      }
      c::Decision decision;std::optional<Impl::PreparedNewOnly> dependencyPlan;
      if(eventTurn&&rule.property.mode.newOnly&&isNew&&membershipCohort&&eventEnd&&!rule.expression->tagDependencies().empty()){
        auto prepared=self.prepareNewOnlyDependencies(store,snapshot,frame,*facts,*membershipCohort,rule.id,*selectedHash,*eventEnd,result.evaluated,stop);
        if(!prepared){result.kept=prepared.error();continue;}
        result.kept.reset();
        if(!prepared->plan.edits()){continue;}dependencyPlan=std::move(*prepared);
      }else{
        const auto* cohort=membershipCohort&&membershipCohort->ordinaryRoot(rule.id)?&*membershipCohort:nullptr;
        if(tagScope&&tagScope->hasConstraintMembershipCohort()!=bool(cohort)){tagScope.reset();}
        auto tagFacts=captureConstraintTagMembership(tagScope?*tagScope:*facts,facts->stamp(),store,*self.lease,snapshot,rule.id,frame.library,wall,nullptr,cohort);
        if(!tagFacts){result.kept=tagFacts.error();continue;}tagScope=*tagFacts;
        if(!rule.expression->namedTagRequests().empty()){
          if(std::ranges::any_of(rule.expression->namedTagRequests(),[](const auto& request){return request.count||request.position;})){captureNamedCensus();}
          auto named=captureConstraintNamedTagMetrics(*tagFacts,facts->stamp(),store,*self.lease,snapshot,
              rule.expression->namedTagRequests(),wall,namedCensus?&*namedCensus:nullptr);
          if(!named){result.kept=named.error();continue;}tagFacts=std::move(named);
        }
        const auto evaluated=evaluateCurrentConstraint(*rule.expression,facts->stamp(),*tagFacts,store,*self.lease,snapshot);
        decision=c::project(evaluated,rule.property,isNew,member);result.kept=decision.problem;
        if(decision.membership==c::Membership::Keep){continue;}
      }
      std::optional<std::int32_t> assignmentTag;
      if(dependencyPlan){for(const auto id:dependencyPlan->additions){if(Impl::needsAssignment(snapshot,id)&&!detail::privateRateAllowed(frame,snapshot,*selectedHash,id)){assignmentTag=id;break;}}}
      else if(Impl::needsAssignment(snapshot,rule.id)&&!detail::privateRateAllowed(frame,snapshot,*selectedHash,rule.id)){assignmentTag=rule.id;}
      const storage::TagAssignmentObservation* assignmentValue=nullptr;
      if(assignmentTag){
        assignmentValue=self.assignmentFor(result,frame,snapshot,*selectedHash,*assignmentTag,dependencyPlan||decision.membership==c::Membership::Add,ConstraintAssignmentKind::Constraint,dependencyPlan?std::span<const std::int32_t>{dependencyPlan->additions}:std::span<const std::int32_t>{});
        if(!assignmentValue){
          if(result.assignmentRequest){
            self.rowCursor=rowBefore;self.ruleCursor=ruleBefore;self.timeRowCursor=timeRowBefore;self.timeRuleCursor=timeRuleBefore;
            if(eventTurn){self.newDownloads.front().cursor=eventBefore;}
            return result;
          }
          continue;
        }
      }
      if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Constraint turn cancelled before tag CAS"));}
      // Repeat source-byte/current-path checks immediately before the separate
      // tag CAS. The held gate serializes cooperative writers; the existing
      // adapters retain their documented noncooperating pathname-swap limit.
      auto finalInventory=storage::ConfigFile::load(*self.lease,"downloads.config");
      if(!finalInventory||finalInventory->recoverySource()!=storage::RecoverySource::Primary
          ||finalInventory->document().originalBytes()!=inventory->document().originalBytes()){
        return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint inventory preimage changed before publication"));
      }
      auto finalStore=storage::TorrentStateStore::open(*self.lease);
      if(!finalStore){return std::unexpected(finalStore.error());}
      auto finalState=finalStore->load(*self.lease,*selectedHash);
      if(!finalState||finalState->recoveredBackup()||finalState->document().originalBytes()!=capturedStateBytes){
        return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint original-state preimage changed before publication"));
      }
      if(categorySource){auto finalCategories=categoryFactSource(*self.lease);
        if(!finalCategories||*finalCategories!=*categorySource){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint category definition preimage changed before publication"));}}
      if(frame.internalNamespace&&!frame.internalNamespace->current(*self.lease,self.session)){result.kept=unavailable("Internal tag namespace changed before tag CAS");return result;}
      if(!self.peerDefinitionsUnchanged()){result.kept=unavailable("Peer-set definitions changed before tag CAS");return result;}
      if(gate->revision()!=frame.sourceRevision){result.kept=unavailable("Constraint source revision changed before tag CAS");return result;}
      if(eventAdded){
        const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if(!eventEnd||std::chrono::steady_clock::now()>=*eventEnd||!detail::NewDownloadEvents::recent(*eventAdded,wall)){
          result.kept=unavailable("New-download event expired before publication");self.newDownloads.front().cursor=self.newDownloads.front().rules->size();self.newDownloads.finishTurn();return result;
        }
      }
      const auto recentBefore=self.recentAdds.size();
      bool reservedAdd=false;
      if(decision.membership==c::Membership::Add){
        const auto monotone=std::chrono::steady_clock::now();
        std::erase_if(self.recentAdds,[&](const auto& value){return monotone-value.at>=std::chrono::seconds(1);});
        if(self.recentAdds.size()==32||std::any_of(self.recentAdds.begin(),self.recentAdds.end(),[&](const auto& value){return value.id==rule.id&&value.hash==*selectedHash;})){
          result.kept=unavailable("Recent automatic addition deferred");continue;
        }
        // Allocate before publication; never report a committed save as an
        // allocation error while recording this short-lived observation.
        self.recentAdds.push_back({rule.id,*selectedHash,monotone});reservedAdd=true;
      }
      if(dependencyPlan){try{for(const auto id:dependencyPlan->additions){self.recentAdds.push_back({id,*selectedHash,std::chrono::steady_clock::now()});}}
        catch(...){while(self.recentAdds.size()>recentBefore){self.recentAdds.pop_back();}throw;}}
      const auto added=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      core::SavedTagEdit edit=decision.membership==c::Membership::Add
          ?core::SavedTagEdit{core::SavedTagAddMember{rule.id,*selectedHash,added}}
          :core::SavedTagEdit{core::SavedTagRemoveMember{rule.id,*selectedHash}};
      std::vector<storage::ManualTagMembershipEdit> privateEdits;
      if(dependencyPlan){for(const auto id:dependencyPlan->additions){privateEdits.emplace_back(core::SavedTagAddMember{id,*selectedHash,added});}}
      else if(const auto* add=std::get_if<core::SavedTagAddMember>(&edit)){privateEdits.emplace_back(*add);}
      else if(const auto* remove=std::get_if<core::SavedTagRemoveMember>(&edit)){privateEdits.emplace_back(*remove);}
      auto committed=detail::publishCurrentTagCommit(frame,*self.lease,gate,
      [&](const storage::ProfileSourceAccess& current)->std::expected<void,storage::Error>{
auto finalInventory=storage::ConfigFile::load(*self.lease,"downloads.config");
      if(!finalInventory||finalInventory->recoverySource()!=storage::RecoverySource::Primary
          ||finalInventory->document().originalBytes()!=inventory->document().originalBytes()){
        return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint inventory preimage changed before publication"));
      }
      auto finalStore=storage::TorrentStateStore::open(*self.lease);
      if(!finalStore){return std::unexpected(finalStore.error());}
      auto finalState=finalStore->load(*self.lease,*selectedHash);
      if(!finalState||finalState->recoveredBackup()||finalState->document().originalBytes()!=capturedStateBytes){
        return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint original-state preimage changed before publication"));
      }
      if(categorySource){auto finalCategories=categoryFactSource(*self.lease);
        if(!finalCategories||*finalCategories!=*categorySource){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint category definition preimage changed before publication"));}}
      if(frame.internalNamespace&&!frame.internalNamespace->current(*self.lease,self.session)){return std::unexpected(error(storage::ErrorCode::Conflict,"Internal tag namespace changed before tag CAS"));}
      if(!self.peerDefinitionsUnchanged()){return std::unexpected(error(storage::ErrorCode::Conflict,"Peer-set definitions changed before tag CAS"));}
      if(current.revision()!=frame.sourceRevision){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint source revision changed before tag CAS"));}

        if(stop.stop_requested()||(eventAdded&&(!eventEnd||std::chrono::steady_clock::now()>=*eventEnd))){return std::unexpected(error(storage::ErrorCode::Cancelled,"Constraint publication cancelled or expired after private reader join"));}

        return {};
      },[&]()->std::expected<storage::ManualTagCommit,storage::Error>{
        if(assignmentValue){
          auto value=dependencyPlan?frame.assignmentOwner->publishPlan(store,snapshot,dependencyPlan->plan,*assignmentValue,stop,Impl::assignmentRates(frame))
              :frame.assignmentOwner->change(store,snapshot,rule.id,*assignmentValue,decision.membership==c::Membership::Add,added,stop,Impl::assignmentRates(frame));
          if(!value){return std::unexpected(value.error());}
          result.assignmentReceipt=std::move(value->receipt);return std::move(value->commit);
        }
        return dependencyPlan?store.publishMembershipPlan(dependencyPlan->plan,stop):store.apply(snapshot,edit,stop);
      },detail::privateRateRequest(frame,snapshot,privateEdits));
      if(!committed){
        if(dependencyPlan){while(self.recentAdds.size()>recentBefore){self.recentAdds.pop_back();}}else if(reservedAdd){self.recentAdds.pop_back();}
        if((assignmentValue||detail::privateRateSubject(frame,*selectedHash))&&(committed.error().code==storage::ErrorCode::Unsupported||committed.error().code==storage::ErrorCode::Busy)){
          result.kept=unavailable("Assignment source is unavailable for this download");continue;
        }
        return std::unexpected(committed.error());
      }
      result.snapshot=std::move(committed->snapshot);result.changed=committed->changed;result.hash=*selectedHash;
      if(eventTurn){self.newDownloads.finishTurn();}
      return result; // Saved survives a cancellation arriving after publication.
    }
    if(eventTurn){self.newDownloads.finishTurn();}
    return result;
  }catch(const std::bad_alloc&){return std::unexpected(error(storage::ErrorCode::Io,"Insufficient memory in constraint worker; reload before retry"));}
}
} // namespace biglybtc::app
