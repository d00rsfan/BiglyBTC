from pathlib import Path
import hashlib,json
D=Path(__file__).resolve().parent;N=D.parents[1].parent/'BiglyBTC/preview/native';O=D/'drafts';B=D/'baseline';pins={}
for name in ['services/i2p_provider/profile_owner.hpp','services/i2p_provider/profile_owner.cpp']:
 target=B/name;target.parent.mkdir(parents=True,exist_ok=True)
 if not target.exists():target.write_bytes((N/name).read_bytes())
 pins[name]=hashlib.sha256(target.read_bytes()).hexdigest();s=target.read_text()
 def write(s):
  p=O/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s)
 if name.endswith('.hpp'):
  needle='  struct MagnetObservation {';assert s.count(needle)==1
  s=s.replace(needle,'''  class TagSourceUpdate final {
  public:~TagSourceUpdate();
  private:friend class OwnedProfile;struct Impl;
    explicit TagSourceUpdate(std::unique_ptr<Impl>);std::unique_ptr<Impl> impl_;
  };
  // One synchronous service-owned transaction. No controller reference is kept.
  std::expected<void,storage::Error> preflightTagSourceUpdate(engine::ProfileTagRates&); // Host I/O,unlocked.
  std::expected<std::unique_ptr<TagSourceUpdate>,storage::Error> beginTagSourceUpdate(); // Private Qt.
  std::expected<void,storage::Error> joinTagSourceUpdate(TagSourceUpdate&); // Host I/O,no FS gate.
  std::expected<void,storage::Error> prepareTagSourceUpdate(TagSourceUpdate&,engine::ProfileTagRates&); // Host I/O,after CAS.
  bool finishTagSourceUpdate(std::unique_ptr<TagSourceUpdate>); // Private Qt;false closes.
'''+needle);write(s)
 else:
  s=s.replace('#include "source_permit.hpp"','#include "source_permit.hpp"\n#include "private_tag_source_refresh.hpp"',1)
  needle='struct OwnedProfile::Impl {';assert s.count(needle)==1
  s=s.replace(needle,'''struct OwnedProfile::TagSourceUpdate::Impl {
  struct Target {
    Prepared::Impl* prepared=nullptr;lt::torrent_handle handle;lt::info_hash_t expected;
    std::shared_ptr<engine::ProfilePreparedTagAttachment> replacement;
    engine::PrivateTagRateSource source;
  };
  OwnedProfile::Impl* owner=nullptr;std::shared_ptr<ProfileSource> root;
  lt::session_handle session;std::vector<Target> targets;
  bool joined=false,prepared=false;
};
OwnedProfile::TagSourceUpdate::TagSourceUpdate(std::unique_ptr<Impl> value):impl_(std::move(value)){}
OwnedProfile::TagSourceUpdate::~TagSourceUpdate()=default;
'''+needle)
  needle='  bool externalQueue=false,externalSnapshotValid=false;';assert s.count(needle)==1;s=s.replace(needle,'  bool sourcePaused=false;\n'+needle)
  needle='  void schedule(){\n    if(phase!=Phase::Active||!session){return;}';assert s.count(needle)==1;s=s.replace(needle,'  void schedule(){\n    if(sourcePaused||phase!=Phase::Active||!session){return;}')
  s=s.replace('void requestStartup(){if((phase==Phase::Starting||phase==Phase::Active)&&permit', 'void requestStartup(){if(!sourcePaused&&(phase==Phase::Starting||phase==Phase::Active)&&permit',1)
  needle='    if((phase!=Phase::Starting&&phase!=Phase::Active)||!permit){return;}';assert s.count(needle)==1;s=s.replace(needle,'    if(sourcePaused||(phase!=Phase::Starting&&phase!=Phase::Active)||!permit){return;}')
  needle='bool OwnedProfile::begin(const Options& options,core::QueuePolicy policy,Rates rates){';assert s.count(needle)==1
  s=s.replace(needle,'''std::expected<void,Error> OwnedProfile::preflightTagSourceUpdate(engine::ProfileTagRates& rates){
  if(impl_->ioWorker!=std::this_thread::get_id()||!impl_->source->matchesTagOwner(rates)){
    return std::unexpected(Error{Code::Conflict,"Private tag refresh requires its original I/O worker/common controller"});
  }
  auto access=impl_->source->lease()->sourceAccess();if(!access){return std::unexpected(access.error());}
  if(!access->observationsAllowed()){return std::unexpected(Error{Code::Conflict,"Release the outer source gate before private tag refresh"});}
  return {};
}
std::expected<std::unique_ptr<OwnedProfile::TagSourceUpdate>,Error> OwnedProfile::beginTagSourceUpdate(){
  auto& s=*impl_;
  if(!s.onThread()||s.sourcePaused||!s.session||!s.permit||!s.magnets.empty()
      ||(s.phase!=Phase::Starting&&s.phase!=Phase::Active)||!s.source->prepared()
      ||s.permit->phase()==SourcePermit::Phase::Executing){
    return std::unexpected(Error{Code::Conflict,"Current stable private source participants required for tag refresh"});
  }
  try{
    auto data=std::make_unique<TagSourceUpdate::Impl>();data->owner=&s;data->root=s.source;data->session=*s.session;data->targets.reserve(s.members.size());
    for(auto& [key,member]:s.members){(void)key;auto& m=*member;
      if(m.retired.requested||m.removing||!m.failure.empty()||!m.data().tagTarget||(m.installed&&!m.handle.is_valid())){
        return std::unexpected(Error{Code::Busy,"Private tag refresh awaits retained member retirement"});
      }
      TagSourceUpdate::Impl::Target target;target.prepared=&m.data();target.handle=m.handle;const auto& full=m.data().storage.identity().full;
      if(full.v1){std::transform(full.v1->begin(),full.v1->end(),target.expected.v1.begin(),[](std::byte b){return std::to_integer<unsigned char>(b);});}
      if(full.v2){std::transform(full.v2->begin(),full.v2->end(),target.expected.v2.begin(),[](std::byte b){return std::to_integer<unsigned char>(b);});}
      data->targets.push_back(std::move(target));
    }
    if(data->targets.empty()){return std::unexpected(Error{Code::Conflict,"Private tag refresh has no prepared participants"});}
    auto value=std::unique_ptr<TagSourceUpdate>(new TagSourceUpdate(std::move(data)));
    if(s.bridge&&!s.bridge->pauseSourceValidation()){return std::unexpected(Error{Code::Busy,"Private bridge source turn cannot be quiesced"});}
    s.sourcePaused=true;s.permit->stop();return value;
  }catch(...){return std::unexpected(Error{Code::Io,"Cannot capture private tag refresh participants"});}
}
std::expected<void,Error> OwnedProfile::joinTagSourceUpdate(TagSourceUpdate& update){
  auto& s=*impl_;auto& value=*update.impl_;
  if(s.ioWorker!=std::this_thread::get_id()||value.owner!=&s||value.joined){return std::unexpected(Error{Code::Conflict,"Actual private tag refresh I/O join required"});}
  {
    auto access=s.source->lease()->sourceAccess();if(!access){return std::unexpected(access.error());}
    if(!access->observationsAllowed()){return std::unexpected(Error{Code::Conflict,"Private tag source join cannot hold an outer source gate"});}
  } // Drop the probe gate before waiting for Ready/Executing source workers.
  if(s.permit){s.permit->join();}if(s.bridge){s.bridge->joinSource();}value.joined=true;return {};
}
std::expected<void,Error> OwnedProfile::prepareTagSourceUpdate(TagSourceUpdate& update,engine::ProfileTagRates& rates){
  auto& s=*impl_;auto& value=*update.impl_;
  if(s.ioWorker!=std::this_thread::get_id()||value.owner!=&s||!value.joined||value.prepared
      ||!value.root->matchesTagOwner(rates)){return std::unexpected(Error{Code::Conflict,"Actual joined private source transaction/controller required"});}
  try{
    auto synchronized=rates.synchronize(value.session);if(!synchronized){return std::unexpected(synchronized.error());}
    std::vector<TagSourceRefresh> sources;sources.reserve(value.targets.size());
    for(auto& target:value.targets){
      if(target.handle.is_valid()){
        auto proof=rates.refreshPrivateSource(target.prepared->hash,value.session,target.handle);
        if(!proof){return std::unexpected(proof.error());}target.source=std::move(*proof);
      }else{
        target.replacement=target.prepared->tagTarget;
        if(!target.replacement||!target.replacement->current()){
          auto pending=rates.prepareAttachment(target.prepared->hash,target.expected,value.session);if(!pending){return std::unexpected(pending.error());}
          target.replacement=std::shared_ptr<engine::ProfilePreparedTagAttachment>(std::move(*pending));
        }
        auto proof=rates.preparePrivateSource(target.prepared->hash,value.session,*target.replacement);
        if(!proof){return std::unexpected(proof.error());}target.source=std::move(*proof);
      }
      sources.push_back({target.prepared->source,target.source});
    }
    auto accepted=value.root->acceptTagSources(sources);if(!accepted){return std::unexpected(accepted.error());}
    for(auto& target:value.targets){if(target.replacement){target.prepared->tagTarget=std::move(target.replacement);}}
    value.prepared=true;return {};
  }catch(...){return std::unexpected(Error{Code::Io,"Cannot prepare complete private tag refresh"});}
}
bool OwnedProfile::finishTagSourceUpdate(std::unique_ptr<TagSourceUpdate> update){
  auto& s=*impl_;
  if(!s.onThread()||!update||update->impl_->owner!=&s||!s.sourcePaused){return false;}
  auto& value=*update->impl_;
  if(!value.joined||!value.prepared||!s.session||!s.source->prepared()
      ||s.phase==Phase::Withdrawn||!s.permit||!s.permit->joined()){
    s.close("Private tag source refresh failed");return false;
  }
  try{
    s.permit=std::make_unique<SourcePermit>(s.source,this);
    if(s.bridge&&!s.bridge->resumeSourceValidation()){s.close("Private bridge tag source refresh failed");return false;}
    s.sourcePaused=false;s.requestStartup();return true;
  }catch(...){s.close("Cannot restart private tag source validation");return false;}
}

'''+needle);write(s)
(D/'owner-update-baseline-sha256.json').write_text(json.dumps(pins,indent=2)+'\n');print('Staged transient private owner update context,pending renewal and actual two-worker joins;Native unchanged')
