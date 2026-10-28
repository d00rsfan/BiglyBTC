// SPDX-License-Identifier: GPL-2.0-or-later
#include "database.hpp"
#include "storage_stats.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <map>
namespace biglybtc::services::azureus_db_runtime {
namespace {
std::int64_t add(std::int64_t a,std::int64_t b)noexcept{return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a)+std::bit_cast<std::uint64_t>(b));}
std::int64_t subtract(std::int64_t a,std::int64_t b)noexcept{return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a)-std::bit_cast<std::uint64_t>(b));}
struct KeyPort final:mapping::StoragePort {
  StorageStatsMode mode;bool present,failed=false;azureus_storage_stats::Key key;
  std::optional<azureus_storage_stats::DiversifyingKey> diversified;
  std::shared_ptr<azureus_storage_stats::DiversificationControl> control;
  lifecycle::WallClock* borrowedClock=nullptr;
  KeyPort(StorageStatsMode value,std::shared_ptr<azureus_storage_stats::DiversificationControl> inputs)
      :StoragePort(value!=StorageStatsMode::Absent),mode(value),present(value!=StorageStatsMode::Absent),control(std::move(inputs)){}
  std::unexpected<mapping::BoundaryError> fail()noexcept{failed=true;return std::unexpected(mapping::BoundaryError::Failed);}
  struct Borrow {KeyPort& port;Borrow(KeyPort& p,lifecycle::WallClock& clock):port(p){port.borrowedClock=&clock;}~Borrow(){port.borrowedClock=nullptr;}};
  struct Clock final:azureus_storage_stats::DiversificationClock {
    lifecycle::WallClock& source;explicit Clock(lifecycle::WallClock& value):source(value){}
    std::int64_t nowMilliseconds()noexcept override{return source.nowMilliseconds();}
  };
  static std::optional<std::int32_t> length(const mapping::ValueView& value)noexcept{if(!value.lifecycle){return {};}const auto payload=value.lifecycle->payload().view();if(!payload||payload->size()>512){return {};}return static_cast<std::int32_t>(payload->size());}
  std::expected<bool,mapping::BoundaryError> keyCreated(const mapping::Key&,bool)noexcept override{
    if(mode==StorageStatsMode::OriginalDiversification){
      if(!borrowedClock||!control){return fail();}Clock clock(*borrowedClock);
      auto made=azureus_storage_stats::DiversifyingKey::create(clock,*control);if(!made){return fail();}diversified=std::move(*made);
    }return present;
  }
  std::expected<void,mapping::BoundaryError> changed(std::int32_t entries,std::int32_t bytes)noexcept{
    if(mode==StorageStatsMode::OriginalDiversification){
      if(!diversified||!borrowedClock||!control){return fail();}Clock clock(*borrowedClock);diversified->changed(entries,bytes,clock,*control);
    }else{key.changed(entries,bytes);}return {};
  }
  std::expected<void,mapping::BoundaryError> valueAdded(const mapping::ValueView& value)noexcept override{auto n=length(value);if(!n){return fail();}return changed(1,*n);}
  std::expected<void,mapping::BoundaryError> valueUpdated(const mapping::ValueView& old,const mapping::ValueView& value)noexcept override{auto a=length(old),b=length(value);if(!a||!b){return fail();}return changed(0,*b-*a);}
  std::expected<void,mapping::BoundaryError> valueDeleted(const mapping::ValueView& value)noexcept override{auto n=length(value);if(!n){return fail();}return changed(-1,-*n);}
  std::expected<void,mapping::BoundaryError> keyRead(const mapping::Contact& contact)noexcept override{
    const auto value=contact.transport().get();if(!borrowedClock||!value||(value->address.length!=4&&value->address.length!=16)){return fail();}
    const auto ip=std::span(value->address.bytes).first(value->address.length);
    if(mode==StorageStatsMode::OriginalDiversification){
      if(!diversified||!control){return fail();}Clock clock(*borrowedClock);if(!diversified->read(ip,clock,*control)){return fail();}
    }else if(!key.read(ip,borrowedClock->nowMilliseconds())){return fail();}return {};
  }
  std::expected<std::int8_t,mapping::BoundaryError> diversification()noexcept override{
    if(failed){return fail();}if(mode==StorageStatsMode::OriginalDiversification){
      if(!diversified||!borrowedClock){return fail();}Clock clock(*borrowedClock);return diversified->diversification(clock).type;
    }return std::int8_t{1};
  }
  std::expected<azureus_storage_stats::Stats,mapping::BoundaryError> statistics()noexcept{
    if(failed){return fail();}if(mode==StorageStatsMode::OriginalDiversification){
      if(!diversified||!borrowedClock){return fail();}Clock clock(*borrowedClock);return diversified->statistics(clock).value;
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
  struct Record {mapping::Owner owner;KeyPort port;Record(mapping::Owner value,const Limits& limits):owner(std::move(value)),port(limits.storageStats,limits.diversificationControl){};};
  Limits limits;std::optional<azureus_nodes::Contact> local;const std::thread::id thread=std::this_thread::get_id();
  std::map<std::string,std::unique_ptr<Record>,std::less<>> records;
  std::int64_t lastScan=0;std::uint64_t stores=0,reads=0,expiredCount=0;bool closed=false,busy=false;
  struct Guard {Impl& owner;explicit Guard(Impl& value):owner(value){owner.busy=true;}~Guard(){owner.busy=false;}};
  std::expected<void,Error> scan(lifecycle::WallClock&,bool);
  std::expected<void,Error> check()const noexcept {
    if(std::this_thread::get_id()!=thread){return std::unexpected(Error::Thread);}
    if(closed){return std::unexpected(Error::Closed);}if(busy){return std::unexpected(Error::Busy);}return {};
  }
  Snapshot snapshot()const {
    Snapshot result;result.closed=closed;result.keys=records.size();result.stores=stores;result.reads=reads;result.expired=expiredCount;
    for(const auto& [key,record]:records){(void)key;const auto value=record->owner.snapshot();
      result.values+=value.direct.size()+value.forwarded.size();
      result.bytes+=static_cast<std::size_t>(value.directBytes)+static_cast<std::size_t>(value.indirectBytes);
    }return result;
  }
};
Owner::Owner(std::unique_ptr<Impl> value):impl_(std::move(value)){}
Owner::~Owner()=default;
std::expected<std::unique_ptr<Owner>,Error> Owner::create(Limits limits){
  if(!limits.maximumKeys||limits.maximumKeys>64||!limits.maximumEntriesPerKey||limits.maximumEntriesPerKey>(limits.storageStats==StorageStatsMode::OriginalDiversification?256U:64U)
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
      if(snapshot.direct.empty()&&snapshot.forwarded.empty()){record.owner.close();it=records.erase(it);continue;}
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
      auto record=std::make_unique<Impl::Record>(std::move(*made),impl_->limits);
      KeyPort::Borrow borrowed(record->port,clock);
      if(!record->owner.open(std::move(*copied),false,record->port)||record->port.failed){return std::unexpected(Error::Mapping);}
      found=impl_->records.emplace(std::string(key),std::move(record)).first;
    }
    auto& record=*found->second;KeyPort::Borrow borrowed(record.port,clock);
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
      Read output;output.diversification=record.owner.snapshot().diversification;
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
    Read output;output.diversification=record.owner.snapshot().diversification;output.values.reserve(read->value.size());
    for(const auto& value:read->value){const auto current=value.snapshot();if(!current||!current->originator().get()||!current->payload().view()){return std::unexpected(Error::Mapping);}
      output.values.push_back({current->version(),current->creationTime(),std::string(*current->payload().view()),*current->originator().get(),current->flags(),current->lifeTimeHours(),current->replicationControl()});
    }
    if(impl_->reads==UINT64_MAX){return std::unexpected(Error::Capacity);}++impl_->reads;
    return std::optional<Read>{std::move(output)};
  }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<Snapshot,Error> Owner::snapshot()const{
  if(std::this_thread::get_id()!=impl_->thread){return std::unexpected(Error::Thread);}
  if(impl_->busy){return std::unexpected(Error::Busy);}
  try{return impl_->snapshot();}catch(...){return std::unexpected(Error::Memory);}
}
void Owner::close()noexcept{if(impl_->busy||std::this_thread::get_id()!=impl_->thread){return;}for(auto& [key,record]:impl_->records){(void)key;record->owner.close();}impl_->records.clear();impl_->closed=true;}
}
