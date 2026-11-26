#include "../engine/runtime_console.hpp"
#include "biglybtc/move_only_function.hpp"
#include "../services/azureus_nat/directory.hpp"
// SPDX-License-Identifier: GPL-2.0-or-later
#include "general_chat_service.hpp"
#include "../services/azureus_db_runtime/survey_policy.hpp"
#include "chat_command.hpp"
#include "send_pace.hpp"
#include "participant_name.hpp"
#include "read_marker.hpp"
#include "notification_policy.hpp"
#include "../services/msgsync/activity_policy.hpp"
#include "private_policy.hpp"
#include "private_linger.hpp"
#include "../services/msgsync/view_references.hpp"
#include "peek_session.hpp"
#include "download_chat_sources.hpp"
#include "chat_profile_io.hpp"
#include "session.hpp"
#include "discovery.hpp"
#include "search_wire.hpp"
#include "azureus_public_policy.hpp"
#include "azureus_contacts.hpp"
#include "azureus_remote_diversification.hpp"
#include "azureus_local_diversification.hpp"
#include "azureus_key_block_area.hpp"
#include "azureus_identity_profile.hpp"
#include "../services/azureus_identity/address_reports.hpp"
#include "../services/azureus_key_block/state.hpp"
#include "biglybtc/azureus_endpoint.hpp"
#include "lookup.hpp"
#include "publication_policy.hpp"
#include "value_envelope.hpp"
#include "chat_notice.hpp"
#include <QCryptographicHash>
#include <thread>
#include <limits>
#include <bit>
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
#include <tuple>
#include <variant>
namespace biglybtc::engine {
std::expected<ChatTransportFactory,storage::Error> ChatTransportFactory::owned(const libtorrent::session_handle& session,const storage::ProfileLease& lease,unsigned short port,bool standardNetworks,std::chrono::milliseconds ownedCachePeriod,std::chrono::milliseconds ownedSurveyPeriod,std::optional<OwnedKeyBlockFixtureKey> fixtureKey,std::chrono::milliseconds ownedBindingPeriod){
 if(ownedCachePeriod.count()<0||(ownedCachePeriod.count()>0&&ownedCachePeriod<std::chrono::milliseconds(25))||ownedCachePeriod>std::chrono::hours(1)){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Invalid owned cache interval"});}
 if(ownedSurveyPeriod.count()<0||(ownedSurveyPeriod.count()>0&&ownedSurveyPeriod<std::chrono::milliseconds(25))||ownedSurveyPeriod>std::chrono::hours(1)){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Invalid owned survey interval"});}
 if(fixtureKey&&(fixtureKey->modulus.empty()||fixtureKey->modulus.size()>2048||fixtureKey->exponent.empty()||fixtureKey->exponent.size()>16||fixtureKey->modulus.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos||fixtureKey->exponent.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Invalid owned key-block fixture key"});}
 if(ownedBindingPeriod.count()<0||(ownedBindingPeriod.count()>0&&ownedBindingPeriod<std::chrono::milliseconds(25))||ownedBindingPeriod>std::chrono::hours(1)){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Invalid owned binding interval"});}
 auto held=lease.retain();if(!held){return std::unexpected(held.error());}
 return ChatTransportFactory{std::move(*held),[session,port](std::optional<unsigned short> requested)->std::expected<std::shared_ptr<services::shared_udp::Provider>,services::azureus_udp::Failure>{
  auto value=services::shared_udp::Provider::attach(session,requested.value_or(port));if(!value){return std::unexpected(value.error());}
  for(const auto& binding:(*value)->bindings()){if(!binding.address.isLoopback()){(*value)->close();return std::unexpected(services::azureus_udp::Failure::Authority);}}
  return std::shared_ptr<services::shared_udp::Provider>(std::move(*value));
 },true,standardNetworks,ownedCachePeriod,ownedSurveyPeriod,std::move(fixtureKey),ownedBindingPeriod};
}
}
namespace biglybtc::app {
namespace {
namespace nat=services::azureus_nat;namespace udp=services::azureus_udp;namespace wire=services::shared_udp;namespace msg=services::msgsync;
namespace nodes=services::azureus_nodes;namespace db=services::azureus_db_runtime;namespace router=services::azureus_router;
using Clock=std::chrono::steady_clock;using Broker=detail::AzureusProfileIo;using Kind=GeneralChatResult::Kind;
struct Submission {std::string key,text;};
struct SendBatch {std::string raw;std::vector<std::string> parts;std::optional<std::int32_t> type;std::size_t index=0;std::uint64_t epoch=0;bool finished=false,reserved=false;Clock::time_point due{};};
struct PrivateSelection {std::uint64_t epoch=0;std::string publicKey,firstText={};};
struct PrivatePolicyChange {std::int32_t expected=3,value=3;};
struct NotificationChange {ChatNotificationSettings expected,value;};
struct IndicationChange {std::uint64_t epoch=0;bool expected=false,value=false;};
struct Command {std::uint64_t id=0;Kind kind=Kind::Join;std::string key{},text{};bool first=false,second=false;std::optional<Submission> reply={};std::shared_ptr<SendBatch> batch={};std::optional<NotificationChange> notifications={};std::optional<IndicationChange> indications={};std::optional<PrivateSelection> privateSelection={};std::optional<PrivatePolicyChange> privatePolicy={};std::string openedRoom={};std::uint64_t ownerEpoch=0,view=0;bool scopedView=false;std::optional<std::pair<ChatWindowSettings,ChatWindowSettings>> windows={};std::uint64_t cancelRequest=0;bool privateOnly=false;std::optional<ActivityReference> activity={};};
QEvent::Type commandEvent(){static const auto type=static_cast<QEvent::Type>(QEvent::registerEventType());return type;}
core::SharedUdpEndpoint endpoint(udp::Peer value){core::SharedUdpEndpoint out;out.port=value.port;if(value.address.protocol()==QAbstractSocket::IPv4Protocol){out.length=4;const auto ip=value.address.toIPv4Address();for(unsigned i=0;i<4;++i){out.address[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}}else{out.length=16;const auto ip=value.address.toIPv6Address();std::copy_n(ip.c,16,out.address.begin());}return out;}
QHostAddress address(const std::array<std::uint8_t,16>& bytes,unsigned length){if(length==4){return QHostAddress((quint32(bytes[0])<<24)|(quint32(bytes[1])<<16)|(quint32(bytes[2])<<8)|bytes[3]);}Q_IPV6ADDR ip{};std::copy_n(bytes.begin(),16,ip.c);return QHostAddress(ip);}
bool publicAddress(udp::Peer peer){const auto value=endpoint(peer);return core::azureusPublicAddress(std::span(value.address).first(value.length));}
storage::Error failure(storage::ErrorCode code,const char* message){return {code,message};}
std::expected<void,storage::Error> automaticChatPermission(const storage::ProfileLease& lease){auto settings=storage::readSharedRatingSettings(lease);if(!settings){return std::unexpected(settings.error());}if(!settings->enabled()||!settings->autoChat){return std::unexpected(failure(storage::ErrorCode::Unsupported,"Automatic download chats are disabled"));}return {};}
struct LocalDiversificationInputs final:services::azureus_storage_stats::DiversificationControl {
 std::expected<std::int64_t,services::azureus_storage_stats::Error> expiryJitterMilliseconds()noexcept override {return QRandomGenerator::system()->bounded(86400000U);}
 std::int64_t suspendedUntilMilliseconds()const noexcept override {return 0;}
};
struct RoomToken {std::uint64_t epoch=0;std::string key,configKey;storage::ChatIdentityStamp stamp{};std::optional<storage::DownloadChatSnapshot> download;std::atomic<bool> active{true};};
struct SwarmCommand {enum class Kind {Refresh,Lookup,Attributes,ScanTags,CancelTagScan,DismissTag,Change,Cycle,Search,CancelSearch};Kind kind=Kind::Refresh;std::string hash;std::int64_t fileSize=0;SwarmDiscoveryChanges changes;std::optional<rcm::SearchRequest> search={};rcm::TagScanSelections tagSelections={};};
using RatingSource=std::variant<storage::DistributedRatingSnapshot,storage::PublicRatingHashSnapshot>;
const auto& ratingSourceKey(const RatingSource& source){return std::visit([](const auto& item)->const std::array<std::uint8_t,27>&{return item.key;},source);}
const auto& ratingSourceRevision(const RatingSource& source){return std::visit([](const auto& item)->const std::array<std::uint8_t,32>&{return item.sourceRevision;},source);}
std::expected<RatingSource,storage::Error> readRatingSource(const storage::ProfileLease& lease,const core::TorrentHash& hash,bool byHash){
 if(byHash){auto source=storage::readPublicRatingHash(lease,hash);if(!source){return std::unexpected(source.error());}return RatingSource{std::move(*source)};}
 auto source=storage::readSharedDistributedRating(lease,hash);if(!source){return std::unexpected(source.error());}return RatingSource{std::move(*source)};
}
std::string ratingWorkKey(const core::TorrentHash& hash,bool byHash){return (byHash?"hash/":"")+hash.hex();}
struct RatingCommand {core::TorrentHash hash;bool publish=false,cancel=false,byHash=false;};
struct PeekCommand {core::TorrentHash hash;bool cancel=false,open=false,automatic=false;std::optional<msg::PeekSummary> summary={};};
struct Shared {
 explicit Shared(std::shared_ptr<const storage::ProfileLease> lease,std::uint64_t generation):generation(generation),lease(std::move(lease)),io(std::make_unique<ChatProfileIo>(this->lease)),networkIo(std::make_unique<Broker>(this->lease)){auto sources=storage::DownloadChatSources::create(*this->lease);if(!sources){throw std::runtime_error("Cannot retain download chat subjects");}downloadSources=std::move(*sources);}
 std::uint64_t generation;std::shared_ptr<const storage::ProfileLease> lease;
 std::shared_ptr<storage::DownloadChatSources> downloadSources;
 std::unique_ptr<ChatProfileIo> io;std::unique_ptr<Broker> networkIo;
 std::atomic<std::shared_ptr<const GeneralChatSnapshot>> snapshot;
 std::atomic<std::shared_ptr<const core::SharedUdpWitness>> witness;
 std::atomic<std::int64_t> prepareUntil{0};std::atomic<bool> prepared{false};
 std::atomic<bool> stop{false};std::mutex mutex;QObject* receiver=nullptr;
 std::string readingRoom;std::uint64_t readingEpoch=0;bool readingWakePending=false;
 std::vector<plugins::rating::publication::ReadHint> ratingHints,lastRatingHints;bool ratingHintsPending=false;
 std::deque<RatingCommand> ratingCommands;std::deque<PeekCommand> peekCommands;
 std::shared_ptr<engine::AzureusTorrentSources> torrentSources;
 std::shared_ptr<SwarmDiscoveryProfile> swarmProfile;std::deque<SwarmCommand> swarmCommands;
 std::vector<SwarmTagUpdate> swarmTagUpdates;
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
  std::unique_ptr<services::azureus_nat::Session> nat;std::unique_ptr<nat::Directory> natDirectory;
  std::shared_ptr<router::RouterOwner> router;std::shared_ptr<db::Owner> database;
  std::shared_ptr<core::AzureusDiversificationLedger> diversifications=std::make_shared<core::AzureusDiversificationLedger>();
  struct RemoteWaiter {core::AzureusRemoteDiversification record;Clock::time_point end;std::function<void(bool)> done;};
  std::vector<RemoteWaiter> remoteWaiters;bool remoteSaving=false;Clock::time_point remoteRetry{};
  std::vector<core::AzureusLocalDiversification> localSeeds;
  std::shared_ptr<services::azureus_key_block::State> keyBlocks;
  std::optional<storage::AzureusKeyBlockArea> keyBlockArea;
  std::uint64_t keyBlockSavedRevision=0;bool keyBlockSaving=false;Clock::time_point keyBlockRetry{};

  std::map<nodes::NodeId,core::AzureusLocalDiversification> localSaved;
  struct LocalWaiter {std::uint64_t revision;Clock::time_point end;biglybtc::core::MoveOnlyFunction<void(bool)> done;};
  std::vector<LocalWaiter> keyBlockWaiters;
  std::vector<LocalWaiter> localWaiters;std::uint64_t localSavedRevision=1;bool localSaving=false;Clock::time_point localRetry{};
  bool surveyAllowed=false,surveyPending=false,surveyObservedActive=false,surveyWarned=false;std::chrono::milliseconds surveyInterval{900000};Clock::time_point surveyAfter{};
  std::int64_t cachePeriod=1800000;std::chrono::milliseconds cacheInterval{1800000};Clock::time_point cacheAfter{};bool cachePending=false,cacheObservedActive=false,cacheWarned=false;
  bool initialReachable=true,natSavePending=false,natPublishing=false,natPublished=false;std::optional<bool> natSavedReachable;
  std::optional<nat::RoutedContact> natRelay,natPublishedRelay;std::vector<nodes::Contact> natCandidates;std::vector<udp::Ticket> natProbes;std::deque<nodes::NodeId> natFailed;
  Clock::time_point natNextSelect{},natNextProbe{},natNextPublish{};
  services::azureus_identity::ReportState addressReports;
  nodes::Contact local;bool initializing=false,activating=false,ready=false,probing=false,dns=false,dnsResolved=false;Clock::time_point probeAfter{};int dnsId=-1;std::string error;
 };
 struct Room {
  msg::ViewReferences viewReferences;
  ChatRoomSettings settings;std::shared_ptr<msg::Room> core;std::unique_ptr<msg::RoomSession> session;
  std::shared_ptr<RoomToken> token;std::vector<std::size_t> lanes;
  bool background=false,download=false;Clock::time_point backgroundUntil{};std::shared_ptr<const std::vector<msg::RoomMessage>> messageCache;std::uint64_t messageCacheMutation=0,messageCacheEpoch=0;
  std::string status,localStatus;msg::SendPace sendPace;Clock::time_point nextSend{};std::uint32_t unread=0;std::set<msg::MessageId> seen,attention,postingCandidates;std::set<std::string> participantKeys;std::optional<std::string> localRead,pendingRead;bool readPending=false,initialObserved=false,readError=false,unseenMention=false;std::map<msg::MessageId,bool> mentionCache;
  std::uint64_t mutation=0,savingMutation=0;bool historyDirty=false,historyPending=false,opening=false;
  std::optional<msg::RoomPeer> privatePeer;std::string privateParentKey;bool privateClosing=false,privateRetiring=false;std::optional<msg::PrivateLinger> privateLinger;
  std::array<Clock::time_point,3> nextLookup{},nextPut{};
 };
 struct Prepared {bool privateChecked=false;std::optional<std::string> privateRefusal;std::optional<rcm::Settings> searchSettings;std::set<const std::atomic<bool>*> related;std::set<const std::atomic<bool>*> torrents;std::array<bool,3> lanes{};std::set<std::uint64_t> keys;std::set<std::uint64_t> changed;std::set<std::uint64_t> downloads,revokedDownloads;};
 struct Operation {Broker::Operation run;Prepared data;};
 struct Frame {
  Clock::time_point end;Broker::Handle job;std::shared_ptr<Operation> operation;
  biglybtc::core::MoveOnlyFunction<void()> effect,failed;bool executed=false;std::uint64_t networkEpoch=0;
 };
 struct Crypto {Broker::Handle barrier;biglybtc::core::MoveOnlyFunction<void()> effect;};
 struct Meta {Command command;std::shared_ptr<RoomToken> token;bool history=false,readMarker=false;};
 std::shared_ptr<Shared> shared_;QEventLoop& loop_;bool owned_=false,standardNetworks_=false;
 std::function<std::expected<std::shared_ptr<wire::Provider>,udp::Failure>(std::optional<unsigned short>)> attach_;
 std::chrono::milliseconds ownedCachePeriod_{},ownedSurveyPeriod_{};
 std::optional<engine::OwnedKeyBlockFixtureKey> ownedKeyBlockFixtureKey_;
 std::shared_ptr<wire::Provider> provider_;std::array<std::unique_ptr<Lane>,3> lanes_;
 std::shared_ptr<std::map<std::int32_t,std::shared_ptr<storage::AzureusValueVersions>>> versions_=std::make_shared<std::map<std::int32_t,std::shared_ptr<storage::AzureusValueVersions>>>();
 std::map<std::string,Room> rooms_;std::map<std::uint64_t,Meta> metadata_;std::list<Frame> frames_;std::deque<Crypto> crypto_;
 bool natPreparing_=false;
 std::uint64_t networkEpoch_=1;bool resettingNetwork_=false,reconnectingNetwork_=false,transportConfigPending_=false;
 std::optional<unsigned short> attachPort_;std::chrono::milliseconds bindingInterval_{30000};Clock::time_point nextTransportConfig_{};
 std::array<std::shared_ptr<db::Owner>,3> parkedDatabases_;
 std::array<std::vector<wire::ProbePeer>,3> parkedSeeds_;
 Frame* active_=nullptr;QTimer pulse_,timer_;std::uint64_t epoch_=0,sequence_=0,ticks_=0;
 std::map<std::string,SignedChatActivity> pendingActivities_;bool activityPending_=false;Clock::time_point nextActivity_{};
 ChatPreferences preferences_;bool preferencesReady_=false,preferencesDirty_=true,preferencesPending_=false;
 std::string lastConsoleAvailability_;
 std::uint64_t readingEpoch_=0;std::string reading_,selected_,availability_="Opening saved Public rooms…";bool dirty_=true,polling_=false,closing_=false;
 Clock::time_point attachEnd_=Clock::now()+std::chrono::seconds(30),nextAttach_{};unsigned attachAttempts_=0;bool attachExhausted_=false;
 Clock::time_point closeEnd_{},lastHistory_=Clock::now(),lastBindings_=Clock::now();
 bool allowed(std::size_t lane)const{return active_&&active_->networkEpoch==networkEpoch_&&!resettingNetwork_&&Clock::now()<active_->end&&active_->operation->data.lanes[lane]&&!shared_->stop.load();}
 void result(const Command& command,std::optional<storage::Error> error={},std::string opened={}){
  if(!command.id){return;}if(privatePending_.erase(command.id)){changed();}if(command.batch){if(command.batch->finished){return;}command.batch->finished=true;if(error&&command.batch->index){error->message+="; "+std::to_string(command.batch->index)+" message parts were already accepted";}}if(!error&&command.reply&&command.kind==Kind::Join){opened=command.key;}if(!error&&opened.empty()){opened=command.openedRoom;}std::lock_guard lock(shared_->mutex);shared_->results.push_back({command.id,command.reply?Kind::Send:command.kind,command.reply?command.reply->key:command.key,command.reply?command.reply->text:command.batch?command.batch->raw:command.text,std::move(error),std::move(opened)});
 }
 void changed(){dirty_=true;}
 bool later(biglybtc::core::MoveOnlyFunction<void()> effect){
  if(closing_||crypto_.size()>=128){return false;}Broker::Handle barrier;
  if(active_){barrier=active_->job;}else{for(auto& frame:frames_){if(frame.job&&frame.executed&&!Broker::released(frame.job)){barrier=frame.job;}}}
  crypto_.push_back({std::move(barrier),std::move(effect)});return true;
 }
 bool gate(Clock::time_point end,biglybtc::core::MoveOnlyFunction<void()> effect,biglybtc::core::MoveOnlyFunction<void()> failed={},Broker::Operation custom={},std::optional<std::string> privatePeer={}){
  if(closing_||resettingNetwork_||frames_.size()>=64||end<=Clock::now()){return false;}
  auto operation=std::make_shared<Operation>();
  if(custom){operation->run=std::move(custom);}else{
   std::array<std::shared_ptr<storage::AzureusPublicPolicy>,3> policies{};
   for(std::size_t i=0;i<3;++i){if(lanes_[i]){policies[i]=lanes_[i]->policy;}}
   std::vector<std::shared_ptr<RoomToken>> keys;for(const auto& [key,room]:rooms_){(void)key;if(room.token){keys.push_back(room.token);}}
   const auto searchProfile=shared_->swarmProfile;
   struct DownloadPermit {std::uint64_t id;std::shared_ptr<PeekWork> work;storage::DownloadChatSnapshot source;};std::vector<DownloadPermit> downloads;for(const auto& [hash,work]:peeks_){(void)hash;if(work->alive&&work->source){downloads.push_back({work->id,work,*work->source});}}
   const auto downloadLease=shared_->lease;const auto downloadSources=shared_->downloadSources;
   operation->run=[searchProfile,operation=std::weak_ptr<Operation>(operation),policies=std::move(policies),keys=std::move(keys),downloads=std::move(downloads),downloadLease,downloadSources,privatePeer=std::move(privatePeer),end](std::stop_token stop)->Broker::Result{
    const auto op=operation.lock();if(!op||stop.stop_requested()||Clock::now()>=end){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Chat source operation expired"));}
    std::shared_ptr<storage::AzureusPublicPolicy> valid;
    for(std::size_t i=0;i<3;++i){if(policies[i]&&policies[i]->validate()){op->data.lanes[i]=true;if(!valid){valid=policies[i];}}}
    if(valid&&searchProfile){auto settings=searchProfile->searchPermission();if(settings){op->data.searchSettings=std::move(*settings);}}
    if(valid){for(const auto& key:keys){if(!key->active.load()){continue;}bool source=true;if(key->download){auto fresh=downloadSources->read(key->download->hash);source=fresh&&fresh->sourceRevision==key->download->sourceRevision;}if(source&&valid->validateChatIdentity(key->configKey,key->stamp)){op->data.keys.insert(key->epoch);}else{op->data.changed.insert(key->epoch);}}}
    if(valid){for(const auto& target:downloads){if(!target.work->alive){continue;}auto source=downloadSources->read(target.source.hash);if(source&&source->sourceRevision==target.source.sourceRevision&&(!target.work->automatic||automaticChatPermission(*downloadLease))){op->data.downloads.insert(target.id);}else{op->data.revokedDownloads.insert(target.id);}}}
    if(valid&&privatePeer){auto profile=ChatProfile::open(*downloadLease);if(!profile){return std::unexpected(profile.error());}const std::array<std::string,1> peers{*privatePeer};auto prefs=(*profile)->preferences(peers);if(!prefs){return std::unexpected(prefs.error());}const auto refusal=msg::privateChatRefusal(prefs->privateChatState,false,true,prefs->participants.at(*privatePeer).pinned);op->data.privateChecked=true;if(refusal){op->data.privateRefusal=std::string(*refusal);}}
    return Broker::Data{};
   };
  }
  frames_.push_back({end,{},std::move(operation),std::move(effect),std::move(failed),false,networkEpoch_});return true;
 }
 void drainFrames(){
  shared_->networkIo->drainWake();
  for(auto at=frames_.begin();at!=frames_.end();){
   if(!at->job){
    if(at->networkEpoch!=networkEpoch_||Clock::now()>=at->end){if(at->failed){at->failed();}at=frames_.erase(at);continue;}
    const auto box=at->operation;
    auto job=shared_->networkIo->submit([box](std::stop_token stop){return box->run(stop);});
    if(!job){++at;continue;}at->job=std::move(*job);
   }
   if(!at->executed&&(at->networkEpoch!=networkEpoch_||Clock::now()>=at->end)){Broker::revoke(at->job);}
   if(!at->executed){if(auto ready=Broker::take(at->job)){
    at->executed=true;active_=&*at;
    const auto acknowledged=qScopeGuard([&]{active_=nullptr;Broker::acknowledge(at->job);});
    if(at->networkEpoch==networkEpoch_){for(auto& [key,room]:rooms_){(void)key;if(room.token&&at->operation->data.changed.contains(room.token->epoch)){room.token->active=false;if(room.session){room.session->close();}room.status="Saved identity changed; rejoin this room";changed();}}
    for(const auto id:at->operation->data.revokedDownloads){for(const auto& [hash,work]:peeks_){(void)hash;if(work->id==id&&work->alive){peekFinish(work,"Download chat source changed");}}}}
    try{if(*ready&&at->networkEpoch==networkEpoch_&&Clock::now()<at->end&&!shared_->stop.load()){at->effect();}else if(at->failed){at->failed();}}catch(...){availability_="A chat operation failed";changed();}
   }}
   if(Broker::released(at->job)){if(!at->executed&&at->failed){at->failed();}shared_->networkIo->release(at->job);at=frames_.erase(at);}else{++at;}
  }
 }
 bool meta(ChatProfileRequest request,Meta info){
  const auto id=shared_->io->submit(std::move(request),shared_->generation,Clock::now()+std::chrono::seconds(30));if(!id){result(info.command,id.error());return false;}metadata_.emplace(*id,std::move(info));return true;
 }
 void catalogue(std::vector<ChatRoomSettings> values){
  for(auto& value:values){auto where=rooms_.find(value.key);if(where==rooms_.end()){if(!(value.favourite||value.saveHistory)||rooms_.size()>=256){continue;}where=rooms_.try_emplace(value.key).first;}if(where->second.settings.privateChat){where->second.settings.nickname=std::move(value.nickname);where->second.settings.sharedNickname=value.sharedNickname;}else{where->second.settings=std::move(value);}}
  changed();
 }
 void loadRoom(const Command& command,std::optional<ChatProfileLoad> request={}){
  if(command.key.empty()||command.key.size()>4096){result(command,failure(storage::ErrorCode::Unsupported,"Invalid room name"));return;}
  if(!rooms_.contains(command.key)&&rooms_.size()>=256){result(command,failure(storage::ErrorCode::Busy,"Saved room list is full"));return;}
  auto& room=rooms_[command.key];room.background=false;if(room.opening){result(command,failure(storage::ErrorCode::Busy,"Room is already opening"));return;}if(room.core&&room.token&&room.token->active){if(command.activity){meta(ChatProfileLoad{command.key,false,command.activity},{command,{},false});}else{selected_=command.key;result(command);changed();}return;}
  std::size_t open=0;for(const auto& [key,value]:rooms_){(void)key;open+=bool(value.core&&value.token&&value.token->active)||value.opening;}
  if(open>=16){result(command,failure(storage::ErrorCode::Busy,"Leave a room before opening another"));return;}
  room.settings.key=command.key;room.status="Opening room…";room.opening=true;selected_=command.key;
  if(!meta(request?std::move(*request):ChatProfileLoad{command.key,!command.activity,command.activity},{command,{},false})){room.opening=false;}changed();
 }
 void metadata(){
  for(auto& completion:shared_->io->advance()){
   const auto found=metadata_.find(completion.id);if(found==metadata_.end()){continue;}auto info=std::move(found->second);metadata_.erase(found);
   const auto key=info.command.key;const auto row=rooms_.find(key);if(row!=rooms_.end()&&info.command.kind==Kind::Join){row->second.opening=false;}
   if(info.readMarker){if(row!=rooms_.end()&&row->second.token==info.token){auto& room=row->second;room.readPending=false;if(completion.result&&completion.result->readMarker){room.readError=false;room.settings.lastMessageInfo=*completion.result->readMarker;if(room.pendingRead==room.settings.lastMessageInfo){room.pendingRead.reset();}}else{room.pendingRead.reset();room.readError=true;room.status="Chat read marker could not be saved";}}changed();continue;}
   if(info.command.kind==Kind::ActivityPosts){activityPending_=false;if(!completion.result){for(auto& [name,room]:rooms_){if(room.settings.postNotifications){room.status="Notification could not be saved";}}}changed();continue;}
   if(info.command.kind==Kind::Posting){if(row!=rooms_.end()&&row->second.token==info.token&&completion.result&&completion.result->posting){row->second.settings.postNotifications=*completion.result->posting;row->second.postingCandidates.clear();pendingActivities_.erase(key);}result(info.command,completion.result?std::optional<storage::Error>{}:std::optional<storage::Error>{completion.result.error()});changed();continue;}
   if(info.command.kind==Kind::Preferences){preferencesPending_=false;if(completion.result&&completion.result->preferences){preferences_=std::move(*completion.result->preferences);preferencesReady_=true;for(auto& [name,room]:rooms_){updateAttention(room);if(name==reading_){readRoom(room);}}}else{availability_="Chat display preferences could not be read";}changed();continue;}
   if(info.command.kind==Kind::NotificationSettings){
    if(completion.result&&completion.result->notifications){preferences_.notifications=std::move(*completion.result->notifications);result(info.command);}
    else{result(info.command,completion.result?failure(storage::ErrorCode::Io,"Notification settings result is missing"):completion.result.error());}
    changed();continue;
   }
   if(info.command.kind==Kind::WindowSettings){if(completion.result&&completion.result->windows){preferences_.windows=*completion.result->windows;result(info.command);}else{result(info.command,completion.result?failure(storage::ErrorCode::Io,"Chat window settings result is missing"):completion.result.error());}changed();continue;}
   if(info.command.kind==Kind::PrivateChatPolicy){if(completion.result&&completion.result->privateChatState){preferences_.privateChatState=*completion.result->privateChatState;result(info.command);}else{result(info.command,completion.result?failure(storage::ErrorCode::Io,"Private chat policy result is missing"):completion.result.error());}changed();continue;}
   if(info.history){if(row!=rooms_.end()&&row->second.token==info.token){row->second.historyPending=false;if(!completion.result){row->second.status="History could not be saved";}else{row->second.historyDirty=row->second.mutation!=row->second.savingMutation;}}changed();continue;}
   if(!completion.result){if(row!=rooms_.end()){row->second.status=completion.result.error().message;}result(info.command,completion.result.error());changed();continue;}
   auto& data=*completion.result;if(data.sharedPublicNickname){for(auto& [name,room]:rooms_){(void)name;if(room.settings.sharedNickname){room.settings.nickname=*data.sharedPublicNickname;}}}
   if(info.command.kind==Kind::Join&&key.empty()){
    catalogue(std::move(data.catalogue));std::vector<std::string> favourites;for(const auto& [name,room]:rooms_){if(room.settings.favourite){favourites.push_back(name);}}
    for(const auto& name:favourites){loadRoom({0,Kind::Join,name});}continue;
   }
   if(data.identity&&!closing_&&row!=rooms_.end()){
    if(row->second.settings.privateChat){result(info.command,failure(storage::ErrorCode::Conflict,"Chat role changed before notification View"));continue;}
    if(info.command.activity&&row->second.core&&row->second.token){if(row->second.token->stamp!=data.identity->sourceStamp){result(info.command,failure(storage::ErrorCode::Conflict,"Public chat identity changed"));continue;}selected_=key;result(info.command,{},key);changed();continue;}
    auto identity=std::move(*data.identity);auto opened=msg::Room::open(key,identity.uid,std::move(identity.signer),"de",identity.settings.readOnly);
    if(!opened){result(info.command,failure(storage::ErrorCode::Unsupported,"Room identity or managed policy is unsupported"));continue;}
    auto& room=rooms_[key];if(room.token){room.token->active=false;}room.session.reset();room.settings=identity.settings;room.core=std::make_shared<msg::Room>(std::move(*opened));room.seen.clear();room.attention.clear();room.mentionCache.clear();room.participantKeys.clear();room.initialObserved=false;room.localRead.reset();room.pendingRead.reset();room.readPending=false;room.readError=false;room.sendPace={};room.nextSend={};
    room.viewReferences=msg::ViewReferences{};room.messageCache.reset();
    room.token=std::make_shared<RoomToken>();room.token->epoch=++epoch_;room.token->key=key;room.token->configKey=ChatProfile::configKey(key);room.token->stamp=identity.sourceStamp;
    if(data.history){static_cast<void>(room.core->importHistory(std::move(*data.history),QDateTime::currentMSecsSinceEpoch()));}
    if(data.historyError){room.status="Stored history is unavailable";}observe(room,false);rebind(room);
   }else if(!data.catalogue.empty()){
    catalogue(std::move(data.catalogue));if(data.history&&row!=rooms_.end()&&row->second.core){static_cast<void>(row->second.core->importHistory(std::move(*data.history),QDateTime::currentMSecsSinceEpoch()));}
   }
   if(info.command.kind==Kind::SpamParticipant){
    if(row==rooms_.end()||row->second.token!=info.token||!info.token||!info.token->active||!row->second.core||row->second.core->spammer(info.command.text)!=info.command.first||!row->second.core->setSpammer(info.command.text,info.command.second,QDateTime::currentMSecsSinceEpoch())){result(info.command,failure(storage::ErrorCode::Conflict,"Signed room changed before moderation completion"));changed();continue;}
    ++row->second.mutation;row->second.messageCache.reset();observe(row->second,false);updateAttention(row->second);
   }
   if(info.command.kind==Kind::HideRatings||info.command.kind==Kind::HideSubscriptions||info.command.kind==Kind::IgnoreParticipant||info.command.kind==Kind::PinParticipant){preferencesDirty_=true;}
   result(info.command,{},info.command.activity?key:std::string{});changed();
  }
 }
 void rebind(Room& room){
  if(!room.core||!room.token||!room.token->active||closing_){return;}
  std::vector<udp::OwnedPingTransport*> actors;std::vector<std::size_t> mapping;
  for(std::size_t i=0;i<3;++i){if(i<2&&(!room.privatePeer||room.privatePeer->lane==i)&&lanes_[i]&&lanes_[i]->ready&&lanes_[i]->actor){actors.push_back(lanes_[i]->actor.get());mapping.push_back(i);}}
  if(room.privatePeer&&std::ranges::find(mapping,room.privatePeer->lane)==mapping.end()){room.session.reset();room.status="Private chat network is unavailable";return;}
  if(mapping==room.lanes&&room.session){return;}room.session.reset();room.lanes=mapping;if(actors.empty()){room.status="Connecting to Public chat network…";return;}
  if(auto encoded=msg::RoomSession::contactMap(actors.front()->endpoint())){room.core->updateContact(std::move(*encoded));}
  msg::RoomSessionHooks hooks;const auto token=room.token;
  hooks.afterRelease=[this](auto callback){return later(std::move(callback));};
  hooks.withPermit=[this](auto end,auto effect){return gate(end,std::move(effect));};
  hooks.revision=[this,token]()->std::optional<std::uint64_t>{if(active_&&token->active&&active_->operation->data.keys.contains(token->epoch)){return token->epoch;}return {};};
  if(!room.core->isPrivate()){hooks.privateRequest=[this,token,mapping](std::size_t lane,auto request,msg::PrivateRequest value){if(lane<mapping.size()){privateIncoming(token,mapping[lane],std::move(request),std::move(value));}};}
  hooks.rendezvous=[this]()->std::optional<std::string>{if(!lanes_[0]||!lanes_[0]->nat){return {};}const auto relay=lanes_[0]->nat->operationalRendezvous();if(!relay){return {};}auto encoded=msg::RoomSession::contactMap(udp::Peer{address(relay->external().address.bytes,relay->external().address.length),relay->external().address.port},relay->external().protocol);return encoded?std::optional{std::move(*encoded)}:std::nullopt;};
  hooks.tunnel=[this,token,mapping](std::size_t lane,const msg::RoomPeer& peer,bool force){if(lane<mapping.size()){natTry(token,mapping[lane],peer,force);}};
  hooks.hint=[this,token,mapping](std::size_t lane,const msg::NodeHint& value){if(lane>=mapping.size()||!token->active){return;}acceptHint(token,mapping[lane],value);};
  hooks.changed=[this,token]{const auto it=rooms_.find(token->key);if(it!=rooms_.end()&&it->second.token==token){observe(it->second,true);}changed();};
  auto privatePeer=room.privatePeer;if(privatePeer){privatePeer->lane=std::size_t(std::ranges::find(mapping,privatePeer->lane)-mapping.begin());}
  auto session=msg::RoomSession::open(room.core,actors,std::move(hooks),std::move(privatePeer));if(session){room.session=std::move(*session);room.status="Finding room nodes…";}else{room.status="Room transport is busy";}
 }
 void updateAttention(Room& room){
  room.unseenMention=false;
  if(!room.core||!preferencesReady_){return;}const auto now=QDateTime::currentMSecsSinceEpoch();auto messages=room.core->messages(now);if(!messages){return;}const auto raw=room.localRead?room.localRead:room.settings.lastMessageInfo;auto marker=raw?msg::parseChatReadMarker(QString::fromUtf8(raw->data(),qsizetype(raw->size())).toStdU16String()):std::nullopt;std::vector<msg::ChatReadMessage> ordered;ordered.reserve(messages->size());for(const auto& message:*messages){ordered.push_back({std::string(reinterpret_cast<const char*>(message.id.data()),message.id.size()),message.timestamp.value_or(now-std::int64_t(message.ageSeconds)*1000)});}std::uint32_t count=0;
  for(std::size_t i=0;i<messages->size();++i){const auto& message=(*messages)[i];if(!room.attention.contains(message.id)){continue;}const auto saved=preferences_.participants.find(message.publicKey);if(saved==preferences_.participants.end()){continue;}if(msg::requiresChatAttention(message.local,saved->second.ignored||message.spammer,message.flashOverride,message.origin,preferences_.display)&&!msg::oldOutstandingMessage(marker,ordered,ordered[i])){++count;if(room.postingCandidates.contains(message.id)&&room.token&&room.token->active&&room.settings.postNotifications&&!room.settings.disableIndicators&&!room.settings.privateChat&&!room.core->isPrivate()){const auto text=msg::activityMessage(QString::fromUtf8(message.localError.empty()?message.text:message.localError),message.origin);const auto channel=msg::activityChannel(QString::fromUtf8(room.settings.key),QString::fromUtf8(room.settings.displayName));const auto token=room.token;pendingActivities_.insert_or_assign(room.settings.key,SignedChatActivity{{room.settings.key,(channel+": "+text).toUtf8().toStdString(),"View",now,false},token->stamp,[token]{return token->active.load();}});}}room.postingCandidates.erase(message.id);}
  room.unread=count;
  // Java getUnseenMessages excludes ignored/self participants, fixes UNKNOWN
  // seen states using the saved marker, then stops at the first seen message.
  // Every tested target is an actual element of this same ledger. Compute
  // the original pointer-range fixup once instead of scanning the ledger for
  // each message on a room update.
  std::optional<std::size_t> readIndex;
  if(marker){for(std::size_t i=0;i<ordered.size();++i){if(ordered[i].id==marker->id){readIndex=i;}}}
  const auto nick=room.settings.nickname.empty()?room.settings.defaultNickname:room.settings.nickname;
  for(std::size_t n=messages->size();n>0;--n){const auto i=n-1;const auto& m=(*messages)[i];const auto participant=preferences_.participants.find(m.publicKey);
   if(m.local||participant==preferences_.participants.end()||(participant->second.ignored||m.spammer)){continue;}
   if(marker&&(ordered[i].id==marker->id||ordered[i].timestamp/1000<=msg::read_marker_detail::cutoff(*marker)||(readIndex&&i<=*readIndex))){break;}
   auto hit=room.mentionCache.find(m.id);
   if(hit==room.mentionCache.end()){hit=room.mentionCache.emplace(m.id,!msg::notificationNickLocations(QString::fromUtf8(m.localError.empty()?m.text:m.localError),QString::fromUtf8(nick.data(),qsizetype(nick.size()))).isEmpty()).first;}
   if(hit->second){room.unseenMention=true;break;}}

 }
 void readRoom(Room& room){
  if(reading_!=room.settings.key||(readingEpoch_&&(!room.token||room.token->epoch!=readingEpoch_))){return;}
  if(room.unread&&room.core){const auto now=QDateTime::currentMSecsSinceEpoch();auto messages=room.core->messages(now);if(messages&&!messages->empty()){const auto& last=messages->back();room.localRead=msg::encodeChatReadMarker(now,last.timestamp.value_or(now-std::int64_t(last.ageSeconds)*1000),std::string_view(reinterpret_cast<const char*>(last.id.data()),last.id.size()));if(!room.core->isPrivate()){room.pendingRead=room.localRead;}}}
  room.unread=0;room.unseenMention=false;room.attention.clear();room.postingCandidates.clear();pendingActivities_.erase(room.settings.key);
 }
 void flushReadMarkers(){
  for(auto& [key,room]:rooms_){if(!room.pendingRead||room.readPending||!room.token||!room.token->active||metadata_.size()>=8){continue;}const auto token=room.token;if(meta(ChatProfileReadMarker{key,room.settings.lastMessageInfo,*room.pendingRead,token->stamp,[token]{return token->active.load();}},{{0,Kind::ReadMarker,key},{token},false,true})){room.readPending=true;}}
 }

 void flushActivities(){
  if(closing_||activityPending_||pendingActivities_.empty()||Clock::now()<nextActivity_||metadata_.size()>=8){return;}ChatProfileActivityPosts posts;posts.now=QDateTime::currentMSecsSinceEpoch();for(auto& [key,post]:pendingActivities_){posts.posts.push_back(post);}if(meta(std::move(posts),{{0,Kind::ActivityPosts,{}},{},false})){pendingActivities_.clear();activityPending_=true;nextActivity_=Clock::now()+std::chrono::seconds(1);}
 }
 void refreshPreferences(){
  if(!preferencesDirty_||preferencesPending_||closing_){return;}std::set<std::string> keys;for(const auto& [name,room]:rooms_){(void)name;keys.insert(room.participantKeys.begin(),room.participantKeys.end());}if(keys.size()>2048){availability_="Chat participant preference bound exceeded";preferencesDirty_=false;changed();return;}
  if(meta(ChatProfilePreferences{{},{keys.begin(),keys.end()}},{{0,Kind::Preferences,{}},{},false})){preferencesPending_=true;preferencesDirty_=false;}
 }
 void observe(Room& room,bool unread){
  if(!room.core){return;}auto messages=room.core->messages(QDateTime::currentMSecsSinceEpoch());if(!messages){return;}
  bool reading=false;{std::lock_guard lock(shared_->mutex);reading=shared_->readingRoom==room.settings.key&&(!shared_->readingEpoch||(room.token&&shared_->readingEpoch==room.token->epoch));}
  std::set<msg::MessageId> current;std::uint32_t added=0;room.participantKeys.clear();
  for(const auto& message:*messages){current.insert(message.id);room.participantKeys.insert(message.publicKey);if(!preferences_.participants.contains(message.publicKey)){preferencesDirty_=true;}if(!room.seen.contains(message.id)){++added;if((unread||!room.initialObserved)&&!reading){room.attention.insert(message.id);if(unread&&room.settings.postNotifications&&!room.settings.privateChat&&!room.settings.disableIndicators){room.postingCandidates.insert(message.id);}}}}
  if(added){++room.mutation;room.historyDirty=true;}std::erase_if(room.attention,[&](const auto& id){return !current.contains(id);});std::erase_if(room.postingCandidates,[&](const auto& id){return !current.contains(id);});std::erase_if(room.mentionCache,[&](const auto& entry){return !current.contains(entry.first);});room.seen=std::move(current);room.initialObserved=true;if(reading){readRoom(room);}else{updateAttention(room);}
 }

 struct RcmDirectTags {
  std::uint64_t request=0;
  std::optional<SwarmTagProbe> capture;
  std::set<std::string> seen;
  std::vector<std::string> replies;
  bool requested=false,observed=false,libraryScan=false;
 };
 struct RcmGroup {
  std::set<std::pair<std::size_t,nodes::NodeId>> seen;
  std::size_t roots=0,hits=0;
  bool diversified=false;
  int publication=-1;
  std::optional<RcmDirectTags> attributes;
 };
 struct RcmLookup {
  std::string parent,advertised;std::int64_t fileSize=0;nodes::NodeId key{};std::size_t lane=0;
  int level=0;unsigned depth=0;bool explicitLookup=false,derived=false,publish=false,queued=false,finished=false,reported=false,observed=false,diversified=false;
  std::shared_ptr<std::atomic<bool>> live=std::make_shared<std::atomic<bool>>(true);
  std::shared_ptr<RcmGroup> group;Clock::time_point end;
  services::azureus_control::LookupController control;std::map<std::uint64_t,udp::Ticket> tickets;
  std::set<std::string> values;
  std::optional<SwarmTagProbe> tagProbe;
  std::vector<std::string> tagReplies;
  bool tagProbeAttempted=false;
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
 std::shared_ptr<const SwarmTagLookupSnapshot> rcmTagLookup_;
 struct RcmTagScan {
  std::uint64_t request=0;
  std::vector<std::string> subjects;
  std::map<std::uint64_t,std::string> active;
  std::size_t next=0,finished=0,unavailable=0;
  bool loading=true,resultLimit=false;
 };
 std::optional<RcmTagScan> rcmTagScan_;
 std::vector<SwarmTagScanFinding> rcmTagFindings_;
 std::set<std::pair<std::string,std::string>> rcmTagFindingKeys_;
 std::shared_ptr<const SwarmTagScanSnapshot> rcmTagScanView_;
 void rcmStartTagScan();
 void rcmPumpTagScan();
 void rcmPublishTagScan(std::string,bool running);
 void rcmStopTagScan(std::string);
 void rcmDismissTags(const rcm::TagScanSelections&);
 bool rcmTagRequestCurrent(std::uint64_t)const;
 std::uint64_t nextTagLookup_=0;
 void rcmTick();void rcmCommand(SwarmCommand);void rcmRefresh();void rcmSnapshot(std::shared_ptr<const SwarmDiscoverySnapshot>);
 bool rcmQueue(std::string,std::string={},std::int64_t=0,int=0,bool=false,bool=false,std::uint64_t=0,bool libraryScan=false);
 void rcmStartDirectTags(std::string);
 void rcmFinishDirectTags(std::uint64_t,std::string,std::vector<std::string> = {},std::string title={},bool complete=false);
 void rcmCompleteDirectTags(const std::shared_ptr<RcmLookup>&);
 bool rcmBegin(std::shared_ptr<RcmLookup>);void rcmPump(std::shared_ptr<RcmLookup>);
 void rcmReply(std::shared_ptr<RcmLookup>,std::uint64_t,udp::FindValueOutcome);
 void rcmFinish(std::shared_ptr<RcmLookup>);void rcmCancel(std::shared_ptr<RcmLookup>);
 void rcmCompleteSwarmTags(const std::shared_ptr<RcmLookup>&);
 bool rcmGate(std::shared_ptr<RcmLookup>,Clock::time_point,biglybtc::core::MoveOnlyFunction<void()>,biglybtc::core::MoveOnlyFunction<void()> = {},std::shared_ptr<std::optional<std::int32_t>> = {},std::shared_ptr<std::string> = {},std::vector<std::string> = {},std::string expectedPayload={},
     std::shared_ptr<std::optional<SwarmTagProbe>> probe={});
 bool rcmAllowed(const RcmLookup&)const;
 void rcmPublish(std::shared_ptr<RcmLookup>);void rcmPumpPut(std::shared_ptr<RcmPublication>);
 bool rcmReadPermit(std::size_t,std::string,biglybtc::core::MoveOnlyFunction<void(bool)>);
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
 bool rcmSearchGate(std::size_t,Clock::time_point,biglybtc::core::MoveOnlyFunction<void()>,biglybtc::core::MoveOnlyFunction<void()> = {},Broker::Operation = {});
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
 struct RatingNotice {
  enum class Step {Queued,Opening,Sync,Delay,Ready,Signing,Committing,Saving,Done};
  core::TorrentHash hash;RatingChatNoticeSnapshot view;std::optional<storage::RatingChatSnapshot> source;
  std::weak_ptr<RoomToken> token;plugins::rating::publication::NoticeWait wait;Step step=Step::Queued;
  Clock::time_point next{};unsigned retries=0;std::atomic<bool> alive{true};
  explicit RatingNotice(core::TorrentHash h):hash(h){}
 };
 std::map<std::string,std::shared_ptr<RatingNotice>> ratingNotices_;std::uint64_t ratingNoticeSequence_=0;
 void ratingNoticeStart(core::TorrentHash);void ratingNoticeCancel(core::TorrentHash);void ratingNoticeTick();
 void ratingNoticeOpen(std::shared_ptr<RatingNotice>);void ratingNoticeSend(std::shared_ptr<RatingNotice>);
 void ratingNoticeSave(std::shared_ptr<RatingNotice>);
 bool ratingNoticeGate(std::shared_ptr<RatingNotice>,biglybtc::core::MoveOnlyFunction<void()>);
 void ratingNoticeDone(std::shared_ptr<RatingNotice> notice,std::string status){notice->step=RatingNotice::Step::Done;notice->view.running=false;notice->view.status=std::move(status);changed();}
 struct RatingQuery {nodes::NodeId key{},chain{};unsigned depth=0;std::uint16_t maximumValues=256;};
 struct RatingLane {
  std::size_t lane=0,consumed=0,nextStore=0,pendingStores=0;
  services::azureus_control::LookupController control;
  std::map<std::uint64_t,udp::Ticket> tickets;
  std::vector<nodes::Contact> destinations;std::vector<udp::Ticket> storeTickets;
  std::optional<services::azureus_values::Value> value;
  std::set<std::string> seen;std::set<nodes::NodeId> readKeys;std::deque<RatingQuery> pendingReads;nodes::NodeId queryKey{},chainKey{};unsigned depth=0;std::uint16_t maximumValues=256;bool diversified=false,diversificationPending=false;std::uint64_t queryEpoch=0;bool keyReady=false;
  bool started=false,queued=false,finished=false,storing=false;
 };
 struct RatingWork {
  core::TorrentHash hash;std::optional<RatingSource> source;
  std::optional<std::array<std::uint8_t,32>> expectedSource;
  nodes::NodeId key{};std::string payload;SharedRatingSnapshot view;std::shared_ptr<const SharedRatingSnapshot> published;
  std::array<RatingLane,2> lanes;Clock::time_point end,auditAfter{};
  std::size_t decodedEvents=0,rawValueBytes=0;bool alive=true,finishing=false;
  explicit RatingWork(core::TorrentHash h):hash(h){}
 };
 std::map<std::string,std::shared_ptr<RatingWork>> ratings_;
 struct RatingLocal {core::TorrentHash hash;std::array<std::uint8_t,32> revision;};
 std::array<std::map<std::string,RatingLocal>,2> localRatingKeys_;
 std::optional<RatingWrite> ratingWriting_;std::weak_ptr<RatingWork> ratingReading_;bool ratingReadActive_=false;std::optional<std::string> ratingReadWaiting_;
 std::deque<RatingCommand> ratingDeferred_;
 void ratingScheduleTick();bool ratingEnqueue(std::string_view,bool);void ratingPolicyReceipt(const plugins::rating::publication::Receipt&);
 std::uint64_t ratingSequence_=0;Clock::time_point ratingTickAfter_{};
 bool ratingGate(std::shared_ptr<RatingWork>,std::size_t,Clock::time_point,biglybtc::core::MoveOnlyFunction<void()>,biglybtc::core::MoveOnlyFunction<void()> = {},std::shared_ptr<std::optional<std::int32_t>> = {});
 void ratingChanged(std::shared_ptr<RatingWork> work){work->published=std::make_shared<const SharedRatingSnapshot>(work->view);changed();}
 void ratingStart(RatingCommand);void ratingTick();void ratingPump(std::shared_ptr<RatingWork>,std::size_t);
 void ratingReply(std::shared_ptr<RatingWork>,std::size_t,std::uint64_t,udp::FindValueOutcome,std::uint64_t);
 void ratingNodeReply(std::shared_ptr<RatingWork>,std::size_t,std::uint64_t,udp::DetailedFindNodeOutcome,std::uint64_t);
 void ratingStore(std::shared_ptr<RatingWork>,std::size_t);
 bool ratingNext(std::shared_ptr<RatingWork>,std::size_t);
 void ratingDiversify(std::shared_ptr<RatingWork>,std::size_t,std::int8_t);
 void ratingNetworkFailed(std::shared_ptr<RatingWork>,std::size_t,std::string);
 void ratingFinish(std::shared_ptr<RatingWork>);void ratingCancel(std::shared_ptr<RatingWork>,std::string,bool removeLocal=true);
 void ratingAdd(std::shared_ptr<RatingWork>,RatingLane&,const services::azureus_values::Value&);
 bool ratingReadPermit(std::size_t,std::string,biglybtc::core::MoveOnlyFunction<void(bool)>);
 void ratingForget(std::shared_ptr<RatingWork>);

 struct Torrent {
  engine::AzureusTorrentTarget target;nodes::NodeId root{};
  std::array<Clock::time_point,3> next{};bool permitted=false,retiring=false;
  std::array<std::map<nodes::NodeId,std::vector<nodes::Contact>>,3> published;
  std::array<std::set<nodes::NodeId>,3> publicationQueued;

  explicit Torrent(engine::AzureusTorrentTarget value):target(std::move(value)){}
 };
 struct TorrentGroup {std::set<nodes::NodeId> seen;};
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
     biglybtc::core::MoveOnlyFunction<void()>,biglybtc::core::MoveOnlyFunction<void()> failed={},
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
 bool torrentReadPermit(std::size_t,std::string,biglybtc::core::MoveOnlyFunction<void(bool)>);
 void forgetLocalTorrent(const std::shared_ptr<Torrent>&,std::size_t,const nodes::NodeId&);
 void acceptHint(std::shared_ptr<RoomToken>,std::size_t,const msg::NodeHint&);
 void attachTransport();
 void startNetworks();
 void reconnectNetwork(std::string);
 void finishNetworkReset();
 void readTransportConfig();
 void addressReport(std::size_t,const udp::AddressReport&,bool force=false);
 void initializeLane(std::size_t);
 void findSeeds(std::size_t);
 void probe(std::size_t);
 void activate(std::size_t,udp::Peer);
 void ingress(wire::Binding,udp::Peer,std::string);
 struct NatAttempt {std::shared_ptr<RoomToken> token;std::size_t lane=0;msg::RoomPeer logical;Clock::time_point started;bool active=false;};
 std::map<std::string,std::shared_ptr<NatAttempt>> natAttempts_;
 void natStart(std::size_t);void natTick(std::size_t);void natPublish(std::size_t,std::optional<nat::RoutedContact>);void natSave(std::size_t,bool);void natPrepare();
 std::optional<core::AzureusRemoteDiversification> remoteRecord(std::size_t,const nodes::NodeId&,std::int8_t);
 void remoteRemember(std::size_t,core::AzureusRemoteDiversification,std::function<void(bool)>);
 bool localCheckpoint(std::size_t,std::uint64_t,biglybtc::core::MoveOnlyFunction<void(bool)>);
 bool keyBlockCheckpoint(std::size_t,std::uint64_t,biglybtc::core::MoveOnlyFunction<void(bool)>);
 void keyBlockTick();
 void localCheckpointTick();void cacheTick();void surveyTick();
 void remoteTick();void rcmDiversify(std::shared_ptr<RcmLookup>,std::int8_t);
 void natTry(std::shared_ptr<RoomToken>,std::size_t,msg::RoomPeer,bool);
 void natResult(const std::shared_ptr<NatAttempt>&,nat::TunnelOutcome);
 void natObserved(std::size_t,const nat::RoutedContact&);

 struct PeekWork {
  explicit PeekWork(core::TorrentHash hash):hash(hash){}
  core::TorrentHash hash;std::uint64_t id=0;std::atomic<bool> alive{true};
  std::optional<storage::DownloadChatSnapshot> source;std::unique_ptr<msg::PeekSession> session;
  DownloadChatPeekSnapshot view;Clock::time_point end;bool opening=false,openRoom=false,automatic=false;
  std::vector<std::size_t> lanes;
 };
 std::map<std::string,std::shared_ptr<PeekWork>> peeks_;std::uint64_t peekId_=0;
 void openDownloadRoom(std::shared_ptr<PeekWork>);
 void peekCommand(PeekCommand);void peekStart(std::shared_ptr<PeekWork>);void peekTick();
 void peekFinish(std::shared_ptr<PeekWork>,std::string);
 bool downloadGate(std::shared_ptr<PeekWork>,Clock::time_point,biglybtc::core::MoveOnlyFunction<void()>,biglybtc::core::MoveOnlyFunction<void()> failed={});
 struct LookupGroup {std::set<nodes::NodeId> keys;std::set<std::pair<nodes::NodeId,std::string>> records;bool diversified=false;};
 struct Lookup {
  std::shared_ptr<RoomToken> token;std::shared_ptr<PeekWork> peek;std::size_t lane=0;services::azureus_control::LookupController control;
  std::map<std::uint64_t,udp::Ticket> tickets;bool diversified=false,finished=false,publishing=false;
  Clock::time_point end;nodes::NodeId target{};std::shared_ptr<LookupGroup> group;unsigned depth=0;bool root=true;
 };
 std::vector<std::shared_ptr<Lookup>> lookups_;std::deque<std::shared_ptr<Lookup>> pendingLookups_;std::uint64_t lookupId_=0;
 bool lookupAlive(const std::shared_ptr<Lookup>& value)const{return value->peek?value->peek->alive.load():bool(value->token&&value->token->active);}
 bool lookupGate(std::shared_ptr<Lookup> value,Clock::time_point end,biglybtc::core::MoveOnlyFunction<void()> effect,biglybtc::core::MoveOnlyFunction<void()> failed={}){if(value->peek){return downloadGate(value->peek,end,std::move(effect),std::move(failed));}return gate(end,std::move(effect),std::move(failed));}
 void lookupStart(Room&,std::size_t);
 void lookupBegin(std::shared_ptr<Lookup>);
 void lookupDiversified(std::shared_ptr<Lookup>,std::int8_t);
 void lookupPump(std::shared_ptr<Lookup>);
 void lookupReply(std::shared_ptr<Lookup>,std::uint64_t,udp::FindValueOutcome);
 void putMembership(std::shared_ptr<Lookup>);
 void discovery();
 struct PrivateOutgoing {Command command;std::shared_ptr<RoomToken> parent;std::string peer;std::size_t lane=0;msg::RoomPeer target;std::optional<msg::PrivateInitiator> exchange;std::optional<udp::Ticket> ticket;Clock::time_point end;bool finished=false;};
 struct PrivateIncoming {std::shared_ptr<RoomToken> parent;std::size_t lane=0;udp::Peer peer;nodes::Snapshot external;msg::PrivateUserKey user;std::string first;std::optional<msg::PrivateResponder> exchange;msg::PrivateReply reply;Clock::time_point end;};
 std::map<std::uint64_t,Command> privatePending_;
 std::map<std::uint64_t,std::shared_ptr<PrivateOutgoing>> privateOutgoing_;
 std::map<std::pair<std::uint64_t,msg::PrivateActivity>,PrivateIncoming> privateIncoming_;
 std::map<QString,std::pair<Clock::time_point,unsigned>> privateRate_;
 std::uint64_t privateSequence_=0;
 void privateOpen(Command);void privateCancel(Command);
 void privateCall(const std::shared_ptr<PrivateOutgoing>&,msg::PrivateRequest);
 void privateCompleted(const std::shared_ptr<PrivateOutgoing>&,udp::TransferOutcome);
 void privateFinished(const std::shared_ptr<PrivateOutgoing>&,std::optional<storage::Error> error={},std::string key={});
 void privateIncoming(std::shared_ptr<RoomToken>,std::size_t,std::shared_ptr<const udp::TransferRequest>,msg::PrivateRequest);
 void privateAnswer(std::shared_ptr<RoomToken>,std::size_t,std::shared_ptr<const udp::TransferRequest>,msg::PrivateReply);
 std::string privateNickname(const std::shared_ptr<RoomToken>&,std::string_view);
 std::optional<std::string> privateInstall(const std::shared_ptr<RoomToken>&,std::shared_ptr<msg::Room>,msg::RoomPeer,std::string_view,bool,std::string);
 void privateClose(Command);void privateDispose(const std::string&,const std::shared_ptr<RoomToken>&);
 void privateTick();void privateStop();
 void poll();
 void command(Command);
 void beginClose();
 void flushHistory();
 void publish();
public:
 Owner(std::shared_ptr<Shared> shared,QEventLoop& loop,bool owned,bool standardNetworks,std::function<std::expected<std::shared_ptr<wire::Provider>,udp::Failure>(std::optional<unsigned short>)> attach,std::chrono::milliseconds cachePeriod,std::chrono::milliseconds surveyPeriod,std::optional<engine::OwnedKeyBlockFixtureKey> fixtureKey,std::chrono::milliseconds bindingPeriod)
  :shared_(std::move(shared)),loop_(loop),owned_(owned),standardNetworks_(standardNetworks),attach_(std::move(attach)),ownedCachePeriod_(cachePeriod),ownedSurveyPeriod_(surveyPeriod),ownedKeyBlockFixtureKey_(owned?std::move(fixtureKey):std::nullopt){
  if(owned_&&bindingPeriod.count()){bindingInterval_=bindingPeriod;}
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
 if(provider_||resettingNetwork_||transportConfigPending_||closing_||shared_->stop.load()){return;}++attachAttempts_;nextAttach_=Clock::now()+std::chrono::seconds(std::min(5U,attachAttempts_));
 auto attached=attach_(attachPort_);
 if(attached){
  provider_=std::move(*attached);
  shared_->witness.store(provider_->witness());
  if(shared_->stop.load()){provider_->witness()->revoke();return;}
  provider_->receive([this](auto local,auto peer,auto bytes){ingress(local,std::move(peer),std::move(bytes));return true;});
  if(!provider_->bindings().empty()){qInfo("Public chat transport: ready");}
  startNetworks();
 }
 else{availability_=Clock::now()<attachEnd_?"Waiting for the Public UDP listener…":"Public chat transport could not be attached";changed();}
}
void Owner::startNetworks(){
 bool added=false;auto listeners=provider_->listeners();std::ranges::stable_sort(listeners,[](const auto& a,const auto& b){return publicAddress(a.local)>publicAddress(b.local);});
 for(const auto& listener:listeners){
  const auto family=endpoint(listener.local).length;const std::size_t i=family==4?0:1;
  if(!listener.local.port||listener.local.address.isNull()||(owned_?!listener.local.address.isLoopback():listener.local.address.isLoopback())){continue;}
  const auto add=[&](std::size_t index,std::int32_t network){if(lanes_[index]){return;}added=true;lanes_[index]=std::make_unique<Lane>();lanes_[index]->listener=listener;lanes_[index]->network=network;initializeLane(index);};
  add(i,owned_&&!standardNetworks_&&i==0?4:(i==0?0:3));
  if(family==4&&(!owned_||standardNetworks_)){add(2,4);}
 }if(added||(!lanes_[0]&&!lanes_[1])){availability_=(lanes_[0]||lanes_[1])?"Connecting to Public chat network…":"No suitable UDP listener for Public chats";changed();}
}
void Owner::initializeLane(std::size_t i){
 auto& lane=*lanes_[i];if(lane.initializing||closing_){return;}lane.initializing=true;
 struct Init {bool reachable=true,surveys=false;std::shared_ptr<services::azureus_key_block::State> keyBlocks;std::optional<storage::AzureusKeyBlockArea> keyBlockArea;std::vector<core::AzureusLocalDiversification> localDiversifications;std::vector<core::AzureusRemoteDiversification> diversifications;std::shared_ptr<storage::AzureusValueVersions> versions;std::shared_ptr<storage::AzureusContacts> contacts;std::shared_ptr<storage::AzureusPublicPolicy> policy;std::vector<wire::ProbePeer> seeds;std::optional<storage::Error> error;};auto init=std::make_shared<Init>();
 const auto lease=shared_->lease;const auto owners=versions_;const auto network=lane.network;const auto listener=lane.listener;const auto witness=provider_->witness();const bool owned=owned_;const auto fixtureKey=ownedKeyBlockFixtureKey_;
 const auto operation=[init,lease,owners,network,listener,witness,owned,fixtureKey](std::stop_token)->Broker::Result{
  auto found=owners->find(network);if(found==owners->end()){auto made=storage::AzureusValueVersions::open(*lease,network);if(!made){init->error=made.error();return std::unexpected(made.error());}found=owners->emplace(network,std::move(*made)).first;}init->versions=found->second;
  auto keyBlockArea=storage::readAzureusKeyBlockArea(*init->versions);if(!keyBlockArea){init->error=keyBlockArea.error();return std::unexpected(keyBlockArea.error());}auto blockDoc=core::Document::parse(keyBlockArea->primary().value_or(keyBlockArea->pending().value_or("de")));if(!blockDoc){init->error=failure(storage::ErrorCode::InvalidFormat,"Saved Azureus key blocks are unreadable; existing files retained");return std::unexpected(*init->error);}
  services::azureus_key_block::Verifier verifier=services::azureus_key_block::verifyOfficialStatus;if(owned&&fixtureKey){verifier=[key=*fixtureKey](auto request,auto signature){return services::azureus_key_block::verifySignatureStatus(request,signature,{key.modulus,key.exponent});};}auto blocks=services::azureus_key_block::decodeSaved(blockDoc->root(),QDateTime::currentMSecsSinceEpoch(),verifier);init->keyBlocks=std::make_shared<services::azureus_key_block::State>(std::move(verifier));if(!blocks||!init->keyBlocks->restore(blocks->records)){init->error=failure(storage::ErrorCode::InvalidFormat,"Saved Azureus key-block policy is unsupported; existing files retained");return std::unexpected(*init->error);}init->keyBlockArea=std::move(*keyBlockArea);

  const auto local=endpoint(listener.local);storage::AzureusPublicBind binding{network,local.length,local.address,local.port};
  auto policy=owned?storage::AzureusPublicPolicy::openOwnedShared(*lease,init->versions,binding,witness):storage::AzureusPublicPolicy::openBootstrapShared(*lease,init->versions,network,local,witness);
  if(!policy){init->error=policy.error();return std::unexpected(policy.error());}init->policy=std::move(*policy);
  auto saved=storage::AzureusContacts::open(*lease,network,local.length,owned?storage::AzureusContactScope::OwnedLoopback:storage::AzureusContactScope::PublicBound);
  if(saved){init->contacts=std::move(*saved);auto snapshot=init->contacts->snapshot();if(snapshot){for(const auto& value:(*snapshot)->contacts){if(value.length==local.length){init->seeds.push_back({{address(value.address,value.length),value.port},value.protocol});}}}}
  auto localDiversifications=storage::readAzureusLocalDiversifications(*init->versions,QDateTime::currentMSecsSinceEpoch());if(!localDiversifications){init->error=localDiversifications.error();return std::unexpected(localDiversifications.error());}init->localDiversifications=std::move(*localDiversifications);
  auto remote=storage::readAzureusRemoteDiversifications(*init->versions,QDateTime::currentMSecsSinceEpoch());if(remote){init->diversifications=std::move(*remote);}
  auto config=storage::ConfigFile::load(*lease,"biglybt.config");if(!config){return std::unexpected(config.error());}auto featureFlags=db::survey::savedFeatureFlags(config->document().root());init->surveys=featureFlags&&db::survey::surveyEnabled(55,network,true,*featureFlags);const auto* reachable=config->document().find("Plugin.DHT.dht.reachable."+std::to_string(network));if(reachable&&reachable->type==core::Node::Type::Integer){init->reachable=reachable->integer!=0;}
  return Broker::Data{};
 };
 if(!gate(Clock::now()+std::chrono::seconds(30),[this,i,init]{auto& value=*lanes_[i];value.initializing=false;value.localSeeds=std::move(init->localDiversifications);value.keyBlocks=std::move(init->keyBlocks);value.keyBlockArea=std::move(init->keyBlockArea);value.keyBlockSavedRevision=value.keyBlocks->revision();static_cast<void>(value.diversifications->restore(init->diversifications,QDateTime::currentMSecsSinceEpoch()));value.initialReachable=init->reachable;value.surveyAllowed=init->surveys;value.natSavedReachable=init->reachable;value.versions=init->versions;value.contacts=init->contacts;value.policy=init->policy;value.seeds=std::move(init->seeds);for(const auto& seed:parkedSeeds_[i]){if(std::ranges::none_of(value.seeds,[&](const auto& old){return old.endpoint==seed.endpoint;})){value.seeds.push_back(seed);}}parkedSeeds_[i].clear();active_->operation->data.lanes[i]=true;
  const auto local=value.listener.local;
  if(owned_){activate(i,local);}else{findSeeds(i);}changed();
 },[this,i,init]{lanes_[i]->initializing=false;lanes_[i]->error=init->error?init->error->message:"Could not open chat network policy";availability_=lanes_[i]->error;changed();},std::move(operation))){lane.initializing=false;}
}
void Owner::findSeeds(std::size_t i){
 auto& lane=*lanes_[i];if(owned_){if(!lane.ready){activate(i,lane.listener.local);}return;}if(lane.dns||closing_){return;}
 if(lane.dnsResolved&&lane.seeds.size()>=4){if(!lane.ready){probe(i);}return;}lane.dns=true;
 const auto dnsEpoch=networkEpoch_;lane.dnsId=QHostInfo::lookupHost(i==1?QStringLiteral("dht6.biglybt.com"):QStringLiteral("dht.biglybt.com"),this,[this,i,dnsEpoch](const QHostInfo& result){if(networkEpoch_!=dnsEpoch||!lanes_[i]){return;}auto& lane=*lanes_[i];lane.dns=false;lane.dnsId=-1;lane.dnsResolved=result.error()==QHostInfo::NoError;if(closing_){return;}
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
 auto& lane=*lanes_[i];if(lane.ready||lane.activating||resettingNetwork_||closing_||!lane.versions){return;}if(!lane.keyBlocks||!lane.keyBlockArea){lane.error="DHT signed identity state is unavailable";changed();return;}lane.activating=true;
 const auto lease=shared_->lease;const auto versions=lane.versions;const auto local=endpoint(lane.listener.local);const auto advertised=endpoint(external);const auto witness=provider_->witness();const auto network=lane.network;const bool owned=owned_;
 auto policy=std::make_shared<std::shared_ptr<storage::AzureusPublicPolicy>>();
 const bool actualContactChanged=bool(parkedDatabases_[i]);const auto blockArea=*lane.keyBlockArea;auto records=lane.keyBlocks->records();const auto fixtureKey=ownedKeyBlockFixtureKey_;auto identity=std::make_shared<std::optional<AzureusIdentityProfile>>();
 const auto queued=gate(Clock::now()+std::chrono::seconds(10),[this,i,external,policy,identity]{
  auto& lane=*lanes_[i];lane.activating=false;if(closing_||!identity->has_value()){return;}auto& prepared=**identity;lane.keyBlocks=std::move(prepared.blocks);lane.keyBlockArea=std::move(prepared.blockArea);lane.keyBlockSavedRevision=lane.keyBlocks->revision();if(prepared.warning){qWarning("BiglyBTC dht.address-cache network=%d state=retained",lane.network);}lane.policy=*policy;active_->operation->data.lanes[i]=true;
  udp::Config config;config.network=lane.network;config.servePings=true;config.initialReachable=lane.initialReachable;config.routingScope=owned_?udp::RoutingScope::OwnedLoopback:udp::RoutingScope::PublicBound;config.maximumPeers=router::MaximumRouterContacts;config.maximumPending=128;config.enableSurveys=lane.surveyAllowed;config.instance=std::bit_cast<std::int32_t>(QRandomGenerator::system()->generate());const auto localInstance=config.instance;
  auto actor=std::make_unique<udp::OwnedPingTransport>(std::move(config));auto channel=provider_->channel(lane.listener.local,external,[this,i]{return allowed(i);});
  if(!channel||!actor->startShared(std::move(*channel),owned_?std::function<bool()>{}:std::function<bool()>{[this,i]{return allowed(i);}})){lane.error="Chat listener could not be attached";changed();return;}
  lane.local=contact(external);lane.local.instance=localInstance;auto router=router::RouterOwner::create(lane.local.id,{},shared_->generation,monotonic(),QDateTime::currentMSecsSinceEpoch());db::Limits limits;limits.maximumKeys=8192;limits.maximumEntriesPerKey=256;limits.storageStats=db::StorageStatsMode::OriginalDiversification;limits.diversificationControl=std::make_shared<LocalDiversificationInputs>();std::shared_ptr<db::Owner> database=parkedDatabases_[i];
  struct RebindClock final:db::lifecycle::WallClock {std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}} clock;
  bool bound=false;if(database){bound=bool(database->rebindLocal(lane.local,clock));}else{auto made=db::Owner::create(limits);if(made){database=std::move(*made);bound=bool(database->restoreLocalDiversifications(lane.localSeeds,QDateTime::currentMSecsSinceEpoch()))&&bool(database->bindLocal(lane.local));}}
  if(!router||!database||!bound){lane.error="Chat routing could not be initialised";changed();return;}
  parkedDatabases_[i].reset();lane.router=std::move(*router);lane.database=std::move(database);lane.actor=std::move(actor);
  if(!lane.actor->serveDatabase(lane.router,lane.database,[this,i]{return allowed(i);},[this,i](auto done){auto callback=std::make_shared<biglybtc::core::MoveOnlyFunction<void(bool)>>(std::move(done));return gate(Clock::now()+std::chrono::seconds(5),[this,i,callback]{(*callback)(allowed(i));},[callback]{(*callback)(false);});})){lane.actor.reset();lane.error="Chat database binding failed";changed();return;}
  if(!lane.keyBlocks||!lane.keyBlockArea||!lane.actor->serveKeyBlocks(lane.keyBlocks,[this,i](auto revision,auto done){return keyBlockCheckpoint(i,revision,std::move(done));})){lane.actor.reset();lane.error="DHT signed key-block binding failed";changed();return;}
  static_cast<void>(lane.actor->setLocalReadPreflight([this,i](std::string key,auto done){return torrentReadPermit(i,std::move(key),std::move(done));}));
  auto initial=lane.database->localDiversificationCheckpoint();if(!initial||!lane.actor->setStorageCheckpoint([this,i](auto revision,auto done){return localCheckpoint(i,revision,std::move(done));})){lane.actor.reset();lane.database.reset();lane.error="DHT local checkpoint binding failed";changed();return;}
  lane.localSaved.clear();for(const auto& record:(*initial)->entries){lane.localSaved.insert_or_assign(record.key,record);}lane.localSavedRevision=(*initial)->revision;lane.localSeeds.clear();
  lane.cachePeriod=owned_&&ownedCachePeriod_.count()?ownedCachePeriod_.count():(lane.network==0?1800000:3600000);lane.cacheInterval=std::chrono::milliseconds(lane.cachePeriod+(owned_&&ownedCachePeriod_.count()?0:10000-int(QRandomGenerator::system()->bounded(20000U))));lane.cacheAfter=Clock::now()+lane.cacheInterval;
  lane.surveyInterval=owned_&&ownedSurveyPeriod_.count()?ownedSurveyPeriod_:std::chrono::milliseconds(900000);lane.surveyAfter=Clock::now()+lane.surveyInterval;
  const auto actorEpoch=networkEpoch_;auto* actorIdentity=lane.actor.get();
  static_cast<void>(lane.actor->setAddressReportSink([this,i,actorEpoch,actorIdentity](const auto& report){if(!resettingNetwork_&&networkEpoch_==actorEpoch&&lanes_[i]&&lanes_[i]->actor.get()==actorIdentity){addressReport(i,report);}}));
  lane.ready=true;lane.error.clear();reconnectingNetwork_=false;
  natStart(i);
  rcmRegisterSearch(i);for(const auto& seed:lane.seeds){if(seed.endpoint!=external){static_cast<void>(lane.actor->attachKnownPeer(seed.endpoint,seed.version,Clock::now()+std::chrono::seconds(10)));}}static_cast<void>(lane.actor->integrateKnownRouting(std::chrono::seconds(30)));
  later([this,i]{if(!owned_){findSeeds(i);}for(auto& [key,room]:rooms_){(void)key;rebind(room);}availability_="Connecting to Public DHT…";changed();});
 },[this,i]{lanes_[i]->activating=false;lanes_[i]->error="Public chat policy was rejected";changed();},[lease,versions,network,local,advertised,witness,owned,policy,identity,blockArea,records=std::move(records),fixtureKey,actualContactChanged](std::stop_token stop)->Broker::Result{
  storage::AzureusPublicBind binding{network,advertised.length,advertised.address,advertised.port};auto result=owned?storage::AzureusPublicPolicy::openOwnedShared(*lease,versions,binding,witness):storage::AzureusPublicPolicy::openShared(*lease,versions,binding,local,witness);if(!result){return std::unexpected(result.error());}*policy=std::move(*result);auto current=(*policy)->validate();if(!current){return std::unexpected(current.error());}services::azureus_key_block::Verifier verifier=services::azureus_key_block::verifyOfficialStatus;if(owned&&fixtureKey){verifier=[key=*fixtureKey](auto request,auto signature){return services::azureus_key_block::verifySignatureStatus(request,signature,{key.modulus,key.exponent});};}auto prepared=prepareAzureusIdentityProfile(*versions,blockArea,records,address(advertised.address,advertised.length),QDateTime::currentMSecsSinceEpoch(),std::move(verifier),stop,actualContactChanged);if(!prepared){return std::unexpected(prepared.error());}*identity=std::move(*prepared);return Broker::Data{};
 });
 if(!queued){lane.activating=false;lane.probeAfter=Clock::now()+std::chrono::seconds(2);}
}
void Owner::ingress(wire::Binding local,udp::Peer peer,std::string bytes){
 if(closing_||resettingNetwork_){return;}for(std::size_t i=0;i<3;++i){auto* lane=lanes_[i].get();if(!lane||!lane->ready||local!=lane->listener.local){continue;}services::azureus_dht::Context networkContext;networkContext.network=lane->network;networkContext.localVersion=55;networkContext.now=QDateTime::currentMSecsSinceEpoch();
  const auto kind=!bytes.empty()&&(static_cast<unsigned char>(bytes[0])&128)?services::azureus_dht::Kind::Request:services::azureus_dht::Kind::Reply;
  const auto header=services::azureus_dht::decodeHeader(kind,bytes,networkContext);if(!header){continue;}
  if(lane->nat&&header->fields().action==1035){auto packet=services::azureus_transfer::decode(bytes,networkContext);if(packet&&packet->data.handler==services::azureus_nat::handlerKey()){static_cast<void>(lane->nat->ingress(peer,std::move(bytes)));return;}}

  // A finite room route is separate from ordinary DHT peer admission. Renew
  // its fixed room epoch before the actor validates an alias DATA receipt.
  if(header->fields().action==1035&&header->fields().originator){
   auto packet=services::azureus_transfer::decode(bytes,networkContext);
   if(packet){nodes::Snapshot external{true,header->fields().originatorVersion,header->fields().originator};
    for(auto& [key,room]:rooms_){(void)key;if(!room.core||!room.token||!room.token->active||!room.session||room.core->handler()!=packet->data.handler){continue;}
     const auto mapped=std::ranges::find(room.lanes,i);if(mapped==room.lanes.end()){continue;}const auto index=std::size_t(mapped-room.lanes.begin());
     if(!room.session->acceptsRoutedPeer(index,peer,external)){continue;}const auto token=room.token;
     gate(Clock::now()+std::chrono::seconds(3),[this,i,index,peer,external,token,bytes=std::move(bytes)]()mutable{
      const auto found=rooms_.find(token->key);if(!allowed(i)||!token->active||!active_->operation->data.keys.contains(token->epoch)||found==rooms_.end()||found->second.token!=token||!found->second.session){return;}
      if(found->second.session->renewRoutedPeer(index,peer,external)){lanes_[i]->actor->receiveShared(peer,std::move(bytes));}
     });return;
    }
   }
  }
  if(!lane->actor->isRoutingPeer(peer)){
   if(bytes.empty()||!(static_cast<unsigned char>(bytes[0])&128)||peer==lane->actor->endpoint()||(owned_?!peer.address.isLoopback():!publicAddress(peer))){return;}
   services::azureus_dht::Context context;context.network=lane->network;context.localVersion=55;context.now=QDateTime::currentMSecsSinceEpoch();
   auto header=services::azureus_dht::decodeHeader(services::azureus_dht::Kind::Request,bytes,context);if(!header){return;}const auto& fields=header->fields();std::shared_ptr<RoomToken> roomToken;const auto actual=contact(peer,fields.originatorVersion);
   if(!fields.originator||*fields.originator!=actual.address){return;}
   if(fields.action==1035){auto decoded=services::azureus_transfer::decode(bytes,context);if(!decoded){return;}bool known=decoded->data.handler==rcm::searchHandler(i==2);for(const auto& [key,room]:rooms_){(void)key;if(room.core&&room.token&&room.token->active&&(room.core->handler()==decoded->data.handler||[&]{std::string peek(room.core->handler());peek[1]^=1;return peek==decoded->data.handler;}())){known=true;roomToken=room.token;break;}}if(!known){return;}}
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
 const auto i=value->lane;if(value->finished||closing_||!lookupAlive(value)||Clock::now()>=value->end||!lanes_[i]||!lanes_[i]->ready||lookups_.size()>=8||value->group->records.size()>=32){return;}
 auto& lane=*lanes_[i];if(auto known=lane.diversifications->find(value->target,QDateTime::currentMSecsSinceEpoch())){lookupGate(value,value->end,[this,value,type=known->type]{if(lookupAlive(value)&&allowed(value->lane)){lookupDiversified(value,type);}value->finished=true;},[value]{value->finished=true;});return;}const auto routing=lane.actor->routingSnapshot();std::vector<control::AdmittedContact> seeds;
 if(routing){for(std::size_t j=0;j<routing->contactCount;++j){const auto& c=routing->contacts[j].contact;if(c.id!=lane.local.id){seeds.push_back({c,true});}}}
 std::ranges::sort(seeds,[&](const auto& a,const auto& b){for(std::size_t j=0;j<20;++j){const auto aa=a.contact.id[j]^value->target[j],bb=b.contact.id[j]^value->target[j];if(aa!=bb){return aa<bb;}}return false;});if(seeds.size()>20){seeds.resize(20);}
 if(seeds.empty()){return;}control::LookupOptions options;options.target=value->target;options.localId=lane.local.id;options.timeoutMillis=std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(value->end-Clock::now()).count());options.maximumValues=32-value->group->records.size();if(value->peek){options.flags=1;}if(!owned_){options.maximumContacts=control::LookupController::MaximumContacts;}options.addressScope=owned_?control::LookupAddressScope::OwnedLoopback:control::LookupAddressScope::PublicValue;
 if(!value->control.start(shared_->generation,++lookupId_,options,seeds,monotonic())){return;}lookups_.push_back(value);lookupPump(value);
}
void Owner::lookupDiversified(std::shared_ptr<Lookup> value,std::int8_t type){
 value->diversified=true;value->group->diversified=true;if((type!=2&&type!=3)||value->depth>=4||value->group->records.size()>=32||value->group->keys.size()>=32){return;}
 auto saved=remoteRecord(value->lane,value->target,type);if(!saved){return;}remoteRemember(value->lane,*saved,[](bool){});type=saved->type;
 const unsigned first=QRandomGenerator::system()->bounded(10U),second=(first+1+QRandomGenerator::system()->bounded(9U))%10;const std::array offsets{first,second};
 auto keys=msg::diversifiedDiscoveryKeys(value->target,type,std::span(offsets).first(type==2?1:2));if(!keys){return;}
 for(const auto& key:*keys){if(value->group->keys.size()>=32||pendingLookups_.size()>=64){break;}if(!value->group->keys.insert(key).second){continue;}auto child=std::make_shared<Lookup>();child->token=value->token;child->peek=value->peek;child->lane=value->lane;child->end=value->end;child->target=key;child->depth=value->depth+1;child->root=false;child->group=value->group;pendingLookups_.push_back(std::move(child));}
}
template<class T>void applied(const std::shared_ptr<T>& value,std::expected<void,control::LookupError> result){if(!result){value->finished=true;}}
void Owner::lookupPump(std::shared_ptr<Lookup> value){
 if(value->finished||closing_){return;}const auto i=value->lane;
 lookupGate(value,std::min(value->end,Clock::now()+std::chrono::seconds(5)),[this,value,i]{
  if(!lookupAlive(value)||!allowed(i)||(!value->peek&&!active_->operation->data.keys.contains(value->token->epoch))){value->finished=true;return;}
  auto& actor=*lanes_[i]->actor;auto intents=value->control.takeIntents(monotonic());if(!intents){value->finished=true;return;}
  for(const auto& intent:*intents){const auto peer=peerOf(intent.contact);if(!actor.attachKnownPeer(peer,intent.contact.protocol,value->end)){applied(value,value->control.finish(intent.node,control::LookupNodeState::Rejected,monotonic()));continue;}
   auto sent=actor.findValue(peer,intent.contact.protocol,intent.target,intent.flags,intent.maximumValues,this,[this,value,node=intent.node](auto out){lookupReply(value,node,std::move(out));},std::min(std::chrono::seconds(15),std::chrono::duration_cast<std::chrono::seconds>(value->end-Clock::now())));
   if(sent){value->tickets[intent.node]=*sent;applied(value,value->control.sent(intent.node,sent->value,monotonic()));}else{applied(value,value->control.finish(intent.node,control::LookupNodeState::Failed,monotonic()));}
  }
  if(auto cancellations=value->control.cancellationIntents()){for(const auto& request:*cancellations){const auto found=value->tickets.find(request.node);if(found!=value->tickets.end()){actor.cancel(found->second);}}}
  if(control::lookupTerminal(value->control.state())){value->finished=true;if(value->root&&!value->group->diversified&&!value->peek){putMembership(value);}changed();}
 },[value]{value->finished=true;});
}
void Owner::lookupReply(std::shared_ptr<Lookup> value,std::uint64_t node,udp::FindValueOutcome outcome){
 if(value->finished||closing_){return;}auto box=std::make_shared<udp::FindValueOutcome>(std::move(outcome));
 lookupGate(value,std::min(value->end,Clock::now()+std::chrono::seconds(5)),[this,value,node,box]{
  if(!lookupAlive(value)||!allowed(value->lane)){value->finished=true;return;}
  auto& actor=*lanes_[value->lane]->actor;
  if(!*box){applied(value,value->control.finish(node,control::LookupNodeState::Timeout,monotonic()));value->tickets.erase(node);}
  else{const auto* reply=std::get_if<udp::lookup::ReplyBody>(&(*box)->body());if(!reply){applied(value,value->control.finish(node,control::LookupNodeState::Failed,monotonic()));}
   else if(const auto* values=std::get_if<udp::lookup::FoundValues>(&reply->result)){
    if(values->diversification!=1&&!value->diversified){lookupDiversified(value,values->diversification);}applied(value,value->control.values(node,values->values,reply->continuation,values->diversification,monotonic()));
    if(value->peek){for(const auto& record:values->values){if(value->group->records.size()>=32){break;}if(!value->group->records.insert({record.originator.id,record.bytes}).second){continue;}const auto work=value->peek;const auto lane=std::ranges::find(work->lanes,value->lane);if(!work->session||lane==work->lanes.end()){continue;}const auto peer=peerOf(record.originator);if(actor.attachKnownPeer(peer,record.originator.protocol,work->end)){static_cast<void>(work->session->addPeer(static_cast<std::size_t>(lane-work->lanes.begin()),peer,record.originator.protocol));}}}
    else{auto records=values->values;later([this,value,records=std::move(records)]{if(!lookupAlive(value)){return;}for(const auto& record:records){if(value->group->records.size()>=32){break;}if(!value->group->records.insert({record.originator.id,record.bytes}).second){continue;}auto data=core::Document::parse(record.bytes,{512,512,32,4});if(!data||data->root().type!=core::Node::Type::Dictionary){continue;}const auto* uid=data->find("u");if(!uid||uid->type!=core::Node::Type::Bytes||uid->bytes.size()!=8){continue;}auto encoded=msg::RoomSession::contactMap(peerOf(record.originator),record.originator.protocol);if(!encoded){continue;}msg::NodeHint hint;std::copy_n(reinterpret_cast<const std::uint8_t*>(uid->bytes.data()),8,hint.uid.begin());hint.encodedContact=std::move(*encoded);acceptHint(value->token,value->lane,hint);}});}
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
 const auto policy=lanes_[i]->policy;const auto versions=lanes_[i]->versions;auto reserved=std::make_shared<std::optional<std::int32_t>>();const auto token=value->token;const auto downloadSources=shared_->downloadSources;
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
 },{},[policy,versions,reserved,token,downloadSources](std::stop_token stop)->Broker::Result{if(stop.stop_requested()||!token->active){return std::unexpected(failure(storage::ErrorCode::Cancelled,"Room left before publication"));}auto valid=policy->validate();if(!valid){return std::unexpected(valid.error());}valid=policy->validateChatIdentity(token->configKey,token->stamp);if(!valid){return std::unexpected(valid.error());}if(token->download){auto source=downloadSources->read(token->download->hash);if(!source||source->sourceRevision!=token->download->sourceRevision){return std::unexpected(failure(storage::ErrorCode::Conflict,"Download chat membership changed"));}}auto version=versions->next(QDateTime::currentMSecsSinceEpoch());if(!version){return std::unexpected(version.error());}*reserved=*version;return Broker::Data{};});
}
#include "azureus_identity_network.inc"
#include "azureus_torrent_network.inc"
#include "swarm_discovery_network.inc"
#include "swarm_search_network.inc"
#include "rating_network.inc"
#include "rating_schedule_network.inc"
#include "rating_chat_network.inc"
#include "download_chat_network.inc"
#include "private_conversations.inc"
#include "nat_network.inc"
#include "remote_diversification_network.inc"
#include "local_diversification_network.inc"
#include "key_block_network.inc"
#include "cached_value_network.inc"
#include "survey_value_network.inc"
void Owner::discovery(){
 if(closing_){return;}std::erase_if(lookups_,[](const auto& v){return v->finished;});
 while(lookups_.size()<8&&!pendingLookups_.empty()){auto next=std::move(pendingLookups_.front());pendingLookups_.pop_front();lookupBegin(std::move(next));}
 for(auto& [key,room]:rooms_){(void)key;if(!room.core||room.core->isPrivate()||!room.token||!room.token->active){continue;}for(std::size_t i=0;i<3;++i){if(Clock::now()>=room.nextLookup[i]){lookupStart(room,i);}}}
 for(const auto& lookup:lookups_){if(!lookup->finished){lookupPump(lookup);}}
 for(std::size_t i=0;i<3;++i){if(!lanes_[i]){continue;}auto& lane=*lanes_[i];if(lane.ready){gate(Clock::now()+std::chrono::seconds(5),[this,i]{if(allowed(i)){natStart(i);natTick(i);static_cast<void>(lanes_[i]->actor->maintainRouting(QDateTime::currentMSecsSinceEpoch()));captureTorrentContacts(i);const auto routing=lanes_[i]->actor->routingSnapshot();if(ticks_%4==0&&routing&&routing->finished&&!routing->seeded){static_cast<void>(lanes_[i]->actor->integrateKnownRouting(std::chrono::seconds(30)));}}});}else if(ticks_%12==0&&!lane.initializing&&!lane.probing&&!lane.dns){if(lane.policy){findSeeds(i);}else{initializeLane(i);}}}
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
 if(command.kind==Kind::Join){const auto prior=rooms_.find(command.key);if(prior!=rooms_.end()&&prior->second.settings.privateChat){result(command,failure(storage::ErrorCode::Unsupported,"Private conversations cannot be opened as public rooms"));return;}loadRoom(command);return;}
 if(command.kind==Kind::PrivateChat){privateOpen(std::move(command));return;}
 if(command.kind==Kind::CancelPrivate){privateCancel(std::move(command));return;}
 if(command.kind==Kind::WindowSettings){if(!command.windows){result(command,failure(storage::ErrorCode::InvalidFormat,"Chat window settings are missing"));return;}meta(ChatProfileWindowSettings{command.windows->first,command.windows->second},{command,{},false});return;}
 if(command.kind==Kind::PrivateChatPolicy){if(!command.privatePolicy){result(command,failure(storage::ErrorCode::InvalidFormat,"Private chat policy is missing"));return;}meta(ChatProfilePrivatePolicy{command.privatePolicy->expected,command.privatePolicy->value},{command,{},false});return;}
 if(command.kind==Kind::NotificationSettings){
  if(!command.notifications){result(command,failure(storage::ErrorCode::InvalidFormat,"Notification settings are missing"));return;}
  meta(ChatProfileNotifications{command.notifications->expected,command.notifications->value},{command,{},false});return;
 }
 const auto found=rooms_.find(command.key);if(found==rooms_.end()){result(command,failure(storage::ErrorCode::Missing,"Room is not open"));return;}auto& room=found->second;
 if(command.privateOnly&&(!room.settings.privateChat||!room.token||!room.token->active||room.token->epoch!=command.ownerEpoch)){result(command,failure(storage::ErrorCode::Conflict,"Private conversation source changed"));return;}
 if(command.scopedView&&(!room.token||!room.token->active||room.token->epoch!=command.ownerEpoch||!room.viewReferences.views().contains(command.view))){result(command,failure(storage::ErrorCode::Conflict,"Chat view source changed before its action"));return;}
 if(room.privateClosing&&command.kind!=Kind::Leave){result(command,failure(storage::ErrorCode::Cancelled,"Private conversation is closing"));return;}
 if(room.settings.privateChat&&(command.kind==Kind::Options||(command.kind==Kind::NotificationSettings))){result(command,failure(storage::ErrorCode::Unsupported,"These saved options apply to public rooms"));return;}
 if(command.kind==Kind::AcquireView||command.kind==Kind::ReleaseView||command.kind==Kind::KeepAlive){if(!room.core||!room.token||!room.token->active||room.token->epoch!=command.ownerEpoch){result(command,failure(storage::ErrorCode::Conflict,"Chat window source changed"));return;}}
 switch(command.kind){
 case Kind::AcquireView:if(!room.viewReferences.acquire(command.view)){result(command,failure(storage::ErrorCode::Busy,"Chat view limit reached"));break;}result(command);break;
 case Kind::ReleaseView:{const auto released=room.viewReferences.release(command.view);if(room.settings.privateChat&&released==msg::ViewReferences::Released::Retire){privateClose(command);}else{result(command);}break;}
 case Kind::KeepAlive:if(room.viewReferences.keepAlive()!=command.first){result(command,failure(storage::ErrorCode::Conflict,"Chat keep-alive reference changed"));break;}room.viewReferences.keepAlive(command.second);if(room.settings.privateChat&&room.viewReferences.shouldRetire()){privateClose(command);}else{result(command);}break;
 case Kind::Select:selected_=command.key;{std::lock_guard lock(shared_->mutex);if(shared_->readingRoom==command.key&&(!shared_->readingEpoch||(room.token&&shared_->readingEpoch==room.token->epoch))){readRoom(room);}}result(command);break;
 case Kind::Leave:
  if(room.settings.privateChat){privateClose(command);break;}
  for(const auto& [id,meta]:metadata_){if(meta.command.kind==Kind::Join&&meta.command.key==command.key){shared_->io->cancel(id);}}
  if(room.token){room.token->active=false;}if(room.session){room.session->close();room.session.reset();}room.opening=false;room.status="Room left";
  // Keep the ledger until a final saved-history write completes. Rejoining
  // creates a new epoch, so old requests cannot become its source authority.
  if(room.settings.privateChat){if(selected_==command.key){selected_.clear();}rooms_.erase(command.key);}
  result(command);break;
 case Kind::Send:{
  if(!room.core||!room.token||!room.token->active){result(command,failure(storage::ErrorCode::Conflict,"Join a room before using its composer"));break;}
  if(!command.batch){
   room.localStatus.clear();const auto decoded=msg::composer::prepareChatText(QString::fromUtf8(command.text.data(),qsizetype(command.text.size())),false);if(decoded.empty()){result(command,failure(storage::ErrorCode::Unsupported,"Empty chat message"));break;}const auto parsed=parseChatCommand(decoded.front());
   if(parsed.kind==ChatCommandKind::Unsupported||parsed.kind==ChatCommandKind::Missing){result(command,failure(storage::ErrorCode::Unsupported,parsed.kind==ChatCommandKind::Missing?"Chat command needs parameters; no message was sent":"Chat command is unavailable; no message was sent"));break;}
   if(parsed.kind==ChatCommandKind::Private){
    if(room.core->isPrivate()){result(command,failure(storage::ErrorCode::Unsupported,"Start private conversations from their public parent room"));break;}
    std::map<std::string,QString> names;auto messages=room.core->messages(QDateTime::currentMSecsSinceEpoch());if(messages){for(const auto& message:*messages){auto name=QString::fromUtf8(message.nickname.data(),qsizetype(message.nickname.size()));if(name.isEmpty()){auto shortName=msg::participantShortName(message.publicKey);if(shortName){name=QString::fromLatin1(shortName->data(),qsizetype(shortName->size()));}}name.replace(u' ',QChar(0xa0));names[message.publicKey]=std::move(name);}}
    std::vector<std::string> matches;for(const auto& [key,name]:names){if(name==parsed.value){matches.push_back(key);}}if(matches.size()!=1||matches.front()==room.core->publicKey()){result(command,failure(storage::ErrorCode::Conflict,"Private nickname is absent, ambiguous or belongs to this client"));break;}
    auto routed=command;routed.kind=Kind::PrivateChat;routed.reply=Submission{command.key,command.text};routed.privateSelection=PrivateSelection{room.token->epoch,matches.front(),parsed.privateText.toUtf8().toStdString()};this->command(std::move(routed));break;
   }
   if(parsed.kind==ChatCommandKind::Help){room.localStatus="Commands: /join room, /pjoin room, /nick name, /me action, /ignore name, /ignore -r name, /msg nick [message], /query nick [message], /clone";result(command);break;}
   if(parsed.kind==ChatCommandKind::Clone){result(command,{},command.key);break;}
   if(parsed.kind==ChatCommandKind::Join){if(parsed.value.isEmpty()||parsed.value.toUtf8().size()>4096){result(command,failure(storage::ErrorCode::Unsupported,"Invalid public room name"));break;}auto routed=command;routed.reply=Submission{command.key,command.text};routed.kind=Kind::Join;routed.key=parsed.value.toUtf8().toStdString();loadRoom(routed);break;}
   if(parsed.kind==ChatCommandKind::Nickname){auto routed=command;routed.reply=Submission{command.key,command.text};routed.kind=Kind::Nickname;routed.text=parsed.value.toUtf8().toStdString();this->command(std::move(routed));break;}
   if(parsed.kind==ChatCommandKind::Ignore){
    std::map<std::string,QString> names;if(room.core){auto messages=room.core->messages(QDateTime::currentMSecsSinceEpoch());if(messages){for(const auto& message:*messages){auto name=QString::fromUtf8(message.nickname.data(),qsizetype(message.nickname.size()));if(name.isEmpty()){auto shortName=msg::participantShortName(message.publicKey);if(shortName){name=QString::fromLatin1(shortName->data(),qsizetype(shortName->size()));}}names[message.publicKey]=std::move(name);}}}std::vector<std::string> matches;for(const auto& [key,name]:names){if(name==parsed.value){matches.push_back(key);}}if(matches.size()!=1||!preferences_.participants.contains(matches.front())){result(command,failure(storage::ErrorCode::Conflict,"Participant name is absent, ambiguous or still loading"));break;}auto routed=command;routed.reply=Submission{command.key,command.text};routed.kind=Kind::IgnoreParticipant;routed.text=matches.front();routed.first=preferences_.participants.at(routed.text).ignored;routed.second=!parsed.remove;this->command(std::move(routed));break;
   }
   auto batch=std::make_shared<SendBatch>();batch->raw=command.text;if(parsed.kind==ChatCommandKind::Action){batch->type=1;}for(const auto& part:msg::composer::chatDeliveryParts(parsed.value)){batch->parts.push_back(part.toUtf8().toStdString());}if(batch->parts.empty()||batch->parts.size()>128){result(command,failure(storage::ErrorCode::Unsupported,"Chat message exceeds its part bound"));break;}command.batch=std::move(batch);
  }
  const auto batch=command.batch;if(batch->finished){break;}if(!room.session||!room.token||!room.token->active||room.core->readOnly()||(batch->epoch&&batch->epoch!=room.token->epoch)){result(command,failure(storage::ErrorCode::Unsupported,"Room is not writable or its owner changed"));break;}const auto token=room.token;batch->epoch=token->epoch;
  if(!batch->reserved){const auto now=Clock::now();const auto delay=std::max<std::int32_t>(0,room.sendPace.reserve(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()));batch->due=std::max(now,room.nextSend)+std::chrono::milliseconds(delay);room.nextSend=batch->due;batch->reserved=true;}
  if(Clock::now()<batch->due){const auto delay=std::chrono::duration_cast<std::chrono::milliseconds>(batch->due-Clock::now()).count()+1;QTimer::singleShot(int(std::max<std::int64_t>(1,delay)),this,[this,command]{this->command(command);});break;}
  if(!later([this,command,token,batch]{
   const auto found=rooms_.find(command.key);if(batch->finished){return;}if(found==rooms_.end()||found->second.token!=token||!token->active||!found->second.session||found->second.privateClosing){result(command,failure(storage::ErrorCode::Conflict,"Chat changed before signing"));return;}auto& room=found->second;
   const auto prepare=[&](const std::string& text){return room.session->prepareSend(text,room.settings.nickname,QDateTime::currentMSecsSinceEpoch(),{},batch->type);};auto prepared=prepare(batch->parts[batch->index]);
   if(!prepared&&prepared.error()==msg::Error::Limit&&batch->parts.size()<128){
    const auto& bytes=batch->parts[batch->index];const auto text=QString::fromUtf8(bytes.data(),qsizetype(bytes.size()));qsizetype low=1,high=text.size(),best=0;std::optional<msg::PreparedSend> fit;
    while(low<=high){const auto middle=(low+high)/2;auto cut=middle;if(cut<text.size()&&cut>0&&text[cut-1].isHighSurrogate()&&text[cut].isLowSurrogate()){--cut;}if(cut<=0){low=middle+1;continue;}auto candidate=prepare(text.left(cut).toUtf8().toStdString());if(candidate){best=cut;fit=std::move(*candidate);low=middle+1;}else{high=middle-1;}}
    if(fit&&best>0&&best<text.size()){batch->parts[batch->index]=text.left(best).toUtf8().toStdString();batch->parts.insert(batch->parts.begin()+std::ptrdiff_t(batch->index+1),text.mid(best).toUtf8().toStdString());prepared=std::move(*fit);}
   }
   if(!prepared){result(command,failure(storage::ErrorCode::Unsupported,"Message was refused or exceeds the room payload limit"));return;}auto pending=std::make_shared<std::optional<msg::PreparedSend>>(std::move(*prepared));auto failed=[this,command]{result(command,failure(storage::ErrorCode::Conflict,"Chat source changed before sending"));};
   if(!gate(Clock::now()+std::chrono::seconds(10),[this,command,token,pending,failed,batch]{
    const auto room=rooms_.find(command.key);if(batch->finished){return;}if(room==rooms_.end()||room->second.token!=token||!token->active||!room->second.session||room->second.privateClosing||!active_->operation->data.keys.contains(token->epoch)||!*pending){failed();return;}auto sent=room->second.session->commitSend(std::move(**pending),QDateTime::currentMSecsSinceEpoch());pending->reset();if(sent){++batch->index;batch->reserved=false;if(batch->index<batch->parts.size()){this->command(command);}else{result(command);}}else{result(command,failure(storage::ErrorCode::Conflict,"Chat changed during signing; retry the message"));}
   },failed)){failed();}
  })){result(command,failure(storage::ErrorCode::Busy,"Chat signing queue is full"));}break;
 }
 case Kind::Posting:{if(!command.indications||!room.token||!room.token->active||room.token->epoch!=command.indications->epoch||room.settings.privateChat){result(command,failure(storage::ErrorCode::Conflict,"Public chat notification source changed"));break;}const auto token=room.token;meta(ChatProfilePosting{command.key,command.indications->expected,command.indications->value,token->stamp,[token]{return token->active.load();}},{command,token,false});break;}
 case Kind::Options:{auto settings=room.settings;
  if(command.indications){const auto change=*command.indications;
   if(!room.token||!room.token->active||room.token->epoch!=change.epoch){result(command,failure(storage::ErrorCode::Conflict,"Chat room changed before indication update"));break;}
   settings.disableIndicators=change.value;const auto token=room.token;
   meta(ChatProfileOptions{std::move(settings),change.expected,token->stamp,[token]{return token->active.load();}},{command,token,false});
  }else{settings.favourite=command.first;settings.saveHistory=command.second;meta(ChatProfileOptions{std::move(settings)},{command,room.token,false});}
  break;}
 case Kind::NicknameMode:{auto request=ChatProfileNicknameMode{command.key,command.first};if(room.token){const auto token=room.token;request.expectedIdentity=token->stamp;request.live=[token]{return token->active.load();};if(room.settings.privateChat){request.sourceKey=room.privateParentKey;}}meta(std::move(request),{command,room.token,false});break;}
 case Kind::Nickname:{auto request=ChatProfileNickname{command.key,command.text,command.reply?std::optional<bool>{}:std::optional<bool>{command.first},bool(command.reply)};if(room.token){const auto token=room.token;request.expectedIdentity=token->stamp;request.live=[token]{return token->active.load();};if(room.settings.privateChat){request.sourceKey=room.privateParentKey;}}meta(std::move(request),{command,room.token,false});break;}
 case Kind::RelatedChannel:{
  if(!room.core||!room.token||!room.token->active||room.token->epoch!=command.ownerEpoch||room.settings.privateChat){result(command,failure(storage::ErrorCode::Conflict,"Related channel parent changed"));break;}
  auto related=ChatProfile::relatedKey(command.key,room.core->publicKey(),command.first);if(!related){result(command,related.error());break;}const auto token=room.token;ChatProfileLoad request{*related,true,{},std::pair{command.key,token->stamp},[token]{return token->active.load();}};command.kind=Kind::Join;command.key=*related;command.openedRoom=*related;loadRoom(command,std::move(request));break;
 }
 case Kind::SpamParticipant:{
  const auto now=QDateTime::currentMSecsSinceEpoch();
  if(!room.core||!room.token||!room.token->active||!command.indications||room.token->epoch!=command.indications->epoch||room.core->spammer(command.text)!=command.first){result(command,failure(storage::ErrorCode::Conflict,"Chat participant changed before moderation"));break;}
  auto messages=room.core->messages(now);auto canMark=room.core->canMarkSpammer(command.text,now);
  if(!messages||std::ranges::none_of(*messages,[&](const auto& m){return !m.local&&m.publicKey==command.text;})||(command.second&&(!canMark||!*canMark))){result(command,failure(storage::ErrorCode::Conflict,"Spam marking requires five signed participant messages"));break;}
  const auto token=room.token;meta(ChatProfilePreference{room.settings.privateChat?room.privateParentKey:command.key,command.text,ChatPreferenceField::Spammer,command.first,command.second,token->stamp,[token]{return token->active.load();}},{command,token,false});break;
 }
 case Kind::HideRatings:case Kind::HideSubscriptions:case Kind::IgnoreParticipant:case Kind::PinParticipant:{
  if(!room.core||!room.token||!room.token->active){result(command,failure(storage::ErrorCode::Conflict,"Room is no longer joined"));break;}
  const bool participant=command.kind==Kind::IgnoreParticipant||command.kind==Kind::PinParticipant;
  if(participant){auto messages=room.core->messages(QDateTime::currentMSecsSinceEpoch());if(!messages||std::ranges::none_of(*messages,[&](const auto& message){return message.publicKey==command.text&&!message.local;})){result(command,failure(storage::ErrorCode::Conflict,"Participant is no longer in the signed room roster"));break;}}
  auto field=command.kind==Kind::HideRatings?ChatPreferenceField::Ratings:command.kind==Kind::HideSubscriptions?ChatPreferenceField::Subscriptions:command.kind==Kind::IgnoreParticipant?ChatPreferenceField::Ignored:ChatPreferenceField::Pinned;const auto token=room.token;
  meta(ChatProfilePreference{room.settings.privateChat?room.privateParentKey:command.key,command.text,field,command.first,command.second,token->stamp,[token]{return token->active.load();}},{command,token,false});break;
 }
 case Kind::ActivityPosts:case Kind::ReadMarker:case Kind::Preferences:case Kind::Join:case Kind::NotificationSettings:case Kind::PrivateChat:case Kind::PrivateChatPolicy:case Kind::WindowSettings:case Kind::CancelPrivate:break;
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
 if(closing_){return;}privateStop();for(const auto& [hash,work]:peeks_){(void)hash;peekFinish(work,"Chats closed");}for(auto& [key,notice]:ratingNotices_){(void)key;notice->alive=false;ratingNoticeDone(notice,"Rating chats closed");}for(auto& [key,work]:ratings_){(void)key;ratingCancel(work,"Ratings closed");}for(auto& [key,torrent]:torrents_){(void)key;if(torrent->target.live){torrent->target.live->store(false);}cancelTorrentLookups(torrent);}torrentFacts_.closed=true;torrentFacts_.state="Closed";if(ratingPolicy_){ratingPolicy_->close();}ratingReads_.reset();ratingTimers_.clear();closing_=true;closeEnd_=Clock::now()+std::chrono::seconds(5);timer_.stop();
 rcmStopTagScan("Tag scan closed");
 for(const auto& job:rcmLookups_){rcmCancel(job);}for(const auto& job:rcmPending_){rcmCancel(job);}
 for(auto& [key,room]:rooms_){(void)key;if(room.session){room.session->close();room.session.reset();}}
 for(auto& lane:lanes_){if(!lane){continue;}if(lane->dnsId>=0){QHostInfo::abortHostLookup(lane->dnsId);}if(lane->natDirectory){lane->natDirectory->close();}if(lane->nat){lane->nat->close();}if(lane->actor){lane->actor->close();}lane->ready=false;for(auto& wait:lane->localWaiters){wait.done(false);}lane->localWaiters.clear();for(auto& wait:lane->keyBlockWaiters){wait.done(false);}lane->keyBlockWaiters.clear();}
 rcmStopSearch("Search closed");
 if(provider_){provider_->close();}saveTorrentContacts(true);rcmSave(true);
 for(auto& frame:frames_){if(frame.job){Broker::revoke(frame.job);}}
 availability_="Saving room history…";changed();
}
void Owner::poll(){
 if(polling_){return;}polling_=true;const auto finish=qScopeGuard([&]{polling_=false;});
 if(shared_->stop.load()){beginClose();}
 if(!closing_&&shared_->prepareUntil.load()){natPrepare();}
 drainFrames();metadata();
 // Expensive signature checks, history encoding and transcript generation run
 // after the originating source operation has physically released its gate.
 for(auto at=crypto_.begin();at!=crypto_.end();){if(!at->barrier||Broker::released(at->barrier)){auto effect=std::move(at->effect);at=crypto_.erase(at);if(!closing_){effect();}}else{++at;}}
 std::deque<Command> commands;std::string reading;std::uint64_t readingEpoch=0;{std::lock_guard lock(shared_->mutex);commands.swap(shared_->commands);reading=shared_->readingRoom;readingEpoch=shared_->readingEpoch;shared_->readingWakePending=false;}
 if(reading!=reading_||readingEpoch!=readingEpoch_){reading_=std::move(reading);readingEpoch_=readingEpoch;const auto room=rooms_.find(reading_);if(room!=rooms_.end()){readRoom(room->second);}changed();}
 privateTick();refreshPreferences();flushReadMarkers();flushActivities();drainTorrentContactSaves();rcmDrainSave();
 for(auto& item:commands){command(std::move(item));}
 if(!closing_&&!resettingNetwork_){localCheckpointTick();keyBlockTick();cacheTick();surveyTick();remoteTick();syncTorrents();torrentTick();rcmTick();ratingTick();ratingNoticeTick();peekTick();if(Clock::now()-lastContactSave_>=std::chrono::seconds(30)){lastContactSave_=Clock::now();saveTorrentContacts();}}
 if(closing_){
  saveTorrentContacts(true);
  flushHistory();bool dirty=false;for(const auto& [key,room]:rooms_){(void)key;dirty|=(room.settings.saveHistory&&room.historyDirty)||bool(room.pendingRead);}
  if((metadata_.empty()&&!dirty&&frames_.empty()&&!rcmCloseSave_&&!rcmSavePending_&&std::ranges::none_of(contactSaves_,[](const auto& handle){return bool(handle);})&&std::ranges::none_of(contactDirty_,[](bool value){return value;}))||Clock::now()>=closeEnd_){
   for(auto& frame:frames_){if(frame.job){Broker::revoke(frame.job);}}shared_->io->requestStop();shared_->networkIo->unbind();shared_->networkIo->requestStop();pulse_.stop();const bool readError=std::ranges::any_of(rooms_,[](const auto& entry){return entry.second.readError;});availability_=dirty?"Chats closed; some history or read markers could not be saved":readError?"Chats closed; some read markers could not be saved":"Chats closed";changed();publish();loop_.quit();return;
  }
 }else{
  if(resettingNetwork_){finishNetworkReset();}
  if(reconnectingNetwork_&&!provider_&&!transportConfigPending_&&Clock::now()>=nextTransportConfig_){readTransportConfig();}
  if(!resettingNetwork_&&!provider_&&!attachExhausted_){if(!reconnectingNetwork_&&Clock::now()>=attachEnd_){attachExhausted_=true;availability_="Public chat transport could not be attached";changed();}else if(Clock::now()>=nextAttach_){attachTransport();}}
  if(provider_&&!resettingNetwork_){for(std::size_t i=0;i<3;++i){if(lanes_[i]){auto& lane=*lanes_[i];if(!lane.ready&&!lane.initializing&&!lane.probing&&!lane.dns&&lane.policy&&lane.policy->current()&&!lane.seeds.empty()&&Clock::now()>=lane.probeAfter){probe(i);}}}}
  if(Clock::now()-lastHistory_>=std::chrono::seconds(30)){lastHistory_=Clock::now();flushHistory();}
  if(provider_&&!resettingNetwork_&&Clock::now()-lastBindings_>=bindingInterval_){lastBindings_=Clock::now();if(!provider_->refresh()){reconnectNetwork("Public UDP listener changed; reconnecting…");}else{const auto currentBindings=provider_->bindings();for(std::size_t i=0;i<3;++i){if(lanes_[i]&&(std::ranges::find(currentBindings,lanes_[i]->listener.local)==currentBindings.end()||(lanes_[i]->policy&&!lanes_[i]->policy->current()))){reconnectNetwork("Public network settings changed; reconnecting…");break;}}if(!resettingNetwork_){startNetworks();}}}
 }
 if(dirty_){publish();}
}
void Owner::publish(){
 std::string availability=availability_;
 const bool ready=std::ranges::any_of(lanes_,[](const auto& lane){return lane&&lane->ready&&lane->actor;});
 if(!closing_&&ready&&std::ranges::any_of(rooms_,[](const auto& entry){const auto& room=entry.second;if(!room.token||!room.token->active||!room.session){return false;}const auto facts=room.session->facts();return facts.alive>0&&facts.exchanges>0;})){availability="Public chats connected";}
 if(lastConsoleAvailability_!=availability){lastConsoleAvailability_=availability;engine::detail::runtimeConsole("chat.network")<<" state="<<engine::detail::consoleText(availability)<<'\n'<<std::flush;}
 auto out=std::make_shared<GeneralChatSnapshot>();out->generation=shared_->generation;out->sequence=++sequence_;out->preferences=preferences_;out->preferencesReady=preferencesReady_;out->availability=std::move(availability);out->torrentDht=torrentFacts_;out->discoveries=swarmSnapshot_;out->discoveryNetwork=rcmFacts_;out->discoverySearch=rcmSearchView_;out->swarmTagLookup=rcmTagLookup_;out->swarmTagScan=rcmTagScanView_;out->selected=selected_;out->closing=closing_;out->closed=closing_&&!pulse_.isActive();
 for(const auto& [id,command]:privatePending_){if(command.privateSelection){const auto& p=*command.privateSelection;const auto nick=msg::participantShortName(p.publicKey);out->privateAttempts.push_back({id,p.epoch,command.key,p.publicKey,nick?*nick:"Participant",command.openedRoom});}}
 for(const auto& [key,work]:ratings_){(void)key;if(work->published){out->ratings.push_back(work->published);}}
 for(const auto& [key,notice]:ratingNotices_){(void)key;out->ratingNotices.push_back(notice->view);}
 for(const auto& [key,peek]:peeks_){(void)key;out->downloadPeeks.push_back(peek->view);}
 for(auto& [key,room]:rooms_){
  if(room.core&&room.token&&(key==selected_||room.download||!room.viewReferences.views().empty())){
   if(room.messageCacheEpoch!=room.token->epoch){room.messageCache.reset();}
   if(!room.messageCache||room.messageCacheMutation!=room.mutation){if(auto messages=room.core->messages(QDateTime::currentMSecsSinceEpoch())){room.messageCache=std::make_shared<const std::vector<msg::RoomMessage>>(std::move(*messages));room.messageCacheMutation=room.mutation;room.messageCacheEpoch=room.token->epoch;}}
  }
  if(room.core&&room.token&&!room.viewReferences.views().empty()){out->transcripts.push_back({key,room.token->epoch,{room.viewReferences.views().begin(),room.viewReferences.views().end()},room.messageCache});}
  if(room.download&&room.token&&room.token->download){const bool joined=room.token->active&&room.session&&!closing_;const auto facts=room.session?room.session->facts():msg::RoomSessionFacts{};auto settings=room.settings;if(room.core){settings.readOnly=room.core->readOnly();}auto status=joined?std::to_string(facts.peers)+" room nodes, "+std::to_string(facts.alive)+" responding":room.status;if(!room.localStatus.empty()){status+="; "+room.localStatus;}if(room.readError){status+="; read marker could not be saved";}out->downloadRooms.push_back({room.token->download->hash.hex(),{std::move(settings),std::move(status),joined,joined&&!room.core->readOnly(),room.unread,facts.peers,facts.active,room.token->epoch,room.unseenMention},room.messageCache});}
  if(room.background){continue;}const bool joined=room.token&&room.token->active&&(room.session||!room.lanes.size())&&!closing_;auto facts=room.session?room.session->facts():msg::RoomSessionFacts{};
  std::string status=room.status;if(joined&&room.session&&!room.privateClosing){status=std::to_string(facts.peers)+" room nodes, "+std::to_string(facts.alive)+" responding";}
  auto settings=room.settings;if(room.core){settings.readOnly=room.core->readOnly();}
  if(!room.localStatus.empty()){status+="; "+room.localStatus;}if(room.readError){status+="; read marker could not be saved";}out->rooms.push_back({std::move(settings),std::move(status),joined,joined&&room.session&&!room.privateClosing&&!room.core->readOnly(),room.unread,facts.peers,facts.active,room.token?room.token->epoch:0,room.unseenMention,room.viewReferences.keepAlive(),room.viewReferences.views().size()});
  if(key==selected_&&room.messageCache){out->messages=*room.messageCache;}
 }
 for(std::size_t lane=0;lane<lanes_.size();++lane){if(!lanes_[lane]||!lanes_[lane]->actor){continue;}
  if(lane<2){const auto& source=*lanes_[lane];const auto reach=source.actor->reachabilityFacts();auto& facts=out->nat[lane];facts.reachable=reach.reachable;facts.reachabilityAccurate=reach.accurate;if(source.nat){auto value=source.nat->facts();facts.activePunches=value.punches;facts.rendezvousActive=value.rendezvousActive;}if(source.natDirectory){const auto value=source.natDirectory->facts();facts.directoryReads=value.reads;facts.publications=value.publications;facts.stores=value.stores;}}
  out->cacheMaintenance[lane]=lanes_[lane]->actor->cacheMaintenanceFacts();out->surveyMaintenance[lane]=lanes_[lane]->actor->surveyMaintenanceFacts();out->surveysEnabled[lane]=lanes_[lane]->surveyAllowed;
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
 std::optional<std::uint64_t> enqueueScoped(std::optional<ChatViewTarget> view,Command command){if(view){if(view->generation!=shared->generation||!view->epoch||!view->view){return {};}command.ownerEpoch=view->epoch;command.view=view->view;command.scopedView=true;}return enqueue(std::move(command));}
 std::optional<std::uint64_t> enqueue(Command command){
  std::lock_guard lock(shared->mutex);if(shared->stop||shared->outstanding>=64||shared->next==std::numeric_limits<std::uint64_t>::max()||(command.key.empty()&&command.kind!=Kind::NotificationSettings&&command.kind!=Kind::PrivateChatPolicy&&command.kind!=Kind::WindowSettings)||command.key.size()>4096||command.text.size()>16*1024){return {};}
  command.id=++shared->next;const auto id=command.id;shared->commands.push_back(std::move(command));++shared->outstanding;if(shared->receiver){QCoreApplication::postEvent(shared->receiver,new QEvent(commandEvent()));}return id;
 }
};
GeneralChats::GeneralChats(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
std::expected<std::unique_ptr<GeneralChats>,storage::Error> GeneralChats::create(ChatTransportFactory factory,std::uint64_t generation){
 if(!generation||!factory.lease_||!factory.attach_){return std::unexpected(failure(storage::ErrorCode::Conflict,"Chat transport is unavailable"));}
 try{
  auto shared=std::make_shared<Shared>(std::move(factory.lease_),generation);auto discoveries=SwarmDiscoveryProfile::create(*shared->lease);if(discoveries){shared->swarmProfile=std::move(*discoveries);}auto sources=engine::AzureusTorrentSources::create(*shared->lease);if(sources){shared->torrentSources=std::move(*sources);}auto initial=std::make_shared<GeneralChatSnapshot>();initial->generation=generation;initial->availability="Opening Public chats…";shared->snapshot.store(std::move(initial));
  auto service=std::unique_ptr<GeneralChats>(new GeneralChats(std::make_unique<Impl>(shared)));service->impl_->worker=std::thread([shared,attach=std::move(factory.attach_),owned=factory.owned_,standards=factory.standardNetworks_,cachePeriod=factory.ownedCachePeriod_,surveyPeriod=factory.ownedSurveyPeriod_,fixtureKey=std::move(factory.ownedKeyBlockFixtureKey_),bindingPeriod=factory.ownedBindingPeriod_]()mutable{
   try{QEventLoop loop;Owner owner(shared,loop,owned,standards,std::move(attach),cachePeriod,surveyPeriod,std::move(fixtureKey),bindingPeriod);QTimer::singleShot(0,&owner,[&]{owner.start();});loop.exec();}
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
std::optional<std::uint64_t> GeneralChats::privateChat(std::string key,std::uint64_t epoch,std::string publicKey){if(!epoch||publicKey.size()<16||publicKey.size()>512){return {};}Command command{0,Kind::PrivateChat,std::move(key)};command.privateSelection=PrivateSelection{epoch,std::move(publicKey)};return impl_->enqueue(std::move(command));}
std::optional<std::uint64_t> GeneralChats::cancelPrivate(std::uint64_t generation,std::string parent,std::uint64_t epoch,std::uint64_t request){if(generation!=impl_->shared->generation||!epoch||!request){return {};}Command command{0,Kind::CancelPrivate,std::move(parent)};command.ownerEpoch=epoch;command.cancelRequest=request;return impl_->enqueue(std::move(command));}
std::optional<std::uint64_t> GeneralChats::closePrivate(std::uint64_t generation,std::string key,std::uint64_t epoch){if(generation!=impl_->shared->generation||!epoch){return {};}Command command{0,Kind::Leave,std::move(key)};command.ownerEpoch=epoch;command.privateOnly=true;return impl_->enqueue(std::move(command));}
std::optional<std::uint64_t> GeneralChats::windowSettings(ChatWindowSettings expected,ChatWindowSettings value){Command command{0,Kind::WindowSettings};command.windows=std::pair{expected,value};return impl_->enqueue(std::move(command));}
std::optional<std::uint64_t> GeneralChats::acquireView(std::uint64_t generation,std::string key,std::uint64_t epoch,std::uint64_t view){if(!impl_||generation!=impl_->shared->generation||!epoch||!view){return {};}Command command{0,Kind::AcquireView,std::move(key)};command.ownerEpoch=epoch;command.view=view;return impl_->enqueue(std::move(command));}
std::optional<std::uint64_t> GeneralChats::releaseView(std::uint64_t generation,std::string key,std::uint64_t epoch,std::uint64_t view){if(!impl_||generation!=impl_->shared->generation||!epoch||!view){return {};}Command command{0,Kind::ReleaseView,std::move(key)};command.ownerEpoch=epoch;command.view=view;return impl_->enqueue(std::move(command));}
std::optional<std::uint64_t> GeneralChats::keepAlive(std::uint64_t generation,std::string key,std::uint64_t epoch,bool expected,bool value){if(!impl_||generation!=impl_->shared->generation||!epoch){return {};}Command command{0,Kind::KeepAlive,std::move(key),{},expected,value};command.ownerEpoch=epoch;return impl_->enqueue(std::move(command));}
std::optional<std::uint64_t> GeneralChats::privateChatPolicy(std::int32_t expected,std::int32_t value){if(value<1||value>3){return {};}Command command{0,Kind::PrivateChatPolicy};command.privatePolicy=PrivatePolicyChange{expected,value};return impl_->enqueue(std::move(command));}
std::optional<std::uint64_t> GeneralChats::displayPreference(std::string key,bool ratings,bool expected,bool value,std::optional<ChatViewTarget> view){return impl_->enqueueScoped(view,{0,ratings?Kind::HideRatings:Kind::HideSubscriptions,std::move(key),{},expected,value});}
std::optional<std::uint64_t> GeneralChats::participantPreference(std::string key,std::string publicKey,bool pinned,bool expected,bool value,std::optional<ChatViewTarget> view){if(publicKey.size()<16||publicKey.size()>512){return {};}return impl_->enqueueScoped(view,{0,pinned?Kind::PinParticipant:Kind::IgnoreParticipant,std::move(key),std::move(publicKey),expected,value});}
std::optional<std::uint64_t> GeneralChats::spammer(std::string key,std::uint64_t epoch,std::string publicKey,bool expected,bool value,std::optional<ChatViewTarget> view){if(!epoch||publicKey.size()<16||publicKey.size()>512){return {};}Command command{0,Kind::SpamParticipant,std::move(key),std::move(publicKey),expected,value};command.indications=IndicationChange{epoch,expected,value};return impl_->enqueueScoped(view,std::move(command));}
std::optional<std::uint64_t> GeneralChats::relatedChannel(std::string key,std::uint64_t epoch,bool readOnly,std::optional<ChatViewTarget> view){if(!epoch){return {};}Command command{0,Kind::RelatedChannel,std::move(key),{},readOnly};command.ownerEpoch=epoch;return impl_->enqueueScoped(view,std::move(command));}
bool GeneralChats::refreshDiscoveries(){
 if(!impl_){return false;}auto& shared=*impl_->shared;std::lock_guard lock(shared.mutex);if(shared.stop||shared.swarmCommands.size()>=64){return false;}shared.swarmCommands.push_back({SwarmCommand::Kind::Refresh,{},{},{}});if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}return true;
}
void GeneralChats::observeCategoryDefinitionChange(const storage::ProfileLease& owner,
    const storage::CategorySnapshot& before,const storage::CategorySnapshot& after,
    std::string_view name,bool added) noexcept {
 if (!impl_ || impl_->shared->stop) { return; }
 if (const auto& profile=impl_->shared->swarmProfile) {
  profile->observeCategoryDefinitionChange(owner,before,after,name,added);
 }
}
bool GeneralChats::discoverNow(){
 if(!impl_){return false;}auto& shared=*impl_->shared;std::lock_guard lock(shared.mutex);if(shared.stop||shared.swarmCommands.size()>=64){return false;}shared.swarmCommands.push_back({SwarmCommand::Kind::Cycle,{},{},{}});if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}return true;
}
std::vector<SwarmTagUpdate> GeneralChats::takeSwarmTagUpdates(){
 if (!impl_) { return {}; }
 auto& shared=*impl_->shared;
 std::lock_guard lock(shared.mutex);
 std::vector<SwarmTagUpdate> updates;
 updates.swap(shared.swarmTagUpdates);
 return updates;
}
bool GeneralChats::lookupDiscoveries(std::string hash,std::int64_t fileSize){
 if(!impl_||hash.size()!=20||(fileSize&&fileSize<50*1024*1024)){return false;}auto& shared=*impl_->shared;std::lock_guard lock(shared.mutex);if(shared.stop||shared.swarmCommands.size()>=64){return false;}shared.swarmCommands.push_back({SwarmCommand::Kind::Lookup,std::move(hash),fileSize,{}});if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}return true;
}
bool GeneralChats::lookupSwarmTags(std::string hash) {
 if (!impl_ || hash.size()!=20) { return false; }
 auto& shared=*impl_->shared;
 std::lock_guard lock(shared.mutex);
 if (shared.stop || shared.swarmCommands.size()>=64) { return false; }
 shared.swarmCommands.push_back({SwarmCommand::Kind::Attributes,std::move(hash),{},{}});
 if (shared.receiver) { QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent())); }
 return true;
}
bool GeneralChats::scanSwarmTags(){
 if(!impl_){return false;}
 auto& shared=*impl_->shared;
 std::lock_guard lock(shared.mutex);
 if(shared.stop||shared.swarmCommands.size()>=64){return false;}
 shared.swarmCommands.push_back({SwarmCommand::Kind::ScanTags,{},{},{}});
 if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}
 return true;
}
bool GeneralChats::cancelSwarmTagScan(){
 if(!impl_){return false;}
 auto& shared=*impl_->shared;
 std::lock_guard lock(shared.mutex);
 if(shared.stop||shared.swarmCommands.size()>=64){return false;}
 shared.swarmCommands.push_back({SwarmCommand::Kind::CancelTagScan,{},{},{}});
 if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}
 return true;
}
bool GeneralChats::dismissSwarmTagFindings(rcm::TagScanSelections selections){
 if(!impl_||selections.empty()||selections.size()>rcm::MaximumTagScanFindings||
     std::ranges::any_of(selections,[](const auto& row){
      return !row.request||row.hash.size()!=20||row.tag.empty()||row.tag.size()>128||row.foundOn<=0;
     })){return false;}
 auto& shared=*impl_->shared;
 std::lock_guard lock(shared.mutex);
 if(shared.stop||shared.swarmCommands.size()>=64){return false;}
 SwarmCommand command;command.kind=SwarmCommand::Kind::DismissTag;
 command.tagSelections=std::move(selections);
 shared.swarmCommands.push_back(std::move(command));
 if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}
 return true;
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
bool GeneralChats::readRatingsByHash(core::TorrentHash hash,std::uint64_t generation){return impl_&&generation==impl_->shared->generation&&queueRating(impl_->shared,{hash,false,false,true});}
bool GeneralChats::cancelRatingsByHash(core::TorrentHash hash,std::uint64_t generation){return impl_&&generation==impl_->shared->generation&&queueRating(impl_->shared,{hash,false,true,true});}
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
bool GeneralChats::setReadingRoom(std::string key,std::uint64_t epoch){
 if(!impl_||key.size()>4096){return false;}auto& shared=*impl_->shared;std::lock_guard lock(shared.mutex);
 if(shared.stop){return false;}if(key.empty()){epoch=0;}if(shared.readingRoom==key&&shared.readingEpoch==epoch){return true;}shared.readingRoom=std::move(key);shared.readingEpoch=epoch;
 if(shared.receiver&&!shared.readingWakePending){shared.readingWakePending=true;QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}return true;
}
std::optional<std::uint64_t> GeneralChats::send(std::string key,std::string text,std::optional<ChatViewTarget> view){return impl_->enqueueScoped(view,{0,Kind::Send,std::move(key),std::move(text)});}
std::optional<std::uint64_t> GeneralChats::notifications(ChatNotificationSettings expected,ChatNotificationSettings value){
 if(expected.soundFile.size()>16384||value.soundFile.size()>16384){return {};}
 Command command;command.kind=Kind::NotificationSettings;command.notifications=NotificationChange{std::move(expected),std::move(value)};return impl_->enqueue(std::move(command));
}
std::optional<std::uint64_t> GeneralChats::openActivity(std::string key,ActivityReference ref){Command command;command.kind=Kind::Join;command.key=std::move(key);command.activity=std::move(ref);return impl_->enqueue(std::move(command));}
std::optional<std::uint64_t> GeneralChats::posting(std::string key,std::uint64_t epoch,bool expected,bool value){Command command;command.kind=Kind::Posting;command.key=std::move(key);command.indications=IndicationChange{epoch,expected,value};return impl_->enqueue(std::move(command));}
std::optional<std::uint64_t> GeneralChats::indications(std::string key,std::uint64_t epoch,bool expected,bool value){
 if(!epoch){return {};}Command command;command.kind=Kind::Options;command.key=std::move(key);command.indications=IndicationChange{epoch,expected,value};return impl_->enqueue(std::move(command));
}
std::optional<std::uint64_t> GeneralChats::options(std::string key,bool favourite,bool saveHistory){return impl_->enqueue({0,Kind::Options,std::move(key),{},favourite,saveHistory});}
std::optional<std::uint64_t> GeneralChats::nicknameMode(std::string key,bool shared,std::optional<ChatViewTarget> view){return impl_->enqueueScoped(view,{0,Kind::NicknameMode,std::move(key),{},shared});}
std::optional<std::uint64_t> GeneralChats::nickname(std::string key,std::string value,bool expectedShared,std::optional<ChatViewTarget> view){return impl_->enqueueScoped(view,{0,Kind::Nickname,std::move(key),std::move(value),expectedShared});}
bool GeneralChats::prepareStop(Clock::time_point deadline)noexcept{
 if(!impl_){return true;}try{auto& shared=*impl_->shared;if(shared.stop.load()){return false;}const auto end=std::min(deadline,Clock::now()+std::chrono::milliseconds(750));if(Clock::now()>=end){return false;}const auto raw=std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count();std::int64_t empty=0;shared.prepareUntil.compare_exchange_strong(empty,raw);{std::lock_guard lock(shared.mutex);if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}}
 while(Clock::now()<end&&!shared.stop.load()&&!shared.prepared.load()){std::this_thread::sleep_for(std::chrono::milliseconds(2));}return shared.prepared.load();}catch(...){return false;}
}
void GeneralChats::requestStop()noexcept{if(!impl_){return;}auto& shared=*impl_->shared;shared.stop=true;if(auto witness=shared.witness.load()){witness->revoke();}try{std::lock_guard lock(shared.mutex);if(shared.receiver){QCoreApplication::postEvent(shared.receiver,new QEvent(commandEvent()));}}catch(...){} }
void GeneralChats::join()noexcept{
 if(!impl_){return;}requestStop();std::lock_guard lock(impl_->joinedMutex);if(impl_->worker.joinable()){impl_->worker.join();}
 auto& shared=*impl_->shared;if(shared.networkIo){shared.networkIo->join();shared.networkIo.reset();}if(shared.io){shared.io->join();shared.io.reset();}shared.swarmProfile.reset();shared.lease.reset();shared.witness.store({});
}
} // namespace biglybtc::app

namespace biglybtc::app {
namespace {
bool queuePeek(const std::shared_ptr<Shared>& shared,PeekCommand command){std::lock_guard lock(shared->mutex);if(shared->stop||shared->peekCommands.size()>=64){return false;}shared->peekCommands.push_back(std::move(command));if(shared->receiver){QCoreApplication::postEvent(shared->receiver,new QEvent(commandEvent()));}return true;}
}
bool GeneralChats::openDownloadChat(core::TorrentHash hash){return impl_&&queuePeek(impl_->shared,{hash,false,true});}
bool GeneralChats::peekDownloadChat(core::TorrentHash hash){return impl_&&queuePeek(impl_->shared,{hash,false});}
bool GeneralChats::cancelDownloadChatPeek(core::TorrentHash hash){return impl_&&queuePeek(impl_->shared,{hash,true});}
}
