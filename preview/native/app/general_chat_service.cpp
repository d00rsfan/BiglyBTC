#include "../engine/runtime_console.hpp"
// SPDX-License-Identifier: GPL-2.0-or-later
#include "general_chat_service.hpp"
#include "chat_profile_io.hpp"
#include "session.hpp"
#include "discovery.hpp"
#include "azureus_public_policy.hpp"
#include "azureus_contacts.hpp"
#include "biglybtc/azureus_endpoint.hpp"
#include "lookup.hpp"
#include <QCryptographicHash>
#include <thread>
#include <limits>
#include <libtorrent/session_handle.hpp>
#include <QCoreApplication>
#include <QEventLoop>
#include <QHostInfo>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <QDateTime>
#include <openssl/evp.h>
#include <atomic>
#include <deque>
#include <list>
#include <map>
#include <mutex>
#include <set>
namespace biglybtc::engine {
std::expected<ChatTransportFactory,storage::Error> ChatTransportFactory::owned(const libtorrent::session_handle& session,const storage::ProfileLease& lease,unsigned short port){
 auto held=lease.retain();if(!held){return std::unexpected(held.error());}
 return ChatTransportFactory{std::move(*held),[session,port]()->std::expected<std::shared_ptr<services::shared_udp::Provider>,services::azureus_udp::Failure>{
  auto value=services::shared_udp::Provider::attach(session,port);if(!value){return std::unexpected(value.error());}
  for(const auto& binding:(*value)->bindings()){if(!binding.address.isLoopback()){(*value)->close();return std::unexpected(services::azureus_udp::Failure::Authority);}}
  return std::shared_ptr<services::shared_udp::Provider>(std::move(*value));
 },true};
}
}
namespace biglybtc::app {
namespace {
namespace udp=services::azureus_udp;namespace wire=services::shared_udp;namespace msg=services::msgsync;
namespace nodes=services::azureus_nodes;namespace db=services::azureus_db_runtime;namespace router=services::azureus_router;
using Clock=std::chrono::steady_clock;using Broker=detail::AzureusProfileIo;using Kind=GeneralChatResult::Kind;
struct Command {std::uint64_t id=0;Kind kind=Kind::Join;std::string key{},text{};bool first=false,second=false;};
QEvent::Type commandEvent(){static const auto type=static_cast<QEvent::Type>(QEvent::registerEventType());return type;}
core::SharedUdpEndpoint endpoint(udp::Peer value){core::SharedUdpEndpoint out;out.port=value.port;if(value.address.protocol()==QAbstractSocket::IPv4Protocol){out.length=4;const auto ip=value.address.toIPv4Address();for(unsigned i=0;i<4;++i){out.address[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}}else{out.length=16;const auto ip=value.address.toIPv6Address();std::copy_n(ip.c,16,out.address.begin());}return out;}
QHostAddress address(const std::array<std::uint8_t,16>& bytes,unsigned length){if(length==4){return QHostAddress((quint32(bytes[0])<<24)|(quint32(bytes[1])<<16)|(quint32(bytes[2])<<8)|bytes[3]);}Q_IPV6ADDR ip{};std::copy_n(bytes.begin(),16,ip.c);return QHostAddress(ip);}
bool publicAddress(udp::Peer peer){const auto value=endpoint(peer);return core::azureusPublicAddress(std::span(value.address).first(value.length));}
storage::Error failure(storage::ErrorCode code,const char* message){return {code,message};}
struct RoomToken {std::uint64_t epoch=0;std::string key,configKey;storage::ChatIdentityStamp stamp{};std::atomic<bool> active{true};};
struct Shared {
 explicit Shared(std::shared_ptr<const storage::ProfileLease> lease,std::uint64_t generation):generation(generation),lease(std::move(lease)),io(std::make_unique<ChatProfileIo>(this->lease)),networkIo(std::make_unique<Broker>(this->lease)){}
 std::uint64_t generation;std::shared_ptr<const storage::ProfileLease> lease;
 std::unique_ptr<ChatProfileIo> io;std::unique_ptr<Broker> networkIo;
 std::atomic<std::shared_ptr<const GeneralChatSnapshot>> snapshot;
 std::atomic<std::shared_ptr<const core::SharedUdpWitness>> witness;
 std::atomic<bool> stop{false};std::mutex mutex;QObject* receiver=nullptr;
 std::deque<Command> commands;std::vector<GeneralChatResult> results;std::uint64_t next=0;std::size_t outstanding=0;
};
class Owner final:public QObject {
 struct Lane {
  wire::Listener listener;std::int32_t network=0;std::shared_ptr<storage::AzureusValueVersions> versions;
  std::shared_ptr<storage::AzureusContacts> contacts;std::shared_ptr<storage::AzureusPublicPolicy> policy;
  std::vector<wire::ProbePeer> seeds;std::unique_ptr<udp::OwnedPingTransport> actor;
  std::shared_ptr<router::RouterOwner> router;std::shared_ptr<db::Owner> database;
  nodes::Contact local;bool initializing=false,ready=false,probing=false,dns=false,dnsResolved=false;Clock::time_point probeAfter{};int dnsId=-1;std::string error;
 };
 struct Room {
  ChatRoomSettings settings;std::shared_ptr<msg::Room> core;std::unique_ptr<msg::RoomSession> session;
  std::shared_ptr<RoomToken> token;std::vector<std::size_t> lanes;
  std::string status;std::uint32_t unread=0;std::set<msg::MessageId> seen;
  std::uint64_t mutation=0,savingMutation=0;bool historyDirty=false,historyPending=false,opening=false;
  std::array<Clock::time_point,2> nextLookup{},nextPut{};
 };
 struct Prepared {std::array<bool,2> lanes{};std::set<std::uint64_t> keys;std::set<std::uint64_t> changed;};
 struct Operation {Broker::Operation run;Prepared data;};
 struct Frame {
  Clock::time_point end;Broker::Handle job;std::shared_ptr<Operation> operation;
  std::move_only_function<void()> effect,failed;bool executed=false;
 };
 struct Crypto {Broker::Handle barrier;std::move_only_function<void()> effect;};
 struct Meta {Command command;std::shared_ptr<RoomToken> token;bool history=false;};
 std::shared_ptr<Shared> shared_;QEventLoop& loop_;bool owned_=false;
 std::function<std::expected<std::shared_ptr<wire::Provider>,udp::Failure>()> attach_;
 std::shared_ptr<wire::Provider> provider_;std::array<std::unique_ptr<Lane>,2> lanes_;
 std::shared_ptr<std::map<std::int32_t,std::shared_ptr<storage::AzureusValueVersions>>> versions_=std::make_shared<std::map<std::int32_t,std::shared_ptr<storage::AzureusValueVersions>>>();
 std::map<std::string,Room> rooms_;std::map<std::uint64_t,Meta> metadata_;std::list<Frame> frames_;std::deque<Crypto> crypto_;
 Frame* active_=nullptr;QTimer pulse_,timer_;std::uint64_t epoch_=0,sequence_=0,ticks_=0;
 std::string lastConsoleAvailability_;
 std::string selected_,availability_="Opening saved Public rooms…";bool dirty_=true,polling_=false,closing_=false;
 Clock::time_point attachEnd_=Clock::now()+std::chrono::seconds(30),nextAttach_{};unsigned attachAttempts_=0;bool attachExhausted_=false;
 Clock::time_point closeEnd_{},lastHistory_=Clock::now(),lastBindings_=Clock::now();
 bool allowed(std::size_t lane)const{return active_&&Clock::now()<active_->end&&active_->operation->data.lanes[lane]&&!shared_->stop.load();}
 void result(const Command& command,std::optional<storage::Error> error={}){
  if(!command.id){return;}std::lock_guard lock(shared_->mutex);shared_->results.push_back({command.id,command.kind,command.key,command.text,std::move(error)});
 }
 void changed(){dirty_=true;}
 bool later(std::move_only_function<void()> effect){
  if(closing_||crypto_.size()>=128){return false;}Broker::Handle barrier;
  if(active_){barrier=active_->job;}else{for(auto& frame:frames_){if(frame.job&&frame.executed&&!Broker::released(frame.job)){barrier=frame.job;}}}
  crypto_.push_back({std::move(barrier),std::move(effect)});return true;
 }
 bool gate(Clock::time_point end,std::move_only_function<void()> effect,std::move_only_function<void()> failed={},Broker::Operation custom={}){
  if(closing_||frames_.size()>=64||end<=Clock::now()){return false;}
  auto operation=std::make_shared<Operation>();
  if(custom){operation->run=std::move(custom);}else{
   std::array<std::shared_ptr<storage::AzureusPublicPolicy>,2> policies{};
   for(std::size_t i=0;i<2;++i){if(lanes_[i]){policies[i]=lanes_[i]->policy;}}
   std::vector<std::shared_ptr<RoomToken>> keys;for(const auto& [key,room]:rooms_){(void)key;if(room.token){keys.push_back(room.token);}}
   operation->run=[operation=std::weak_ptr<Operation>(operation),policies=std::move(policies),keys=std::move(keys),end](std::stop_token stop)->Broker::Result{
    const auto op=operation.lock();if(!op||stop.stop_requested()||Clock::now()>=end){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Chat source operation expired"));}
    std::shared_ptr<storage::AzureusPublicPolicy> valid;
    for(std::size_t i=0;i<2;++i){if(policies[i]&&policies[i]->validate()){op->data.lanes[i]=true;if(!valid){valid=policies[i];}}}
    if(valid){for(const auto& key:keys){if(!key->active.load()){continue;}if(valid->validateChatIdentity(key->configKey,key->stamp)){op->data.keys.insert(key->epoch);}else{op->data.changed.insert(key->epoch);}}}
    return Broker::Data{};
   };
  }
  frames_.push_back({end,{},std::move(operation),std::move(effect),std::move(failed),false});return true;
 }
 void drainFrames(){
  shared_->networkIo->drainWake();
  for(auto at=frames_.begin();at!=frames_.end();){
   if(!at->job){
    if(Clock::now()>=at->end){if(at->failed){at->failed();}at=frames_.erase(at);continue;}
    const auto box=at->operation;
    auto job=shared_->networkIo->submit([box](std::stop_token stop){return box->run(stop);});
    if(!job){++at;continue;}at->job=std::move(*job);
   }
   if(!at->executed&&Clock::now()>=at->end){Broker::revoke(at->job);}
   if(!at->executed){if(auto ready=Broker::take(at->job)){
    at->executed=true;active_=&*at;
    const auto acknowledged=qScopeGuard([&]{active_=nullptr;Broker::acknowledge(at->job);});
    for(auto& [key,room]:rooms_){(void)key;if(room.token&&at->operation->data.changed.contains(room.token->epoch)){room.token->active=false;if(room.session){room.session->close();}room.status="Saved identity changed; rejoin this room";changed();}}
    try{if(*ready&&Clock::now()<at->end&&!shared_->stop.load()){at->effect();}else if(at->failed){at->failed();}}catch(...){availability_="A chat operation failed";changed();}
   }}
   if(Broker::released(at->job)){if(!at->executed&&at->failed){at->failed();}shared_->networkIo->release(at->job);at=frames_.erase(at);}else{++at;}
  }
 }
 bool meta(ChatProfileRequest request,Meta info){
  const auto id=shared_->io->submit(std::move(request),shared_->generation,Clock::now()+std::chrono::seconds(30));if(!id){result(info.command,id.error());return false;}metadata_.emplace(*id,std::move(info));return true;
 }
 void catalogue(std::vector<ChatRoomSettings> values){
  for(auto& value:values){auto where=rooms_.find(value.key);if(where==rooms_.end()){if(!(value.favourite||value.saveHistory)||rooms_.size()>=256){continue;}where=rooms_.try_emplace(value.key).first;}where->second.settings=std::move(value);}
  changed();
 }
 void loadRoom(const Command& command){
  if(command.key.empty()||command.key.size()>4096){result(command,failure(storage::ErrorCode::Unsupported,"Invalid room name"));return;}
  if(!rooms_.contains(command.key)&&rooms_.size()>=256){result(command,failure(storage::ErrorCode::Busy,"Saved room list is full"));return;}
  auto& room=rooms_[command.key];if(room.opening){result(command,failure(storage::ErrorCode::Busy,"Room is already opening"));return;}if(room.core&&room.token&&room.token->active){selected_=command.key;result(command);changed();return;}
  std::size_t open=0;for(const auto& [key,value]:rooms_){(void)key;open+=bool(value.core&&value.token&&value.token->active)||value.opening;}
  if(open>=16){result(command,failure(storage::ErrorCode::Busy,"Leave a room before opening another"));return;}
  room.settings.key=command.key;room.status="Opening room…";room.opening=true;selected_=command.key;
  if(!meta(ChatProfileLoad{command.key,true},{command,{},false})){room.opening=false;}changed();
 }
 void metadata(){
  for(auto& completion:shared_->io->advance()){
   const auto found=metadata_.find(completion.id);if(found==metadata_.end()){continue;}auto info=std::move(found->second);metadata_.erase(found);
   const auto key=info.command.key;const auto row=rooms_.find(key);if(row!=rooms_.end()&&info.command.kind==Kind::Join){row->second.opening=false;}
   if(info.history){if(row!=rooms_.end()&&row->second.token==info.token){row->second.historyPending=false;if(!completion.result){row->second.status="History could not be saved";}else{row->second.historyDirty=row->second.mutation!=row->second.savingMutation;}}changed();continue;}
   if(!completion.result){if(row!=rooms_.end()){row->second.status=completion.result.error().message;}result(info.command,completion.result.error());changed();continue;}
   auto& data=*completion.result;
   if(info.command.kind==Kind::Join&&key.empty()){
    catalogue(std::move(data.catalogue));std::vector<std::string> favourites;for(const auto& [name,room]:rooms_){if(room.settings.favourite){favourites.push_back(name);}}
    for(const auto& name:favourites){loadRoom({0,Kind::Join,name});}continue;
   }
   if(data.identity&&!closing_&&row!=rooms_.end()){
    auto identity=std::move(*data.identity);auto opened=msg::Room::open(key,identity.uid,std::move(identity.signer),"de",identity.settings.readOnly);
    if(!opened){result(info.command,failure(storage::ErrorCode::Unsupported,"Room identity or managed policy is unsupported"));continue;}
    auto& room=rooms_[key];if(room.token){room.token->active=false;}room.session.reset();room.settings=identity.settings;room.core=std::make_shared<msg::Room>(std::move(*opened));
    room.token=std::make_shared<RoomToken>();room.token->epoch=++epoch_;room.token->key=key;room.token->configKey=ChatProfile::configKey(key);room.token->stamp=identity.sourceStamp;
    if(data.history){static_cast<void>(room.core->importHistory(std::move(*data.history),QDateTime::currentMSecsSinceEpoch()));}
    if(data.historyError){room.status="Stored history is unavailable";}observe(room,false);rebind(room);
   }else if(!data.catalogue.empty()){
    catalogue(std::move(data.catalogue));if(data.history&&row!=rooms_.end()&&row->second.core){static_cast<void>(row->second.core->importHistory(std::move(*data.history),QDateTime::currentMSecsSinceEpoch()));}
   }
   result(info.command);changed();
  }
 }
 void rebind(Room& room){
  if(!room.core||!room.token||!room.token->active||closing_){return;}
  std::vector<udp::OwnedPingTransport*> actors;std::vector<std::size_t> mapping;
  for(std::size_t i=0;i<2;++i){if(lanes_[i]&&lanes_[i]->ready&&lanes_[i]->actor){actors.push_back(lanes_[i]->actor.get());mapping.push_back(i);}}
  if(mapping==room.lanes&&room.session){return;}room.session.reset();room.lanes=mapping;if(actors.empty()){room.status="Connecting to Public chat network…";return;}
  if(auto encoded=msg::RoomSession::contactMap(actors.front()->endpoint())){room.core->updateContact(std::move(*encoded));}
  msg::RoomSessionHooks hooks;const auto token=room.token;
  hooks.afterRelease=[this](auto callback){return later(std::move(callback));};
  hooks.withPermit=[this](auto end,auto effect){return gate(end,std::move(effect));};
  hooks.revision=[this,token]()->std::optional<std::uint64_t>{if(active_&&token->active&&active_->operation->data.keys.contains(token->epoch)){return token->epoch;}return {};};
  hooks.hint=[this,token,mapping](std::size_t lane,const msg::NodeHint& value){if(lane>=mapping.size()||!token->active){return;}acceptHint(token,mapping[lane],value);};
  hooks.changed=[this,token]{const auto it=rooms_.find(token->key);if(it!=rooms_.end()&&it->second.token==token){observe(it->second,true);}changed();};
  auto session=msg::RoomSession::open(room.core,actors,std::move(hooks));if(session){room.session=std::move(*session);room.status="Finding room nodes…";}else{room.status="Room transport is busy";}
 }
 void observe(Room& room,bool unread){
  if(!room.core){return;}auto messages=room.core->messages(QDateTime::currentMSecsSinceEpoch());if(!messages){return;}
  std::set<msg::MessageId> current;std::uint32_t added=0;
  for(const auto& message:*messages){current.insert(message.id);if(!room.seen.contains(message.id)){++added;}}
  if(added){++room.mutation;room.historyDirty=true;if(unread&&selected_!=room.settings.key){room.unread=std::min<std::uint32_t>(100000,room.unread+added);}}
  room.seen=std::move(current);
 }
 void acceptHint(std::shared_ptr<RoomToken>,std::size_t,const msg::NodeHint&);
 void attachTransport();
 void startNetworks();
 void initializeLane(std::size_t);
 void findSeeds(std::size_t);
 void probe(std::size_t);
 void activate(std::size_t,udp::Peer);
 void ingress(wire::Binding,udp::Peer,std::string);
 struct LookupGroup {std::set<nodes::NodeId> keys;std::set<std::pair<nodes::NodeId,std::string>> records;bool diversified=false;};
 struct Lookup {
  std::shared_ptr<RoomToken> token;std::size_t lane=0;services::azureus_control::LookupController control;
  std::map<std::uint64_t,udp::Ticket> tickets;bool diversified=false,finished=false,publishing=false;
  Clock::time_point end;nodes::NodeId target{};std::shared_ptr<LookupGroup> group;unsigned depth=0;bool root=true;
 };
 std::vector<std::shared_ptr<Lookup>> lookups_;std::deque<std::shared_ptr<Lookup>> pendingLookups_;std::uint64_t lookupId_=0;
 void lookupStart(Room&,std::size_t);
 void lookupBegin(std::shared_ptr<Lookup>);
 void lookupDiversified(std::shared_ptr<Lookup>,std::int8_t);
 void lookupPump(std::shared_ptr<Lookup>);
 void lookupReply(std::shared_ptr<Lookup>,std::uint64_t,udp::FindValueOutcome);
 void putMembership(std::shared_ptr<Lookup>);
 void discovery();
 void poll();
 void command(Command);
 void beginClose();
 void flushHistory();
 void publish();
public:
 Owner(std::shared_ptr<Shared> shared,QEventLoop& loop,bool owned,std::function<std::expected<std::shared_ptr<wire::Provider>,udp::Failure>()> attach)
  :shared_(std::move(shared)),loop_(loop),owned_(owned),attach_(std::move(attach)){
  pulse_.setInterval(25);connect(&pulse_,&QTimer::timeout,this,[this]{poll();});timer_.setInterval(2500);connect(&timer_,&QTimer::timeout,this,[this]{if(!closing_){++ticks_;for(auto& [key,room]:rooms_){(void)key;if(room.session){room.session->tick(ticks_,QDateTime::currentMSecsSinceEpoch());}}discovery();}});
 }
 ~Owner()override{std::lock_guard lock(shared_->mutex);shared_->receiver=nullptr;}
 void start(){
  {std::lock_guard lock(shared_->mutex);shared_->receiver=this;}shared_->io->bind(this);shared_->networkIo->bind(this);
  meta(ChatProfileList{},{{0,Kind::Join,{}},{},false});
  attachTransport();
  pulse_.start();timer_.start();poll();
 }
 bool event(QEvent* event)override{if(event->type()==Broker::wakeEvent()||event->type()==commandEvent()){poll();return true;}return QObject::event(event);}
};
namespace control=services::azureus_control;
std::int64_t monotonic(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
nodes::Contact contact(udp::Peer peer,std::int8_t version=55){
 const auto e=endpoint(peer);nodes::ResolvedEndpoint resolved;resolved.address.bytes=e.address;resolved.address.length=e.length;resolved.address.port=e.port;
 nodes::NodeCache cache;const auto id=cache.lookup(resolved,version);nodes::Contact out;out.address=resolved.address;out.protocol=version;if(id){out.id=id->id;}return out;
}
udp::Peer peerOf(const nodes::Contact& c){return {address(c.address.bytes,c.address.length),c.address.port};}
void Owner::attachTransport(){
 if(provider_||closing_||shared_->stop.load()){return;}++attachAttempts_;nextAttach_=Clock::now()+std::chrono::seconds(std::min(5U,attachAttempts_));
 auto attached=attach_();if(attached){provider_=std::move(*attached);shared_->witness.store(provider_->witness());if(shared_->stop.load()){provider_->witness()->revoke();return;}provider_->receive([this](auto local,auto peer,auto bytes){ingress(local,std::move(peer),std::move(bytes));return true;});startNetworks();}
 else{availability_=Clock::now()<attachEnd_?"Waiting for the Public UDP listener…":"Public chat transport could not be attached";changed();}
}
void Owner::startNetworks(){
 auto listeners=provider_->listeners();std::ranges::stable_sort(listeners,[](const auto& a,const auto& b){return publicAddress(a.local)>publicAddress(b.local);});
 for(const auto& listener:listeners){
  const auto family=endpoint(listener.local).length;const std::size_t i=family==4?0:1;
  if(lanes_[i]||!listener.local.port||listener.local.address.isNull()||(owned_?!listener.local.address.isLoopback():listener.local.address.isLoopback())){continue;}
  lanes_[i]=std::make_unique<Lane>();lanes_[i]->listener=listener;lanes_[i]->network=owned_?4:(i==0?0:3);initializeLane(i);
 }availability_=(lanes_[0]||lanes_[1])?"Connecting to Public chat network…":"No suitable UDP listener for Public chats";changed();
}
void Owner::initializeLane(std::size_t i){
 auto& lane=*lanes_[i];if(lane.initializing||closing_){return;}lane.initializing=true;
 struct Init {std::shared_ptr<storage::AzureusValueVersions> versions;std::shared_ptr<storage::AzureusContacts> contacts;std::shared_ptr<storage::AzureusPublicPolicy> policy;std::vector<wire::ProbePeer> seeds;std::optional<storage::Error> error;};auto init=std::make_shared<Init>();
 const auto lease=shared_->lease;const auto owners=versions_;const auto network=lane.network;const auto listener=lane.listener;const auto witness=provider_->witness();const bool owned=owned_;
 const auto operation=[init,lease,owners,network,listener,witness,owned](std::stop_token)->Broker::Result{
  auto found=owners->find(network);if(found==owners->end()){auto made=storage::AzureusValueVersions::open(*lease,network);if(!made){init->error=made.error();return std::unexpected(made.error());}found=owners->emplace(network,std::move(*made)).first;}init->versions=found->second;
  const auto local=endpoint(listener.local);storage::AzureusPublicBind binding{network,local.length,local.address,local.port};
  auto policy=owned?storage::AzureusPublicPolicy::openOwnedShared(*lease,init->versions,binding,witness):storage::AzureusPublicPolicy::openBootstrapShared(*lease,init->versions,network,local,witness);
  if(!policy){init->error=policy.error();return std::unexpected(policy.error());}init->policy=std::move(*policy);
  auto saved=storage::AzureusContacts::open(*lease,network,local.length,owned?storage::AzureusContactScope::OwnedLoopback:storage::AzureusContactScope::PublicBound);
  if(saved){init->contacts=std::move(*saved);auto snapshot=init->contacts->snapshot();if(snapshot){for(const auto& value:(*snapshot)->contacts){if(value.length==local.length){init->seeds.push_back({{address(value.address,value.length),value.port},value.protocol});}}}}
  return Broker::Data{};
 };
 if(!gate(Clock::now()+std::chrono::seconds(30),[this,i,init]{auto& value=*lanes_[i];value.initializing=false;value.versions=init->versions;value.contacts=init->contacts;value.policy=init->policy;value.seeds=std::move(init->seeds);active_->operation->data.lanes[i]=true;
  const auto local=value.listener.local;
  if(owned_){activate(i,local);}else{findSeeds(i);}changed();
 },[this,i,init]{lanes_[i]->initializing=false;lanes_[i]->error=init->error?init->error->message:"Could not open chat network policy";availability_=lanes_[i]->error;changed();},std::move(operation))){lane.initializing=false;}
}
void Owner::findSeeds(std::size_t i){
 auto& lane=*lanes_[i];if(owned_){if(!lane.ready){activate(i,lane.listener.local);}return;}if(lane.dns||closing_){return;}
 if(lane.dnsResolved&&lane.seeds.size()>=4){if(!lane.ready){probe(i);}return;}lane.dns=true;
 lane.dnsId=QHostInfo::lookupHost(i==0?QStringLiteral("dht.biglybt.com"):QStringLiteral("dht6.biglybt.com"),this,[this,i](const QHostInfo& result){auto& lane=*lanes_[i];lane.dns=false;lane.dnsId=-1;lane.dnsResolved=result.error()==QHostInfo::NoError;if(closing_){return;}
  std::vector<wire::ProbePeer> fresh;for(const auto& ip:result.addresses()){if(endpoint({ip,6881}).length==(i==0?4:16)&&publicAddress({ip,6881})){fresh.push_back({{ip,6881},55});}}
  // Put current bootstrap contacts ahead of stale saved hints; keep the bound.
  for(auto at=fresh.rbegin();at!=fresh.rend();++at){std::erase_if(lane.seeds,[&](const auto& old){return old.endpoint==at->endpoint;});lane.seeds.insert(lane.seeds.begin(),*at);}if(lane.seeds.size()>32){lane.seeds.resize(32);}
  if(lane.ready){gate(Clock::now()+std::chrono::seconds(5),[this,i]{if(!allowed(i)){return;}auto& lane=*lanes_[i];for(const auto& seed:lane.seeds){static_cast<void>(lane.actor->attachKnownPeer(seed.endpoint,seed.version,Clock::now()+std::chrono::seconds(5)));}static_cast<void>(lane.actor->integrateKnownRouting(std::chrono::seconds(30)));});}else{probe(i);}
 });
}
void Owner::probe(std::size_t i){
 auto& lane=*lanes_[i];if(lane.ready||lane.probing||closing_||!lane.policy||Clock::now()<lane.probeAfter){return;}if(lane.seeds.empty()){lane.error="No DHT bootstrap contacts; retrying";changed();return;}lane.probing=true;
 const bool queued=gate(Clock::now()+std::chrono::seconds(5),[this,i]{
  auto& lane=*lanes_[i];if(!allowed(i)){lane.probing=false;lane.probeAfter=Clock::now()+std::chrono::seconds(30);return;}
  const auto accepted=provider_->probeAddress(lane.listener.local,lane.network,lane.seeds,owned_,
   [this](auto end,auto effect){return gate(end,std::move(effect));},[this,i]{return allowed(i);},
   [this,i](wire::AddressResult result){auto& lane=*lanes_[i];lane.probing=false;lane.probeAfter=Clock::now()+std::chrono::seconds(30);if(closing_){return;}if(!result){lane.error="DHT address discovery is retrying";changed();return;}lane.seeds=std::move(result->candidates);activate(i,result->external);});
  if(!accepted){lane.probing=false;lane.probeAfter=Clock::now()+std::chrono::seconds(2);lane.error="Waiting for DHT address discovery";changed();}
 },[this,i]{lanes_[i]->probing=false;lanes_[i]->probeAfter=Clock::now()+std::chrono::seconds(30);});
 if(!queued){lane.probing=false;lane.probeAfter=Clock::now()+std::chrono::seconds(2);}
}
void Owner::activate(std::size_t i,udp::Peer external){
 auto& lane=*lanes_[i];if(lane.ready||closing_||!lane.versions){return;}
 const auto lease=shared_->lease;const auto versions=lane.versions;const auto local=endpoint(lane.listener.local);const auto advertised=endpoint(external);const auto witness=provider_->witness();const auto network=lane.network;const bool owned=owned_;
 auto policy=std::make_shared<std::shared_ptr<storage::AzureusPublicPolicy>>();
 gate(Clock::now()+std::chrono::seconds(10),[this,i,external,policy]{
  auto& lane=*lanes_[i];lane.policy=*policy;active_->operation->data.lanes[i]=true;
  udp::Config config;config.network=lane.network;config.servePings=true;config.routingScope=owned_?udp::RoutingScope::OwnedLoopback:udp::RoutingScope::PublicBound;config.maximumPeers=router::MaximumRouterContacts;config.maximumPending=128;
  auto actor=std::make_unique<udp::OwnedPingTransport>(std::move(config));auto channel=provider_->channel(lane.listener.local,external,[this,i]{return allowed(i);});
  if(!channel||!actor->startShared(std::move(*channel),owned_?std::function<bool()>{}:std::function<bool()>{[this,i]{return allowed(i);}})){lane.error="Chat listener could not be attached";changed();return;}
  lane.local=contact(external);auto router=router::RouterOwner::create(lane.local.id,{},shared_->generation,monotonic(),QDateTime::currentMSecsSinceEpoch());auto database=db::Owner::create();
  if(!router||!database||!(*database)->bindLocal(lane.local)){lane.error="Chat routing could not be initialised";changed();return;}
  lane.router=std::move(*router);lane.database=std::move(*database);lane.actor=std::move(actor);
  if(!lane.actor->serveDatabase(lane.router,lane.database,[this,i]{return allowed(i);},[this,i](auto done){auto callback=std::make_shared<std::move_only_function<void(bool)>>(std::move(done));return gate(Clock::now()+std::chrono::seconds(5),[this,i,callback]{(*callback)(allowed(i));},[callback]{(*callback)(false);});})){lane.actor.reset();lane.error="Chat database binding failed";changed();return;}
  lane.ready=true;lane.error.clear();for(const auto& seed:lane.seeds){if(seed.endpoint!=external){static_cast<void>(lane.actor->attachKnownPeer(seed.endpoint,seed.version,Clock::now()+std::chrono::seconds(10)));}}static_cast<void>(lane.actor->integrateKnownRouting(std::chrono::seconds(30)));
  later([this,i]{if(!owned_){findSeeds(i);}for(auto& [key,room]:rooms_){(void)key;rebind(room);}availability_="Connecting to Public DHT…";changed();});
 },[this,i]{lanes_[i]->error="Public chat policy was rejected";changed();},[lease,versions,network,local,advertised,witness,owned,policy](std::stop_token)->Broker::Result{
  storage::AzureusPublicBind binding{network,advertised.length,advertised.address,advertised.port};auto result=owned?storage::AzureusPublicPolicy::openOwnedShared(*lease,versions,binding,witness):storage::AzureusPublicPolicy::openShared(*lease,versions,binding,local,witness);if(!result){return std::unexpected(result.error());}*policy=std::move(*result);return Broker::Data{};
 });
}
void Owner::ingress(wire::Binding local,udp::Peer peer,std::string bytes){
 if(closing_){return;}for(std::size_t i=0;i<2;++i){auto* lane=lanes_[i].get();if(!lane||!lane->ready||local!=lane->listener.local){continue;}
  if(!lane->actor->isRoutingPeer(peer)){
   if(bytes.empty()||!(static_cast<unsigned char>(bytes[0])&128)||peer==lane->actor->endpoint()||(owned_?!peer.address.isLoopback():!publicAddress(peer))){return;}
   services::azureus_dht::Context context;context.network=lane->network;context.localVersion=55;context.now=QDateTime::currentMSecsSinceEpoch();
   auto header=services::azureus_dht::decodeHeader(services::azureus_dht::Kind::Request,bytes,context);if(!header){return;}const auto& fields=header->fields();std::shared_ptr<RoomToken> roomToken;const auto actual=contact(peer,fields.originatorVersion);
   if(!fields.originator||*fields.originator!=actual.address){return;}
   if(fields.action==1035){auto decoded=services::azureus_transfer::decode(bytes,context);if(!decoded){return;}bool known=false;for(const auto& [key,room]:rooms_){(void)key;if(room.core&&room.token&&room.token->active&&room.core->handler()==decoded->data.handler){known=true;roomToken=room.token;break;}}if(!known){return;}}
   else if(fields.action!=1024&&fields.action!=1028){return;}
   gate(Clock::now()+std::chrono::seconds(3),[this,i,peer,roomToken,version=fields.originatorVersion,bytes=std::move(bytes)]()mutable{auto& lane=*lanes_[i];if(!allowed(i)){return;}if(roomToken&&(!roomToken->active||!active_->operation->data.keys.contains(roomToken->epoch))){return;}const auto admitted=roomToken?lane.actor->allowPeer(peer):lane.actor->attachKnownPeer(peer,version,Clock::now()+std::chrono::seconds(3));if(admitted){lane.actor->receiveShared(peer,std::move(bytes));}});return;
  }
  lane->actor->receiveShared(std::move(peer),std::move(bytes));return;
 }
}
void Owner::acceptHint(std::shared_ptr<RoomToken> token,std::size_t lane,const msg::NodeHint& hint){
 auto candidate=msg::RoomSession::contactHint(lane,hint);if(!candidate||candidate->uid==rooms_[token->key].core->uid()){return;}
 if(owned_?!candidate->endpoint.address.isLoopback():!publicAddress(candidate->endpoint)){return;}
 lane=candidate->endpoint.address.protocol()==QAbstractSocket::IPv4Protocol?0:1;if(!lanes_[lane]||!lanes_[lane]->ready){return;}
 gate(Clock::now()+std::chrono::seconds(5),[this,token,lane,candidate=std::move(*candidate)]()mutable{
  const auto found=rooms_.find(token->key);if(found==rooms_.end()||found->second.token!=token||!token->active||!allowed(lane)||!active_->operation->data.keys.contains(token->epoch)){return;}
  auto& room=found->second;const auto mapping=std::ranges::find(room.lanes,lane);if(mapping==room.lanes.end()||!room.session){return;}
  if(!lanes_[lane]->actor->allowPeer(candidate.endpoint)){return;}
  candidate.lane=static_cast<std::size_t>(mapping-room.lanes.begin());room.session->addPeer(std::move(candidate));changed();
 });
}
void Owner::lookupStart(Room& room,std::size_t i){
 if(!room.session||!room.token||!room.token->active||!lanes_[i]||!lanes_[i]->ready||lookups_.size()>=8){return;}
 const auto same=[&](const auto& v){return !v->finished&&v->lane==i&&v->token==room.token;};if(std::ranges::any_of(lookups_,same)||std::ranges::any_of(pendingLookups_,same)){return;}
 auto value=std::make_shared<Lookup>();value->token=room.token;value->lane=i;value->end=Clock::now()+std::chrono::seconds(60);value->group=std::make_shared<LookupGroup>();
 const auto digest=QCryptographicHash::hash(QByteArray(room.core->handler().data(),static_cast<qsizetype>(room.core->handler().size())),QCryptographicHash::Sha1);std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.constData()),20,value->target.begin());
 value->group->keys.insert(value->target);room.nextLookup[i]=Clock::now()+(room.session->facts().peers<2?std::chrono::seconds(10):std::chrono::seconds(300));lookupBegin(value);
}
void Owner::lookupBegin(std::shared_ptr<Lookup> value){
 const auto i=value->lane;if(closing_||!value->token->active||Clock::now()>=value->end||!lanes_[i]||!lanes_[i]->ready||lookups_.size()>=8||value->group->records.size()>=32){return;}
 auto& lane=*lanes_[i];const auto routing=lane.actor->routingSnapshot();std::vector<control::AdmittedContact> seeds;
 if(routing){for(std::size_t j=0;j<routing->contactCount;++j){const auto& c=routing->contacts[j].contact;if(c.id!=lane.local.id){seeds.push_back({c,true});}}}
 std::ranges::sort(seeds,[&](const auto& a,const auto& b){for(std::size_t j=0;j<20;++j){const auto aa=a.contact.id[j]^value->target[j],bb=b.contact.id[j]^value->target[j];if(aa!=bb){return aa<bb;}}return false;});if(seeds.size()>20){seeds.resize(20);}
 if(seeds.empty()){return;}control::LookupOptions options;options.target=value->target;options.localId=lane.local.id;options.timeoutMillis=std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(value->end-Clock::now()).count());options.maximumValues=32-value->group->records.size();if(!owned_){options.maximumContacts=control::LookupController::MaximumContacts;}options.addressScope=owned_?control::LookupAddressScope::OwnedLoopback:control::LookupAddressScope::PublicValue;
 if(!value->control.start(shared_->generation,++lookupId_,options,seeds,monotonic())){return;}lookups_.push_back(value);lookupPump(value);
}
void Owner::lookupDiversified(std::shared_ptr<Lookup> value,std::int8_t type){
 value->diversified=true;value->group->diversified=true;if((type!=2&&type!=3)||value->depth>=4||value->group->records.size()>=32||value->group->keys.size()>=32){return;}
 const unsigned first=QRandomGenerator::system()->bounded(10U),second=(first+1+QRandomGenerator::system()->bounded(9U))%10;const std::array offsets{first,second};
 auto keys=msg::diversifiedDiscoveryKeys(value->target,type,std::span(offsets).first(type==2?1:2));if(!keys){return;}
 for(const auto& key:*keys){if(value->group->keys.size()>=32||pendingLookups_.size()>=64){break;}if(!value->group->keys.insert(key).second){continue;}auto child=std::make_shared<Lookup>();child->token=value->token;child->lane=value->lane;child->end=value->end;child->target=key;child->depth=value->depth+1;child->root=false;child->group=value->group;pendingLookups_.push_back(std::move(child));}
}
template<class T>void applied(const std::shared_ptr<T>& value,std::expected<void,control::LookupError> result){if(!result){value->finished=true;}}
void Owner::lookupPump(std::shared_ptr<Lookup> value){
 if(value->finished||closing_){return;}const auto i=value->lane;
 gate(std::min(value->end,Clock::now()+std::chrono::seconds(5)),[this,value,i]{
  if(!value->token->active||!allowed(i)||!active_->operation->data.keys.contains(value->token->epoch)){value->finished=true;return;}
  auto& actor=*lanes_[i]->actor;auto intents=value->control.takeIntents(monotonic());if(!intents){value->finished=true;return;}
  for(const auto& intent:*intents){const auto peer=peerOf(intent.contact);if(!actor.attachKnownPeer(peer,intent.contact.protocol,value->end)){applied(value,value->control.finish(intent.node,control::LookupNodeState::Rejected,monotonic()));continue;}
   auto sent=actor.findValue(peer,intent.contact.protocol,intent.target,intent.flags,intent.maximumValues,this,[this,value,node=intent.node](auto out){lookupReply(value,node,std::move(out));},std::min(std::chrono::seconds(15),std::chrono::duration_cast<std::chrono::seconds>(value->end-Clock::now())));
   if(sent){value->tickets[intent.node]=*sent;applied(value,value->control.sent(intent.node,sent->value,monotonic()));}else{applied(value,value->control.finish(intent.node,control::LookupNodeState::Failed,monotonic()));}
  }
  if(auto cancellations=value->control.cancellationIntents()){for(const auto& request:*cancellations){const auto found=value->tickets.find(request.node);if(found!=value->tickets.end()){actor.cancel(found->second);}}}
  if(control::lookupTerminal(value->control.state())){value->finished=true;if(value->root&&!value->group->diversified){putMembership(value);}changed();}
 },[value]{value->finished=true;});
}
void Owner::lookupReply(std::shared_ptr<Lookup> value,std::uint64_t node,udp::FindValueOutcome outcome){
 if(value->finished||closing_){return;}auto box=std::make_shared<udp::FindValueOutcome>(std::move(outcome));
 gate(std::min(value->end,Clock::now()+std::chrono::seconds(5)),[this,value,node,box]{
  if(!value->token->active||!allowed(value->lane)){value->finished=true;return;}
  auto& actor=*lanes_[value->lane]->actor;
  if(!*box){applied(value,value->control.finish(node,control::LookupNodeState::Timeout,monotonic()));value->tickets.erase(node);}
  else{const auto* reply=std::get_if<udp::lookup::ReplyBody>(&(*box)->body());if(!reply){applied(value,value->control.finish(node,control::LookupNodeState::Failed,monotonic()));}
   else if(const auto* values=std::get_if<udp::lookup::FoundValues>(&reply->result)){
    if(values->diversification!=1&&!value->diversified){lookupDiversified(value,values->diversification);}applied(value,value->control.values(node,values->values,reply->continuation,values->diversification,monotonic()));
    auto records=values->values;later([this,value,records=std::move(records)]{if(!value->token->active){return;}for(const auto& record:records){if(value->group->records.size()>=32){break;}if(!value->group->records.insert({record.originator.id,record.bytes}).second){continue;}auto data=core::Document::parse(record.bytes,{512,512,32,4});if(!data||data->root().type!=core::Node::Type::Dictionary){continue;}const auto* uid=data->find("u");if(!uid||uid->type!=core::Node::Type::Bytes||uid->bytes.size()!=8){continue;}auto encoded=msg::RoomSession::contactMap(peerOf(record.originator),record.originator.protocol);if(!encoded){continue;}msg::NodeHint hint;std::copy_n(reinterpret_cast<const std::uint8_t*>(uid->bytes.data()),8,hint.uid.begin());hint.encodedContact=std::move(*encoded);acceptHint(value->token,value->lane,hint);}});
   }else if(const auto* contacts=std::get_if<udp::lookup::FoundContacts>(&reply->result)){
    std::vector<control::AdmittedContact> accepted;for(const auto& c:contacts->contacts){if(actor.attachKnownPeer(peerOf(c),c.protocol,value->end)){accepted.push_back({c,true});}}
    applied(value,value->control.contacts(node,accepted,reply->continuation,monotonic()));
   }
   if(reply&&!reply->continuation){value->tickets.erase(node);}
  }later([this,value]{lookupPump(value);});
 },[value]{value->finished=true;});
}
struct PublicationClock final:services::azureus_db_value::WallClock{std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}};
void Owner::putMembership(std::shared_ptr<Lookup> value){
 auto room=rooms_.find(value->token->key);if(room==rooms_.end()||room->second.token!=value->token||!value->token->active||value->publishing){return;}const auto i=value->lane;
 if(Clock::now()<room->second.nextPut[i]){return;}room->second.nextPut[i]=Clock::now()+std::chrono::minutes(20);value->publishing=true;
 const auto policy=lanes_[i]->policy;const auto versions=lanes_[i]->versions;auto reserved=std::make_shared<std::optional<std::int32_t>>();const auto token=value->token;
 gate(Clock::now()+std::chrono::seconds(10),[this,value,i,reserved]{
  if(!value->token->active||!reserved->has_value()){return;}auto& lane=*lanes_[i];active_->operation->data.lanes[i]=true;active_->operation->data.keys.insert(value->token->epoch);
  auto found=rooms_.find(value->token->key);if(found==rooms_.end()||found->second.token!=value->token){return;}
  const auto uid=found->second.core->uid();const std::string payload="d1:u8:"+std::string(reinterpret_cast<const char*>(uid.data()),uid.size())+"e";
  const std::string key(reinterpret_cast<const char*>(value->target.data()),value->target.size());PublicationClock clock;auto local=lane.database->putLocal(key,payload,**reserved,clock);if(!local){return;}
  auto publication=std::make_shared<services::azureus_values::Value>(services::azureus_values::Value{local->version(),local->creationTime(),payload,lane.local,local->flags(),local->lifeTimeHours(),local->replicationControl()});
  const auto snapshot=value->control.snapshot();if(!snapshot){return;}std::vector<nodes::Contact> nodes;for(const auto& n:snapshot->nodes){if(n.state==control::LookupNodeState::Replied){nodes.push_back(n.contact);}}
  std::ranges::sort(nodes,[&](const auto& a,const auto& b){for(std::size_t j=0;j<20;++j){const auto aa=a.id[j]^value->target[j],bb=b.id[j]^value->target[j];if(aa!=bb){return aa<bb;}}return false;});if(nodes.size()>20){nodes.resize(20);}
  for(const auto& candidate:nodes){const auto peer=peerOf(candidate);static_cast<void>(lane.actor->requestStoreToken(peer,candidate.protocol,this,[this,value,publication,peer,version=candidate.protocol,key](auto outcome){
   if(closing_||!value->token->active||!outcome){return;}const auto* decoded=std::get_if<udp::find::Decoded>(&*outcome);const auto* reply=decoded?std::get_if<udp::find::ReplyBody>(&decoded->body()):nullptr;if(!reply){return;}const auto nonce=reply->randomId;
   gate(Clock::now()+std::chrono::seconds(5),[this,value,publication,peer,version,key,nonce]{if(!allowed(value->lane)||!value->token->active||!active_->operation->data.keys.contains(value->token->epoch)){return;}udp::StoreRequest request;request.randomId=nonce;request.keys={key};request.valueSets={{*publication}};static_cast<void>(lanes_[value->lane]->actor->store(peer,version,std::move(request),this,[](auto){},std::chrono::seconds(15)));});
  },std::chrono::seconds(15)));}changed();
 },{},[policy,versions,reserved,token](std::stop_token stop)->Broker::Result{if(stop.stop_requested()||!token->active){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Room left before publication"));}auto valid=policy->validate();if(!valid){return std::unexpected(valid.error());}valid=policy->validateChatIdentity(token->configKey,token->stamp);if(!valid){return std::unexpected(valid.error());}auto version=versions->next(QDateTime::currentMSecsSinceEpoch());if(!version){return std::unexpected(version.error());}*reserved=*version;return Broker::Data{};});
}
void Owner::discovery(){
 if(closing_){return;}std::erase_if(lookups_,[](const auto& v){return v->finished;});
 while(lookups_.size()<8&&!pendingLookups_.empty()){auto next=std::move(pendingLookups_.front());pendingLookups_.pop_front();lookupBegin(std::move(next));}
 for(auto& [key,room]:rooms_){(void)key;if(!room.core||!room.token||!room.token->active){continue;}for(std::size_t i=0;i<2;++i){if(Clock::now()>=room.nextLookup[i]){lookupStart(room,i);}}}
 for(const auto& lookup:lookups_){if(!lookup->finished){lookupPump(lookup);}}
 for(std::size_t i=0;i<2;++i){if(!lanes_[i]){continue;}auto& lane=*lanes_[i];if(lane.ready){gate(Clock::now()+std::chrono::seconds(5),[this,i]{if(allowed(i)){static_cast<void>(lanes_[i]->actor->maintainRouting(QDateTime::currentMSecsSinceEpoch()));const auto routing=lanes_[i]->actor->routingSnapshot();if(ticks_%4==0&&routing&&routing->finished&&!routing->seeded){static_cast<void>(lanes_[i]->actor->integrateKnownRouting(std::chrono::seconds(30)));}}});}else if(ticks_%12==0&&!lane.initializing&&!lane.probing&&!lane.dns){if(lane.policy){findSeeds(i);}else{initializeLane(i);}}}
 if(ticks_%10==0){for(std::size_t i=0;i<2;++i){if(!lanes_[i]){continue;}const auto& lane=*lanes_[i];const auto stats=lane.actor?lane.actor->statistics():udp::Statistics{};const auto routing=lane.actor?lane.actor->routingSnapshot():nullptr;
  engine::detail::runtimeConsole("chat.routing")<<" family="<<(i==0?4:6)<<" ready="<<lane.ready<<" probing="<<lane.probing<<" dns="<<lane.dnsResolved<<" seeds="<<lane.seeds.size()<<" sent="<<stats.sent<<" received="<<stats.received<<" replies="<<stats.replies<<" contacts="<<(routing?routing->contactCount:0)<<" seeded="<<(routing&&routing->seeded)<<" failure="<<(routing&&routing->failure?static_cast<int>(*routing->failure):-1)<<" error="<<engine::detail::consoleText(lane.error)<<'\n'<<std::flush;
 }}
 if(qEnvironmentVariableIsSet("BIGLYBTC_CHAT_DIAGNOSTIC")){for(std::size_t i=0;i<2;++i){if(lanes_[i]){const auto& lane=*lanes_[i];const auto stats=lane.actor?lane.actor->statistics():udp::Statistics{};const auto route=lane.actor?lane.actor->routingSnapshot():nullptr;qInfo("chat-diagnostic family=%u ready=%d probing=%d seeds=%zu dns=%d sent=%llu received=%llu replies=%llu unowned=%llu foreign=%llu authority=%llu contacts=%zu seeded=%d routingFailure=%d lookupNodes=%zu lookupStop=%d",i==0?4U:6U,lane.ready,lane.probing,lane.seeds.size(),lane.dnsResolved,(unsigned long long)stats.sent,(unsigned long long)stats.received,(unsigned long long)stats.replies,(unsigned long long)stats.unowned,(unsigned long long)stats.foreign,(unsigned long long)stats.authorityErrors,route?route->contactCount:0,route&&route->seeded,route&&route->failure?int(*route->failure):-1,route&&route->lookup?route->lookup->nodes.size():0,route&&route->lookup&&route->lookup->stop?int(*route->lookup->stop):-1);}}}
 bool ready=false,responding=false;for(const auto& lane:lanes_){if(lane&&lane->ready&&lane->actor){ready=true;const auto route=lane->actor->routingSnapshot();responding|=route&&route->seeded;}}
 if(!ready){std::string state;for(std::size_t i=0;i<2;++i){if(lanes_[i]&&!lanes_[i]->error.empty()){if(!state.empty()){state+="; ";}state+=(i==0?"IPv4: ":"IPv6: ")+lanes_[i]->error;}}if(!state.empty()&&availability_!=state){availability_=std::move(state);changed();}}
 if(ready){const std::string state=responding?"Public chats connected":"Connecting to Public DHT…";if(availability_!=state){availability_=state;changed();}}

}
void Owner::command(Command command){
 if(closing_){result(command,failure(storage::ErrorCode::Cancelled,"Chats are closing"));return;}
 if(command.kind==Kind::Join){loadRoom(command);return;}
 const auto found=rooms_.find(command.key);if(found==rooms_.end()){result(command,failure(storage::ErrorCode::Missing,"Room is not open"));return;}auto& room=found->second;
 switch(command.kind){
 case Kind::Select:selected_=command.key;room.unread=0;result(command);break;
 case Kind::Leave:
  for(const auto& [id,meta]:metadata_){if(meta.command.kind==Kind::Join&&meta.command.key==command.key){shared_->io->cancel(id);}}
  if(room.token){room.token->active=false;}if(room.session){room.session->close();room.session.reset();}room.opening=false;room.status="Room left";
  // Keep the ledger until a final saved-history write completes. Rejoining
  // creates a new epoch, so old requests cannot become its source authority.
  result(command);break;
 case Kind::Send:
  if(!room.session||!room.token||!room.token->active||room.core->readOnly()){result(command,failure(storage::ErrorCode::Unsupported,"Room is not writable"));break;}
  if(auto sent=room.session->send(command.text,room.settings.nickname,QDateTime::currentMSecsSinceEpoch());sent){result(command);}else{result(command,failure(storage::ErrorCode::Unsupported,"Message was refused or exceeds the room limit"));}break;
 case Kind::Options:{auto settings=room.settings;settings.favourite=command.first;settings.saveHistory=command.second;meta(ChatProfileOptions{std::move(settings)},{command,room.token,false});break;}
 case Kind::NicknameMode:meta(ChatProfileNicknameMode{command.key,command.first},{command,room.token,false});break;
 case Kind::Nickname:meta(ChatProfileNickname{command.key,command.text,command.first},{command,room.token,false});break;
 case Kind::Join:break;
 }changed();
}
void Owner::flushHistory(){
 for(auto& [key,room]:rooms_){
  if(!room.core||!room.token||!room.settings.saveHistory||!room.historyDirty||room.historyPending||metadata_.size()>=8){continue;}
  auto history=room.core->exportHistory(QDateTime::currentMSecsSinceEpoch());if(!history){room.status="History could not be encoded";continue;}
  room.savingMutation=room.mutation;
  if(meta(ChatProfileHistory{key,std::move(*history),room.token->stamp},{{0,Kind::Options,key},room.token,true})){room.historyPending=true;}
 }
}
void Owner::beginClose(){
 if(closing_){return;}closing_=true;closeEnd_=Clock::now()+std::chrono::seconds(5);timer_.stop();
 for(auto& [key,room]:rooms_){(void)key;if(room.session){room.session->close();room.session.reset();}}
 for(auto& lane:lanes_){if(!lane){continue;}if(lane->dnsId>=0){QHostInfo::abortHostLookup(lane->dnsId);}if(lane->actor){lane->actor->close();}lane->ready=false;}
 if(provider_){provider_->close();}shared_->networkIo->unbind();shared_->networkIo->requestStop();
 for(auto& frame:frames_){if(frame.job){Broker::revoke(frame.job);}}
 availability_="Saving room history…";changed();
}
void Owner::poll(){
 if(polling_){return;}polling_=true;const auto finish=qScopeGuard([&]{polling_=false;});
 if(shared_->stop.load()){beginClose();}
 drainFrames();metadata();
 // Expensive signature checks, history encoding and transcript generation run
 // after the originating source operation has physically released its gate.
 for(auto at=crypto_.begin();at!=crypto_.end();){if(!at->barrier||Broker::released(at->barrier)){auto effect=std::move(at->effect);at=crypto_.erase(at);if(!closing_){effect();}}else{++at;}}
 std::deque<Command> commands;{std::lock_guard lock(shared_->mutex);commands.swap(shared_->commands);}for(auto& item:commands){command(std::move(item));}
 if(closing_){
  flushHistory();bool dirty=false;for(const auto& [key,room]:rooms_){(void)key;dirty|=room.settings.saveHistory&&room.historyDirty;}
  if((metadata_.empty()&&!dirty&&frames_.empty())||Clock::now()>=closeEnd_){
   for(auto& frame:frames_){if(frame.job){Broker::revoke(frame.job);}}shared_->io->requestStop();pulse_.stop();availability_=dirty?"Chats closed; some history could not be saved":"Chats closed";changed();publish();loop_.quit();return;
  }
 }else{
  if(!provider_&&!attachExhausted_){if(Clock::now()>=attachEnd_){attachExhausted_=true;availability_="Public chat transport could not be attached";changed();}else if(Clock::now()>=nextAttach_){attachTransport();}}
  if(provider_){for(std::size_t i=0;i<2;++i){if(lanes_[i]){auto& lane=*lanes_[i];if(!lane.ready&&!lane.initializing&&!lane.probing&&!lane.dns&&lane.policy&&lane.policy->current()&&!lane.seeds.empty()&&Clock::now()>=lane.probeAfter){probe(i);}}}}
  if(Clock::now()-lastHistory_>=std::chrono::seconds(30)){lastHistory_=Clock::now();flushHistory();}
  if(provider_&&Clock::now()-lastBindings_>=std::chrono::seconds(30)){lastBindings_=Clock::now();if(!provider_->refresh()){shared_->stop=true;beginClose();}else{for(std::size_t i=0;i<2;++i){if(lanes_[i]&&lanes_[i]->policy&&!lanes_[i]->policy->current()){lanes_[i]->error="Chat network settings changed; reopen chats";lanes_[i]->ready=false;if(lanes_[i]->actor){lanes_[i]->actor->close();}changed();}}}}
 }
 if(dirty_){publish();}
}
void Owner::publish(){
 if(lastConsoleAvailability_!=availability_){lastConsoleAvailability_=availability_;engine::detail::runtimeConsole("chat.network")<<" state="<<engine::detail::consoleText(availability_)<<'\n'<<std::flush;}
 auto out=std::make_shared<GeneralChatSnapshot>();out->generation=shared_->generation;out->sequence=++sequence_;out->availability=availability_;out->selected=selected_;out->closing=closing_;out->closed=closing_&&!pulse_.isActive();
 for(auto& [key,room]:rooms_){
  const bool joined=room.token&&room.token->active&&(room.session||!room.lanes.size())&&!closing_;auto facts=room.session?room.session->facts():msg::RoomSessionFacts{};
  std::string status=room.status;if(joined&&room.session){status=std::to_string(facts.peers)+" room nodes, "+std::to_string(facts.alive)+" responding";}
  auto settings=room.settings;if(room.core){settings.readOnly=room.core->readOnly();}
  out->rooms.push_back({std::move(settings),std::move(status),joined,joined&&room.session&&!room.core->readOnly(),room.unread,facts.peers,facts.active});
  if(key==selected_&&room.core){if(auto messages=room.core->messages(QDateTime::currentMSecsSinceEpoch())){out->messages=std::move(*messages);}}
 }
 shared_->snapshot.store(std::move(out));dirty_=false;
}
} // anonymous namespace
struct GeneralChats::Impl {
 std::shared_ptr<Shared> shared;std::thread worker;std::mutex joinedMutex;
 explicit Impl(std::shared_ptr<Shared> value):shared(std::move(value)){}
 std::optional<std::uint64_t> enqueue(Command command){
  std::lock_guard lock(shared->mutex);if(shared->stop||shared->outstanding>=64||shared->next==std::numeric_limits<std::uint64_t>::max()||command.key.empty()||command.key.size()>4096||command.text.size()>16*1024){return {};}
  command.id=++shared->next;const auto id=command.id;shared->commands.push_back(std::move(command));++shared->outstanding;if(shared->receiver){QCoreApplication::postEvent(shared->receiver,new QEvent(commandEvent()));}return id;
 }
};
GeneralChats::GeneralChats(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
std::expected<std::unique_ptr<GeneralChats>,storage::Error> GeneralChats::create(ChatTransportFactory factory,std::uint64_t generation){
 if(!generation||!factory.lease_||!factory.attach_){return std::unexpected(failure(storage::ErrorCode::Conflict,"Chat transport is unavailable"));}
 try{
  auto shared=std::make_shared<Shared>(std::move(factory.lease_),generation);auto initial=std::make_shared<GeneralChatSnapshot>();initial->generation=generation;initial->availability="Opening Public chats…";shared->snapshot.store(std::move(initial));
  auto service=std::unique_ptr<GeneralChats>(new GeneralChats(std::make_unique<Impl>(shared)));service->impl_->worker=std::thread([shared,attach=std::move(factory.attach_),owned=factory.owned_]()mutable{
   try{QEventLoop loop;Owner owner(shared,loop,owned,std::move(attach));QTimer::singleShot(0,&owner,[&]{owner.start();});loop.exec();}
   catch(...){shared->stop=true;if(auto witness=shared->witness.load()){witness->revoke();}auto out=std::make_shared<GeneralChatSnapshot>();out->generation=shared->generation;out->closed=true;out->availability="Public chats stopped after an internal failure";shared->snapshot.store(std::move(out));}
   shared->io->requestStop();shared->networkIo->requestStop();
  });return service;
 }catch(...){return std::unexpected(failure(storage::ErrorCode::Io,"Could not start Public chat worker"));}
}
GeneralChats::~GeneralChats(){requestStop();join();}
std::shared_ptr<const GeneralChatSnapshot> GeneralChats::snapshot()const noexcept{return impl_?impl_->shared->snapshot.load():nullptr;}
std::vector<GeneralChatResult> GeneralChats::takeResults(){if(!impl_){return {};}std::lock_guard lock(impl_->shared->mutex);std::vector<GeneralChatResult> out;out.swap(impl_->shared->results);impl_->shared->outstanding-=out.size();return out;}
std::optional<std::uint64_t> GeneralChats::joinRoom(std::string key){return impl_->enqueue({0,Kind::Join,std::move(key)});}
std::optional<std::uint64_t> GeneralChats::leaveRoom(std::string key){return impl_->enqueue({0,Kind::Leave,std::move(key)});}
std::optional<std::uint64_t> GeneralChats::selectRoom(std::string key){return impl_->enqueue({0,Kind::Select,std::move(key)});}
std::optional<std::uint64_t> GeneralChats::send(std::string key,std::string text){return impl_->enqueue({0,Kind::Send,std::move(key),std::move(text)});}
std::optional<std::uint64_t> GeneralChats::options(std::string key,bool favourite,bool saveHistory){return impl_->enqueue({0,Kind::Options,std::move(key),{},favourite,saveHistory});}
std::optional<std::uint64_t> GeneralChats::nicknameMode(std::string key,bool shared){return impl_->enqueue({0,Kind::NicknameMode,std::move(key),{},shared});}
std::optional<std::uint64_t> GeneralChats::nickname(std::string key,std::string value,bool expectedShared){return impl_->enqueue({0,Kind::Nickname,std::move(key),std::move(value),expectedShared});}
void GeneralChats::requestStop()noexcept{if(!impl_){return;}auto& shared=*impl_->shared;shared.stop=true;if(auto witness=shared.witness.load()){witness->revoke();}try{std::lock_guard lock(shared.mutex);if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}}catch(...){} }
void GeneralChats::join()noexcept{
 if(!impl_){return;}requestStop();std::lock_guard lock(impl_->joinedMutex);if(impl_->worker.joinable()){impl_->worker.join();}
 auto& shared=*impl_->shared;if(shared.networkIo){shared.networkIo->join();shared.networkIo.reset();}if(shared.io){shared.io->join();shared.io.reset();}shared.lease.reset();shared.witness.store({});
}
} // namespace biglybtc::app
