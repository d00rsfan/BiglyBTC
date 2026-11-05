// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_constraint_worker.hpp"
#include "library_view.hpp"
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
bool compatibleConfiguration(const core::Node& record){
  const auto* config=record.find("c");if(!config){return true;}
  if(config->type!=core::Node::Type::Dictionary){return false;}
  for(std::size_t i=0;i<config->children.size();i+=2){
    const auto key=config->children[i].bytes;const auto& value=config->children[i+1];
    if(key=="pp.constraint"||key=="pp.enabled.constraint"||key=="vis"){continue;}
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
const core::SavedDownloadTag* tag(const core::SavedDownloadTags& tags,std::int32_t id){
  const core::SavedDownloadTag* found=nullptr;
  for(const auto& row:tags.tags){if(row.id==id){if(found||row.duplicateId){return nullptr;}found=&row;}}
  return found;
}
}
struct TagConstraintWorker::Impl {
  struct Rule {std::int32_t id=0;std::string configuration;c::Property property;std::optional<c::Expression> expression;std::optional<c::Problem> problem;};
  struct RecentAdd {std::int32_t id;core::TorrentHash hash;std::chrono::steady_clock::time_point at;};
  std::shared_ptr<const storage::ProfileLease> lease;
  std::uint64_t session=0,lastSequence=0;
  std::thread::id worker=std::this_thread::get_id();
  bool closed=false,started=false,settingsKnown=false;
  std::shared_ptr<const core::Document> cachedSource;
  std::vector<Rule> rules;
  std::deque<std::int32_t> compiled; // <=128 retained ASTs, lazy bounded cache.
  std::deque<RecentAdd> recentAdds; // Original one-second per-tag/DM add guard.
  std::vector<QString> rows;
  std::size_t rowCursor=0,ruleCursor=0;
  std::chrono::steady_clock::time_point nextSweep{};
  std::chrono::seconds reapply{30};
  bool refresh(const core::SavedDownloadTags& tags){
    if(cachedSource==tags.source){return false;}
    const auto* manual=tags.source->find("3");std::vector<Rule> next;bool changed=false;
    for(const auto& row:tags.tags){
      if(!row.id||row.duplicateId||row.membersMalformed||!manual){continue;}
      const auto& record=manual->children[row.sourceIndex*2+1];const auto* config=record.find("c");
      if(!config||!config->find("pp.constraint")){continue;}
      const auto raw=tags.source->originalBytes().substr(config->begin,config->end-config->begin);
      const auto old=std::find_if(rules.begin(),rules.end(),[&](const auto& value){return value.id==*row.id&&value.configuration==raw;});
      if(old!=rules.end()){next.push_back(*old);continue;}
      Rule value;value.id=*row.id;value.configuration=raw;
      auto parsed=property(record);
      if(!compatibleConfiguration(record)){value.problem=unavailable("Tag has unsupported configured actions or properties");}
      else if(!parsed){value.problem=parsed.error();}
      else{value.property=std::move(*parsed);
        if(value.property.mode.newOnly){value.problem=unavailable("Original creation event/add-time binding is not available; new-only rule kept");}
      }
      next.push_back(std::move(value));changed=true;
    }
    if(next.size()!=rules.size()){changed=true;}
    rules=std::move(next);cachedSource=tags.source;
    std::erase_if(compiled,[&](auto id){return std::none_of(rules.begin(),rules.end(),[&](const auto& value){return value.id==id&&value.expression.has_value();});});
    return changed;
  }
};
TagConstraintWorker::TagConstraintWorker(std::unique_ptr<Impl> value):impl_(std::move(value)){}
TagConstraintWorker::~TagConstraintWorker()=default;
std::expected<std::unique_ptr<TagConstraintWorker>,storage::Error> TagConstraintWorker::activate(
    const storage::ProfileLease& lease,std::uint64_t session){
  try{
    if(!session){return std::unexpected(error(storage::ErrorCode::Conflict,"Normal profile session required"));}
    auto held=lease.retain();if(!held){return std::unexpected(held.error());}
    auto impl=std::make_unique<Impl>();impl->lease=std::move(*held);impl->session=session;
    return std::unique_ptr<TagConstraintWorker>(new TagConstraintWorker(std::move(impl)));
  }catch(const std::bad_alloc&){return std::unexpected(error(storage::ErrorCode::Io,"Insufficient memory activating constraint worker"));}
}
void TagConstraintWorker::close() noexcept{if(impl_){impl_->closed=true;impl_->lease.reset();}}
std::expected<ConstraintWorkerUpdate,storage::Error> TagConstraintWorker::step(
    storage::ManualTagStore& store,const storage::ManualTagSnapshot& snapshot,
    const ConstraintWorkerFrame& frame,std::stop_token stop){
  try{
    auto& self=*impl_;ConstraintWorkerUpdate result;result.snapshot=snapshot;
    if(self.worker!=std::this_thread::get_id()||self.closed||!self.lease||frame.session!=self.session
        ||!frame.sequence||frame.sequence<=self.lastSequence||!frame.library||!frame.library->revision
        ||frame.library->items.size()>ui::LibraryModel::MaximumRows){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint worker lifetime/turn is stale"));}
    self.lastSequence=frame.sequence;
    if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Constraint turn cancelled"));}
    if(!store.sharesOwnership(*self.lease)){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint store belongs to another profile OFD"));}
    auto latest=store.snapshot();if(!latest){return std::unexpected(latest.error());}
    if(!snapshot.valid()||latest->generation()!=snapshot.generation()||latest->rawBytes()!=snapshot.rawBytes()
        ||!snapshot.writable()||!snapshot.tags()){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint tag preimage is stale or read-only"));}
    const bool rulesChanged=self.refresh(*snapshot.tags());const auto now=std::chrono::steady_clock::now();
    if(rulesChanged){self.rows.clear();self.rowCursor=self.ruleCursor=0;}
    if(self.rules.empty()){self.rowCursor=self.rows.size();return result;}
    bool work=!self.started||rulesChanged||self.rowCursor<self.rows.size()
        ||(self.reapply.count()>0&&now>=self.nextSweep);
    if(!work&&!frame.refreshSettings){return result;}
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
      const auto period=std::chrono::seconds(seconds<=0?0:std::max(10,seconds));
      work=work||period!=self.reapply;self.reapply=period;self.settingsKnown=true;
    }
    if(!work){return result;}
    if(self.rowCursor>=self.rows.size()){
      self.rows.clear();self.rowCursor=self.ruleCursor=0;
      std::set<QString> unique;
      for(const auto& row:frame.library->items){if(!unique.insert(row.hash).second){return std::unexpected(error(storage::ErrorCode::Conflict,"Ambiguous constraint library identity"));}self.rows.push_back(row.hash);}
      self.started=true;self.nextSweep=now+self.reapply;
    }
    if(self.rows.empty()){return result;}
    auto inventory=storage::ConfigFile::load(*self.lease,"downloads.config");if(!inventory){return std::unexpected(inventory.error());}
    if(inventory->recoverySource()!=storage::RecoverySource::Primary){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint inventory recovery requires explicit review"));}
    const auto library=core::readLibrary(inventory->document());if(!library||!library->issues.empty()){return std::unexpected(error(storage::ErrorCode::Conflict,"Constraint inventory is ambiguous"));}
    std::optional<QString> captured;
    std::optional<ConstraintFactSnapshot> facts;
    std::optional<c::Problem> factProblem;
    std::optional<core::TorrentHash> selectedHash;
    std::string capturedStateBytes;
    for(unsigned budget=0;budget<32&&self.rowCursor<self.rows.size();++budget){
      if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Constraint turn cancelled before publication"));}
      const auto hashText=self.rows[self.rowCursor];auto& rule=self.rules[self.ruleCursor];
      if(++self.ruleCursor==self.rules.size()){self.ruleCursor=0;++self.rowCursor;}
      ++result.evaluated;result.tag=rule.id;result.kept=rule.problem;
      if(!rule.expression&&!rule.problem&&rule.property.configured&&rule.property.enabled&&!rule.property.mode.newOnly){
        auto expression=c::compile(rule.property.expression);
        if(!expression){rule.problem=expression.error();result.kept=rule.problem;}
        else{
          if(self.compiled.size()==128){const auto evicted=self.compiled.front();self.compiled.pop_front();
            for(auto& value:self.rules){if(value.id==evicted){value.expression.reset();break;}}
          }
          rule.expression=std::move(*expression);self.compiled.push_back(rule.id);
        }
      }
      if(!rule.expression){continue;} // Includes disabled, malformed and am=3.
      if(!captured||*captured!=hashText){
        captured=hashText;facts.reset();selectedHash.reset();capturedStateBytes.clear();
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
        ConstraintFactInputs input;input.stamp=stamp;input.library=std::move(owning);
        if(selected.providerAdmission){input.queue=frame.queue;}
        bool duplicateProvider=false;
        for(const auto& value:frame.managed){if(value.hash==*hash){if(input.managed){duplicateProvider=true;}input.managed=value;}}
        for(const auto& value:frame.i2p){if(value.hash==*hash){if(input.i2p){duplicateProvider=true;}input.i2p=value;}}
        if(duplicateProvider){factProblem=unavailable("Ambiguous current constraint provider observations");result.kept=factProblem;continue;}
        auto stateStore=storage::TorrentStateStore::open(*self.lease);if(!stateStore){continue;}
        auto state=stateStore->load(*self.lease,*hash);if(!state||state->recoveredBackup()){continue;}
        capturedStateBytes=state->document().originalBytes();
        auto original=captureConstraintOriginalState(stamp,*state);if(!original){factProblem=original.error();result.kept=factProblem;continue;}input.original=std::move(*original);
        auto detail=engine::queryTorrent(*self.lease,*hash);if(!detail){continue;}
        auto files=std::make_shared<ui::FileDetails>();files->target=stamp.binding.target;
        for(const auto& value:detail->entries){files->items.push_back({value.index,QString::fromStdString(value.path),value.bytes,value.priority,value.skipped,value.padding});}
        // All name, geometry and selected file policy come from this held
        // original state, not a translated table state or a hash fallback.
        const auto name=detail->displayName&&!detail->displayName->empty()?*detail->displayName:detail->name;
        input.details=ui::Details{stamp.binding.target,QString::fromStdString(name),detail->bytes,std::move(files)};
        auto fresh=captureConstraintFacts(stamp,input);if(!fresh){factProblem=fresh.error();result.kept=factProblem;continue;}
        facts=std::move(*fresh);selectedHash=*hash;factProblem.reset();
      }
      if(!facts||!selectedHash){if(factProblem){result.kept=factProblem;}continue;}
      const auto* selected=tag(*snapshot.tags(),rule.id);if(!selected){continue;}
      const bool member=std::find(selected->uniqueHashes.begin(),selected->uniqueHashes.end(),*selectedHash)!=selected->uniqueHashes.end();
      const auto evaluated=evaluateCurrentConstraint(*rule.expression,facts->stamp(),*facts);
      const auto decision=c::project(evaluated,rule.property,false,member);result.kept=decision.problem;
      if(decision.membership==c::Membership::Keep){continue;}
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
      if(gate->revision()!=frame.sourceRevision){result.kept=unavailable("Constraint source revision changed before tag CAS");return result;}
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
      const auto added=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      core::SavedTagEdit edit=decision.membership==c::Membership::Add
          ?core::SavedTagEdit{core::SavedTagAddMember{rule.id,*selectedHash,added}}
          :core::SavedTagEdit{core::SavedTagRemoveMember{rule.id,*selectedHash}};
      auto committed=store.apply(snapshot,edit,stop);if(!committed){if(reservedAdd){self.recentAdds.pop_back();}return std::unexpected(committed.error());}
      result.snapshot=std::move(committed->snapshot);result.changed=committed->changed;result.hash=*selectedHash;
      return result; // Saved survives a cancellation arriving after publication.
    }
    return result;
  }catch(const std::bad_alloc&){return std::unexpected(error(storage::ErrorCode::Io,"Insufficient memory in constraint worker; reload before retry"));}
}
} // namespace biglybtc::app
