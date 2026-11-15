// SPDX-License-Identifier: GPL-2.0-or-later
#include "database.hpp"
#include "storage_stats.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <functional>
namespace biglybtc::services::azureus_db_runtime {
namespace {
std::int64_t add(std::int64_t a,std::int64_t b)noexcept{return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a)+std::bit_cast<std::uint64_t>(b));}
std::int64_t subtract(std::int64_t a,std::int64_t b)noexcept{return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a)-std::bit_cast<std::uint64_t>(b));}
struct KeyPort final:mapping::StoragePort {
  StorageStatsMode mode;bool present,failed=false;azureus_storage_stats::Key key;
  std::optional<azureus_storage_stats::DiversifyingKey> diversified;
  std::shared_ptr<azureus_storage_stats::DiversificationControl> control;
  lifecycle::WallClock* borrowedClock=nullptr;
  std::string checkpointKey;std::optional<azureus_storage_stats::DiversificationCheckpoint> initial;
  using Changed=std::function<std::expected<void,mapping::BoundaryError>(std::string_view,azureus_storage_stats::DiversificationCheckpoint)>;
  Changed checkpointChanged;
  using ContentChanged=std::function<std::expected<std::uint64_t,mapping::BoundaryError>()>;
  ContentChanged contentChanged;std::uint64_t contentRevision=0;
  std::expected<void,mapping::BoundaryError> touch()noexcept{try{if(contentChanged){auto next=contentChanged();if(!next){return fail();}contentRevision=*next;}return {};}catch(...){return fail();}}

  std::expected<void,mapping::BoundaryError> report()noexcept{try{if(checkpointChanged&&diversified){auto saved=checkpointChanged(checkpointKey,diversified->checkpoint());if(!saved){return fail();}}return {};}catch(...){return fail();}}

  KeyPort(StorageStatsMode value,std::shared_ptr<azureus_storage_stats::DiversificationControl> inputs,
      std::optional<azureus_storage_stats::DiversificationCheckpoint> seed={},Changed changed={},ContentChanged content={})
      :StoragePort(value!=StorageStatsMode::Absent),mode(value),present(value!=StorageStatsMode::Absent),control(std::move(inputs)),initial(seed),checkpointChanged(std::move(changed)),contentChanged(std::move(content)){}
  std::unexpected<mapping::BoundaryError> fail()noexcept{failed=true;return std::unexpected(mapping::BoundaryError::Failed);}
  struct Borrow {KeyPort& port;Borrow(KeyPort& p,lifecycle::WallClock& clock):port(p){port.borrowedClock=&clock;}~Borrow(){port.borrowedClock=nullptr;}};
  struct Clock final:azureus_storage_stats::DiversificationClock {
    lifecycle::WallClock& source;explicit Clock(lifecycle::WallClock& value):source(value){}
    std::int64_t nowMilliseconds()noexcept override{return source.nowMilliseconds();}
  };
  static std::optional<std::int32_t> length(const mapping::ValueView& value)noexcept{if(!value.lifecycle){return {};}const auto payload=value.lifecycle->payload().view();if(!payload||payload->size()>512){return {};}return static_cast<std::int32_t>(payload->size());}
  std::expected<bool,mapping::BoundaryError> keyCreated(const mapping::Key& key,bool)noexcept override{
    if(mode==StorageStatsMode::OriginalDiversification){
      if(!borrowedClock||!control){return fail();}Clock clock(*borrowedClock);
      try{checkpointKey=key.bytes();}catch(...){return fail();}
      auto made=initial?azureus_storage_stats::DiversifyingKey::restore(*initial):azureus_storage_stats::DiversifyingKey::create(clock,*control);if(!made){return fail();}diversified=std::move(*made);if(!touch()){return fail();}
    }return present;
  }
  std::expected<void,mapping::BoundaryError> changed(std::int32_t entries,std::int32_t bytes)noexcept{
    if(mode==StorageStatsMode::OriginalDiversification){
      if(!diversified||!borrowedClock||!control){return fail();}Clock clock(*borrowedClock);if(diversified->changed(entries,bytes,clock,*control)!=azureus_storage_stats::PolicyTransition::None){return report();}
    }else{key.changed(entries,bytes);}return {};
  }
  std::expected<void,mapping::BoundaryError> valueAdded(const mapping::ValueView& value)noexcept override{auto n=length(value);if(!n||!touch()){return fail();}return changed(1,*n);}
  std::expected<void,mapping::BoundaryError> valueUpdated(const mapping::ValueView& old,const mapping::ValueView& value)noexcept override{auto a=length(old),b=length(value);if(!a||!b||!touch()){return fail();}return changed(0,*b-*a);}
  std::expected<void,mapping::BoundaryError> valueDeleted(const mapping::ValueView& value)noexcept override{auto n=length(value);if(!n||!touch()){return fail();}return changed(-1,-*n);}
  std::expected<void,mapping::BoundaryError> keyRead(const mapping::Contact& contact)noexcept override{
    const auto value=contact.transport().get();if(!borrowedClock||!value||(value->address.length!=4&&value->address.length!=16)){return fail();}
    const auto ip=std::span(value->address.bytes).first(value->address.length);
    if(mode==StorageStatsMode::OriginalDiversification){
      if(!diversified||!control){return fail();}Clock clock(*borrowedClock);auto changed=diversified->read(ip,clock,*control);if(!changed){return fail();}if(*changed!=azureus_storage_stats::PolicyTransition::None){return report();}
    }else if(!key.read(ip,borrowedClock->nowMilliseconds())){return fail();}return {};
  }
  std::expected<std::int8_t,mapping::BoundaryError> diversification()noexcept override{
    if(failed){return fail();}if(mode==StorageStatsMode::OriginalDiversification){
      if(!diversified||!borrowedClock){return fail();}Clock clock(*borrowedClock);auto state=diversified->diversification(clock);if(state.transition!=azureus_storage_stats::PolicyTransition::None&&!report()){return fail();}return state.type;
    }return std::int8_t{1};
  }
  std::expected<azureus_storage_stats::Stats,mapping::BoundaryError> statistics()noexcept{
    if(failed){return fail();}if(mode==StorageStatsMode::OriginalDiversification){
      if(!diversified||!borrowedClock){return fail();}Clock clock(*borrowedClock);auto stats=diversified->statistics(clock);if(stats.transition!=azureus_storage_stats::PolicyTransition::None&&!report()){return fail();}return stats.value;
    }return key.stats();
  }
};
bool expired(const lifecycle::DbValue& value,std::int64_t now,std::int32_t interval)noexcept{
  if(value.isLocal()){return false;}
  const auto hours=value.lifeTimeHours();
  const std::int32_t age=hours==0?interval:std::min<std::int32_t>(static_cast<std::int32_t>(hours)*60*60*1000,3*24*60*60*1000);
  // PUT_AND_FORGET0x100 is local-only and cannot occur in the remote byte flags.
  const auto grace=std::min<std::int32_t>(60*60*1000,age/4);
  return now>add(add(value.creationTime(),age),grace);
}
}
struct Owner::Impl {
  struct Record {mapping::Owner owner;KeyPort port;std::uint64_t storeEpoch=0;Record(mapping::Owner value,const Limits& limits,
      std::optional<azureus_storage_stats::DiversificationCheckpoint> seed={},KeyPort::Changed changed={},KeyPort::ContentChanged content={}):owner(std::move(value)),port(limits.storageStats,limits.diversificationControl,seed,std::move(changed),std::move(content)){};};
  Limits limits;std::optional<azureus_nodes::Contact> local;const std::thread::id thread=std::this_thread::get_id();
  std::map<std::string,std::unique_ptr<Record>,std::less<>> records;

  using ShortKey=std::array<char,6>;
  std::map<ShortKey,std::string_view> prefixIndex;
  static ShortKey shortKey(std::string_view key){ShortKey result;std::copy_n(key.begin(),6,result.begin());return result;}
  void indexAdded(std::string_view key){
    if(key.size()!=20||!local){return;}const auto prefix=shortKey(key);auto at=prefixIndex.find(prefix);
    if(at!=prefixIndex.end()){
      for(std::size_t i=0;i<20;++i){const auto oldDistance=static_cast<unsigned char>(at->second[i])^local->id[i];const auto newDistance=static_cast<unsigned char>(key[i])^local->id[i];if(oldDistance<newDistance){return;}if(oldDistance>newDistance){break;}}
      at->second=key;
    }else{prefixIndex.emplace(prefix,key);}
  }
  void indexRemoved(std::string_view key){if(key.size()!=20){return;}auto at=prefixIndex.find(shortKey(key));if(at!=prefixIndex.end()&&at->second==key){prefixIndex.erase(at);}}
  using LocalState=azureus_storage_stats::DiversificationCheckpoint;
  std::map<std::string,LocalState,std::less<>> localPolicies;
  std::uint64_t policyRevision=1,contentSequence=0,storeSequence=0;bool localRestored=false;mutable bool policyObserved=false;
  mutable std::shared_ptr<const LocalCheckpoint> checkpointCache;
  struct CacheCycleState {CachedCycle token;std::int64_t now=0,period=0;std::uint64_t storeFence=0;std::vector<std::string> keys;};
  std::optional<CacheCycleState> cacheCycle;std::uint64_t cacheCycleSequence=0;
  std::expected<void,mapping::BoundaryError> policyChanged(std::string_view key,LocalState state){
    const auto found=localPolicies.find(key);if((state.type==1&&found==localPolicies.end())||(found!=localPolicies.end()&&found->second==state)){return {};}
    if(policyRevision==UINT64_MAX||(state.type!=1&&found==localPolicies.end()&&localPolicies.size()>=core::MaximumAzureusLocalDiversifications)){return std::unexpected(mapping::BoundaryError::Failed);}
    if(state.type==1){localPolicies.erase(found);}else{localPolicies.insert_or_assign(std::string(key),state);}++policyRevision;checkpointCache.reset();return {};
  }
  std::unique_ptr<Record> makeRecord(mapping::Owner value,std::string_view key){
    std::optional<LocalState> initial;if(auto found=localPolicies.find(key);found!=localPolicies.end()){initial=found->second;}
    KeyPort::ContentChanged content;KeyPort::Changed changed;if(limits.storageStats==StorageStatsMode::OriginalDiversification){changed=[this](auto key,auto state){return policyChanged(key,state);};content=[this]()->std::expected<std::uint64_t,mapping::BoundaryError>{if(contentSequence==UINT64_MAX){return std::unexpected(mapping::BoundaryError::Failed);}return ++contentSequence;};}
    return std::make_unique<Record>(std::move(value),limits,initial,std::move(changed),std::move(content));
  }
  std::expected<void,Error> changedStore(Record& record){if(limits.storageStats!=StorageStatsMode::OriginalDiversification){return {};}if(storeSequence==UINT64_MAX){return std::unexpected(Error::Capacity);}record.storeEpoch=++storeSequence;return {};}
  std::int64_t lastScan=0;std::uint64_t stores=0,reads=0,expiredCount=0,localPuts=0;bool closed=false,busy=false;
  struct Guard {Impl& owner;explicit Guard(Impl& value):owner(value){owner.busy=true;}~Guard(){owner.busy=false;}};
  std::expected<void,Error> scan(lifecycle::WallClock&,bool);
  std::expected<void,Error> check()const noexcept {
    if(std::this_thread::get_id()!=thread){return std::unexpected(Error::Thread);}
    if(closed){return std::unexpected(Error::Closed);}if(busy){return std::unexpected(Error::Busy);}return {};
  }
  Snapshot snapshot()const {
    Snapshot result;result.closed=closed;result.keys=records.size();result.stores=stores;result.reads=reads;result.expired=expiredCount;result.localPuts=localPuts;
    for(const auto& [key,record]:records){(void)key;const auto value=record->owner.snapshot();
      result.values+=value.direct.size()+value.forwarded.size();
      result.bytes+=static_cast<std::size_t>(value.directBytes)+static_cast<std::size_t>(value.indirectBytes);
    }return result;
  }
};
Owner::Owner(std::unique_ptr<Impl> value):impl_(std::move(value)){}
Owner::~Owner()=default;
std::expected<std::unique_ptr<Owner>,Error> Owner::create(Limits limits){
  if(!limits.maximumKeys||limits.maximumKeys>8192||!limits.maximumEntriesPerKey||limits.maximumEntriesPerKey>(limits.storageStats==StorageStatsMode::OriginalDiversification?256U:64U)
      ||limits.payloadQuota>4*1024*1024||(limits.storageStats!=StorageStatsMode::Absent&&limits.storageStats!=StorageStatsMode::SuspendedDiversification&&limits.storageStats!=StorageStatsMode::OriginalDiversification)
      ||(limits.storageStats==StorageStatsMode::OriginalDiversification)!=static_cast<bool>(limits.diversificationControl)){return std::unexpected(Error::Invalid);}
  try{auto impl=std::make_unique<Impl>();impl->limits=limits;return std::unique_ptr<Owner>(new Owner(std::move(impl)));}
  catch(...){return std::unexpected(Error::Memory);}
}
std::expected<void,Error> Owner::bindLocal(const azureus_nodes::Contact& contact){
  if(auto valid=impl_->check();!valid){return valid;}
  if(impl_->local||!impl_->records.empty()||!contact.address.port||(contact.address.length!=4&&contact.address.length!=16)){return std::unexpected(Error::Invalid);}
  impl_->local=contact;return {};
}
std::expected<void,Error> Owner::restoreLocalDiversifications(std::span<const core::AzureusLocalDiversification> records,std::int64_t now){
  if(auto valid=impl_->check();!valid){return valid;}if(impl_->limits.storageStats!=StorageStatsMode::OriginalDiversification){return std::unexpected(Error::Unsupported);}if(impl_->localRestored||impl_->policyObserved||!impl_->records.empty()||records.size()>core::MaximumAzureusLocalDiversifications){return std::unexpected(Error::Invalid);}
  try{std::map<std::string,Impl::LocalState,std::less<>> restored;for(const auto& r:records){if(r.type!=2&&r.type!=3){return std::unexpected(Error::Invalid);}if(r.expiry<=now){continue;}restored.insert_or_assign(std::string(reinterpret_cast<const char*>(r.key.data()),20),Impl::LocalState{r.type,r.expiry});}impl_->localPolicies=std::move(restored);impl_->localRestored=true;impl_->checkpointCache.reset();return {};}catch(...){return std::unexpected(Error::Memory);}
}
std::expected<std::uint64_t,Error> Owner::localDiversificationRevision()const{
  if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}if(impl_->limits.storageStats!=StorageStatsMode::OriginalDiversification){return std::unexpected(Error::Unsupported);}impl_->policyObserved=true;return impl_->policyRevision;
}
std::expected<std::optional<std::uint64_t>,Error> Owner::readRevision(std::string_view key)const{
  if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}if(impl_->limits.storageStats!=StorageStatsMode::OriginalDiversification){return std::unexpected(Error::Unsupported);}auto found=impl_->records.find(key);if(found==impl_->records.end()){return std::optional<std::uint64_t>{};}if(found->second->port.failed||!found->second->port.contentRevision){return std::unexpected(Error::Mapping);}return std::optional{found->second->port.contentRevision};
}
std::expected<std::shared_ptr<const LocalCheckpoint>,Error> Owner::localDiversificationCheckpoint()const{
  if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}if(impl_->limits.storageStats!=StorageStatsMode::OriginalDiversification){return std::unexpected(Error::Unsupported);}
  impl_->policyObserved=true;if(impl_->checkpointCache){return impl_->checkpointCache;}try{auto snapshot=std::make_shared<LocalCheckpoint>();snapshot->revision=impl_->policyRevision;snapshot->entries.reserve(impl_->localPolicies.size());for(const auto& [key,state]:impl_->localPolicies){if(key.size()!=20){return std::unexpected(Error::Unsupported);}core::AzureusLocalDiversification r;std::copy_n(reinterpret_cast<const std::uint8_t*>(key.data()),20,r.key.begin());r.type=state.type;r.expiry=state.expiry;snapshot->entries.push_back(r);}impl_->checkpointCache=std::move(snapshot);return impl_->checkpointCache;}catch(...){return std::unexpected(Error::Memory);}
}
std::expected<std::optional<azureus_nodes::Contact>,Error> Owner::localContact()const{
  if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}return impl_->local;
}
std::expected<bool,Error> Owner::contains(std::string_view key)const {
  if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}
  return impl_->records.contains(key);
}
std::expected<void,Error> Owner::Impl::scan(lifecycle::WallClock& clock,bool force){
  try{
    const auto now=clock.nowMilliseconds(),elapsed=subtract(now,lastScan);
    if(!force&&elapsed>0&&elapsed<60000){return {};}
    lastScan=now;
    for(auto it=records.begin();it!=records.end();){
      auto& record=*it->second;const auto snapshot=record.owner.snapshot();
      if(snapshot.direct.empty()&&snapshot.forwarded.empty()){if(limits.storageStats==StorageStatsMode::OriginalDiversification&&!policyChanged(it->first,{1,0})){return std::unexpected(Error::Capacity);}indexRemoved(it->first);record.owner.close();it=records.erase(it);continue;}
      for(const bool direct:{true,false}){
        for(const auto& entry:direct?snapshot.direct:snapshot.forwarded){
          if(entry.value.lifecycle&&expired(*entry.value.lifecycle,now,limits.originalRepublishMillis)){
            if(expiredCount==UINT64_MAX){return std::unexpected(Error::Capacity);}
            KeyPort::Borrow borrowed(record.port,clock);
            const auto removed=direct?record.owner.removeDirect(entry.key,record.port):record.owner.removeForwarded(entry.key,record.port);
            if(!removed||record.port.failed){return std::unexpected(Error::Mapping);}++expiredCount;
          }
        }
      }++it; // A newly emptied mapping survives until a later scan, as source.
    }return {};
  }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<void,Error> Owner::expire(lifecycle::WallClock& clock,bool force){
  if(auto valid=impl_->check();!valid){return valid;}Impl::Guard guard(*impl_);return impl_->scan(clock,force);
}
std::expected<std::int8_t,Error> Owner::store(std::string_view key,const azureus_nodes::Contact& sender,
    std::span<const azureus_values::Value> values,lifecycle::WallClock& clock){
  if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}
  if(key.size()<6||key.size()>255||values.size()>256){return std::unexpected(Error::Invalid);}
  for(const auto& value:values){if(value.bytes.size()>512){return std::unexpected(Error::Invalid);}}
  try{
    Impl::Guard guard(*impl_);
    const auto before=impl_->snapshot();
    if(before.bytes+before.values*4>impl_->limits.payloadQuota){return std::int8_t{3};} // Source DT_SIZE; strict > before scan/store.
    if(auto scanned=impl_->scan(clock,false);!scanned){return std::unexpected(scanned.error());}
    auto found=impl_->records.find(key);
    if(found==impl_->records.end()){
      if(impl_->records.size()==impl_->limits.maximumKeys){return std::unexpected(Error::Capacity);}
      mapping::Limits limits;limits.entries=impl_->limits.maximumEntriesPerKey;limits.contacts=limits.entries*4+16;
      limits.values=limits.entries*4+16;limits.retainedPayloadBytes=(limits.entries+256)*512;
      auto made=mapping::Owner::create(limits);auto copied=mapping::Key::copy(key);
      if(!made||!copied){return std::unexpected(Error::Memory);}
      auto record=impl_->makeRecord(std::move(*made),key);
      KeyPort::Borrow borrowed(record->port,clock);
      if(!record->owner.open(std::move(*copied),false,record->port)||record->port.failed){return std::unexpected(Error::Mapping);}
      found=impl_->records.emplace(std::string(key),std::move(record)).first;impl_->indexAdded(found->first);
    }
    auto& record=*found->second;if(auto changed=impl_->changedStore(record);!changed){return std::unexpected(changed.error());}KeyPort::Borrow borrowed(record.port,clock);
    for(const auto& value:values){
      auto originator=record.owner.contact(value.originator),source=record.owner.contact(sender);
      auto payload=lifecycle::Payload::copy(value.bytes);
      if(!originator||!source||!payload){return std::unexpected(Error::Capacity);}
      mapping::ValueFields fields{value.created,std::move(*payload),value.version,*originator,*source,false,value.flags,value.lifeHours,value.replication};
      auto copied=record.owner.value(std::move(fields),clock);
      if(!copied||!record.owner.add(copied->value,clock,record.port)||record.port.failed){return std::unexpected(Error::Mapping);}
    }
    if(impl_->stores==UINT64_MAX){return std::unexpected(Error::Capacity);}++impl_->stores;
    return record.owner.snapshot().diversification;
  }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<lifecycle::DbValue,Error> Owner::putLocal(std::string_view key,
    std::string_view bytes,std::int32_t version,lifecycle::WallClock& clock){return putLocal(key,bytes,version,clock,LocalPublicationFlags::Normal);}
std::expected<lifecycle::DbValue,Error> Owner::putLocal(std::string_view key,
    std::string_view bytes,std::int32_t version,lifecycle::WallClock& clock,LocalPublicationFlags flags){
  if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}
  if((flags!=LocalPublicationFlags::Normal&&flags!=LocalPublicationFlags::Anonymous&&flags!=LocalPublicationFlags::Downloading&&flags!=LocalPublicationFlags::Seeding)||!impl_->local||key.size()!=20||bytes.empty()||bytes.size()>512){return std::unexpected(Error::Invalid);}
  try{
    Impl::Guard guard(*impl_);if(impl_->localPuts==UINT64_MAX){return std::unexpected(Error::Capacity);}
    auto found=impl_->records.find(key);
    if(found==impl_->records.end()){
      if(impl_->records.size()==impl_->limits.maximumKeys){return std::unexpected(Error::Capacity);}
      mapping::Limits limits;limits.entries=impl_->limits.maximumEntriesPerKey;limits.contacts=limits.entries*4+16;
      limits.values=limits.entries*4+16;limits.retainedPayloadBytes=(limits.entries+256)*512;
      auto made=mapping::Owner::create(limits);auto copied=mapping::Key::copy(key);
      if(!made||!copied){return std::unexpected(Error::Memory);}
      auto record=impl_->makeRecord(std::move(*made),key);KeyPort::Borrow borrowed(record->port,clock);
      if(!record->owner.open(std::move(*copied),true,record->port)||record->port.failed){return std::unexpected(Error::Mapping);}
      found=impl_->records.emplace(std::string(key),std::move(record)).first;impl_->indexAdded(found->first);
    }
    auto& record=*found->second;if(auto changed=impl_->changedStore(record);!changed){return std::unexpected(changed.error());}KeyPort::Borrow borrowed(record.port,clock);
    auto origin=record.owner.contact(*impl_->local);auto payload=lifecycle::Payload::copy(bytes);
    if(!origin||!payload){return std::unexpected(Error::Memory);}
    mapping::ValueFields fields{clock.nowMilliseconds(),std::move(*payload),version,*origin,*origin,true,static_cast<std::int32_t>(flags),0,-1};
    auto value=record.owner.value(std::move(fields),clock);
    if(!value||!record.owner.add(value->value,clock,record.port)||record.port.failed){return std::unexpected(Error::Mapping);}
    auto actual=value->value.snapshot();if(!actual){return std::unexpected(Error::Mapping);}
    ++impl_->localPuts;return *actual;
  }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<std::optional<lifecycle::DbValue>,Error> Owner::removeLocal(std::string_view key,std::int32_t version,lifecycle::WallClock& clock){
 if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}
 if(!impl_->local||key.size()!=20){return std::unexpected(Error::Invalid);}
 try{Impl::Guard guard(*impl_);auto at=impl_->records.find(key);if(at==impl_->records.end()){return std::optional<lifecycle::DbValue>{};}
  std::optional<lifecycle::DbValue> result;bool empty=false;
  {auto& record=*at->second;if(auto changed=impl_->changedStore(record);!changed){return std::unexpected(changed.error());}KeyPort::Borrow borrowed(record.port,clock);auto contact=record.owner.contact(*impl_->local);if(!contact){return std::unexpected(Error::Mapping);}
  auto removed=record.owner.remove(*contact,record.port);if(!removed||record.port.failed){return std::unexpected(Error::Mapping);}
  if(removed->value){const auto value=removed->value->snapshot();if(!value){return std::unexpected(Error::Mapping);}result=value->deletion(version,clock);}
  const auto remaining=record.owner.snapshot();empty=remaining.direct.empty()&&remaining.forwarded.empty();}
  if(empty){impl_->indexRemoved(at->first);impl_->records.erase(at);}return result;
 }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<std::optional<Read>,Error> Owner::read(std::string_view key,const azureus_nodes::Contact& reader,
    std::int32_t maximum,std::int16_t flags,lifecycle::WallClock& clock){
  if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}
  if(maximum<0||maximum>255){return std::unexpected(Error::Unsupported);}
  try{
    Impl::Guard guard(*impl_);
    if(auto scanned=impl_->scan(clock,false);!scanned){return std::unexpected(scanned.error());}
    const auto found=impl_->records.find(key);if(found==impl_->records.end()){return std::optional<Read>{};}
    auto& record=*found->second;if(record.port.failed){return std::unexpected(Error::Mapping);}
    if(!record.owner.hit()){return std::unexpected(Error::Mapping);}
    KeyPort::Borrow borrowed(record.port,clock);
    if((static_cast<std::uint16_t>(flags)&8U)!=0){
      Read output;output.revision=record.port.contentRevision;output.diversification=record.owner.snapshot().diversification;
      if(record.port.present){
        if(!impl_->local){return std::unexpected(Error::Unsupported);}
        auto statistics=record.port.statistics();if(!statistics||record.port.failed){return std::unexpected(Error::Mapping);}
        auto payload=lifecycle::Payload::copy(azureus_storage_stats::encode(*statistics));if(!payload){return std::unexpected(Error::Memory);}
        auto local=lifecycle::ContactHandle::copy(*impl_->local);lifecycle::Fields fields{clock.nowMilliseconds(),std::move(*payload),0,local,local,true,8,0,-1};
        const auto value=lifecycle::DbValue::create(std::move(fields),clock);
        output.values.push_back({value.version(),value.creationTime(),std::string(*value.payload().view()),*impl_->local,value.flags(),value.lifeTimeHours(),value.replicationControl()});
      }
      if(impl_->reads==UINT64_MAX){return std::unexpected(Error::Capacity);}++impl_->reads;
      return std::optional<Read>{std::move(output)}; // Absent adapter: original empty result, no normal-read callback.
    }
    auto contact=record.owner.contact(reader);if(!contact){return std::unexpected(Error::Capacity);}
    auto read=record.owner.read(*contact,maximum,flags,record.port);if(!read||record.port.failed){return std::unexpected(Error::Mapping);}
    Read output;output.revision=record.port.contentRevision;output.diversification=record.owner.snapshot().diversification;output.values.reserve(read->value.size());
    for(const auto& value:read->value){const auto current=value.snapshot();if(!current||!current->originator().get()||!current->payload().view()){return std::unexpected(Error::Mapping);}
      output.values.push_back({current->version(),current->creationTime(),std::string(*current->payload().view()),*current->originator().get(),current->flags(),current->lifeTimeHours(),current->replicationControl()});
    }
    if(impl_->reads==UINT64_MAX){return std::unexpected(Error::Capacity);}++impl_->reads;
    return std::optional<Read>{std::move(output)};
  }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<std::vector<std::optional<std::string>>,Error> Owner::queryStoredKeys(std::span<const std::string> prefixes)const{
 if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}if(prefixes.size()>8192){return std::unexpected(Error::Capacity);}for(const auto& prefix:prefixes){if(prefix.size()!=6){return std::unexpected(Error::Invalid);}}
 try{std::vector<std::optional<std::string>> result;result.reserve(prefixes.size());for(const auto& prefix:prefixes){const auto at=impl_->prefixIndex.find(Impl::shortKey(prefix));if(at==impl_->prefixIndex.end()){result.emplace_back(std::nullopt);}else{result.emplace_back(at->second);}}return result;}catch(...){return std::unexpected(Error::Memory);}
}
std::expected<std::vector<std::optional<std::string>>,Error> Owner::queryStoredPrefixes(std::span<const std::string> prefixes)const{
 auto result=queryStoredKeys(prefixes);if(!result){return std::unexpected(result.error());}for(auto& key:*result){if(key){*key=key->substr(18,2);}}return result;
}
std::expected<std::optional<CacheReceipt>,Error> Owner::cacheReceipt(std::string_view key)const{
 if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}if(impl_->limits.storageStats!=StorageStatsMode::OriginalDiversification){return std::unexpected(Error::Unsupported);}const auto found=impl_->records.find(key);if(found==impl_->records.end()){return std::optional<CacheReceipt>{};}if(found->second->port.failed){return std::unexpected(Error::Mapping);}return std::optional<CacheReceipt>{{found->second->port.contentRevision,found->second->storeEpoch}};
}
std::expected<CachedCycle,Error> Owner::beginCachedRepublish(std::int64_t now,std::int64_t period,lifecycle::WallClock& clock,std::string_view after){
 if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}if(impl_->limits.storageStats!=StorageStatsMode::OriginalDiversification){return std::unexpected(Error::Unsupported);}if(period<0||period>INT32_MAX||(!after.empty()&&after.size()!=20)){return std::unexpected(Error::Invalid);}if(impl_->cacheCycleSequence==UINT64_MAX){return std::unexpected(Error::Capacity);}
 try{Impl::Guard guard(*impl_);if(auto scanned=impl_->scan(clock,true);!scanned){return std::unexpected(scanned.error());}Impl::CacheCycleState cycle;cycle.now=now;cycle.period=period;cycle.storeFence=impl_->storeSequence;cycle.keys.reserve(impl_->records.size());for(const auto& [key,record]:impl_->records){(void)record;if(key.size()!=20){return std::unexpected(Error::Unsupported);}cycle.keys.push_back(key);}if(!after.empty()){const auto next=std::upper_bound(cycle.keys.begin(),cycle.keys.end(),after,[](std::string_view value,const std::string& key){return value<key;});std::rotate(cycle.keys.begin(),next,cycle.keys.end());}cycle.token.value=++impl_->cacheCycleSequence;const auto token=cycle.token;impl_->cacheCycle=std::move(cycle);return token;
 }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<CachedPage,Error> Owner::cachedRepublishPage(CachedCycle token,std::size_t cursor,std::size_t maximumKeys,std::size_t maximumValues){
 if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}if(impl_->limits.storageStats!=StorageStatsMode::OriginalDiversification){return std::unexpected(Error::Unsupported);}if(!impl_->cacheCycle||token.value!=impl_->cacheCycle->token.value||cursor>impl_->cacheCycle->keys.size()||!maximumKeys||maximumKeys>8192||maximumValues<256||maximumValues>8192){return std::unexpected(Error::Invalid);}
 try{Impl::Guard guard(*impl_);const auto& cycle=*impl_->cacheCycle;CachedPage result;result.next=cursor;std::size_t examined=0,total=0;
  while(result.next<cycle.keys.size()&&examined<maximumKeys){const auto& key=cycle.keys[result.next];const auto found=impl_->records.find(key);
   if(found==impl_->records.end()||found->second->storeEpoch>cycle.storeFence){result.through=key;++result.next;++examined;continue;}auto& record=*found->second;if(record.port.failed){return std::unexpected(Error::Mapping);}const auto state=record.owner.snapshot();if(state.diversification!=1){result.through=key;++result.next;++examined;continue;}
   std::vector<lifecycle::DbValue> values;std::vector<std::uint64_t> ids;std::vector<CachedValueAge> ages;const auto count=state.direct.size()+state.forwarded.size();if(count>256){return std::unexpected(Error::Capacity);}values.reserve(count);ids.reserve(count);ages.reserve(count);
   for(const auto* entries:{&state.direct,&state.forwarded}){for(const auto& entry:*entries){if(!entry.value.lifecycle){return std::unexpected(Error::Mapping);}const auto& value=*entry.value.lifecycle;values.push_back(value);ids.push_back(entry.value.id);ages.push_back({value.isLocal(),value.replicationControl(),value.storeTime()});}}
   const auto selected=selectCachedValues(state.diversification,ages,cycle.now,cycle.period);if(!selected){return std::unexpected(Error::Invalid);}if(selected->dueCount>maximumValues-total){break;}
   if(selected->rollbackCount){if(auto changed=impl_->changedStore(record);!changed){return std::unexpected(changed.error());}for(const auto index:selected->clockRollbacks()){if(!record.owner.setStoreTime(ids[index],cycle.now)){return std::unexpected(Error::Mapping);}}}
   if(selected->surveyOnly||selected->dueCount){CachedMapping candidate{key,{record.port.contentRevision,record.storeEpoch},{},selected->surveyOnly};candidate.values.reserve(selected->dueCount);for(const auto index:selected->dueValues()){candidate.values.push_back(std::move(values[index]));}total+=selected->dueCount;result.mappings.push_back(std::move(candidate));}result.through=key;++result.next;++examined;
  }result.more=result.next<cycle.keys.size();return result;
 }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<void,Error> Owner::endCachedRepublish(CachedCycle token){
 if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}if(impl_->limits.storageStats!=StorageStatsMode::OriginalDiversification){return std::unexpected(Error::Unsupported);}if(!impl_->cacheCycle||token.value!=impl_->cacheCycle->token.value){return std::unexpected(Error::Invalid);}impl_->cacheCycle.reset();return {};
}
std::expected<std::vector<CachedMapping>,Error> Owner::prepareCachedRepublish(std::int64_t now,std::int64_t period,lifecycle::WallClock& clock){
 auto cycle=beginCachedRepublish(now,period,clock);if(!cycle){return std::unexpected(cycle.error());}struct End {Owner& owner;CachedCycle token;~End(){static_cast<void>(owner.endCachedRepublish(token));}} end{*this,*cycle};
 try{std::vector<CachedMapping> result;std::size_t cursor=0;for(;;){auto page=cachedRepublishPage(*cycle,cursor,128,8192);if(!page){return std::unexpected(page.error());}cursor=page->next;for(auto& mapping:page->mappings){result.push_back(std::move(mapping));}if(!page->more){return result;}}
 }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<bool,Error> Owner::eraseCachedMapping(std::string_view key,CacheReceipt receipt){
 if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}if(impl_->limits.storageStats!=StorageStatsMode::OriginalDiversification){return std::unexpected(Error::Unsupported);}try{Impl::Guard guard(*impl_);const auto found=impl_->records.find(key);if(found==impl_->records.end()){return false;}auto& record=*found->second;if(record.port.failed){return std::unexpected(Error::Mapping);}if(CacheReceipt{record.port.contentRevision,record.storeEpoch}!=receipt){return false;}if(!impl_->policyChanged(key,{1,0})){return std::unexpected(Error::Capacity);}impl_->indexRemoved(found->first);record.owner.close();impl_->records.erase(found);return true;}catch(...){return std::unexpected(Error::Memory);}
}
std::expected<std::vector<std::string>,Error> Owner::nonLocalPayloads(std::size_t maximum)const{
  if(auto valid=impl_->check();!valid){return std::unexpected(valid.error());}if(maximum>8192){return std::unexpected(Error::Capacity);}
  try{std::vector<std::string> out;std::size_t bytes=0;for(const auto& [key,record]:impl_->records){(void)key;const auto snapshot=record->owner.snapshot();for(const auto* entries:{&snapshot.direct,&snapshot.forwarded}){for(const auto& entry:*entries){const auto& value=entry.value;if(!value.lifecycle||value.lifecycle->isLocal()){continue;}const auto payload=value.lifecycle->payload().view();if(!payload||payload->empty()){continue;}if(out.size()>=maximum||bytes+payload->size()>4*1024*1024){return out;}bytes+=payload->size();out.emplace_back(*payload);}}}return out;}catch(...){return std::unexpected(Error::Memory);}
}
std::expected<Snapshot,Error> Owner::snapshot()const{
  if(std::this_thread::get_id()!=impl_->thread){return std::unexpected(Error::Thread);}
  if(impl_->busy){return std::unexpected(Error::Busy);}
  try{return impl_->snapshot();}catch(...){return std::unexpected(Error::Memory);}
}
void Owner::close()noexcept{if(impl_->busy||std::this_thread::get_id()!=impl_->thread){return;}for(auto& [key,record]:impl_->records){(void)key;record->owner.close();}impl_->prefixIndex.clear();impl_->records.clear();impl_->cacheCycle.reset();impl_->closed=true;}
}
