#include "../engine/runtime_console.hpp"
// SPDX-License-Identifier: GPL-2.0-or-later
#include "general_chat_service.hpp"
#include "chat_profile_io.hpp"
#include "session.hpp"
#include "discovery.hpp"
#include "search_wire.hpp"
#include "azureus_public_policy.hpp"
#include "azureus_contacts.hpp"
#include "biglybtc/azureus_endpoint.hpp"
#include "lookup.hpp"
#include "publication_policy.hpp"
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
std::expected<ChatTransportFactory,storage::Error> ChatTransportFactory::owned(const libtorrent::session_handle& session,const storage::ProfileLease& lease,unsigned short port,bool standardNetworks){
 auto held=lease.retain();if(!held){return std::unexpected(held.error());}
 return ChatTransportFactory{std::move(*held),[session,port]()->std::expected<std::shared_ptr<services::shared_udp::Provider>,services::azureus_udp::Failure>{
  auto value=services::shared_udp::Provider::attach(session,port);if(!value){return std::unexpected(value.error());}
  for(const auto& binding:(*value)->bindings()){if(!binding.address.isLoopback()){(*value)->close();return std::unexpected(services::azureus_udp::Failure::Authority);}}
  return std::shared_ptr<services::shared_udp::Provider>(std::move(*value));
 },true,standardNetworks};
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
struct SwarmCommand {enum class Kind {Refresh,Lookup,Change,Cycle,Search,CancelSearch};Kind kind=Kind::Refresh;std::string hash;std::int64_t fileSize=0;SwarmDiscoveryChanges changes;std::optional<rcm::SearchRequest> search={};};
struct RatingCommand {core::TorrentHash hash;bool publish=false,cancel=false;};
struct Shared {
 explicit Shared(std::shared_ptr<const storage::ProfileLease> lease,std::uint64_t generation):generation(generation),lease(std::move(lease)),io(std::make_unique<ChatProfileIo>(this->lease)),networkIo(std::make_unique<Broker>(this->lease)){}
 std::uint64_t generation;std::shared_ptr<const storage::ProfileLease> lease;
 std::unique_ptr<ChatProfileIo> io;std::unique_ptr<Broker> networkIo;
 std::atomic<std::shared_ptr<const GeneralChatSnapshot>> snapshot;
 std::atomic<std::shared_ptr<const core::SharedUdpWitness>> witness;
 std::atomic<bool> stop{false};std::mutex mutex;QObject* receiver=nullptr;
 std::string readingRoom;bool readingWakePending=false;
 std::vector<plugins::rating::publication::ReadHint> ratingHints,lastRatingHints;bool ratingHintsPending=false;
 std::deque<RatingCommand> ratingCommands;
 std::shared_ptr<engine::AzureusTorrentSources> torrentSources;
 std::shared_ptr<SwarmDiscoveryProfile> swarmProfile;std::deque<SwarmCommand> swarmCommands;
 std::vector<engine::AzureusTorrentTarget> torrentTargets,lastTorrentTargets;
 std::atomic<std::uint64_t> deliveredTorrentPeers{0};
 std::vector<engine::AzureusPeerBatch> torrentPeers;
 bool torrentTargetsPending=false;
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
  std::array<Clock::time_point,3> nextLookup{},nextPut{};
 };
 struct Prepared {std::optional<rcm::Settings> searchSettings;std::set<const std::atomic<bool>*> related;std::set<const std::atomic<bool>*> torrents;std::array<bool,3> lanes{};std::set<std::uint64_t> keys;std::set<std::uint64_t> changed;};
 struct Operation {Broker::Operation run;Prepared data;};
 struct Frame {
  Clock::time_point end;Broker::Handle job;std::shared_ptr<Operation> operation;
  std::move_only_function<void()> effect,failed;bool executed=false;
 };
 struct Crypto {Broker::Handle barrier;std::move_only_function<void()> effect;};
 struct Meta {Command command;std::shared_ptr<RoomToken> token;bool history=false;};
 std::shared_ptr<Shared> shared_;QEventLoop& loop_;bool owned_=false,standardNetworks_=false;
 std::function<std::expected<std::shared_ptr<wire::Provider>,udp::Failure>()> attach_;
 std::shared_ptr<wire::Provider> provider_;std::array<std::unique_ptr<Lane>,3> lanes_;
 std::shared_ptr<std::map<std::int32_t,std::shared_ptr<storage::AzureusValueVersions>>> versions_=std::make_shared<std::map<std::int32_t,std::shared_ptr<storage::AzureusValueVersions>>>();
 std::map<std::string,Room> rooms_;std::map<std::uint64_t,Meta> metadata_;std::list<Frame> frames_;std::deque<Crypto> crypto_;
 Frame* active_=nullptr;QTimer pulse_,timer_;std::uint64_t epoch_=0,sequence_=0,ticks_=0;
 std::string lastConsoleAvailability_;
 std::string reading_,selected_,availability_="Opening saved Public rooms…";bool dirty_=true,polling_=false,closing_=false;
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
   std::array<std::shared_ptr<storage::AzureusPublicPolicy>,3> policies{};
   for(std::size_t i=0;i<3;++i){if(lanes_[i]){policies[i]=lanes_[i]->policy;}}
   std::vector<std::shared_ptr<RoomToken>> keys;for(const auto& [key,room]:rooms_){(void)key;if(room.token){keys.push_back(room.token);}}
   const auto searchProfile=shared_->swarmProfile;
   operation->run=[searchProfile,operation=std::weak_ptr<Operation>(operation),policies=std::move(policies),keys=std::move(keys),end](std::stop_token stop)->Broker::Result{
    const auto op=operation.lock();if(!op||stop.stop_requested()||Clock::now()>=end){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Chat source operation expired"));}
    std::shared_ptr<storage::AzureusPublicPolicy> valid;
    for(std::size_t i=0;i<3;++i){if(policies[i]&&policies[i]->validate()){op->data.lanes[i]=true;if(!valid){valid=policies[i];}}}
    if(valid&&searchProfile){auto settings=searchProfile->searchPermission();if(settings){op->data.searchSettings=std::move(*settings);}}
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
  for(std::size_t i=0;i<3;++i){if(i<2&&lanes_[i]&&lanes_[i]->ready&&lanes_[i]->actor){actors.push_back(lanes_[i]->actor.get());mapping.push_back(i);}}
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
  bool reading=false;{std::lock_guard lock(shared_->mutex);reading=shared_->readingRoom==room.settings.key;}
  std::set<msg::MessageId> current;std::uint32_t added=0,incoming=0;
  for(const auto& message:*messages){current.insert(message.id);if(!room.seen.contains(message.id)){++added;if(!message.local){++incoming;}}}
  if(added){++room.mutation;room.historyDirty=true;if(unread&&!reading){room.unread=std::min<std::uint32_t>(100000,room.unread+incoming);}}
  room.seen=std::move(current);
 }
 struct RcmGroup {std::set<std::pair<std::size_t,nodes::NodeId>> seen;std::size_t roots=0,hits=0;bool diversified=false;int publication=-1;};
 struct RcmLookup {
  std::string parent,advertised;std::int64_t fileSize=0;nodes::NodeId key{};std::size_t lane=0;
  int level=0;unsigned depth=0;bool explicitLookup=false,derived=false,publish=false,queued=false,finished=false,reported=false,observed=false,diversified=false;
  std::shared_ptr<std::atomic<bool>> live=std::make_shared<std::atomic<bool>>(true);
  std::shared_ptr<RcmGroup> group;Clock::time_point end;
  services::azureus_control::LookupController control;std::map<std::uint64_t,udp::Ticket> tickets;
  std::set<std::string> values;
 };
 struct RcmPublication {
  std::shared_ptr<RcmLookup> lookup;std::string payload;services::azureus_values::Value value;
  std::vector<nodes::Contact> destinations;std::size_t next=0,pending=0;
  bool queued=false,finished=false;Clock::time_point end;
 };
 std::shared_ptr<const SwarmDiscoverySnapshot> swarmSnapshot_;
 std::deque<std::shared_ptr<RcmLookup>> rcmPending_,rcmAwaitPublish_,rcmReadyPublish_;
 std::vector<std::shared_ptr<RcmLookup>> rcmLookups_;
 std::vector<std::shared_ptr<RcmPublication>> rcmPublications_;
 struct RcmLocal {std::string parent,advertised,payload;std::int64_t fileSize=0;bool operator==(const RcmLocal&)const=default;};
 std::array<std::map<std::string,RcmLocal>,3> localRcmKeys_;
 std::size_t rcmPublicationPreflights_=0;
 bool rcmMetadataPending_=false,rcmCandidatesPending_=false,rcmSavePending_=false;
 Broker::Handle rcmCloseSave_;
 Clock::time_point rcmTickAfter_{},rcmRefreshAfter_{},rcmSaveAfter_{},rcmAutoAfter_=Clock::now()+std::chrono::minutes(3),rcmSecondaryAfter_=Clock::now()+std::chrono::minutes(15);
 SwarmNetworkObservation rcmFacts_;
 void rcmTick();void rcmCommand(SwarmCommand);void rcmRefresh();void rcmSnapshot(std::shared_ptr<const SwarmDiscoverySnapshot>);
 void rcmQueue(std::string,std::string={},std::int64_t=0,int=0,bool=false,bool=false);
 bool rcmBegin(std::shared_ptr<RcmLookup>);void rcmPump(std::shared_ptr<RcmLookup>);
 void rcmReply(std::shared_ptr<RcmLookup>,std::uint64_t,udp::FindValueOutcome);
 void rcmFinish(std::shared_ptr<RcmLookup>);void rcmCancel(std::shared_ptr<RcmLookup>);
 bool rcmGate(std::shared_ptr<RcmLookup>,Clock::time_point,std::move_only_function<void()>,std::move_only_function<void()> = {},std::shared_ptr<std::optional<std::int32_t>> = {},std::shared_ptr<std::string> = {},std::vector<std::string> = {},std::string expectedPayload={});
 bool rcmAllowed(const RcmLookup&)const;
 void rcmPublish(std::shared_ptr<RcmLookup>);void rcmPumpPut(std::shared_ptr<RcmPublication>);
 bool rcmReadPermit(std::size_t,std::string,std::move_only_function<void(bool)>);
 void rcmAuto();void rcmSave(bool closing=false);void rcmDrainSave();
 struct SearchPeer {std::size_t lane=0;nodes::Contact contact;};
 struct ForeignSearchBloom {SearchPeer peer;rcm::SearchBloom bloom;Clock::time_point created,updated;};
 struct SearchWork {std::uint64_t id=0;rcm::SearchRequest request;std::optional<rcm::SearchQuery> query;Clock::time_point end;std::deque<SearchPeer> pending;std::set<std::string> seen,flight;std::map<std::string,std::pair<std::size_t,udp::Ticket>> tickets;std::map<std::string,rcm::Item> results;std::size_t attempted=0,preflights=0;bool running=true,localPending=true;};
 std::shared_ptr<SearchWork> rcmSearch_;std::shared_ptr<const rcm::SearchProgress> rcmSearchView_;
 std::uint64_t rcmSearchId_=0,rcmSearchEpoch_=1;
 std::map<std::string,ForeignSearchBloom> rcmForeign_;
 std::deque<std::string> rcmSearchFailed_;bool rcmHarvestPending_=false;Clock::time_point rcmHarvestAfter_{};
 struct SearchRate {Clock::time_point queryReset{},bloomReset{};unsigned queries=0;bool harvested=false;};
 std::map<std::string,SearchRate> rcmSearchRates_;
 std::optional<std::uint64_t> rcmSearchRevision(std::size_t)const;
 bool rcmSearchGate(std::size_t,Clock::time_point,std::move_only_function<void()>,std::move_only_function<void()> = {},Broker::Operation = {});
 void rcmRegisterSearch(std::size_t);void rcmReceiveSearch(std::size_t,std::shared_ptr<const udp::TransferRequest>);
 void rcmStartSearch(rcm::SearchRequest);void rcmStopSearch(std::string);void rcmPublishSearch(std::string = {});
 void rcmSearchTick();std::vector<SearchPeer> rcmSearchPeers()const;
 void rcmAddSearchPeer(const std::shared_ptr<SearchWork>&,SearchPeer);
 void rcmSearchCall(SearchPeer,rcm::SearchMessage,std::uint64_t,bool);


 struct RatingWork;
 using RatingPolicy=plugins::rating::publication::Policy;
 using RatingInputs=plugins::rating::publication::Inputs;
 using RatingReadSchedule=plugins::rating::publication::ReadSchedule;
 std::optional<RatingPolicy> ratingPolicy_;RatingReadSchedule ratingReads_;
 std::map<std::string,plugins::rating::publication::Download> ratingRegistrations_;
 std::map<std::uint64_t,std::string> ratingLabels_;
 struct RatingTimer {plugins::rating::publication::Timer timer;std::int64_t when;};
 std::vector<RatingTimer> ratingTimers_;
 struct RatingWrite {plugins::rating::publication::Completion completion;std::string hash;std::weak_ptr<RatingWork> work;bool started=false;};
 struct RatingWork;
 struct RatingLane {
  std::size_t lane=0,consumed=0,nextStore=0,pendingStores=0;
  services::azureus_control::LookupController control;
  std::map<std::uint64_t,udp::Ticket> tickets;
  std::vector<nodes::Contact> destinations;std::vector<udp::Ticket> storeTickets;
  std::optional<services::azureus_values::Value> value;
  std::set<std::string> seen;
  bool started=false,queued=false,finished=false,storing=false;
 };
 struct RatingWork {
  core::TorrentHash hash;std::optional<storage::DistributedRatingSnapshot> source;
  std::optional<std::array<std::uint8_t,32>> expectedSource;
  nodes::NodeId key{};std::string payload;SharedRatingSnapshot view;std::shared_ptr<const SharedRatingSnapshot> published;
  std::array<RatingLane,2> lanes;Clock::time_point end,auditAfter{};
  bool alive=true,finishing=false;
  explicit RatingWork(core::TorrentHash h):hash(h){}
 };
 std::map<std::string,std::shared_ptr<RatingWork>> ratings_;
 struct RatingLocal {core::TorrentHash hash;std::array<std::uint8_t,32> revision;};
 std::array<std::map<std::string,RatingLocal>,2> localRatingKeys_;
 std::optional<RatingWrite> ratingWriting_;std::weak_ptr<RatingWork> ratingReading_;bool ratingReadActive_=false;std::optional<std::string> ratingReadWaiting_;
 std::deque<RatingCommand> ratingDeferred_;
 void ratingScheduleTick();bool ratingEnqueue(std::string_view,bool);void ratingPolicyReceipt(const plugins::rating::publication::Receipt&);
 std::uint64_t ratingSequence_=0;Clock::time_point ratingTickAfter_{};
 bool ratingGate(std::shared_ptr<RatingWork>,std::size_t,Clock::time_point,std::move_only_function<void()>,std::move_only_function<void()> = {},std::shared_ptr<std::optional<std::int32_t>> = {});
 void ratingChanged(std::shared_ptr<RatingWork> work){work->published=std::make_shared<const SharedRatingSnapshot>(work->view);changed();}
 void ratingStart(RatingCommand);void ratingTick();void ratingPump(std::shared_ptr<RatingWork>,std::size_t);
 void ratingReply(std::shared_ptr<RatingWork>,std::size_t,std::uint64_t,udp::FindValueOutcome);
 void ratingNodeReply(std::shared_ptr<RatingWork>,std::size_t,std::uint64_t,udp::DetailedFindNodeOutcome);
 void ratingStore(std::shared_ptr<RatingWork>,std::size_t);
 void ratingNetworkFailed(std::shared_ptr<RatingWork>,std::size_t,std::string);
 void ratingFinish(std::shared_ptr<RatingWork>);void ratingCancel(std::shared_ptr<RatingWork>,std::string,bool removeLocal=true);
 void ratingAdd(std::shared_ptr<RatingWork>,RatingLane&,const services::azureus_values::Value&);
 bool ratingReadPermit(std::size_t,std::string,std::move_only_function<void(bool)>);
 void ratingForget(std::shared_ptr<RatingWork>);

 struct Torrent {
  engine::AzureusTorrentTarget target;nodes::NodeId root{};
  std::array<Clock::time_point,3> next{};bool permitted=false,retiring=false;
  std::array<std::map<nodes::NodeId,std::vector<nodes::Contact>>,3> published;
  std::array<std::set<nodes::NodeId>,3> publicationQueued;
  std::array<std::map<nodes::NodeId,std::pair<unsigned,unsigned>>,3> fixedPut;
  explicit Torrent(engine::AzureusTorrentTarget value):target(std::move(value)){}
 };
 struct TorrentGroup {std::set<nodes::NodeId> seen;std::map<nodes::NodeId,std::pair<unsigned,unsigned>> fixedPut;};
 struct TorrentLookup {
  std::shared_ptr<Torrent> torrent;std::size_t lane=0;nodes::NodeId key{};
  services::azureus_control::LookupController control;std::map<std::uint64_t,udp::Ticket> tickets;
  std::shared_ptr<TorrentGroup> group;Clock::time_point end;unsigned depth=0;
  bool read=true,write=true,finished=false,diversified=false,queued=false,observed=false;
 };
 struct TorrentPublication {
  std::shared_ptr<TorrentLookup> lookup;services::azureus_values::Value value;
  std::vector<nodes::Contact> destinations;std::size_t next=0,pending=0;
  bool withdrawing=false,done=false,queued=false;Clock::time_point end;
 };
 std::map<std::string,std::shared_ptr<Torrent>> torrents_;
 std::array<std::map<std::string,std::shared_ptr<Torrent>>,3> localTorrentKeys_;
 std::vector<std::shared_ptr<Torrent>> retiringTorrents_;
 std::vector<std::shared_ptr<TorrentLookup>> torrentLookups_;
 std::deque<std::shared_ptr<TorrentLookup>> torrentPending_;
 std::vector<std::shared_ptr<TorrentPublication>> torrentPublications_;
 std::size_t torrentPublicationPreflights_=0;
 std::deque<std::pair<std::shared_ptr<TorrentLookup>,bool>> torrentPublicationPending_;
 engine::AzureusTorrentObservation torrentFacts_;
 std::array<std::array<std::uint64_t,6>,3> torrentTransportStamp_{};
 std::array<std::vector<storage::AzureusSavedContact>,3> lastTorrentContacts_;
 std::array<Broker::Handle,3> contactSaves_{};
 std::array<bool,3> contactDirty_{};
 Clock::time_point lastContactSave_{};
 void captureTorrentContacts(std::size_t);
 void saveTorrentContacts(bool closing=false);
 void drainTorrentContactSaves();
 Clock::time_point lastTorrentTick_{},lastTorrentConsole_{};std::string torrentCursor_;
 bool torrentGate(std::shared_ptr<Torrent>,std::size_t,bool,Clock::time_point,
     std::move_only_function<void()>,std::move_only_function<void()> failed={},
     std::shared_ptr<std::optional<std::int32_t>> reserved={});
 bool torrentAllowed(const Torrent&,std::size_t,bool=false)const;
 void syncTorrents();void torrentTick();void cancelTorrentLookups(const std::shared_ptr<Torrent>&);
 bool beginTorrentLookup(std::shared_ptr<TorrentLookup>);
 void pumpTorrentLookup(std::shared_ptr<TorrentLookup>);
 void torrentReply(std::shared_ptr<TorrentLookup>,std::uint64_t,udp::FindValueOutcome);
 void torrentNodeReply(std::shared_ptr<TorrentLookup>,std::uint64_t,udp::DetailedFindNodeOutcome);
 void diversifyTorrent(std::shared_ptr<TorrentLookup>,std::int8_t);
 void finishTorrentLookup(std::shared_ptr<TorrentLookup>);
 void publishTorrent(std::shared_ptr<TorrentLookup>,bool=false);
 void pumpTorrentPublication(std::shared_ptr<TorrentPublication>);
 void withdrawTorrent(const std::shared_ptr<Torrent>&);
 bool torrentReadPermit(std::size_t,std::string,std::move_only_function<void(bool)>);
 void forgetLocalTorrent(const std::shared_ptr<Torrent>&,std::size_t,const nodes::NodeId&);
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
 Owner(std::shared_ptr<Shared> shared,QEventLoop& loop,bool owned,bool standardNetworks,std::function<std::expected<std::shared_ptr<wire::Provider>,udp::Failure>()> attach)
  :shared_(std::move(shared)),loop_(loop),owned_(owned),standardNetworks_(standardNetworks),attach_(std::move(attach)){
  auto policy=RatingPolicy::create({8192,8192,8192,64,64});if(policy){ratingPolicy_.emplace(std::move(*policy));}
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
  if(!listener.local.port||listener.local.address.isNull()||(owned_?!listener.local.address.isLoopback():listener.local.address.isLoopback())){continue;}
  const auto add=[&](std::size_t index,std::int32_t network){if(lanes_[index]){return;}lanes_[index]=std::make_unique<Lane>();lanes_[index]->listener=listener;lanes_[index]->network=network;initializeLane(index);};
  add(i,owned_&&!standardNetworks_?4:(i==0?0:3));
  if(family==4&&(!owned_||standardNetworks_)){add(2,4);}
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
 lane.dnsId=QHostInfo::lookupHost(i==1?QStringLiteral("dht6.biglybt.com"):QStringLiteral("dht.biglybt.com"),this,[this,i](const QHostInfo& result){auto& lane=*lanes_[i];lane.dns=false;lane.dnsId=-1;lane.dnsResolved=result.error()==QHostInfo::NoError;if(closing_){return;}
  std::vector<wire::ProbePeer> fresh;for(const auto& ip:result.addresses()){if(endpoint({ip,6881}).length==(i==1?16:4)&&publicAddress({ip,6881})){fresh.push_back({{ip,6881},55});}}
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
  lane.local=contact(external);auto router=router::RouterOwner::create(lane.local.id,{},shared_->generation,monotonic(),QDateTime::currentMSecsSinceEpoch());db::Limits limits;limits.maximumKeys=8192;auto database=db::Owner::create(limits);
  if(!router||!database||!(*database)->bindLocal(lane.local)){lane.error="Chat routing could not be initialised";changed();return;}
  lane.router=std::move(*router);lane.database=std::move(*database);lane.actor=std::move(actor);
  if(!lane.actor->serveDatabase(lane.router,lane.database,[this,i]{return allowed(i);},[this,i](auto done){auto callback=std::make_shared<std::move_only_function<void(bool)>>(std::move(done));return gate(Clock::now()+std::chrono::seconds(5),[this,i,callback]{(*callback)(allowed(i));},[callback]{(*callback)(false);});})){lane.actor.reset();lane.error="Chat database binding failed";changed();return;}
  static_cast<void>(lane.actor->setLocalReadPreflight([this,i](std::string key,auto done){return torrentReadPermit(i,std::move(key),std::move(done));}));
  lane.ready=true;lane.error.clear();rcmRegisterSearch(i);for(const auto& seed:lane.seeds){if(seed.endpoint!=external){static_cast<void>(lane.actor->attachKnownPeer(seed.endpoint,seed.version,Clock::now()+std::chrono::seconds(10)));}}static_cast<void>(lane.actor->integrateKnownRouting(std::chrono::seconds(30)));
  later([this,i]{if(!owned_){findSeeds(i);}for(auto& [key,room]:rooms_){(void)key;rebind(room);}availability_="Connecting to Public DHT…";changed();});
 },[this,i]{lanes_[i]->error="Public chat policy was rejected";changed();},[lease,versions,network,local,advertised,witness,owned,policy](std::stop_token)->Broker::Result{
  storage::AzureusPublicBind binding{network,advertised.length,advertised.address,advertised.port};auto result=owned?storage::AzureusPublicPolicy::openOwnedShared(*lease,versions,binding,witness):storage::AzureusPublicPolicy::openShared(*lease,versions,binding,local,witness);if(!result){return std::unexpected(result.error());}*policy=std::move(*result);return Broker::Data{};
 });
}
void Owner::ingress(wire::Binding local,udp::Peer peer,std::string bytes){
 if(closing_){return;}for(std::size_t i=0;i<3;++i){auto* lane=lanes_[i].get();if(!lane||!lane->ready||local!=lane->listener.local){continue;}services::azureus_dht::Context networkContext;networkContext.network=lane->network;networkContext.localVersion=55;networkContext.now=QDateTime::currentMSecsSinceEpoch();
  const auto kind=!bytes.empty()&&(static_cast<unsigned char>(bytes[0])&128)?services::azureus_dht::Kind::Request:services::azureus_dht::Kind::Reply;
  if(!services::azureus_dht::decodeHeader(kind,bytes,networkContext)){continue;}

  if(!lane->actor->isRoutingPeer(peer)){
   if(bytes.empty()||!(static_cast<unsigned char>(bytes[0])&128)||peer==lane->actor->endpoint()||(owned_?!peer.address.isLoopback():!publicAddress(peer))){return;}
   services::azureus_dht::Context context;context.network=lane->network;context.localVersion=55;context.now=QDateTime::currentMSecsSinceEpoch();
   auto header=services::azureus_dht::decodeHeader(services::azureus_dht::Kind::Request,bytes,context);if(!header){return;}const auto& fields=header->fields();std::shared_ptr<RoomToken> roomToken;const auto actual=contact(peer,fields.originatorVersion);
   if(!fields.originator||*fields.originator!=actual.address){return;}
   if(fields.action==1035){auto decoded=services::azureus_transfer::decode(bytes,context);if(!decoded){return;}bool known=decoded->data.handler==rcm::searchHandler(i==2);for(const auto& [key,room]:rooms_){(void)key;if(room.core&&room.token&&room.token->active&&room.core->handler()==decoded->data.handler){known=true;roomToken=room.token;break;}}if(!known){return;}}
   else if(fields.action!=1024&&fields.action!=1028&&fields.action!=1030&&fields.action!=1026){return;}
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
 if(i>=2){return;} // Existing general-room scope remains main IPv4/IPv6.
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
#include "azureus_torrent_network.inc"
#include "swarm_discovery_network.inc"
#include "swarm_search_network.inc"
#include "rating_network.inc"
#include "rating_schedule_network.inc"
void Owner::discovery(){
 if(closing_){return;}std::erase_if(lookups_,[](const auto& v){return v->finished;});
 while(lookups_.size()<8&&!pendingLookups_.empty()){auto next=std::move(pendingLookups_.front());pendingLookups_.pop_front();lookupBegin(std::move(next));}
 for(auto& [key,room]:rooms_){(void)key;if(!room.core||!room.token||!room.token->active){continue;}for(std::size_t i=0;i<3;++i){if(Clock::now()>=room.nextLookup[i]){lookupStart(room,i);}}}
 for(const auto& lookup:lookups_){if(!lookup->finished){lookupPump(lookup);}}
 for(std::size_t i=0;i<3;++i){if(!lanes_[i]){continue;}auto& lane=*lanes_[i];if(lane.ready){gate(Clock::now()+std::chrono::seconds(5),[this,i]{if(allowed(i)){static_cast<void>(lanes_[i]->actor->maintainRouting(QDateTime::currentMSecsSinceEpoch()));captureTorrentContacts(i);const auto routing=lanes_[i]->actor->routingSnapshot();if(ticks_%4==0&&routing&&routing->finished&&!routing->seeded){static_cast<void>(lanes_[i]->actor->integrateKnownRouting(std::chrono::seconds(30)));}}});}else if(ticks_%12==0&&!lane.initializing&&!lane.probing&&!lane.dns){if(lane.policy){findSeeds(i);}else{initializeLane(i);}}}
 if(ticks_%10==0){for(std::size_t i=0;i<3;++i){if(!lanes_[i]){continue;}const auto& lane=*lanes_[i];const auto stats=lane.actor?lane.actor->statistics():udp::Statistics{};const auto routing=lane.actor?lane.actor->routingSnapshot():nullptr;
  engine::detail::runtimeConsole("chat.routing")<<" family="<<(i==1?6:4)<<" ready="<<lane.ready<<" probing="<<lane.probing<<" dns="<<lane.dnsResolved<<" seeds="<<lane.seeds.size()<<" sent="<<stats.sent<<" received="<<stats.received<<" replies="<<stats.replies<<" contacts="<<(routing?routing->contactCount:0)<<" seeded="<<(routing&&routing->seeded)<<" failure="<<(routing&&routing->failure?static_cast<int>(*routing->failure):-1)<<" error="<<engine::detail::consoleText(lane.error)<<'\n'<<std::flush;
 }}
 if(qEnvironmentVariableIsSet("BIGLYBTC_CHAT_DIAGNOSTIC")){for(std::size_t i=0;i<3;++i){if(lanes_[i]){const auto& lane=*lanes_[i];const auto stats=lane.actor?lane.actor->statistics():udp::Statistics{};const auto route=lane.actor?lane.actor->routingSnapshot():nullptr;qInfo("chat-diagnostic family=%u ready=%d probing=%d seeds=%zu dns=%d sent=%llu received=%llu replies=%llu unowned=%llu foreign=%llu authority=%llu contacts=%zu seeded=%d routingFailure=%d lookupNodes=%zu lookupStop=%d",i==1?6U:4U,lane.ready,lane.probing,lane.seeds.size(),lane.dnsResolved,(unsigned long long)stats.sent,(unsigned long long)stats.received,(unsigned long long)stats.replies,(unsigned long long)stats.unowned,(unsigned long long)stats.foreign,(unsigned long long)stats.authorityErrors,route?route->contactCount:0,route&&route->seeded,route&&route->failure?int(*route->failure):-1,route&&route->lookup?route->lookup->nodes.size():0,route&&route->lookup&&route->lookup->stop?int(*route->lookup->stop):-1);}}}
 bool ready=false,responding=false;for(std::size_t i=0;i<2;++i){const auto& lane=lanes_[i];if(lane&&lane->ready&&lane->actor){ready=true;const auto route=lane->actor->routingSnapshot();responding|=route&&route->seeded;}}
 if(!ready){std::string state;for(std::size_t i=0;i<3;++i){if(lanes_[i]&&!lanes_[i]->error.empty()){if(!state.empty()){state+="; ";}state+=(i==1?"IPv6: ":"IPv4: ")+lanes_[i]->error;}}if(!state.empty()&&availability_!=state){availability_=std::move(state);changed();}}
 if(ready){const std::string state=responding?"Public chats connected":"Connecting to Public DHT…";if(availability_!=state){availability_=state;changed();}}

}
void Owner::command(Command command){
 if(closing_){result(command,failure(storage::ErrorCode::Cancelled,"Chats are closing"));return;}
 if(command.kind==Kind::Join){loadRoom(command);return;}
 const auto found=rooms_.find(command.key);if(found==rooms_.end()){result(command,failure(storage::ErrorCode::Missing,"Room is not open"));return;}auto& room=found->second;
 switch(command.kind){
 case Kind::Select:selected_=command.key;{std::lock_guard lock(shared_->mutex);if(shared_->readingRoom==command.key){room.unread=0;}}result(command);break;
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
 if(closing_){return;}for(auto& [key,work]:ratings_){(void)key;ratingCancel(work,"Ratings closed");}for(auto& [key,torrent]:torrents_){(void)key;if(torrent->target.live){torrent->target.live->store(false);}cancelTorrentLookups(torrent);}torrentFacts_.closed=true;torrentFacts_.state="Closed";if(ratingPolicy_){ratingPolicy_->close();}ratingReads_.reset();ratingTimers_.clear();closing_=true;closeEnd_=Clock::now()+std::chrono::seconds(5);timer_.stop();
 for(const auto& job:rcmLookups_){rcmCancel(job);}for(const auto& job:rcmPending_){rcmCancel(job);}
 for(auto& [key,room]:rooms_){(void)key;if(room.session){room.session->close();room.session.reset();}}
 for(auto& lane:lanes_){if(!lane){continue;}if(lane->dnsId>=0){QHostInfo::abortHostLookup(lane->dnsId);}if(lane->actor){lane->actor->close();}lane->ready=false;}
 rcmStopSearch("Search closed");
 if(provider_){provider_->close();}saveTorrentContacts(true);rcmSave(true);
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
 std::deque<Command> commands;std::string reading;{std::lock_guard lock(shared_->mutex);commands.swap(shared_->commands);reading=shared_->readingRoom;shared_->readingWakePending=false;}
 if(reading!=reading_){reading_=std::move(reading);const auto room=rooms_.find(reading_);if(room!=rooms_.end()){room->second.unread=0;}changed();}
 drainTorrentContactSaves();rcmDrainSave();
 for(auto& item:commands){command(std::move(item));}
 if(!closing_){syncTorrents();torrentTick();rcmTick();ratingTick();if(Clock::now()-lastContactSave_>=std::chrono::seconds(30)){lastContactSave_=Clock::now();saveTorrentContacts();}}
 if(closing_){
  saveTorrentContacts(true);
  flushHistory();bool dirty=false;for(const auto& [key,room]:rooms_){(void)key;dirty|=room.settings.saveHistory&&room.historyDirty;}
  if((metadata_.empty()&&!dirty&&frames_.empty()&&!rcmCloseSave_&&!rcmSavePending_&&std::ranges::none_of(contactSaves_,[](const auto& handle){return bool(handle);})&&std::ranges::none_of(contactDirty_,[](bool value){return value;}))||Clock::now()>=closeEnd_){
   for(auto& frame:frames_){if(frame.job){Broker::revoke(frame.job);}}shared_->io->requestStop();shared_->networkIo->unbind();shared_->networkIo->requestStop();pulse_.stop();availability_=dirty?"Chats closed; some history could not be saved":"Chats closed";changed();publish();loop_.quit();return;
  }
 }else{
  if(!provider_&&!attachExhausted_){if(Clock::now()>=attachEnd_){attachExhausted_=true;availability_="Public chat transport could not be attached";changed();}else if(Clock::now()>=nextAttach_){attachTransport();}}
  if(provider_){for(std::size_t i=0;i<3;++i){if(lanes_[i]){auto& lane=*lanes_[i];if(!lane.ready&&!lane.initializing&&!lane.probing&&!lane.dns&&lane.policy&&lane.policy->current()&&!lane.seeds.empty()&&Clock::now()>=lane.probeAfter){probe(i);}}}}
  if(Clock::now()-lastHistory_>=std::chrono::seconds(30)){lastHistory_=Clock::now();flushHistory();}
  if(provider_&&Clock::now()-lastBindings_>=std::chrono::seconds(30)){lastBindings_=Clock::now();if(!provider_->refresh()){shared_->stop=true;beginClose();}else{for(std::size_t i=0;i<3;++i){if(lanes_[i]&&lanes_[i]->policy&&!lanes_[i]->policy->current()){lanes_[i]->error="Chat network settings changed; reopen chats";lanes_[i]->ready=false;if(lanes_[i]->actor){lanes_[i]->actor->close();}changed();}}}}
 }
 if(dirty_){publish();}
}
void Owner::publish(){
 std::string availability=availability_;
 const bool ready=std::ranges::any_of(lanes_,[](const auto& lane){return lane&&lane->ready&&lane->actor;});
 if(!closing_&&ready&&std::ranges::any_of(rooms_,[](const auto& entry){const auto& room=entry.second;if(!room.token||!room.token->active||!room.session){return false;}const auto facts=room.session->facts();return facts.alive>0&&facts.exchanges>0;})){availability="Public chats connected";}
 if(lastConsoleAvailability_!=availability){lastConsoleAvailability_=availability;engine::detail::runtimeConsole("chat.network")<<" state="<<engine::detail::consoleText(availability)<<'\n'<<std::flush;}
 auto out=std::make_shared<GeneralChatSnapshot>();out->generation=shared_->generation;out->sequence=++sequence_;out->availability=std::move(availability);out->torrentDht=torrentFacts_;out->discoveries=swarmSnapshot_;out->discoveryNetwork=rcmFacts_;out->discoverySearch=rcmSearchView_;out->selected=selected_;out->closing=closing_;out->closed=closing_&&!pulse_.isActive();
 for(const auto& [key,work]:ratings_){(void)key;if(work->published){out->ratings.push_back(work->published);}}
 for(auto& [key,room]:rooms_){
  const bool joined=room.token&&room.token->active&&(room.session||!room.lanes.size())&&!closing_;auto facts=room.session?room.session->facts():msg::RoomSessionFacts{};
  std::string status=room.status;if(joined&&room.session){status=std::to_string(facts.peers)+" room nodes, "+std::to_string(facts.alive)+" responding";}
  auto settings=room.settings;if(room.core){settings.readOnly=room.core->readOnly();}
  out->rooms.push_back({std::move(settings),std::move(status),joined,joined&&room.session&&!room.core->readOnly(),room.unread,facts.peers,facts.active});
  if(key==selected_&&room.core){if(auto messages=room.core->messages(QDateTime::currentMSecsSinceEpoch())){out->messages=std::move(*messages);}}
 }
 for(std::size_t lane=0;lane<lanes_.size();++lane){if(!lanes_[lane]||!lanes_[lane]->actor){continue;}
  const auto observed=lanes_[lane]->actor->endpoint();if(!closing_&&!owned_&&publicAddress(observed)){(observed.address.protocol()==QAbstractSocket::IPv4Protocol?out->externalV4:out->externalV6)=observed.address.toString().toStdString();}
  if(const auto captured=lanes_[lane]->actor->dhtStatisticsSnapshot()){out->transports[lane]=std::make_shared<const udp::OwnedDhtStatisticsSnapshot>(*captured);}
  if(lanes_[lane]->router){if(const auto captured=lanes_[lane]->router->snapshot()){out->routers[lane]=std::make_shared<const router::RouterStatisticsSnapshot>(*captured);}}
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
  auto shared=std::make_shared<Shared>(std::move(factory.lease_),generation);auto discoveries=SwarmDiscoveryProfile::create(*shared->lease);if(discoveries){shared->swarmProfile=std::move(*discoveries);}auto sources=engine::AzureusTorrentSources::create(*shared->lease);if(sources){shared->torrentSources=std::move(*sources);}auto initial=std::make_shared<GeneralChatSnapshot>();initial->generation=generation;initial->availability="Opening Public chats…";shared->snapshot.store(std::move(initial));
  auto service=std::unique_ptr<GeneralChats>(new GeneralChats(std::make_unique<Impl>(shared)));service->impl_->worker=std::thread([shared,attach=std::move(factory.attach_),owned=factory.owned_,standards=factory.standardNetworks_]()mutable{
   try{QEventLoop loop;Owner owner(shared,loop,owned,standards,std::move(attach));QTimer::singleShot(0,&owner,[&]{owner.start();});loop.exec();}
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
bool GeneralChats::refreshDiscoveries(){
 if(!impl_){return false;}auto& shared=*impl_->shared;std::lock_guard lock(shared.mutex);if(shared.stop||shared.swarmCommands.size()>=64){return false;}shared.swarmCommands.push_back({SwarmCommand::Kind::Refresh,{},{},{}});if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}return true;
}
bool GeneralChats::discoverNow(){
 if(!impl_){return false;}auto& shared=*impl_->shared;std::lock_guard lock(shared.mutex);if(shared.stop||shared.swarmCommands.size()>=64){return false;}shared.swarmCommands.push_back({SwarmCommand::Kind::Cycle,{},{},{}});if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}return true;
}
bool GeneralChats::lookupDiscoveries(std::string hash,std::int64_t fileSize){
 if(!impl_||hash.size()!=20||(fileSize&&fileSize<50*1024*1024)){return false;}auto& shared=*impl_->shared;std::lock_guard lock(shared.mutex);if(shared.stop||shared.swarmCommands.size()>=64){return false;}shared.swarmCommands.push_back({SwarmCommand::Kind::Lookup,std::move(hash),fileSize,{}});if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}return true;
}
bool GeneralChats::searchDiscoveries(rcm::SearchRequest request){
 if(!impl_||!rcm::encodeSearchRequest({rcm::SearchOperation::Query,request})){return false;}auto& shared=*impl_->shared;std::lock_guard lock(shared.mutex);if(shared.stop||shared.swarmCommands.size()>=64){return false;}SwarmCommand command;command.kind=SwarmCommand::Kind::Search;command.search=std::move(request);shared.swarmCommands.push_back(std::move(command));if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}return true;
}
bool GeneralChats::cancelDiscoverySearch(){
 if(!impl_){return false;}auto& shared=*impl_->shared;std::lock_guard lock(shared.mutex);if(shared.stop||shared.swarmCommands.size()>=64){return false;}SwarmCommand command;command.kind=SwarmCommand::Kind::CancelSearch;shared.swarmCommands.push_back(std::move(command));if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}return true;
}
bool GeneralChats::changeDiscoveries(SwarmDiscoveryChanges changes){
 if(!impl_||changes.read.size()>rcm::MaximumItems||changes.unread.size()>rcm::MaximumItems||changes.removed.size()>rcm::MaximumItems){return false;}
 for(const auto* keys:{&changes.read,&changes.unread,&changes.removed}){for(const auto& key:*keys){if(key.size()>4096){return false;}}}if(changes.settings){if(changes.settings->sources.size()>4096){return false;}for(const auto& source:changes.settings->sources){if(source.size()>1024){return false;}}}
 auto& shared=*impl_->shared;std::lock_guard lock(shared.mutex);if(shared.stop||shared.swarmCommands.size()>=64){return false;}shared.swarmCommands.push_back({SwarmCommand::Kind::Change,{},{},std::move(changes)});if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}return true;
}
namespace {
bool queueRating(const std::shared_ptr<Shared>& shared,RatingCommand command){
 std::lock_guard lock(shared->mutex);if(shared->stop||shared->ratingCommands.size()>=64){return false;}
 shared->ratingCommands.push_back(std::move(command));if(shared->receiver){QCoreApplication::postEvent(shared->receiver,new QEvent(commandEvent()));}return true;
}
}
bool GeneralChats::updateRatingDownloads(std::vector<plugins::rating::publication::ReadHint> hints){
 if(!impl_||hints.size()>plugins::rating::publication::ReadSchedule::maximumDownloads){return false;}for(const auto& hint:hints){const auto raw=QByteArray::fromHex(QByteArray::fromStdString(hint.id));if((hint.id.size()!=40&&hint.id.size()!=64)||raw.toHex().toUpper().toStdString()!=hint.id){return false;}}auto& shared=*impl_->shared;std::lock_guard lock(shared.mutex);if(shared.stop){return false;}if(hints==shared.lastRatingHints){return true;}shared.lastRatingHints=hints;shared.ratingHints=std::move(hints);if(!shared.ratingHintsPending){shared.ratingHintsPending=true;if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}}return true;
}
bool GeneralChats::readRatings(core::TorrentHash hash){return impl_&&queueRating(impl_->shared,{hash,false,false});}
bool GeneralChats::publishRating(core::TorrentHash hash){return impl_&&queueRating(impl_->shared,{hash,true,false});}
bool GeneralChats::cancelRatings(core::TorrentHash hash){return impl_&&queueRating(impl_->shared,{hash,false,true});}
bool GeneralChats::updateTorrents(std::vector<engine::AzureusTorrentTarget> values){
 if(!impl_||values.size()>engine::MaximumAzureusTorrentTargets){return false;}auto& shared=*impl_->shared;
 std::erase_if(values,[](const auto& target){return !target.live||!target.live->load();});
 std::lock_guard lock(shared.mutex);if(shared.stop){return false;}if(values==shared.lastTorrentTargets){return true;}shared.lastTorrentTargets=values;shared.torrentTargets=std::move(values);
 if(!shared.torrentTargetsPending){shared.torrentTargetsPending=true;if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}}return true;
}
std::vector<engine::AzureusPeerBatch> GeneralChats::takeTorrentPeers(){auto& shared=*impl_->shared;std::lock_guard lock(shared.mutex);std::vector<engine::AzureusPeerBatch> out;out.swap(shared.torrentPeers);return out;}
void GeneralChats::recordDeliveredTorrentPeers(std::size_t count)noexcept{if(impl_&&count){impl_->shared->deliveredTorrentPeers.fetch_add(count,std::memory_order_relaxed);}}
bool GeneralChats::setReadingRoom(std::string key){
 if(!impl_||key.size()>4096){return false;}auto& shared=*impl_->shared;std::lock_guard lock(shared.mutex);
 if(shared.stop){return false;}if(shared.readingRoom==key){return true;}shared.readingRoom=std::move(key);
 if(shared.receiver&&!shared.readingWakePending){shared.readingWakePending=true;QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}return true;
}
std::optional<std::uint64_t> GeneralChats::send(std::string key,std::string text){return impl_->enqueue({0,Kind::Send,std::move(key),std::move(text)});}
std::optional<std::uint64_t> GeneralChats::options(std::string key,bool favourite,bool saveHistory){return impl_->enqueue({0,Kind::Options,std::move(key),{},favourite,saveHistory});}
std::optional<std::uint64_t> GeneralChats::nicknameMode(std::string key,bool shared){return impl_->enqueue({0,Kind::NicknameMode,std::move(key),{},shared});}
std::optional<std::uint64_t> GeneralChats::nickname(std::string key,std::string value,bool expectedShared){return impl_->enqueue({0,Kind::Nickname,std::move(key),std::move(value),expectedShared});}
void GeneralChats::requestStop()noexcept{if(!impl_){return;}auto& shared=*impl_->shared;shared.stop=true;if(auto witness=shared.witness.load()){witness->revoke();}try{std::lock_guard lock(shared.mutex);if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}}catch(...){} }
void GeneralChats::join()noexcept{
 if(!impl_){return;}requestStop();std::lock_guard lock(impl_->joinedMutex);if(impl_->worker.joinable()){impl_->worker.join();}
 auto& shared=*impl_->shared;if(shared.networkIo){shared.networkIo->join();shared.networkIo.reset();}if(shared.io){shared.io->join();shared.io.reset();}shared.swarmProfile.reset();shared.lease.reset();shared.witness.store({});
}
} // namespace biglybtc::app
