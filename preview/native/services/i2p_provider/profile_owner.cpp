// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../platform/module_symbols.hpp"
#include "../../platform/utf8_path.hpp"
#include "state_file_io.hpp"
#include "profile_owner.hpp"
#include "saved_download_tags_view.hpp"
#include "profile_tag_rates.hpp"
#include "profile_prepared_tag_attachment.hpp"
#include "private_tag_rate_source.hpp"
#include "contact_owner.hpp"
#include "owner_runtime_guard.hpp"
#include "source_permit.hpp"
#include "private_tag_source_refresh.hpp"
#include "retirement_counters.hpp"
#include "metadata_disk_router.hpp"
#include "backup_piece_map.hpp"
#include "subscription_seed_owner.hpp"
#include "subscription_source.hpp"
#include "managed_queue_observations.hpp"
#include "managed_tag_state.hpp"
#include "biglybtc/queue_observations.hpp"
#include "biglybtc/queue_positions.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/biglybtc_private_i2p_owner.hpp>
#include <libtorrent/biglybtc_queue_observer.hpp>
#include <libtorrent/ip_filter.hpp>
#include <QDateTime>
#include <QDebug>
#include <QMetaEnum>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <openssl/rand.h>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <map>

#include <thread>
namespace biglybtc::services::i2p::provider {
namespace {
namespace lt=libtorrent;
using Error=storage::Error;using Code=storage::ErrorCode;
std::int64_t nowMillis(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
bool entropy(std::uint64_t& value){return RAND_bytes(reinterpret_cast<unsigned char*>(&value),sizeof(value))==1&&value;}
std::expected<storage::ProfileSourceAccess,Error> bootstrapSourceAccess(
    const storage::ProfileLease& lease,std::optional<std::string_view> expected){
  auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
  if(!access->observationsAllowed()){return std::unexpected(Error{Code::Conflict,"Release the outer source gate before private SDK preparation"});}
  if(expected){
    auto config=storage::ConfigFile::load(lease,"biglybt.config");if(!config){return std::unexpected(config.error());}
    if(config->document().serialize()!=*expected){return std::unexpected(Error{Code::Conflict,"Original I2P configuration changed before payload preparation"});}
  }
  return std::move(*access);
}
struct QueueSink final:lt::BiglybtcQueueObserverV1 {
  std::shared_ptr<engine::detail::ManagedQueueObservations> state;std::shared_ptr<void> pin;
  QueueSink(decltype(state) s,decltype(pin) p):state(std::move(s)),pin(std::move(p)){}
  void observe(const lt::BiglybtcQueueEventV1& event)noexcept override{
    core::QueueObservation v;v.sequence=event.sequence;v.request=event.request;v.observedNanos=event.observed_nanoseconds;v.requestStartedNanos=event.request_started_nanoseconds;
    v.observedWallMillis=event.observed_wall_milliseconds;v.requestStartedWallMillis=event.request_started_wall_milliseconds;
    v.seeds=event.seeds;v.peers=event.peers;v.protocol=event.protocol;v.kind=static_cast<core::QueueObservationKind>(event.kind);v.tracker=event.tracker_sha256;state->observe(v);
  }
};
}
struct OwnedProfile::Prepared::Impl {
  core::TorrentHash hash;std::shared_ptr<ProfileSource> source;engine::detail::ManagedStorage storage;
  std::shared_ptr<const core::SavedDownload> backupBaseline;
  std::uint64_t admission=0;std::size_t inventoryIndex=0;
  std::shared_ptr<engine::ProfilePreparedTagAttachment> tagTarget;
  Impl(core::TorrentHash h,std::shared_ptr<ProfileSource> s,engine::detail::ManagedStorage m):hash(h),source(std::move(s)),storage(std::move(m)){}
};
OwnedProfile::Prepared::Prepared(std::unique_ptr<Impl> v):impl_(std::move(v)){}
OwnedProfile::Prepared::~Prepared()=default;
struct OwnedProfile::PreparedMagnet::Impl {
  engine::PendingMagnet intent;std::shared_ptr<PendingMagnetSource> source;
  std::shared_ptr<engine::detail::PendingMetadataLifetime> lifetime;
  std::shared_ptr<const engine::PendingMetadataSource> seal;
  lt::add_torrent_params parameters;std::shared_ptr<engine::ProfilePreparedTagAttachment> tagTarget;std::uint64_t admission=0;
  std::chrono::steady_clock::time_point expires{};
};
OwnedProfile::PreparedMagnet::PreparedMagnet(std::unique_ptr<Impl> value):impl_(std::move(value)){}
OwnedProfile::PreparedMagnet::~PreparedMagnet()=default;
struct OwnedProfile::PreparedSubscription::Impl {
  std::shared_ptr<SubscriptionSource> source;
  std::shared_ptr<engine::detail::MetadataDiskPolicy> disks;
  std::filesystem::path slot;
  engine::detail::PreparedSubscriptionTorrent torrent;
  std::uint64_t admission = 0;
  ~Impl() {
    if (disks) { disks->releaseOriginalPath(slot, torrent.files); }
  }
};
OwnedProfile::PreparedSubscription::PreparedSubscription(std::unique_ptr<Impl> value):impl_(std::move(value)){}
OwnedProfile::PreparedSubscription::~PreparedSubscription()=default;
struct OwnedProfile::QueueState::Impl {
  struct Row {core::TorrentHash hash;std::uint64_t admission;std::optional<std::int64_t> position;bool force;};
  std::vector<Row> rows;std::uint64_t revision=0;
};
OwnedProfile::QueueState::QueueState(std::unique_ptr<Impl> p):impl_(std::move(p)){}
OwnedProfile::QueueState::~QueueState()=default;
struct OwnedProfile::TagSourceUpdate::Impl {
  struct Target {
    Prepared::Impl* prepared=nullptr;PreparedMagnet::Impl* magnet=nullptr;lt::torrent_handle handle;
    explicit Target(Prepared::Impl* p):prepared(p){}
    explicit Target(PreparedMagnet::Impl* p):magnet(p){}
    const core::TorrentHash& hash()const{return prepared?prepared->hash:magnet->intent.hash;}
    std::shared_ptr<ProfileSource> child()const{return prepared?prepared->source:magnet->source->source_;}
    auto& token(){return prepared?prepared->tagTarget:magnet->tagTarget;}lt::info_hash_t expected;
    std::shared_ptr<engine::ProfilePreparedTagAttachment> replacement;
    engine::PrivateTagRateSource source;
  };
  OwnedProfile::Impl* owner=nullptr;std::shared_ptr<ProfileSource> root;
  lt::session_handle session;std::vector<Target> targets;
  bool joined=false,prepared=false;
};
OwnedProfile::TagSourceUpdate::TagSourceUpdate(std::unique_ptr<Impl> value):impl_(std::move(value)){}
OwnedProfile::TagSourceUpdate::~TagSourceUpdate()=default;
struct OwnedProfile::BackupCapture::Impl {
  struct Entry {
    core::TorrentHash hash;
    std::uint64_t admission;
    std::vector<std::uint8_t> pieces;
    std::optional<std::int64_t> received,sent;
    bool accepted=false;
  };
  const OwnedProfile* owner;
  std::uint64_t serial;
  std::chrono::steady_clock::time_point deadline;
  std::vector<Entry> entries;
  bool settled=false;
};
OwnedProfile::BackupCapture::BackupCapture(std::unique_ptr<Impl> value):impl_(std::move(value)){}
OwnedProfile::BackupCapture::~BackupCapture()=default;
struct OwnedProfile::Impl {
  const std::thread::id ioWorker=std::this_thread::get_id();
  struct Member {
    std::unique_ptr<Prepared> prepared;lt::torrent_handle handle;
    std::shared_ptr<detail::RetirementCounters> counters=std::make_shared<detail::RetirementCounters>();
    std::shared_ptr<engine::detail::ManagedQueueObservations> queue;
    core::QueueObservedFacts queueFacts;core::QueueActivityMemory activity;
    engine::detail::ManagedTagState tagState;
    std::optional<std::int64_t> livePosition;std::optional<bool> observedComplete;
    std::int64_t nextDiagnostic=0,nextQueueDiagnostic=0;
    bool forceStart=false,clearCompletedForce=false,justCompleted=false,completionPositionUnavailable=false;
    std::atomic<bool> checkpointed{false};
    std::optional<core::QueueObservedDurations> finalDurations;
    OwnedTorrent::RetirementObservation retired;std::chrono::steady_clock::time_point deadline{};
    engine::detail::ManagedPayloadCounters::Sample finalCounters;
    std::vector<std::uint8_t> pieces;std::int64_t nextScrape=0;bool scrapePending=false,weakExpiryObserved=false;
    bool complete=false,unchecked=false,pauseSettled=false,pausePending=false,pauseSeen=false,saveRequested=false,removed=false,removing=false,queued=true,installed=false,registered=false;
    std::uint64_t backupFlushSerial=0;
    bool backupFlushPending=false;
    OwnedTorrent::AlertDiagnostics alerts;std::string failure;
    auto& data(){return *prepared->impl_;}const auto& data()const{return *prepared->impl_;}
  };
  struct Magnet {
    std::unique_ptr<PreparedMagnet> prepared;lt::torrent_handle handle;
    bool installed=false,registered=false,cancel=false,removing=false,removed=false,barrier=false,ready=false,taken=false;
    std::atomic_bool released=false;std::string bytes;std::optional<Error> failure;
    std::chrono::steady_clock::time_point end{};
    auto& data(){return *prepared->impl_;}const auto& data()const{return *prepared->impl_;}
  };
  struct Subscription {
    explicit Subscription(std::unique_ptr<PreparedSubscription> value):prepared(std::move(value)){}
    std::unique_ptr<PreparedSubscription> prepared;
    lt::torrent_handle handle;
    std::chrono::steady_clock::time_point deadline{}, lastActivity{};
    int connectedPeers=0;
    bool discovering=false, demanded=false, registered=false, installed=false, checked=false;
    bool ready=false, removed=false, retiring=false, barrier=false;
    auto& data() const { return *prepared->impl_; }
    void resetTorrent() {
      handle={};
      deadline={};
      lastActivity={};
      connectedPeers=0;
      demanded=false;
      installed=false;
      checked=false;
      ready=false;
      removed=false;
      retiring=false;
      barrier=false;
    }
  };
  struct SubscriptionDownload {
    search::I2pSubscriptionUpgrade update;
    std::shared_ptr<SubscriptionSource> source;
    core::TorrentHash selected;
    std::uint64_t admission;
    std::filesystem::path slot;
    engine::detail::PreparedSubscriptionTorrent prepared;
    std::chrono::steady_clock::time_point deadline;
    lt::torrent_handle handle{};
    std::string bytes;
    std::vector<bool> pieces;
    bool registered=false,reading=false;
    std::optional<std::expected<std::string,Error>> result;
  };
  std::map<std::string,std::unique_ptr<Magnet>> magnets;
  std::optional<dht::Actor::Options> discoveryOptions,mixedPublicationOptions;
  std::shared_ptr<ContactExchange> contactExchange;
  std::shared_ptr<const engine::detail::MixedManagedOrigin> metadataOrigin=std::make_shared<engine::detail::MixedManagedOrigin>();
  OwnedProfile* q=nullptr;std::shared_ptr<ProfileSource> source;std::optional<ClientIdentity> identity;
  std::shared_ptr<void> pin;std::shared_ptr<engine::detail::MetadataDiskPolicy> disks=std::make_shared<engine::detail::MetadataDiskPolicy>();
  std::unique_ptr<lt::session> session;std::optional<lt::session_proxy> joining;
  std::unique_ptr<i2cp::InitialChannel> channel;std::unique_ptr<Bridge> bridge;std::unique_ptr<SourcePermit> permit;
  std::map<std::string,std::unique_ptr<Member>> members;
  std::uint64_t backupCaptureSerial=0;
  std::map<std::string,Subscription> subscriptions;
  std::optional<search::I2pSubscriptionUpgrade> offeredUpgrade;
  std::optional<SubscriptionDownload> subscriptionDownload;
  QTimer timer;Options options;core::QueuePolicy queuePolicy;Rates rates;Phase phase=Phase::Prepared;
  std::uint64_t admission=0,generation=0;bool startRequested=false,bridgeRequested=false,installQueued=false,memberInstallRequested=false;std::int64_t started=nowMillis();
  std::uint32_t queueUnavailable=0;std::string failure;
  bool sourcePaused=false;
  bool externalQueue=false,externalSnapshotValid=false;std::uint64_t externalQueueSequence=0;
  std::optional<core::SessionRateLimits> externalRates;
  bool onThread()const{return QThread::currentThread()==q->thread();}
  Member* find(const core::TorrentHash& h)const{const auto i=members.find(h.hex());return i==members.end()?nullptr:i->second.get();}
  std::size_t privateAdmissionCount() const {
    const auto pending=std::ranges::count_if(magnets,[](const auto& entry){return !entry.second->released.load();});
    return members.size()+static_cast<std::size_t>(pending)+subscriptions.size()+
        static_cast<std::size_t>(subscriptionDownload&&!subscriptionDownload->result);
  }
  bool hasSubscriptionCapacity() const {
    const auto loaded=std::ranges::count_if(subscriptions,[](const auto& entry) {
      return entry.second.installed;
    });
    return loaded<4;
  }
  auto rotatableSubscription(std::chrono::steady_clock::time_point now) {
    if (!bridge || std::ranges::any_of(subscriptions,[](const auto& entry) { return entry.second.retiring; })) {
      return subscriptions.end();
    }
    return std::ranges::find_if(subscriptions,[&](const auto& entry) {
      const auto& seed=entry.second;
      return !seed.installed && !seed.demanded && !seed.data().source->revoked()
          && bridge->subscriptionDiscoveryWindowElapsed(seed.data().admission,now);
    });
  }
  void makeRoomForDownload(const std::string& key) {
    if (const auto seed=subscriptions.find(key); seed!=subscriptions.end()) { retireSubscription(seed->second); }
    else if (privateAdmissionCount()>=Bridge::MaximumDiscoveryJobs) {
      auto seed=std::ranges::find_if(subscriptions,[](const auto& entry) {
        return !entry.second.installed && !entry.second.retiring;
      });
      if (seed==subscriptions.end()) {
        seed=std::ranges::find_if(subscriptions,[](const auto& entry){ return !entry.second.retiring; });
      }
      if (seed!=subscriptions.end()) { retireSubscription(seed->second); }
    }
  }
  void requestInstall(){
    if(phase!=Phase::Starting||!bridge||installQueued||bridge->observation().phase!=Bridge::Phase::Ready){return;}
    if(std::ranges::none_of(members,[](const auto& entry){return !entry.second->retired.requested;})&&std::ranges::none_of(magnets,[](const auto& entry){return !entry.second->cancel&&!entry.second->released.load();})
        && std::ranges::none_of(subscriptions,[](const auto& entry){ return !entry.second.data().source->revoked(); })){return;}
    installQueued=true;if(!bridge->installProvider([this](void* policy){installQueued=false;return installSession(policy);})){installQueued=false;close();}
  }
  void close(const char* reason=nullptr){
    if(failure.empty()){if(bridge&&bridge->observation().phase==Bridge::Phase::Withdrawn){failure=bridge->failureReason();}else if(channel&&channel->failure()!=i2cp::InitialChannel::Failure::None){const auto* key=QMetaEnum::fromType<i2cp::InitialChannel::Failure>().valueToKey(static_cast<int>(channel->failure()));failure=std::string("I2CP: ")+(key?key:"Unknown");}else if(reason){failure=reason;}}if(phase==Phase::Withdrawn){return;}phase=Phase::Withdrawn;timer.stop();source->revoke();identity.reset();
    if(bridge){bridge->close();}if(channel){channel->close();}if(permit){permit->stop();}
    if(session){if(generation){(void)lt::biglybtc_withdraw_private_i2p_owner_v1(session.get(),generation);}joining.emplace(session->abort());session.reset();}
    for(auto& [key,m]:magnets){(void)key;m->data().lifetime->retained.store(false);m->handle={};
      if(!m->ready&&!m->failure){m->failure=Error{Code::Conflict,failure.empty()
        ?"I2P metadata session withdrew; original intent retained":failure};}
    }
    for(auto& [key,m]:members){(void)key;m->handle={};if(m->retired.requested&&!m->retired.readyForJoin&&!m->retired.failure){m->retired.failure=Error{Code::Conflict,"Shared I2P session withdrew before target retirement; recheck retained"};}}
    for (auto& [key, seed] : subscriptions) { (void)key; seed.data().source->revoke(); seed.handle = {}; }
    offeredUpgrade.reset();
    if (subscriptionDownload) {
      subscriptionDownload->handle={};
      subscriptionDownload->result=std::unexpected(Error{Code::Conflict,"The I2P subscription session closed"});
    }
  }
  void fail(Member& m,Error e){m.failure=e.message;m.retired.failure=std::move(e);/* Preserve siblings; this member cannot checkpoint. */if(m.handle.is_valid()){m.handle.pause();}}
  lt::BiglybtcPrivateI2pTorrentV1 registration(const Member& m)const{
    lt::BiglybtcPrivateI2pTorrentV1 v;v.generation=generation;v.admission=m.data().admission;const auto& full=m.data().storage.identity().full;
    if(full.v1){v.hashes|=1;std::transform(full.v1->begin(),full.v1->end(),v.torrent_v1.begin(),[](std::byte b){return std::to_integer<unsigned char>(b);});}
    if(full.v2){v.hashes|=2;std::transform(full.v2->begin(),full.v2->end(),v.torrent_v2.begin(),[](std::byte b){return std::to_integer<unsigned char>(b);});}return v;
  }
  lt::BiglybtcPrivateI2pTorrentV1 registration(const Magnet& m)const{
    lt::BiglybtcPrivateI2pTorrentV1 tuple;tuple.generation=generation;tuple.admission=m.data().admission;const auto& requested=m.data().intent.requested;
    if(requested.has_v1()){tuple.hashes|=1;std::copy(requested.v1.begin(),requested.v1.end(),tuple.torrent_v1.begin());}
    if(requested.has_v2()){tuple.hashes|=2;std::copy(requested.v2.begin(),requested.v2.end(),tuple.torrent_v2.begin());}return tuple;
  }
  lt::BiglybtcPrivateI2pTorrentV1 registration(const Subscription& seed) const {
    return subscriptionTuple(seed.data().source->hash(),seed.data().admission);
  }
  lt::BiglybtcPrivateI2pTorrentV1 registration(const SubscriptionDownload& download) const {
    return subscriptionTuple(download.selected,download.admission);
  }
  lt::BiglybtcPrivateI2pTorrentV1 subscriptionTuple(const core::TorrentHash& hash,std::uint64_t route) const {
    lt::BiglybtcPrivateI2pTorrentV1 tuple;
    tuple.generation = generation;
    tuple.admission = route;
    tuple.hashes = 1;
    std::ranges::transform(hash.bytes(),tuple.torrent_v1.begin(),
        [](std::byte value) { return std::to_integer<unsigned char>(value); });
    return tuple;
  }
  template<class Target>bool deliverDiscovered(Target& m,const Hash& peer,std::uint64_t operation){
#if defined(BIGLYBTC_I2P_DISCOVERY_ABI_V1)
    if(!session||phase!=Phase::Active||!operation||!m.handle.is_valid()){return false;}
    lt::BiglybtcI2pDiscoveredPeerV1 request;request.torrent=registration(m);request.operation=operation;
    std::copy(peer.begin(),peer.end(),request.peer_hash.begin());
    const auto deliver=reinterpret_cast<decltype(&lt::biglybtc_discovered_i2p_peer_v1)>(platform::loadedSymbol(pin.get(),"biglybtc_discovered_i2p_peer_v1"));
    return deliver&&deliver(session.get(),&request)==0;
#else
    (void)m;(void)peer;(void)operation;return false;
#endif
  }
  template<class Target>bool discovered(Target& m,const Hash& peer,std::uint64_t operation){
    return m.data().source->prepared() && m.data().source->discoveryAllowed()
        && deliverDiscovered(m,peer,operation);
  }
  void startDiscovery(Member& m){
    if(!bridge||!discoveryOptions||!m.installed||m.retired.requested||!m.data().source->discoveryAllowed()){return;}
    dht::Id wire{};std::transform(m.data().hash.bytes().begin(),m.data().hash.bytes().end(),wire.begin(),[](std::byte value){return std::to_integer<Byte>(value);});const auto id=m.data().admission;const auto hash=m.data().hash;
    (void)bridge->discover(m.data().source,hash,wire,id,m.handle.status().is_seeding,[this,id,hash](const Hash& peer,std::uint64_t op){auto* target=find(hash);return target&&target->data().admission==id&&!target->retired.requested&&discovered(*target,peer,op);});
  }
  void startDiscovery(Magnet& m){
    if(!bridge||!discoveryOptions||!m.installed||m.cancel||!m.data().source->discoveryAllowed()){return;}
    dht::Id wire{};const auto& hash=m.data().intent.hash;std::transform(hash.bytes().begin(),hash.bytes().end(),wire.begin(),[](std::byte value){return std::to_integer<Byte>(value);});const auto id=m.data().admission;
    (void)bridge->discover(m.data().source,hash,wire,id,false,[this,id,hash](const Hash& peer,std::uint64_t op){const auto at=magnets.find(hash.hex());return at!=magnets.end()&&at->second->data().admission==id&&!at->second->cancel&&!at->second->removing&&discovered(*at->second,peer,op);});
  }
  bool subscriptionCurrent(const Subscription& seed, std::uint64_t revision) const {
    return source->revisionCurrent(revision) && seed.data().source->current(revision);
  }
  bool subscriptionDownloadCurrent(const SubscriptionDownload& download,std::uint64_t revision) const {
    const auto definition=download.source->upgrade(revision);
    return definition&&source->revisionCurrent(revision)&&definition->publicKey==download.update.publicKey&&
        definition->minimumVersion<download.update.presence.version;
  }
  void finishSubscriptionDownload(std::expected<std::string,Error> result) {
    auto& download=*subscriptionDownload;
    if (download.result) { return; }
    if (bridge) { bridge->cancelDiscovery(download.admission); }
    if (download.registered) {
      const auto tuple=registration(download);
      if (lt::biglybtc_revoke_private_i2p_torrent_v1(session.get(),&tuple)!=0) {
        close("Cannot withdraw the I2P subscription download");
        return;
      }
      download.registered=false;
    }
    if (download.handle.is_valid()) { session->remove_torrent(download.handle); }
    download.handle={};
    download.result=std::move(result);
  }
  void advanceSubscriptionDownload(std::optional<std::uint64_t> revision={}) {
    if (!subscriptionDownload||subscriptionDownload->result) { return; }
    auto& download=*subscriptionDownload;
    if (download.source->revoked()||std::chrono::steady_clock::now()>=download.deadline||
        (revision&&!subscriptionDownloadCurrent(download,*revision))) {
      finishSubscriptionDownload(std::unexpected(Error{Code::Conflict,"The I2P subscription update expired or changed"}));
      return;
    }
    if (!download.registered) {
      if (!revision) { requestStartup();return; }
      const auto tuple=registration(download);
      if (lt::biglybtc_admit_private_i2p_torrent_v1(session.get(),&tuple)!=0) {
        finishSubscriptionDownload(std::unexpected(Error{Code::Conflict,"The I2P update torrent is already owned"}));
        return;
      }
      download.registered=true;
      lt::error_code error;
      download.handle=session->add_torrent(download.prepared.parameters,error);
      const auto route=download.admission;
      if (error||!download.handle.is_valid()||!bridge->discoverSubscriptionUpgrade(download.source,download.selected,route,
          [this,route](const Hash& peer,std::uint64_t operation) {
            return subscriptionDownload&&subscriptionDownload->admission==route&&!subscriptionDownload->result&&
                subscriptionDownloadCurrent(*subscriptionDownload,source->lease()->sourceRevision())&&
                deliverDiscovered(*subscriptionDownload,peer,operation);
          })) {
        finishSubscriptionDownload(std::unexpected(Error{Code::Io,"Cannot start the I2P subscription transfer"}));
      }
      return;
    }
    if (!download.handle.is_valid()) {
      finishSubscriptionDownload(std::unexpected(Error{Code::Io,"The I2P subscription transfer disappeared"}));
      return;
    }
    if (!download.reading&&download.handle.status({}).is_seeding) {
      const auto& metadata=*download.prepared.parameters.ti;
      download.bytes.resize(static_cast<std::size_t>(metadata.total_size()));
      download.pieces.resize(static_cast<std::size_t>(metadata.num_pieces()));
      download.reading=true;
      for (int i=0;i<metadata.num_pieces();++i) { download.handle.read_piece(lt::piece_index_t{i}); }
    }
  }
  void subscriptionPiece(const lt::read_piece_alert& piece) {
    auto& download=*subscriptionDownload;
    const auto& metadata=*download.prepared.parameters.ti;
    const auto index=static_cast<int>(piece.piece);
    if (!download.reading||piece.error||!piece.buffer||index<0||index>=metadata.num_pieces()||
        piece.size!=metadata.files().piece_size(piece.piece)) {
      finishSubscriptionDownload(std::unexpected(Error{Code::Io,"Cannot read the verified I2P subscription payload"}));
      return;
    }
    std::copy_n(piece.buffer.get(),piece.size,download.bytes.data()+static_cast<std::size_t>(index)*metadata.piece_length());
    download.pieces[static_cast<std::size_t>(index)]=true;
    if (!std::ranges::all_of(download.pieces,[](bool value){return value;})) { return; }
    if (!search::validateSubscriptionSeedPayload(download.bytes,download.update.presence,download.update.publicKey,true)) {
      finishSubscriptionDownload(std::unexpected(Error{Code::InvalidFormat,"The received I2P subscription signature is invalid"}));
      return;
    }
    finishSubscriptionDownload(std::move(download.bytes));
  }
  bool startDiscovery(Subscription& seed) {
    if (!bridge || !discoveryOptions) { return false; }
    const auto key = seed.data().source->hash().hex();
    const auto id = seed.data().admission;
    return bridge->discoverSubscription(seed.data().source, id, [this,key,id](const Hash& peer, std::uint64_t op) {
      const auto found = subscriptions.find(key);
      if (found==subscriptions.end() || found->second.data().admission!=id || found->second.retiring
          || !subscriptionCurrent(found->second,source->lease()->sourceRevision())) { return false; }
      auto& target = found->second;
      if (!target.ready) {
        // Keep announcing the dormant source while the SDK seed slots are in use.
        // The next discovery turn can retry a peer when capacity is available.
        if (!target.installed && !hasSubscriptionCapacity()) { return true; }
        if (!target.demanded) {
          target.demanded = true;
          memberInstallRequested = true;
          requestStartup();
        }
        return false;
      }
      if (!deliverDiscovered(target,peer,op)) { return false; }
      target.lastActivity=std::chrono::steady_clock::now();
      return true;
    });
  }
  bool install(Subscription& seed, std::uint64_t revision) {
    if (!session || !subscriptionCurrent(seed, revision)) { return false; }
    const auto tuple = registration(seed);
    if (!seed.registered && lt::biglybtc_admit_private_i2p_torrent_v1(session.get(), &tuple) != 0) { return false; }
    seed.registered = true;
    lt::error_code error;
    seed.handle = session->add_torrent(seed.data().torrent.parameters, error);
    seed.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    seed.installed = !error && seed.handle.is_valid();
    return seed.installed;
  }
  void removeSubscriptionTorrent(Subscription& seed) {
    if (seed.retiring) { return; }
    seed.retiring = true;
    seed.ready = false;
    seed.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    if (seed.handle.is_valid()) {
      seed.handle.pause();
      session->remove_torrent(seed.handle);
    } else { seed.removed = true; }
  }
  void retireSubscription(Subscription& seed) {
    seed.data().source->revoke();
    if (seed.discovering && bridge) { bridge->cancelDiscovery(seed.data().admission); }
    seed.discovering=false;
    removeSubscriptionTorrent(seed);
  }
  void observeSubscriptionActivity(Subscription& seed,const lt::torrent_status& status,
      std::chrono::steady_clock::time_point now) {
    if (seed.connectedPeers!=status.num_peers) {
      seed.connectedPeers=status.num_peers;
      seed.lastActivity=now;
    }
    const bool potential=status.connect_candidates>0 || status.num_connections>0;
    const auto idleLimit=potential ? std::chrono::minutes(15) : std::chrono::minutes(5);
    if (!seed.connectedPeers && now-seed.lastActivity>=idleLimit) { removeSubscriptionTorrent(seed); }
  }
  void advanceSubscriptions(std::optional<std::uint64_t> permitRevision = {}) {
    if (!session) { return; }
    const auto now = std::chrono::steady_clock::now();
    for (auto at = subscriptions.begin(); at != subscriptions.end();) {
      auto& seed = at->second;
      if (seed.data().source->revoked()) { retireSubscription(seed); }
      if (!seed.retiring && seed.installed) {
        const auto status = seed.handle.is_valid() ? seed.handle.status({}) : lt::torrent_status{};
        if (!seed.handle.is_valid() || status.errc || (!seed.ready && now >= seed.deadline)
            || (seed.checked && (status.flags & lt::torrent_flags::paused) && !status.is_seeding)) {
          retireSubscription(seed);
        } else if (!seed.ready && seed.checked && status.is_seeding && (status.flags & lt::torrent_flags::paused)) {
          if (!permitRevision) { requestStartup(); }
          else if (subscriptionCurrent(seed, *permitRevision)) {
            seed.ready = true;
            seed.lastActivity=now;
            seed.handle.resume();
          }
        } else if (seed.ready) { observeSubscriptionActivity(seed,status,now); }
      }
      if (seed.retiring && seed.removed && !seed.handle.is_valid()) {
        if (!seed.barrier && seed.registered) {
          (void)session->get_settings();
          seed.barrier = true;
        } else if (seed.data().source->revoked()) {
          const auto tuple = registration(seed);
          if (seed.registered && lt::biglybtc_revoke_private_i2p_torrent_v1(session.get(), &tuple) != 0) {
            close("Cannot withdraw the I2P subscription registration");
            return;
          }
          at = subscriptions.erase(at);
          if (!members.empty() || !magnets.empty()) {
            memberInstallRequested = true;
            requestStartup();
          }
          continue;
        } else {
          // The source and admission are still current. The checked payload
          // can be opened again under this same route on another DHT demand.
          seed.resetTorrent();
        }
      }
      if (seed.retiring && now >= seed.deadline) {
        close("I2P subscription storage removal did not settle");
        return;
      }
      ++at;
    }
  }
  bool install(Magnet& m,bool configured=false){
#if defined(BIGLYBTC_I2P_DISCOVERY_ABI_V1)
    if(!session||m.installed||m.cancel||m.released.load()||!m.data().source->prepared()||std::chrono::steady_clock::now()>=m.data().expires){return false;}
    const auto tuple=registration(m);if(!configured&&lt::biglybtc_admit_private_i2p_torrent_v1(session.get(),&tuple)!=0){return false;}m.registered=true;
    const auto authorize=reinterpret_cast<decltype(&lt::biglybtc_authorize_i2p_metadata_v1)>(platform::loadedSymbol(pin.get(),"biglybtc_authorize_i2p_metadata_v1"));
    if(!authorize||authorize(session.get(),&tuple)!=0){return false;}
    m.data().lifetime->generation.store(generation);m.data().lifetime->route.store(m.data().admission);m.data().lifetime->authorized.store(true);
    lt::error_code error;auto params=m.data().parameters;params.flags|=lt::torrent_flags::paused|lt::torrent_flags::duplicate_is_error;
    if(m.data().tagTarget){params.flags&=~lt::torrent_flags::auto_managed;if(!m.data().tagTarget->install(params)){return false;}}
    m.handle=session->add_torrent(std::move(params),error);if(error||!m.handle.is_valid()){return false;}
    m.installed=true;m.data().lifetime->attached.store(true);
    if(m.data().tagTarget&&(!m.data().tagTarget->boundTo(m.handle)||!m.data().source->prepared())){return false;}
    m.handle.resume();return true;
#else
    (void)m;(void)configured;return false;
#endif
  }
  void retireMagnet(Magnet& m){
    if(m.removing||m.ready){return;}m.data().lifetime->retiring.store(true,std::memory_order_release);m.data().lifetime->authorized.store(false);if(bridge){bridge->cancelDiscovery(m.data().admission);}
    m.end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    if(m.handle.is_valid()){m.handle.pause();session->remove_torrent(m.handle);m.removing=true;}
    else{
      if(m.registered){const auto tuple=registration(m);if(!session||lt::biglybtc_revoke_private_i2p_torrent_v1(session.get(),&tuple)!=0){m.failure=Error{Code::Conflict,"Unused metadata registration withdrawal failed"};return;}m.registered=false;}
      m.data().lifetime->retained.store(false);m.ready=true;
    }
  }
  void advanceMagnets(){
    if(!session){return;}const auto now=std::chrono::steady_clock::now();
    for(auto& [key,slot]:magnets){(void)key;auto& m=*slot;if(m.ready||m.released.load()){continue;}
      if(!m.removing&&!m.installed&&now>=m.data().expires){m.failure=Error{Code::Io,"I2P metadata preparation deadline expired; original intent retained"};retireMagnet(m);continue;}
      if(!m.removing&&(m.cancel||m.failure)){retireMagnet(m);continue;}
      if(m.installed&&!m.removing){
        if(now>=m.data().expires){m.failure=Error{Code::Io,"I2P metadata deadline expired; original intent retained"};retireMagnet(m);continue;}
        const auto actual=m.handle.torrent_file();if(actual&&actual->is_valid()){
          const auto hashes=actual->info_hashes();const auto& wanted=m.data().intent.requested;
          if((wanted.has_v1()&&(!hashes.has_v1()||hashes.v1!=wanted.v1))||(wanted.has_v2()&&(!hashes.has_v2()||hashes.v2!=wanted.v2))){m.failure=Error{Code::Conflict,"Received metadata differs from requested full hashes"};retireMagnet(m);continue;}
          lt::add_torrent_params params;params.ti=std::make_shared<lt::torrent_info>(*actual);const auto bytes=lt::write_torrent_file_buf(params,{});
          if(bytes.empty()||bytes.size()>32*1024*1024){m.failure=Error{Code::InvalidFormat,"Actual I2P metadata exceeds bound"};}else{m.bytes.assign(bytes.data(),bytes.size());}retireMagnet(m);
        }
      }
      if(m.removing){
        if(now>=m.end){m.failure=Error{Code::Io,"I2P metadata removal fence expired; original intent retained"};close();return;}
        if(m.removed&&!m.handle.is_valid()){
          if(!m.barrier){(void)session->get_settings();m.barrier=true;continue;}
          const auto tuple=registration(m);if(m.registered&&lt::biglybtc_revoke_private_i2p_torrent_v1(session.get(),&tuple)!=0){m.failure=Error{Code::Conflict,"I2P metadata registration withdrawal failed"};close();return;}
          m.registered=false;m.data().lifetime->retained.store(false,std::memory_order_release);m.ready=true;
        }
      }
    }
  }
  void observeTagState(Member& member, const lt::torrent_status& actual,
      const engine::detail::ManagedPayloadCounters::Sample& counts) {
    if (!source->tagManagerEnabled()) { return; }
    using Phase = engine::ManagedPhase;
    engine::ManagedDownloadStatus status;
    status.checked = member.counters->checked.load() && !actual.errc;
    status.selectedComplete = actual.is_finished;
    status.received = counts.received;
    status.sent = counts.sent;
    status.downloadRate = actual.download_payload_rate;
    status.uploadRate = actual.upload_payload_rate;
    if (member.retired.requested) { status.phase = Phase::Stopping; }
    else if (actual.errc) { status.phase = Phase::Failed; }
    else if (actual.state == lt::torrent_status::checking_files
        || actual.state == lt::torrent_status::checking_resume_data) { status.phase = Phase::Checking; }
    else if (actual.flags & lt::torrent_flags::paused) { status.phase = Phase::Queued; }
    else { status.phase = actual.is_finished ? Phase::Seeding : Phase::Downloading; }
    member.tagState.observe(status, engine::detail::ManagedTagState::Clock::now(),
                            QDateTime::currentMSecsSinceEpoch());
  }
  engine::detail::ManagedPayloadCounters::Sample sample(Member& m,lt::torrent_status& actual,bool pieces=false){
    for(int n=0;n<4;++n){const auto epoch=m.counters->payload.epoch();if(!epoch){break;}actual=m.handle.status(pieces?lt::torrent_handle::query_pieces:lt::status_flags_t{});
      std::optional<engine::detail::ManagedPayloadCounters::Bad> bad;
      if(actual.total_redundant_bytes>=0&&actual.total_failed_bytes>=0&&actual.total_redundant_bytes<std::numeric_limits<std::int64_t>::max()&&actual.total_failed_bytes<std::numeric_limits<std::int64_t>::max()){bad={{actual.total_redundant_bytes,actual.total_failed_bytes}};}
      std::optional<engine::detail::ManagedPayloadCounters::Protocol> protocol;
      if(actual.total_payload_download>=0&&actual.total_payload_upload>=0&&actual.total_download>=actual.total_payload_download&&actual.total_upload>=actual.total_payload_upload){protocol={{actual.total_download-actual.total_payload_download,actual.total_upload-actual.total_payload_upload}};}
      auto v=m.counters->payload.combine(*epoch,actual.total_payload_download,actual.total_payload_upload,protocol,bad);
      if (v.state == engine::detail::ManagedPayloadCounters::Read::Accepted) { observeTagState(m, actual, v); }
      if(v.state!=engine::detail::ManagedPayloadCounters::Read::Retry){return v;}
    }return {};
  }
  bool install(Member& m,bool configured=false){
    if(!session||m.installed||m.retired.requested||!m.data().source->prepared()){return false;}
    const auto tuple=registration(m);if(!configured&&lt::biglybtc_admit_private_i2p_torrent_v1(session.get(),&tuple)!=0){return false;}m.registered=true;
    auto params=m.data().storage.parameters();if(params.total_downloaded||params.total_uploaded){return false;}
    if(auto files=m.data().storage.mappedFiles()){
      const auto path=(platform::pathFromUtf8(params.save_path)/".BiglyBTC-original-storage"/(m.data().hash.hex()+"-owned-i2p-"+std::to_string(m.data().admission))).lexically_normal();
      if(!disks->registerOriginalPath(path,std::move(files))){return false;}params.save_path=path.string();
    }
    m.queue=std::make_shared<engine::detail::ManagedQueueObservations>(m.data().storage.identity().selected==storage::OriginalSelectedHash::V1?1u:2u);
    const auto& metadata=m.data().storage.queueMetadata();m.queue->trackerOrder(metadata.trackerOrder);m.queue->restore(metadata.aggregateScrapeCache?std::optional<std::string_view>{*metadata.aggregateScrapeCache}:std::nullopt,metadata.legacyScrapeCache);
    const auto ledger=m.counters;const auto queue=m.queue;const auto held=pin;
    params.extensions.push_back([ledger,queue,held](const lt::torrent_handle& h,lt::client_data_t)->std::shared_ptr<lt::torrent_plugin>{
      std::shared_ptr<lt::BiglybtcQueueObserverV1> sink=std::make_shared<QueueSink>(queue,held);
      if(lt::biglybtc_attach_queue_observer_v1(&h,&sink)==0){queue->installed();}
      return std::make_shared<detail::RetirementCounterPlugin>(h,ledger,held);
    });
    // Let the SDK check immediately and perform its own ready-state pause.
    // A post-add resume could overwrite an already completed ready pause.
    params.flags|=lt::torrent_flags::stop_when_ready|lt::torrent_flags::duplicate_is_error;params.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed);
    if(m.data().tagTarget&&!m.data().tagTarget->installForChecking(params)){return false;}
    lt::error_code ec;m.handle=session->add_torrent(params,ec);if(ec||!m.handle.is_valid()){return false;}
    if(m.data().tagTarget&&!m.data().tagTarget->boundTo(m.handle)){m.handle.pause();return false;}
    m.installed=true;m.pauseSettled=false;m.pausePending=false;return true; // No resume until a genuine checked/paused queue fence.
  }
  bool installSession(void* opaque){
    if(phase!=Phase::Starting){return false;}
    auto firstLive=std::ranges::find_if(members,[](const auto& entry){return !entry.second->retired.requested;});
    // A queued bridge-install turn may race a genuine never-installed Stop.
    // Keep the shared endpoint unconfigured until another live member exists.
    auto firstMagnet=std::ranges::find_if(magnets,[](const auto& entry){return !entry.second->cancel&&!entry.second->released.load();});
    const auto revision = source->lease()->sourceRevision();
    const auto firstSeed = std::ranges::find_if(subscriptions, [&](const auto& entry) {
      return subscriptionCurrent(entry.second, revision);
    });
    if(firstLive==members.end()&&firstMagnet==magnets.end()&&firstSeed==subscriptions.end()){return true;}
    auto& policy=*static_cast<lt::BiglybtcPrivateI2pOwnerPolicyV1*>(opaque);generation=policy.generation;
    const auto tuple=firstLive!=members.end()?registration(*firstLive->second)
        :firstMagnet!=magnets.end()?registration(*firstMagnet->second):registration(firstSeed->second);
    policy.admission=tuple.admission;policy.hashes=tuple.hashes;policy.torrent_v1=tuple.torrent_v1;policy.torrent_v2=tuple.torrent_v2;
    if(lt::biglybtc_configure_private_i2p_owner_v1(session.get(),&policy)!=0){return false;}
    lt::settings_pack network;network.set_str(lt::settings_pack::i2p_hostname,"127.0.0.1");network.set_int(lt::settings_pack::i2p_port,policy.bridge_port);session->apply_settings(network);
    // All I2P peers are subject to the shared global class even though the
    // authenticated private bridge itself is a loopback transport.
    lt::ip_filter filter;filter.add_rule(lt::address_v4::any(),lt::address_v4::broadcast(),1u<<static_cast<unsigned>(lt::session::global_peer_class_id));
    filter.add_rule(lt::address_v6::any(),lt::make_address_v6("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),1u<<static_cast<unsigned>(lt::session::global_peer_class_id));session->set_peer_class_filter(filter);
    if(externalQueue&&externalRates){lt::settings_pack limits;
      limits.set_int(lt::settings_pack::download_rate_limit,static_cast<int>(externalRates->download.bytesPerSecond));
      limits.set_int(lt::settings_pack::upload_rate_limit,static_cast<int>(externalRates->upload.bytesPerSecond));session->apply_settings(limits);
    }
    bool firstRecord=true;for(auto& [key,m]:members){(void)key;if(m->retired.requested){continue;}if(!install(*m,firstRecord)){return false;}firstRecord=false;}
    for(auto& [key,m]:magnets){(void)key;if(m->cancel||m->released.load()){continue;}if(!install(*m,firstRecord)){return false;}firstRecord=false;}
    // A subscription-only owner uses the first signed hash to configure the
    // measured endpoint. SDK disk checking starts when DHT reports a peer.
    if (firstRecord) { firstSeed->second.registered = true; }
    phase=Phase::Active;
    if (discoveryOptions&&!bridge->beginDiscovery(*discoveryOptions,contactExchange,&options.router,{},
        std::move(mixedPublicationOptions))) { return false; }
    for(auto& [key,m]:members){(void)key;startDiscovery(*m);}for(auto& [key,m]:magnets){(void)key;startDiscovery(*m);}
    for (auto& [key, seed] : subscriptions) {
      (void)key;
      if (subscriptionCurrent(seed,revision)) { seed.discovering=startDiscovery(seed); }
    }
    timer.start(250);return true;
  }
  void alerts(){
    if(!session){return;}std::vector<lt::alert*> batch;session->pop_alerts(&batch);if(batch.size()>4096){failure="I2P alert batch exceeded4096";close();return;}
    for(const auto* alert:batch){
      if(lt::alert_cast<lt::alerts_dropped_alert>(alert)||lt::alert_cast<lt::session_error_alert>(alert)){failure="Shared I2P provider lost an essential observation";close();return;}
      const auto* torrent=dynamic_cast<const lt::torrent_alert*>(alert);if(!torrent){continue;}
      if (subscriptionDownload&&!subscriptionDownload->result&&subscriptionDownload->handle==torrent->handle) {
        if (const auto* piece=lt::alert_cast<lt::read_piece_alert>(alert);piece) { subscriptionPiece(*piece); }
        else if (lt::alert_cast<lt::file_error_alert>(alert)||lt::alert_cast<lt::torrent_error_alert>(alert)||
                 lt::alert_cast<lt::torrent_removed_alert>(alert)) {
          finishSubscriptionDownload(std::unexpected(Error{Code::Io,"I2P subscription transfer failed"}));
        }
        if (!session) { return; }
        continue;
      }
      const auto subscription = std::ranges::find_if(subscriptions, [&](const auto& entry) {
        return entry.second.registered && entry.second.handle == torrent->handle;
      });
      if (subscription != subscriptions.end()) {
        auto& seed = subscription->second;
        if (lt::alert_cast<lt::torrent_checked_alert>(alert)) { seed.checked = true; }
        if (lt::alert_cast<lt::torrent_removed_alert>(alert)) {
          seed.removed=true;
          if (!seed.retiring) { retireSubscription(seed); }
        }
        if (lt::alert_cast<lt::file_error_alert>(alert) || lt::alert_cast<lt::torrent_error_alert>(alert)) {
          retireSubscription(seed);
        }
        continue;
      }
      Magnet* pending=nullptr;for(auto& [key,m]:magnets){(void)key;if(m->handle==torrent->handle){pending=m.get();break;}}
      if(pending){
        if(pending->ready){continue;}
        if(lt::alert_cast<lt::torrent_removed_alert>(alert)){if(pending->removing){pending->removed=true;}else{pending->failure=Error{Code::Conflict,"Metadata handle disappeared before retirement"};}}
        if(lt::alert_cast<lt::file_error_alert>(alert)||lt::alert_cast<lt::torrent_error_alert>(alert)){pending->failure=Error{Code::Io,"Metadata provider/storage observation failed"};}
        continue;
      }
      Member* target=nullptr;for(auto& [key,m]:members){(void)key;if(m->handle==torrent->handle){target=m.get();break;}}if(!target){continue;}auto& m=*target;if(m.retired.readyForJoin){continue;}
      if(lt::alert_cast<lt::tracker_announce_alert>(alert)){++m.alerts.announces;}
      if(lt::alert_cast<lt::tracker_reply_alert>(alert)){++m.alerts.trackerReplies;}
      if(lt::alert_cast<lt::tracker_error_alert>(alert)){++m.alerts.trackerErrors;}
      if(lt::alert_cast<lt::scrape_reply_alert>(alert)||lt::alert_cast<lt::scrape_failed_alert>(alert)){m.scrapePending=false;}
      if(lt::alert_cast<lt::file_error_alert>(alert)||lt::alert_cast<lt::torrent_error_alert>(alert)||lt::alert_cast<lt::save_resume_data_failed_alert>(alert)){fail(m,{Code::Io,"Target I2P provider/storage observation failed; recheck retained"});continue;}
      if(lt::alert_cast<lt::torrent_checked_alert>(alert)){m.counters->checked.store(true,std::memory_order_release);}
      if (lt::alert_cast<lt::cache_flushed_alert>(alert)) { m.backupFlushPending=false; }
      if(lt::alert_cast<lt::torrent_paused_alert>(alert)){m.pauseSettled=true;m.pausePending=false;}
      if(!m.retired.requested||m.retired.failure){continue;}
      if(std::chrono::steady_clock::now()>=m.deadline){fail(m,{Code::Io,"Target I2P retirement deadline expired; recheck retained"});continue;}
      if(lt::alert_cast<lt::torrent_paused_alert>(alert)){
        m.pauseSeen=true;if(!m.saveRequested){m.saveRequested=true;m.handle.save_resume_data(lt::torrent_handle::flush_disk_cache);}
      }
      if(const auto* saved=lt::alert_cast<lt::save_resume_data_alert>(alert)){
        lt::torrent_status actual;auto count=sample(m,actual,true);const int n=m.data().storage.parameters().ti->num_pieces();
        if(!m.pauseSeen||!m.saveRequested||m.removing||actual.errc||!(actual.flags&lt::torrent_flags::paused)||count.state!=engine::detail::ManagedPayloadCounters::Read::Accepted){fail(m,{Code::Io,"Exact target I2P flushed pieces/counters unavailable; recheck retained"});continue;}
        const bool checked=m.counters->checked.load();
        if(checked&&(saved->params.have_pieces.size()!=n||actual.pieces.size()!=n)){fail(m,{Code::Io,"Exact checked I2P bitfield unavailable; recheck retained"});continue;}
        bool exact=true;m.pieces.resize(checked?static_cast<std::size_t>(n):0);for(int p=0;checked&&p<n;++p){const auto at=lt::piece_index_t{p};exact&=saved->params.have_pieces[at]==actual.pieces[at];m.pieces[static_cast<std::size_t>(p)]=actual.pieces[at]?1:0;}
        if(!exact){fail(m,{Code::Conflict,"Target I2P flushed-piece snapshot changed"});continue;}
        if(!m.counters->checked.load()){
          lt::BiglybtcTrafficCountersV3 raw{};
          if(lt::biglybtc_read_traffic_counters_v3(&m.handle,&raw,sizeof(raw))!=0||raw.traffic.payload.files_checked||raw.traffic.payload.received||raw.traffic.payload.sent||count.received||count.sent){fail(m,{Code::Conflict,"Unchecked I2P target has unaccounted traffic; recheck retained"});continue;}
          m.unchecked=true;
        }
        m.finalCounters=count;m.complete=actual.is_seeding;m.removing=true;session->remove_torrent(m.handle);
      }
      if(lt::alert_cast<lt::torrent_removed_alert>(alert)){if(!m.removing){fail(m,{Code::Conflict,"Target I2P disappeared before flush"});}else{m.removed=true;}}
    }
    advanceMagnets();if(!session){return;}
    advanceSubscriptions();if(!session){return;}
    advanceSubscriptionDownload();if(!session){return;}
    for(auto& [key,m]:members){(void)key;if(!m->retired.requested||m->retired.failure||m->retired.readyForJoin){continue;}
      if(std::chrono::steady_clock::now()>=m->deadline){fail(*m,{Code::Io,"Target I2P retirement deadline expired before its final fence"});continue;}
      if(m->removed&&m->removing&&!m->handle.is_valid()){
        if(!m->weakExpiryObserved){
          // Cross the real executor after weak expiry, then scan another whole
          // essential batch before freezing the member for external IO.
          (void)session->get_settings();if(std::chrono::steady_clock::now()>=m->deadline){fail(*m,{Code::Io,"Target I2P retirement deadline expired at executor fence"});continue;}m->weakExpiryObserved=true;continue;
        }
        const auto tuple=registration(*m);if(m->registered&&lt::biglybtc_revoke_private_i2p_torrent_v1(session.get(),&tuple)!=0){fail(*m,{Code::Conflict,"Target I2P registration withdrawal failed"});continue;}
        if(m->queue){m->finalDurations=m->queue->settledDurations();}m->registered=false;if(std::chrono::steady_clock::now()>=m->deadline){fail(*m,{Code::Io,"Target I2P retirement deadline expired before readiness"});continue;}m->retired.readyForJoin=true;m->retired.joined=true; // Exact matching removed + weak expiration: the same per-storage fence used by Public.
      }else if(std::chrono::steady_clock::now()>=m->deadline){fail(*m,{Code::Io,"Target I2P storage retirement timed out; recheck retained"});}
    }
  }
  engine::ExternalQueueSnapshot collectQueue(bool maintenance,bool positionProof=false,
      const core::SavedTagQueueRules* tagRules=nullptr){
    const auto now=nowMillis();std::vector<core::QueueTorrentFacts> facts;std::map<std::uint64_t,Member*> targets;
    core::QueueContext context;context.nowMillis=now;context.ownerStartedMillis=started;std::int64_t down=0,up=0;bool any=false,onlySeeds=true;
    for(auto& [key,m]:members){if(!m->installed||m->retired.requested||!m->failure.empty()||!m->handle.is_valid()){continue;}
      lt::torrent_status s;const auto counters=sample(*m,s);if(counters.state!=engine::detail::ManagedPayloadCounters::Read::Accepted){s=m->handle.status();}
      const bool checked=m->counters->checked.load()&&!s.errc;
      if(maintenance&&checked&&m->queued&&!m->scrapePending&&now>=m->nextScrape){m->handle.scrape_tracker();m->scrapePending=true;m->nextScrape=now+10000;}
      core::QueueTorrentFacts f;const auto& saved=m->data().storage.queueMetadata();
      storage::applySavedTagQueueRules(tagRules, key, f);
      f.token=m->data().admission;f.position=static_cast<std::uint32_t>(std::clamp<std::int64_t>(saved.position,1,std::numeric_limits<std::int32_t>::max()));
      f.complete=s.is_finished;
      const bool held=externalQueue?bool(s.flags&lt::torrent_flags::paused):m->queued;
      f.state=!checked?core::QueueTorrentState::Checking:held?core::QueueTorrentState::Queued:f.complete?core::QueueTorrentState::Seeding:core::QueueTorrentState::Downloading;
      if(checked){if(m->observedComplete&&!*m->observedComplete&&f.complete){m->justCompleted=true;if(!externalQueue&&m->forceStart&&!queuePolicy.retainForceStartOnComplete){m->clearCompletedForce=true;}}m->observedComplete=f.complete;}
      f.forceStart=m->forceStart;f.newlyCompleted=m->justCompleted;f.minimumShareRatio=saved.minimumShareRatio;f.maximumShareRatio=saved.maximumShareRatio;f.activity=m->activity;
      f.connectedLeechers=std::max(0,s.num_peers-s.num_seeds);f.downloadBytesPerSecond=s.download_payload_rate;f.uploadBytesPerSecond=s.upload_payload_rate;
      f.sizeBytes=s.total_wanted;f.remainingBytes=std::max<std::int64_t>(0,s.total_wanted-s.total_wanted_done);f.idleUploadSourceKnown=false;
      if(m->queue){m->queueFacts=m->queue->facts(now);const auto& observed=m->queueFacts;f.runningMillis=observed.runningMillis;f.seedingMillis=observed.seedingMillis;
        if(observed.durations){f.secondsDownloading=core::sourceQueueSum(saved.secondsDownloading,observed.durations->downloading);f.secondsSeeding=core::sourceQueueSum(saved.secondsSeeding,observed.durations->seeding);}
        if(observed.swarm){const auto& swarm=*observed.swarm;f.swarm=core::QueueSwarm{swarm.seeds,swarm.peers,swarm.success,swarm.startupSuccess};}}
      if(counters.state==engine::detail::ManagedPayloadCounters::Read::Accepted&&counters.badPrefix&&!saved.badPayloadCountersLost&&counters.received<=std::numeric_limits<std::int64_t>::max()-saved.downloaded&&counters.sent<=std::numeric_limits<std::int64_t>::max()-saved.uploaded&&counters.badPrefix->discarded<=std::numeric_limits<std::int64_t>::max()-saved.discarded&&counters.badPrefix->hashFailed<=std::numeric_limits<std::int64_t>::max()-saved.hashFailed){f.shareRatio=core::sourceQueueShareRatio(saved.downloaded+counters.received,saved.uploaded+counters.sent,saved.discarded+counters.badPrefix->discarded,saved.hashFailed+counters.badPrefix->hashFailed);}
      if(qEnvironmentVariableIsSet("BIGLYBTC_I2P_DIAGNOSTICS")&&now>=m->nextQueueDiagnostic){
        qInfo().noquote().nospace()<<"I2P_QUEUE_FACT admission="<<m->data().admission<<" sample="<<static_cast<int>(counters.state)<<" bad_prefix="<<counters.badPrefix.has_value()<<" bad_tail="<<counters.bad.has_value()<<" saved_bad_lost="<<saved.badPayloadCountersLost<<" share_known="<<f.shareRatio.has_value()<<" share="<<f.shareRatio.value_or(0)<<" checked="<<checked<<" complete="<<f.complete<<" state="<<static_cast<int>(f.state);
      }
      down+=s.download_payload_rate;up+=s.upload_payload_rate;any=true;onlySeeds&=f.complete;targets.emplace(f.token,m.get());facts.push_back(std::move(f));
    }
    const bool pendingMetadata=std::ranges::any_of(magnets,[](const auto& entry){return !entry.second->released.load()&&!entry.second->ready&&!entry.second->cancel;});
    context.seedingOnly=any&&onlySeeds&&!pendingMetadata;context.uploadBytesPerSecond=up;context.downloadBytesPerSecond=down;context.downloadLimitBytesPerSecond=rates.download;context.uploadLimitBytesPerSecond=context.seedingOnly&&rates.seedingOnly?rates.seedingUpload:rates.upload;
    if(externalQueue&&externalRates){context.downloadLimitBytesPerSecond=externalRates->download.bytesPerSecond;context.uploadLimitBytesPerSecond=externalRates->upload.bytesPerSecond;}
    engine::ExternalQueueSnapshot snapshot;snapshot.context=context;snapshot.pendingMetadata=pendingMetadata;snapshot.rows.reserve(facts.size());
    for(auto& fact:facts){auto& m=*targets.at(fact.token);
      if(m.livePosition){fact.position=static_cast<std::uint32_t>(std::clamp<std::int64_t>(*m.livePosition,1,std::numeric_limits<std::int32_t>::max()));}
      engine::ExternalQueueRow row;row.binding={m.data().hash,m.data().admission};row.facts=std::move(fact);row.checked=row.facts.state!=core::QueueTorrentState::Checking;
      if(positionProof){const auto policy=m.data().storage.capturedFilePolicy();
        row.position={m.data().hash,m.data().storage.identity(),m.data().storage.capturedDownload(),
            std::vector<compat::FilePolicy>(policy.begin(),policy.end()),m.observedComplete.value_or(false),m.data().storage.capturedQueueMappingScope()};
        if(m.justCompleted){row.transition=storage::LibraryGroupTransition{m.data().hash,true};}
      }snapshot.rows.push_back(std::move(row));
    }return snapshot;
  }
  void schedule(){
    if(sourcePaused||phase!=Phase::Active||!session){return;}alerts();if(!session){return;}
    std::shared_ptr<const core::SavedTagQueueRules> tagRules;
    if (!externalQueue && !members.empty()) {
      auto current=source->queueTagRules();
      if (!current) { requestStartup();return; }
      tagRules=std::move(*current);
    }
    auto snapshot=collectQueue(true,false,tagRules.get());if(externalQueue){return;}
    auto context=snapshot.context;const auto now=context.nowMillis;
    std::vector<core::QueueTorrentFacts> facts;std::map<std::uint64_t,Member*> targets;
    for(auto& row:snapshot.rows){targets.emplace(row.binding.admission,find(row.binding.hash));facts.push_back(std::move(row.facts));}
    lt::settings_pack limits;limits.set_int(lt::settings_pack::download_rate_limit,rates.download);limits.set_int(lt::settings_pack::upload_rate_limit,static_cast<int>(context.uploadLimitBytesPerSecond));session->apply_settings(limits);
    std::vector<std::size_t> order;for(std::size_t i=0;i<facts.size();++i){order.push_back(i);}
    std::ranges::sort(order,[&](auto a,auto b){return targets.at(facts[a].token)->data().inventoryIndex<targets.at(facts[b].token)->data().inventoryIndex;});
    std::vector<core::QueuePositionInput> positions;for(auto i:order){const auto* m=targets.at(facts[i].token);positions.push_back({facts[i].token,m->livePosition,facts[i].complete,true});}
    const auto normalized=core::normalizeQueuePositions(positions);if(!normalized){failure="Original I2P queue positions are ambiguous";close();return;}
    for(std::size_t i=0;i<order.size();++i){facts[order[i]].position=(*normalized)[i];}
    auto plan=core::evaluateQueue(queuePolicy,facts,context);if(!plan){failure="Original I2P queue facts are inconsistent";close();return;}queueUnavailable=plan->unavailableInputs;
    if(qEnvironmentVariableIsSet("BIGLYBTC_I2P_DIAGNOSTICS")){for(const auto& decision:plan->decisions){auto* m=targets.at(decision.token);if(now<m->nextQueueDiagnostic){continue;}m->nextQueueDiagnostic=now+1000;const auto actual=m->handle.status();
      qInfo().noquote().nospace()<<"I2P_QUEUE_DECISION admission="<<m->data().admission<<" action="<<static_cast<int>(decision.action)<<" reason="<<static_cast<int>(decision.reason)<<" first_priority="<<decision.firstPriority<<" fp_known="<<decision.firstPriorityKnown<<" rank="<<decision.seedingRank<<" activity_known="<<decision.activityKnown<<" queued="<<m->queued<<" paused="<<bool(actual.flags&lt::torrent_flags::paused)<<" pause_settled="<<m->pauseSettled<<" pause_pending="<<m->pausePending<<" unavailable="<<plan->unavailableInputs;
    }}
    for(auto action:{core::QueueAction::Queue,core::QueueAction::Run}){for(const auto& decision:plan->decisions){auto* m=targets.at(decision.token);m->activity=decision.activity;if(decision.moveNewSeedToTop){m->completionPositionUnavailable=true;}m->justCompleted=false;if(decision.action!=action||!m->counters->checked.load()){continue;}
      const auto actual=m->handle.status();const bool paused=bool(actual.flags&lt::torrent_flags::paused);
      if(action==core::QueueAction::Queue){
        m->queued=true;
        if(!paused&&!m->pausePending){m->pauseSettled=false;m->pausePending=true;m->handle.pause();}
      }else if(m->queued){
        if(m->data().tagTarget&&!m->data().source->prepared()){fail(*m,{Code::Conflict,"Private tag source is no longer current before Run"});continue;}
        // A queued intent is not proof of an actual settled pause. Request it
        // once if necessary; a historical alert cannot authorize this resume.
        if(!paused&&!m->pausePending){m->pauseSettled=false;m->pausePending=true;m->handle.pause();}
        else if(paused&&m->pauseSettled&&!m->pausePending){
          m->handle.unset_flags(lt::torrent_flags::auto_managed|lt::torrent_flags::stop_when_ready);
          m->pauseSettled=false;m->handle.resume();m->queued=false;
        }
      }
    }}
  }
  void requestStartup(){if(!sourcePaused&&(phase==Phase::Starting||phase==Phase::Active)&&permit&&permit->phase()==SourcePermit::Phase::Idle){permit->request();}}
  void startup(){
    if(sourcePaused||(phase!=Phase::Starting&&phase!=Phase::Active)||!permit){return;}auto revision=permit->take();if(!revision){if(permit->phase()==SourcePermit::Phase::Failed||permit->phase()==SourcePermit::Phase::Stopped){close("I2P owner source validation failed");}return;}
    struct Ack{SourcePermit& p;~Ack(){p.acknowledge();}} ack{*permit};
    for (auto& [key, seed] : subscriptions) {
      (void)key;
      if (seed.data().source->revoked()) { retireSubscription(seed); }
    }
    if(!source->revisionCurrent(*revision)||!channel->resumeAuthorizedEffects()){close("I2P owner effect authority refused");return;}
    if(startRequested){startRequested=false;const auto wall=QDateTime::currentMSecsSinceEpoch();if(!identity||wall<=0||!channel->createLeaseSession(std::move(*identity),options.session,static_cast<std::uint64_t>(wall))){close();return;}identity.reset();}
    if(phase==Phase::Active&&memberInstallRequested){
      memberInstallRequested=false;
      const bool waitForCapacity = !subscriptions.empty() && privateAdmissionCount()>Bridge::MaximumDiscoveryJobs;
      try{for(auto& [key,m]:members){if(waitForCapacity||subscriptions.contains(key)||m->installed||m->retired.requested||!m->failure.empty()){continue;}if(!source->revisionCurrent(*revision)||!install(*m)){fail(*m,{Code::Io,"Cannot install measured I2P member under its fresh source permit"});}else{startDiscovery(*m);}}
        for(auto& [key,m]:magnets){if(waitForCapacity||subscriptions.contains(key)||m->installed||m->cancel||m->released.load()){continue;}if(!source->revisionCurrent(*revision)||!install(*m)){m->failure=Error{Code::Io,"Cannot install authorized I2P metadata member"};retireMagnet(*m);}else{startDiscovery(*m);}}
        for (auto& [key, seed] : subscriptions) {
          (void)key;
          if (seed.retiring) { continue; }
          if (!subscriptionCurrent(seed,*revision)) { retireSubscription(seed); continue; }
          if (!seed.discovering) { seed.discovering=startDiscovery(seed); }
          if (seed.demanded && !seed.installed) {
            if (!hasSubscriptionCapacity()) { seed.demanded=false; }
            else if (!install(seed,*revision)) { retireSubscription(seed); }
          }
        }}
      catch(...){failure="I2P member installation ownership became uncertain; recheck retained";close();return;}
    }
    if (phase==Phase::Active) {
      advanceSubscriptions(*revision);
      if (session) { advanceSubscriptionDownload(*revision); }
    }
    if(bridgeRequested){if(!channel->ownedTransportIdle()){return;}bridgeRequested=false;auto actual=channel->ownedLeaseDestination();if(!actual||actual->wire!=source->destination().wire){close();return;}
      bridge=std::make_unique<Bridge>(source,*channel,q);QObject::connect(bridge.get(),&Bridge::changed,q,[this]{if(!bridge||(phase!=Phase::Starting&&phase!=Phase::Active)){return;}const auto v=bridge->observation();if(v.phase==Bridge::Phase::Withdrawn){close(bridge->failureReason());}else if(v.phase==Bridge::Phase::Ready&&phase==Phase::Starting){requestInstall();}});
      if(!bridge->begin(options.ports,admission)){close();}}
  }
};
OwnedProfile::OwnedProfile(std::unique_ptr<Impl> p):impl_(std::move(p)){
  auto& s=*impl_;s.q=this;s.channel=std::make_unique<i2cp::InitialChannel>(this);s.timer.setParent(this);
  QObject::connect(&s.timer,&QTimer::timeout,this,[this]{try{impl_->schedule();}catch(...){impl_->failure="I2P queue observation failed";impl_->close();}});
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::sourceEffectRequested,this,[this]{impl_->requestStartup();});
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::dateReceived,this,[this]{if(impl_->phase==Phase::Starting){impl_->startRequested=true;impl_->requestStartup();}});
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::leaseSetSubmitted,this,[this]{if(impl_->phase==Phase::Starting&&!impl_->bridge){impl_->bridgeRequested=true;impl_->requestStartup();}});
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::failed,this,[this]{if(impl_->phase==Phase::Starting||impl_->phase==Phase::Active){impl_->close("I2CP transport ended");}});
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::closed,this,[this]{if(impl_->phase==Phase::Starting||impl_->phase==Phase::Active){impl_->close("I2CP transport ended");}});
}
OwnedProfile::~OwnedProfile(){impl_->close();join();}

std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::prepareSession(
    const storage::ProfileLease& lease, const storage::TagManagerStartupMode* startup, std::shared_ptr<void> runtimePin) {
  auto source = ProfileSource::createSession(lease, "azneti2phelper", startup);
  if (!source) { return std::unexpected(source.error()); }
  auto state = std::make_unique<Impl>();
  state->source = *source;
  state->pin = std::move(runtimePin);
  auto owner = std::unique_ptr<OwnedProfile>(new OwnedProfile(std::move(state)));
  auto identity = (*source)->takeIdentity();
  if (!identity) { return std::unexpected(identity.error()); }
  owner->impl_->identity.emplace(std::move(*identity));
  auto settings = (*source)->policy().settings();
  settings.set_int(lt::settings_pack::alert_mask, static_cast<int>(lt::alert_category::error | lt::alert_category::status
      | lt::alert_category::storage | lt::alert_category::tracker));
  settings.set_int(lt::settings_pack::alert_queue_size, 4096);
  lt::session_params parameters(std::move(settings));
  parameters.disk_io_constructor = engine::detail::metadataDiskConstructor(owner->impl_->disks);
  owner->impl_->session = std::make_unique<lt::session>(std::move(parameters));
  return owner;
}

std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::prepare(const storage::ProfileLease& lease,const core::TorrentHash& hash,const Retained& external,const storage::TagManagerStartupMode* startup,std::optional<std::string_view> expectedConfiguration){
 return prepareImpl(lease,hash,external,startup,expectedConfiguration,nullptr);
}
std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::prepareWithTagRates(const storage::ProfileLease& lease,const core::TorrentHash& hash,engine::ProfileTagRates& rates,const Retained& external,const storage::TagManagerStartupMode* startup,std::optional<std::string_view> expectedConfiguration){
 return prepareImpl(lease,hash,external,startup,expectedConfiguration,&rates);
}
std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::prepareImpl(const storage::ProfileLease& lease,const core::TorrentHash& hash,const Retained& external,const storage::TagManagerStartupMode* startup,std::optional<std::string_view> expectedConfiguration,engine::ProfileTagRates* tagRates){
  try{auto pin=privateOwnerRuntimePin();if(!pin||lt::biglybtc_private_i2p_torrents_abi_v1()!=lt::BiglybtcPrivateI2pTorrentsAbiV1){return std::unexpected(Error{Code::Unsupported,"Approved multi-torrent private I2P provider required"});}
    {auto access=bootstrapSourceAccess(lease,expectedConfiguration);if(!access){return std::unexpected(access.error());}}
    auto prepared = prepareSession(lease, startup, std::move(pin));
    if (!prepared) { return std::unexpected(prepared.error()); }
    auto owner = std::move(*prepared);
    auto first=tagRates?owner->prepareTorrentWithTagRates(hash,external,*tagRates,expectedConfiguration):[&]()->std::expected<std::unique_ptr<Prepared>,Error>{
      auto access=bootstrapSourceAccess(lease,expectedConfiguration);if(!access){return std::unexpected(access.error());}
      return owner->prepareTorrent(hash,external);
    }();if(!first){return std::unexpected(first.error());}owner->impl_->admission=(*first)->impl_->admission;
    auto added=owner->add(std::move(*first));if(!added){return std::unexpected(added.error());}return owner;
  }catch(...){return std::unexpected(Error{Code::Io,"Cannot prepare shared original I2P profile"});}
}
std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::preparePending(const storage::ProfileLease& lease,const engine::PendingMagnet& intent,const storage::TagManagerStartupMode* startup,std::optional<std::string_view> expectedConfiguration){return preparePendingImpl(lease,intent,startup,expectedConfiguration,nullptr);}
std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::preparePendingWithTagRates(
    const storage::ProfileLease& lease,const engine::PendingMagnet& intent,engine::ProfileTagRates& rates,
    const storage::TagManagerStartupMode* startup,std::optional<std::string_view> expectedConfiguration){
 return preparePendingImpl(lease,intent,startup,expectedConfiguration,&rates);
}
std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::preparePendingImpl(const storage::ProfileLease& lease,const engine::PendingMagnet& intent,const storage::TagManagerStartupMode* startup,std::optional<std::string_view> expectedConfiguration,engine::ProfileTagRates* tagRates){
#if defined(BIGLYBTC_I2P_DISCOVERY_ABI_V1)
  try{
    auto pin=privateOwnerRuntimePin();if(!pin){return std::unexpected(Error{Code::Unsupported,"Qualified discovery provider required"});}
    const auto abi=reinterpret_cast<decltype(&lt::biglybtc_i2p_discovery_abi_v1)>(platform::loadedSymbol(pin.get(),"biglybtc_i2p_discovery_abi_v1"));
    if(!abi||abi()!=lt::BiglybtcI2pDiscoveryAbiV1){return std::unexpected(Error{Code::Unsupported,"Qualified discovery ABI required"});}
    {auto access=bootstrapSourceAccess(lease,expectedConfiguration);if(!access){return std::unexpected(access.error());}}
    if(tagRates&&(!startup||!tagRates->matchesProfile(lease,*startup))){return std::unexpected(Error{Code::Conflict,"Pending tag controller must match original profile/startup"});}
    auto prepared = prepareSession(lease, startup, std::move(pin));
    if (!prepared) { return std::unexpected(prepared.error()); }
    auto owner = std::move(*prepared);
    auto first=tagRates?owner->prepareMagnetWithTagRates(intent,*tagRates):[&]()->std::expected<std::unique_ptr<PreparedMagnet>,Error>{
      auto access=bootstrapSourceAccess(lease,expectedConfiguration);if(!access){return std::unexpected(access.error());}
      return owner->prepareMagnet(intent);
    }();if(!first){return std::unexpected(first.error());}owner->impl_->admission=(*first)->impl_->admission;
    auto added=owner->startMagnet(std::move(*first));if(!added){return std::unexpected(added.error());}return owner;
  }catch(...){return std::unexpected(Error{Code::Io,"Cannot prepare pending-first I2P session"});}
#else
  (void)tagRates;(void)lease;(void)intent;(void)startup;(void)expectedConfiguration;return std::unexpected(Error{Code::Unsupported,"Qualified discovery SDK unavailable"});
#endif
}
std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::prepareSubscription(
    const storage::ProfileLease& lease, const core::TorrentHash& hash, engine::HeldSubscriptionSeedResolver resolve,
    const storage::TagManagerStartupMode* startup, std::optional<std::string_view> expectedConfiguration) {
  try {
    auto pin = privateOwnerRuntimePin();
    if (!pin || lt::biglybtc_private_i2p_torrents_abi_v1() != lt::BiglybtcPrivateI2pTorrentsAbiV1
        || !engine::detail::SubscriptionSeedOwner::supported()) {
      return std::unexpected(Error{Code::Unsupported, "Approved private I2P subscription provider required"});
    }
    { const auto access = bootstrapSourceAccess(lease, expectedConfiguration);
      if (!access) { return std::unexpected(access.error()); } }
    auto owner = prepareSession(lease, startup, std::move(pin));
    if (!owner) { return std::unexpected(owner.error()); }
    auto first = (*owner)->prepareSubscriptionSeed(hash, std::move(resolve));
    if (!first) { return std::unexpected(first.error()); }
    (*owner)->impl_->admission = (*first)->impl_->admission;
    const auto added = (*owner)->addSubscriptionSeed(std::move(*first));
    if (!added) { return std::unexpected(added.error()); }
    return std::move(*owner);
  } catch (...) {
    return std::unexpected(Error{Code::Io, "Cannot prepare the first private I2P subscription"});
  }
}

std::expected<std::unique_ptr<OwnedProfile::PreparedSubscription>,Error> OwnedProfile::prepareSubscriptionSeed(
    const core::TorrentHash& hash, engine::HeldSubscriptionSeedResolver resolve) {
  auto& state = *impl_;
  if (state.ioWorker != std::this_thread::get_id() || !state.source->prepared()
      || !engine::detail::SubscriptionSeedOwner::supported()) {
    return std::unexpected(Error{Code::Conflict, "Live private I2P subscription I/O worker required"});
  }
  try {
    auto source = state.source->prepareSubscriptionSource(hash, std::move(resolve));
    if (!source) { return std::unexpected(source.error()); }
    auto seed = std::make_unique<PreparedSubscription::Impl>();
    seed->source = std::move(*source);
    if (!entropy(seed->admission)) { return std::unexpected(Error{Code::Io, "I2P subscription admission entropy unavailable"}); }
    seed->slot = seed->source->initial_.directory / (".BiglyBTC-i2p-subscription-" + std::to_string(seed->admission));
    seed->disks = state.disks;
    lt::sha1_hash selected;
    std::ranges::transform(hash.bytes(), selected.begin(), [](std::byte value) { return std::to_integer<char>(value); });
    const auto stillCurrent = [source = seed->source] { return !source->revoked(); };
    auto torrent = engine::detail::prepareSubscriptionTorrent(selected, seed->source->initial_, seed->slot,
        *seed->disks, stillCurrent);
    if (!torrent) { return std::unexpected(Error{Code::Conflict, "Cannot open the original I2P subscription payload"}); }
    seed->torrent = std::move(*torrent);
    return std::unique_ptr<PreparedSubscription>(new PreparedSubscription(std::move(seed)));
  } catch (...) {
    return std::unexpected(Error{Code::Io, "Cannot prepare private I2P subscription storage"});
  }
}

std::expected<void,Error> OwnedProfile::addSubscriptionSeed(std::unique_ptr<PreparedSubscription> prepared) {
  auto& state = *impl_;
  if (!state.onThread() || state.phase == Phase::Withdrawn || !prepared || prepared->impl_->source->revoked()
      || prepared->impl_->disks != state.disks) {
    return std::unexpected(Error{Code::Conflict, "I2P subscription admission is stale"});
  }
  if (state.phase != Phase::Prepared && !state.discoveryOptions) {
    return std::unexpected(Error{Code::Unsupported, "I2P subscription discovery is disabled"});
  }
  if (state.subscriptions.size()>=engine::PrivateSubscriptionCandidateLimit
      || state.privateAdmissionCount()>=Bridge::MaximumDiscoveryJobs) {
    return std::unexpected(Error{Code::Busy, "I2P subscription seed limit reached"});
  }
  const auto key = prepared->impl_->source->hash().hex();
  if (state.members.contains(key) || state.magnets.contains(key) || state.subscriptions.contains(key)) {
    return std::unexpected(Error{Code::Conflict, "The I2P subscription hash is already owned"});
  }
  state.subscriptions.try_emplace(key, std::move(prepared));
  if (state.phase == Phase::Active) { state.memberInstallRequested = true; state.requestStartup(); }
  else if (state.phase == Phase::Starting) { state.requestInstall(); }
  return {};
}

std::vector<core::TorrentHash> OwnedProfile::retainSubscriptionSeeds(std::span<const core::TorrentHash> wanted,
    std::optional<std::chrono::steady_clock::time_point> rotateAt) {
  auto& state = *impl_;
  std::vector<core::TorrentHash> retained;
  if (!state.onThread() || state.phase == Phase::Withdrawn) { return retained; }
  for (auto& [key, seed] : state.subscriptions) {
    (void)key;
    const auto& hash = seed.data().source->hash();
    if (std::ranges::find(wanted, hash) == wanted.end() || seed.data().source->revoked()) {
      state.retireSubscription(seed);
    } else { retained.push_back(hash); }
  }
  state.advanceSubscriptions();
  const bool windowFull = state.subscriptions.size()>=engine::PrivateSubscriptionCandidateLimit
      || state.privateAdmissionCount()>=Bridge::MaximumDiscoveryJobs;
  if (rotateAt && windowFull && wanted.size()>retained.size()) {
    const auto next=state.rotatableSubscription(*rotateAt);
    if (next!=state.subscriptions.end()) {
      std::erase(retained,next->second.data().source->hash());
      state.retireSubscription(next->second);
      state.advanceSubscriptions();
    }
  }
  return retained;
}

bool OwnedProfile::subscriptionSeedSlotAvailable(std::chrono::steady_clock::time_point now) const {
  auto& state=*impl_;
  if (!state.onThread() || state.phase==Phase::Withdrawn) { return false; }
  return (state.subscriptions.size()<engine::PrivateSubscriptionCandidateLimit
      && state.privateAdmissionCount()<Bridge::MaximumDiscoveryJobs)
      || state.rotatableSubscription(now)!=state.subscriptions.end();
}

bool OwnedProfile::subscriptionSeedLoaded(const core::TorrentHash& hash) const {
  const auto& state=*impl_;
  if (!state.onThread()) { return false; }
  const auto found=state.subscriptions.find(hash.hex());
  return found!=state.subscriptions.end() && found->second.handle.is_valid();
}
std::optional<search::I2pSubscriptionUpgrade> OwnedProfile::takeSubscriptionUpgrade() {
  auto& state=*impl_;
  if (!state.onThread()||state.phase!=Phase::Active||!state.bridge||state.subscriptionDownload) { return {}; }
  auto offer=state.bridge->takeSubscriptionUpgrade();
  if (offer) { state.offeredUpgrade=*offer; }
  return offer;
}
std::expected<void,Error> OwnedProfile::downloadSubscriptionUpgrade(const std::array<char,20>& previous,
    std::uint32_t version) try {
  auto& state=*impl_;
  if (!state.onThread()||state.phase!=Phase::Active||state.sourcePaused||!state.offeredUpgrade||
      state.subscriptionDownload||state.offeredUpgrade->previous!=previous||state.offeredUpgrade->presence.version!=version) {
    return std::unexpected(Error{Code::Conflict,"The I2P subscription offer is no longer available"});
  }
  auto update=std::move(*state.offeredUpgrade);
  state.offeredUpgrade.reset();
  const auto oldHash=core::TorrentHash::parse({previous.data(),previous.size()});
  const auto newHash=core::TorrentHash::parse({update.presence.publicationHash.data(),update.presence.publicationHash.size()});
  if (!oldHash||!newHash||*oldHash==*newHash) { return std::unexpected(Error{Code::InvalidFormat,"Invalid I2P subscription hash"}); }
  const auto old=state.subscriptions.find(oldHash->hex());
  if (old==state.subscriptions.end()||old->second.data().source->revoked()) {
    return std::unexpected(Error{Code::Conflict,"The previous I2P subscription changed"});
  }
  const auto newKey=newHash->hex();
  if (state.subscriptions.contains(newKey)||state.members.contains(newKey)||state.magnets.contains(newKey)||
      state.privateAdmissionCount()>=Bridge::MaximumDiscoveryJobs) {
    return std::unexpected(Error{Code::Busy,"I2P subscription transfer capacity is in use"});
  }
  auto metainfo=search::validateSubscriptionSeedMetainfo(update.metainfo,update.presence);
  if (!metainfo) { return std::unexpected(Error{Code::InvalidFormat,"Invalid I2P subscription metainfo"}); }
  std::uint64_t admission;
  if (!entropy(admission)) { return std::unexpected(Error{Code::Io,"I2P subscription transfer entropy unavailable"}); }
  auto slot=old->second.data().source->initial_.directory/(".BiglyBTC-i2p-update-"+std::to_string(admission));
  const lt::sha1_hash hash(lt::span<const char>{update.presence.publicationHash});
  auto prepared=engine::detail::prepareSubscriptionUpgradeTorrent(hash,*metainfo,slot,*state.disks);
  if (!prepared) { return std::unexpected(Error{Code::Io,"Cannot reserve the I2P subscription transfer"}); }
  state.subscriptionDownload.emplace(Impl::SubscriptionDownload{std::move(update),old->second.data().source,*newHash,
      admission,std::move(slot),std::move(*prepared),std::chrono::steady_clock::now()+std::chrono::minutes(3),{},{},{},false,false,{}});
  state.requestStartup();
  return {};
} catch (...) {
  return std::unexpected(Error{Code::Io,"Cannot prepare the I2P subscription transfer"});
}
std::optional<std::expected<std::string,Error>> OwnedProfile::takeSubscriptionUpgradePayload() {
  auto& state=*impl_;
  if (!state.onThread()||!state.subscriptionDownload||!state.subscriptionDownload->result) { return {}; }
  auto& download=*state.subscriptionDownload;
  if (*download.result&&!state.subscriptionDownloadCurrent(download,state.source->lease()->sourceRevision())) {
    if (!download.source->revoked()&&std::chrono::steady_clock::now()<download.deadline) { return {}; }
    download.result=std::unexpected(Error{Code::Conflict,"The I2P subscription changed before installation"});
  }
  auto result=std::move(download.result);
  state.disks->releaseOriginalPath(download.slot,download.prepared.files);
  state.subscriptionDownload.reset();
  return result;
}
std::expected<std::unique_ptr<OwnedProfile::PreparedMagnet>,Error> OwnedProfile::prepareMagnetWithTagRates(
    const engine::PendingMagnet& intent,engine::ProfileTagRates& rates){
 if(impl_->ioWorker!=std::this_thread::get_id()||!impl_->session||!impl_->source->matchesTagControllerProfile(rates)){return std::unexpected(Error{Code::Conflict,"Pending tag preparation requires its original I/O worker/session"});}
 auto prepared=prepareMagnet(intent);if(!prepared){return std::unexpected(prepared.error());}
 const auto expected=(*prepared)->impl_->parameters.info_hashes;
 auto target=rates.prepareAttachment(intent.hash,expected,*impl_->session);if(!target){return std::unexpected(target.error());}
 if(rates.tagManagerEnabled()){
  auto source=rates.preparePrivateSource(intent.hash,*impl_->session,**target);if(!source){return std::unexpected(source.error());}
  auto bound=(*prepared)->impl_->source->source_->bindPendingTagSource(*source,expected,*impl_->session);if(!bound){return std::unexpected(bound.error());}
 }
 (*prepared)->impl_->tagTarget=std::shared_ptr<engine::ProfilePreparedTagAttachment>(std::move(*target));return std::move(*prepared);
}
std::expected<std::unique_ptr<OwnedProfile::PreparedMagnet>,Error> OwnedProfile::prepareMagnet(const engine::PendingMagnet& intent){
#if defined(BIGLYBTC_I2P_DISCOVERY_ABI_V1)
  if(intent.network!=engine::MagnetNetwork::I2p){return std::unexpected(Error{Code::Unsupported,"Pure metadata requires I2P-only intent"});}
  const auto entered=std::chrono::steady_clock::now();auto access=impl_->source->lease()->sourceAccess();if(!access){return std::unexpected(access.error());}
  auto source=impl_->source->preparePendingMagnet(intent);if(!source){return std::unexpected(source.error());}
  try{
    auto data=std::make_unique<PreparedMagnet::Impl>();data->intent=intent;data->source=*source;data->lifetime=std::make_shared<engine::detail::PendingMetadataLifetime>();
    if(!entropy(data->admission)){return std::unexpected(Error{Code::Io,"Metadata admission entropy unavailable"});}data->expires=entered+std::chrono::milliseconds(intent.timeoutMillis);
    data->seal=std::shared_ptr<const engine::PendingMetadataSource>(new engine::PendingMetadataSource((*source)->lease(),intent,data->admission,data->lifetime,impl_->metadataOrigin));
    const auto path=storage::detail::stateDirectoryPath(impl_->source->lease()->directoryHandle());
    if(!path){return std::unexpected(Error{Code::UnsafePath,"Cannot identify held metadata root"});}
    const auto virtualPath=*path/(".BiglyBTC.metadata."+intent.hash.hex()+"."+std::to_string(data->admission));
    auto registered=impl_->disks->registerMetadataPath(virtualPath);if(!registered){return std::unexpected(registered.error());}
    auto parameters=engine::metadataOnlyParameters(intent,engine::NetworkPolicy{},virtualPath);if(!parameters){return std::unexpected(parameters.error());}data->parameters=std::move(*parameters);
    auto bound=impl_->source->publishPendingMagnet(intent.hash,[&]{return (*source)->bindMetadata(data->seal);});if(!bound){return std::unexpected(bound.error());}
    return std::unique_ptr<PreparedMagnet>(new PreparedMagnet(std::move(data)));
  }catch(...){(*source)->revoke();return std::unexpected(Error{Code::Io,"Cannot prepare bounded I2P metadata member"});}
#else
  (void)intent;return std::unexpected(Error{Code::Unsupported,"Qualified discovery SDK unavailable"});
#endif
}
bool OwnedProfile::configureContactExchange(std::shared_ptr<ContactExchange> value){auto& s=*impl_;if(!s.onThread()||s.phase!=Phase::Prepared||!s.discoveryOptions||!s.discoveryOptions->inbound||!s.discoveryOptions->routingMaintenance){return false;}s.contactExchange=std::move(value);return true;}
bool OwnedProfile::configureDiscovery(const dht::Actor::Options& options,std::optional<dht::Actor::Options> mixedPublication){
  auto& s=*impl_;if(!s.onThread()||s.phase!=Phase::Prepared||s.discoveryOptions){return false;}auto accepted=options;
  if(std::ranges::all_of(accepted.localId,[](Byte b){return b==Byte{};})){
    std::array<Byte,14> random{};if(RAND_bytes(reinterpret_cast<unsigned char*>(random.data()),random.size())!=1){return false;}
    auto node=dht::makeNodeId(s.source->destination().hash,accepted.queryPort,random);if(!node){return false;}accepted.localId=*node;
  }
  s.discoveryOptions=std::move(accepted);
  s.mixedPublicationOptions=std::move(mixedPublication);
  return true;
}
std::expected<void,Error> OwnedProfile::startMagnet(std::unique_ptr<PreparedMagnet> prepared){
  auto& s=*impl_;if(s.onThread()){std::erase_if(s.magnets,[](const auto& entry){return entry.second->released.load();});}
  if(!s.onThread()||s.phase==Phase::Withdrawn||!prepared||!prepared->impl_->seal||prepared->impl_->seal->origin_!=s.metadataOrigin||!prepared->impl_->seal->current()||!prepared->impl_->source->prepared()||!prepared->impl_->source->lease()->sharesOwnership(*s.source->lease())||s.members.contains(prepared->impl_->intent.hash.hex())||s.members.size()+s.magnets.size()>=32){return std::unexpected(Error{Code::Conflict,"I2P metadata member unavailable or bound reached"});}
  const auto key=prepared->impl_->intent.hash.hex();const auto old=s.magnets.find(key);if(old!=s.magnets.end()){if(!old->second->released.load()){return std::unexpected(Error{Code::Conflict,"Metadata identity still retained"});}s.magnets.erase(old);}
  s.makeRoomForDownload(key);
  auto value=std::make_unique<Impl::Magnet>();value->prepared=std::move(prepared);s.magnets.emplace(key,std::move(value));
  if(s.phase==Phase::Active){s.memberInstallRequested=true;s.requestStartup();}else if(s.phase==Phase::Starting){s.requestInstall();}return {};
}
bool OwnedProfile::cancelMagnet(const core::TorrentHash& hash,std::chrono::steady_clock::time_point end){
  auto& s=*impl_;const auto at=s.magnets.find(hash.hex());if(!s.onThread()||at==s.magnets.end()||end<=std::chrono::steady_clock::now()){return false;}auto& m=*at->second;if(m.ready){return false;}m.cancel=true;s.retireMagnet(m);m.end=end;return true;
}
std::vector<OwnedProfile::MagnetObservation> OwnedProfile::magnetObservations(){
  auto& s=*impl_;if(!s.onThread()){return {};}s.alerts();std::vector<MagnetObservation> out;
  for(const auto& [key,m]:s.magnets){(void)key;if(m->released.load()){continue;}out.push_back({m->data().intent.hash,m->data().admission,s.generation,!m->ready,m->ready&&!m->bytes.empty()&&!m->failure&&!m->cancel,m->ready&&m->cancel,m->removing,m->failure});}return out;
}
std::expected<std::string,Error> OwnedProfile::takeCompletedMetadata(const core::TorrentHash& hash){
  auto& s=*impl_;const auto at=s.magnets.find(hash.hex());if(!s.onThread()||at==s.magnets.end()){return std::unexpected(Error{Code::Missing,"Metadata member absent"});}auto& m=*at->second;
  if(!m.ready||m.handle.is_valid()||!m.removed||!m.barrier||m.failure||m.cancel||m.bytes.empty()){return std::unexpected(Error{Code::Conflict,"Actual metadata result/removal fence incomplete"});}return m.bytes; // Keep the actual result retryable until selected-row import succeeds.
}
std::expected<void,Error> OwnedProfile::releaseMagnet(const core::TorrentHash& hash){
  auto& s=*impl_;const auto at=s.magnets.find(hash.hex());if(at==s.magnets.end()||!at->second->ready||at->second->released.load()){return std::unexpected(Error{Code::Conflict,"Metadata member still retained"});}
  auto released=s.source->releasePendingChild(hash);if(!released){return released;}at->second->released.store(true);return {};
}
std::expected<void,Error> OwnedProfile::publishPendingMagnet(const core::TorrentHash& hash,const std::function<std::expected<void,Error>()>& f){return impl_->source->publishPendingMagnet(hash,f);}
std::expected<std::unique_ptr<OwnedProfile::Prepared>,Error> OwnedProfile::prepareTorrentWithTagRates(
    const core::TorrentHash& hash,const Retained& retained,engine::ProfileTagRates& rates,
    std::optional<std::string_view> expectedConfiguration){
  if(impl_->ioWorker!=std::this_thread::get_id()||!impl_->session||!impl_->source->matchesTagControllerProfile(rates)){return std::unexpected(Error{Code::Conflict,"Private tag preparation requires its original I/O worker/session"});}
  if(!rates.tagManagerEnabled()){
    auto common=rates.prepare(hash,*impl_->session);if(!common){return std::unexpected(common.error());}
    auto access=bootstrapSourceAccess(*impl_->source->lease(),expectedConfiguration);if(!access){return std::unexpected(access.error());}
    return prepareTorrent(hash,retained);
  }
  std::shared_ptr<ProfileSource> child;lt::info_hash_t expected;
  {
    auto access=bootstrapSourceAccess(*impl_->source->lease(),expectedConfiguration);if(!access){return std::unexpected(access.error());}
    auto captured=impl_->source->prepareChild(hash,retained.claims,retained.identities);if(!captured){return std::unexpected(captured.error());}child=*captured;
    auto full=child->previewTagTarget();if(!full){return std::unexpected(full.error());}expected=*full;
  }
  // Both SDK class preparation and complete-cohort capture run without FS gates.
  auto target=rates.prepareAttachment(hash,expected,*impl_->session);if(!target){return std::unexpected(target.error());}
  auto witness=rates.preparePrivateSource(hash,*impl_->session,**target);if(!witness){return std::unexpected(witness.error());}
  auto access=bootstrapSourceAccess(*impl_->source->lease(),expectedConfiguration);if(!access){return std::unexpected(access.error());}
  auto storage=child->prepareStorageWithTagSource(*witness,expected,*impl_->session);if(!storage){return std::unexpected(storage.error());}
  if(!storage->parameters().ti||storage->parameters().ti->info_hashes()!=expected){return std::unexpected(Error{Code::Conflict,"Prepared private metadata changed its full SDK identity"});}
  auto value=std::unique_ptr<Prepared>(new Prepared(std::make_unique<Prepared::Impl>(hash,child,std::move(*storage))));
  value->impl_->tagTarget=std::shared_ptr<engine::ProfilePreparedTagAttachment>(std::move(*target));
  auto inventory=storage::ConfigFile::load(*impl_->source->lease(),"downloads.config");if(!inventory){return std::unexpected(inventory.error());}auto library=core::readLibrary(inventory->document());if(!library){return std::unexpected(Error{Code::InvalidFormat,"Invalid I2P queue inventory"});}
  for(std::size_t n=0;n<library->downloads.size();++n){if(library->downloads[n].hash==hash){value->impl_->inventoryIndex=n;break;}}
  if(!entropy(value->impl_->admission)){return std::unexpected(Error{Code::Io,"I2P member admission entropy unavailable"});}return value;
}

std::expected<std::unique_ptr<OwnedProfile::Prepared>,Error> OwnedProfile::prepareTorrent(const core::TorrentHash& hash,const Retained& retained){
  auto access=impl_->source->lease()->sourceAccess();if(!access){return std::unexpected(access.error());}
  auto child=impl_->source->prepareChild(hash,retained.claims,retained.identities);if(!child){return std::unexpected(child.error());}auto storage=(*child)->prepareStorage();if(!storage){return std::unexpected(storage.error());}
  auto value=std::unique_ptr<Prepared>(new Prepared(std::make_unique<Prepared::Impl>(hash,*child,std::move(*storage))));
  auto inventory=storage::ConfigFile::load(*impl_->source->lease(),"downloads.config");if(!inventory){return std::unexpected(inventory.error());}auto library=core::readLibrary(inventory->document());if(!library){return std::unexpected(Error{Code::InvalidFormat,"Invalid I2P queue inventory"});}
  for(std::size_t n=0;n<library->downloads.size();++n){if(library->downloads[n].hash==hash){value->impl_->inventoryIndex=n;break;}}
  if(!entropy(value->impl_->admission)){return std::unexpected(Error{Code::Io,"I2P member admission entropy unavailable"});}return value;
}
std::expected<void,Error> OwnedProfile::preflightTagSourceUpdate(engine::ProfileTagRates& rates){
  if(impl_->ioWorker!=std::this_thread::get_id()||!impl_->source->matchesTagOwner(rates)){
    return std::unexpected(Error{Code::Conflict,"Private tag refresh requires its original I/O worker/common controller"});
  }
  auto access=impl_->source->lease()->sourceAccess();if(!access){return std::unexpected(access.error());}
  if(!access->observationsAllowed()){return std::unexpected(Error{Code::Conflict,"Release the outer source gate before private tag refresh"});}
  return {};
}
std::expected<std::unique_ptr<OwnedProfile::TagSourceUpdate>,Error> OwnedProfile::beginTagSourceUpdate(){
  auto& s=*impl_;
  if(!s.onThread()||s.sourcePaused||!s.session||!s.permit
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
      TagSourceUpdate::Impl::Target target(&m.data());target.handle=m.handle;const auto& full=m.data().storage.identity().full;
      if(full.v1){std::transform(full.v1->begin(),full.v1->end(),target.expected.v1.begin(),[](std::byte b){return std::to_integer<unsigned char>(b);});}
      if(full.v2){std::transform(full.v2->begin(),full.v2->end(),target.expected.v2.begin(),[](std::byte b){return std::to_integer<unsigned char>(b);});}
      data->targets.push_back(std::move(target));
    }
    for(auto& [key,entry]:s.magnets){(void)key;auto& m=*entry;if(m.released.load()){continue;}
      if(m.cancel||m.removing||m.ready||m.failure||!m.data().tagTarget||(m.installed&&!m.handle.is_valid())){
        return std::unexpected(Error{Code::Busy,"Pending tag refresh awaits actual metadata retirement"});
      }
      TagSourceUpdate::Impl::Target target(&m.data());target.handle=m.handle;
      target.expected=m.handle.is_valid()?m.handle.info_hashes():m.data().parameters.info_hashes;
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
std::expected<storage::ManualTagCommit,Error> OwnedProfile::applyRateOnlyMembership(TagSourceUpdate& update,
    engine::ProfileTagRates& rates,storage::ManualTagStore& store,const storage::ManualTagSnapshot& snapshot,
    const core::TorrentHash& hash,std::span<const storage::ManualTagMembershipEdit> requests,
    const std::function<std::expected<void,Error>(const storage::ProfileSourceAccess&)>& validate,
    std::stop_token stop,std::optional<std::pair<std::uint64_t,std::uint64_t>> expectedOwner){
 auto& s=*impl_;auto& value=*update.impl_;
 if(s.ioWorker!=std::this_thread::get_id()||value.owner!=&s||!value.joined||value.prepared
     ||!value.root->matchesTagOwner(rates)||requests.empty()||requests.size()>32){return std::unexpected(Error{Code::Conflict,"Actual joined private rate-membership transaction required"});}
 const auto target=std::find_if(value.targets.begin(),value.targets.end(),[&](const auto& t){return t.prepared&&t.hash()==hash;});
 if(target==value.targets.end()){return std::unexpected(Error{Code::Conflict,"Private rate membership requires its actual full prepared member"});}
 if(expectedOwner&&(expectedOwner->first!=target->prepared->admission||expectedOwner->second!=s.generation)){
  return std::unexpected(Error{Code::Conflict,"Private rate-membership provider admission changed"});
 }
 const auto* tags=snapshot.tags();const auto* manual=tags&&tags->source?tags->source->find("3"):nullptr;
 if(!manual){return std::unexpected(Error{Code::Conflict,"Private rate membership requires its intact original tag snapshot"});}
 std::vector<std::int32_t> active;std::vector<core::SavedTagEdit> edits;edits.reserve(requests.size());
 for(const auto& request:requests){
  const auto valid=std::visit([&](const auto& item){
   const auto* record=manual->find(std::to_string(item.id));if(item.hash!=hash||!record||!core::rateOnlySavedTagMembershipEffects(*tags,*record)){return false;}
   const auto limits=core::readSavedTagRateLimits(*record);if(!limits){return false;}
   if(!limits->unlimited()&&std::ranges::find(active,item.id)==active.end()){active.push_back(item.id);}edits.emplace_back(item);return true;
  },request);if(!valid){return std::unexpected(Error{Code::Unsupported,"Private membership requires rates without lifecycle or other unsupported effects"});}
 }
 std::optional<storage::PreparedTagRateSource> prepared;
 if(!active.empty()){auto ready=rates.prepareSource(*s.source->lease(),store,snapshot,active,value.session);if(!ready){return std::unexpected(ready.error());}prepared=std::move(*ready);}
 // SDK class/capacity acknowledgements above precede this local FS-only CAS.
 auto access=s.source->lease()->sourceAccess();if(!access){return std::unexpected(access.error());}
 auto current=s.source->validate();if(!current){return std::unexpected(current.error());}
 if(validate){auto checked=validate(*access);if(!checked){return std::unexpected(checked.error());}}
 return store.applySequence(snapshot,edits,stop,std::nullopt,false,prepared?&*prepared:nullptr,true);
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
        auto proof=rates.refreshPrivateSource(target.hash(),value.session,target.handle);
        if(!proof){return std::unexpected(proof.error());}target.source=std::move(*proof);
      }else{
        target.replacement=target.token();
        if(!target.replacement||!target.replacement->current()){
          auto pending=rates.prepareAttachment(target.hash(),target.expected,value.session);if(!pending){return std::unexpected(pending.error());}
          target.replacement=std::shared_ptr<engine::ProfilePreparedTagAttachment>(std::move(*pending));
        }
        auto proof=rates.preparePrivateSource(target.hash(),value.session,*target.replacement);
        if(!proof){return std::unexpected(proof.error());}target.source=std::move(*proof);
      }
      sources.push_back({target.child(),target.source,target.expected});
    }
    auto accepted=value.root->acceptTagSources(sources);if(!accepted){return std::unexpected(accepted.error());}
    for(auto& target:value.targets){if(target.replacement){target.token()=std::move(target.replacement);}}
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

bool OwnedProfile::begin(const Options& options,core::QueuePolicy policy,Rates rates){
  auto& s=*impl_;if(!s.onThread()||s.phase!=Phase::Prepared||!options.ports.peer||!options.ports.localPeer){return false;}s.options=options;s.queuePolicy=std::move(policy);s.rates=rates;s.phase=Phase::Starting;
  s.permit=std::make_unique<SourcePermit>(s.source,this);if(!s.channel->bindEffectAuthority(s.source->effectAuthority())||!s.channel->begin(options.router)){s.close();return false;}return true;
}
OwnedProfile::Retained OwnedProfile::retained()const{
  Retained v;for(const auto& [key,m]:impl_->members){(void)key;const auto claims=m->data().storage.claims();v.claims.insert(v.claims.end(),claims.begin(),claims.end());v.identities.push_back(m->data().storage.identity());}return v;
}
std::expected<bool,Error> OwnedProfile::pauseForProfileBackup() {
  auto& state=*impl_;
  if (!state.onThread()) {
    return std::unexpected(Error{Code::Conflict,"Only the private I2P owner may pause its engine"});
  }
  bool requested=false;
  try {
    if (!state.session||state.session->is_paused()) { return false; }
    requested=true;
    state.session->pause();
    if (state.session->is_paused()) { return true; }
  } catch (...) {}
  if (requested) { resumeAfterProfileBackup(); }
  return std::unexpected(Error{Code::Io,"Cannot pause the private I2P engine for backup"});
}
void OwnedProfile::resumeAfterProfileBackup() noexcept {
  try {
    if (impl_->onThread()&&impl_->session) { impl_->session->resume(); }
  } catch (...) {}
}
std::unique_ptr<OwnedProfile::BackupCapture> OwnedProfile::beginProfileBackupCapture() try {
  auto& state=*impl_;
  if (!state.onThread()||state.phase!=Phase::Active||state.sourcePaused||!state.session||
      state.backupCaptureSerial==std::numeric_limits<std::uint64_t>::max()) { return {}; }
  state.alerts();
  if (!state.session) { return {}; }
  using Clock=std::chrono::steady_clock;
  auto capture=std::make_unique<BackupCapture::Impl>();
  capture->owner=this;
  capture->serial=++state.backupCaptureSerial;
  capture->deadline=Clock::now()+std::chrono::seconds(4);
  constexpr std::size_t MaximumSnapshots=128;
  std::size_t piecesRemaining=8*1024*1024;
  for (const auto& [key,owner]:state.members) {
    (void)key;
    if (capture->entries.size()==MaximumSnapshots||Clock::now()>=capture->deadline) { break; }
    auto& member=*owner;
    if (!member.installed||member.backupFlushPending||member.retired.requested||!member.failure.empty()||
        !member.handle.is_valid()||!member.counters->checked.load(std::memory_order_acquire)) { continue; }
    lt::torrent_status status;
    const auto counts=state.sample(member,status,true);
    const bool hasCounts=counts.state==engine::detail::ManagedPayloadCounters::Read::Accepted;
    if (!hasCounts) { status=member.handle.status(lt::torrent_handle::query_pieces); }
    if (!engine::detail::backupPieceMapAvailable(status)||static_cast<std::size_t>(status.pieces.size())>piecesRemaining) {
      continue;
    }
    std::vector<std::uint8_t> pieces;
    pieces.reserve(static_cast<std::size_t>(status.pieces.size()));
    for (const auto piece:status.pieces) { pieces.push_back(piece?1:0); }
    const auto& admitted=member.data().storage.capturedDownload();
    const auto verified=member.data().storage.verifiedOriginalBytes(pieces);
    if (!verified||*verified<=admitted.completedBytes.value_or(0)) { continue; }
    const auto received=hasCounts?engine::cumulativePayload(admitted.downloaded,counts.received):std::nullopt;
    const auto sent=hasCounts?engine::cumulativePayload(admitted.uploaded,counts.sent):std::nullopt;
    piecesRemaining-=pieces.size();
    capture->entries.push_back({member.data().hash,member.data().admission,std::move(pieces),received,sent});
    member.handle.flush_cache();
    member.backupFlushSerial=capture->serial;
    member.backupFlushPending=true;
  }
  if (capture->entries.empty()) { return {}; }
  return std::unique_ptr<BackupCapture>(new BackupCapture(std::move(capture)));
} catch (...) {
  return {};
}

bool OwnedProfile::settleProfileBackupCapture(BackupCapture& request,bool cancelled) try {
  auto& state=*impl_;
  auto& capture=*request.impl_;
  if (!state.onThread()||capture.owner!=this) { return false; }
  if (capture.settled) { return true; }
  state.alerts();
  if (cancelled||state.phase!=Phase::Active||state.sourcePaused||!state.session) {
    capture.entries.clear();
    capture.settled=true;
    return true;
  }
  const auto timedOut=std::chrono::steady_clock::now()>=capture.deadline;
  bool waiting=false;
  for (auto& entry:capture.entries) {
    entry.accepted=false;
    const auto* member=state.find(entry.hash);
    if (!member||member->data().admission!=entry.admission||member->backupFlushSerial!=capture.serial||
        member->retired.requested||!member->failure.empty()||!member->handle.is_valid()) { continue; }
    if (member->backupFlushPending) {
      waiting|=!timedOut;
      continue;
    }
    entry.accepted=engine::detail::backupPieceMapStillPresent(
        member->handle.status(lt::torrent_handle::query_pieces),entry.pieces);
  }
  if (waiting) { return false; }
  capture.settled=true;
  return true;
} catch (...) {
  request.impl_->entries.clear();
  request.impl_->settled=true;
  return true;
}

std::vector<storage::ProfileBackupTransferStatistics> OwnedProfile::readProfileBackupCapture(
    const BackupCapture& request,std::stop_token stop) {
  auto& state=*impl_;
  const auto& capture=*request.impl_;
  std::vector<storage::ProfileBackupTransferStatistics> result;
  if (std::this_thread::get_id()!=state.ioWorker||capture.owner!=this||!capture.settled||!state.source->prepared()) {
    return result;
  }
  std::size_t budget=32*1024*1024;
  for (const auto& entry:capture.entries) {
    if (stop.stop_requested()||!budget) { break; }
    if (!entry.accepted) { continue; }
    // Host control serializes member release and state edits with this read.
    const auto* member=state.find(entry.hash);
    if (!member||member->data().admission!=entry.admission||member->checkpointed.load(std::memory_order_acquire)||
        !member->data().source->prepared()) { continue; }
    const auto& storage=member->data().storage;
    auto image=storage.prepareBackupResume(*state.source->lease(),entry.pieces);
    if (!image||image->source.size()>budget||image->destination.size()>budget-image->source.size()) { continue; }
    budget-=image->source.size()+image->destination.size();
    result.push_back({storage.capturedDownload(),entry.received,entry.sent,std::move(image)});
  }
  return result;
}
bool OwnedProfile::configureExternalQueue(bool enabled){
  auto& s=*impl_;if(!s.onThread()||(s.phase!=Phase::Prepared&&s.externalQueue!=enabled)){return false;}s.externalQueue=enabled;return true;
}
std::expected<engine::ExternalQueueSnapshot,Error> OwnedProfile::externalQueueSnapshot(){
  auto& s=*impl_;if(!s.onThread()||!s.externalQueue||s.phase==Phase::Withdrawn||s.externalQueueSequence==std::numeric_limits<std::uint64_t>::max()){
    return std::unexpected(Error{Code::Conflict,"External I2P queue snapshot unavailable"});
  }
  s.alerts();if(s.phase==Phase::Withdrawn){return std::unexpected(Error{Code::Conflict,"I2P source withdrew"});}
  auto snapshot=s.collectQueue(false,true);snapshot.sequence=++s.externalQueueSequence;s.externalSnapshotValid=true;return snapshot;
}
bool OwnedProfile::externalQueueCurrent(std::uint64_t sequence,std::span<const engine::ExternalQueueDecision> rows)const{
  const auto& s=*impl_;if(!s.onThread()||!s.externalQueue||!s.externalSnapshotValid||!sequence||sequence!=s.externalQueueSequence||s.phase==Phase::Withdrawn){return false;}
  for(std::size_t n=0;n<rows.size();++n){const auto& row=rows[n];const auto* m=s.find(row.binding.hash);
    if(!m||!row.binding.admission||m->data().admission!=row.binding.admission||row.decision.token!=row.binding.admission
        ||!m->installed||m->retired.requested||!m->failure.empty()||!m->handle.is_valid()){return false;}
    for(std::size_t i=0;i<n;++i){if(rows[i].binding==row.binding){return false;}}
  }return true;
}
std::expected<bool,Error> OwnedProfile::applyExternalQueue(std::uint64_t sequence,
    std::span<const engine::ExternalQueueDecision> rows,std::uint32_t unavailable,core::QueueAction action,std::optional<std::uint64_t> validatedSource){
  auto& s=*impl_;if(!s.onThread()||!s.externalQueue||!s.externalSnapshotValid||!sequence||sequence!=s.externalQueueSequence||s.phase==Phase::Withdrawn
      ||(action!=core::QueueAction::Queue&&action!=core::QueueAction::Run&&action!=core::QueueAction::Preserve)){
    return std::unexpected(Error{Code::Conflict,"External I2P queue plan is stale"});
  }
  std::vector<Impl::Member*> targets;targets.reserve(rows.size());
  for(const auto& row:rows){auto* m=s.find(row.binding.hash);
    if(!m||!row.binding.admission||m->data().admission!=row.binding.admission||row.decision.token!=row.binding.admission
        ||!m->installed||m->retired.requested||!m->failure.empty()||!m->handle.is_valid()
        ||std::ranges::find(targets,m)!=targets.end()){
      return std::unexpected(Error{Code::Conflict,"External I2P queue binding changed"});
    }targets.push_back(m);
  }
  if(action==core::QueueAction::Run&&(!validatedSource||!s.source->revisionCurrent(*validatedSource))){return std::unexpected(Error{Code::Conflict,"I2P source changed before external Run"});}
  bool settled=true;s.queueUnavailable=unavailable;
  for(std::size_t i=0;i<rows.size();++i){const auto& d=rows[i].decision;auto& m=*targets[i];if(action!=core::QueueAction::Queue&&d.action!=action){continue;}
    if(action==core::QueueAction::Run&&m.data().tagTarget&&!m.data().source->prepared()){
      s.fail(m,{Code::Conflict,"Private tag source is no longer current before external Run"});return std::unexpected(Error{Code::Conflict,"Private tag Run source changed"});
    }
    m.activity=d.activity;m.justCompleted=false;
    if(d.action!=action){continue;}
    if(action==core::QueueAction::Preserve){continue;}
    if(!m.counters->checked.load()){settled=false;continue;}
    const auto actual=m.handle.status();if(actual.errc){settled=false;continue;}
    const bool paused=bool(actual.flags&lt::torrent_flags::paused);
    if(action==core::QueueAction::Queue){m.queued=true;
      if(!paused&&!m.pausePending){m.pauseSettled=false;m.pausePending=true;m.handle.pause();}
      settled&=paused&&m.pauseSettled&&!m.pausePending;
    }else if(m.queued){
      if(!paused&&!m.pausePending){m.pauseSettled=false;m.pausePending=true;m.handle.pause();}
      else if(paused&&m.pauseSettled&&!m.pausePending){
        if(!validatedSource||!s.source->revisionCurrent(*validatedSource)){return std::unexpected(Error{Code::Conflict,"I2P source changed during external Run preparation"});}
        m.handle.unset_flags(lt::torrent_flags::auto_managed|lt::torrent_flags::stop_when_ready);m.pauseSettled=false;m.handle.resume();m.queued=false;
      }else{settled=false;}
    }
  }return settled;
}
std::expected<void,Error> OwnedProfile::applyExternalQueuePositions(
    std::span<const std::pair<engine::QueueBinding,std::uint32_t>> rows){
  auto& s=*impl_;if(!s.onThread()||!s.externalQueue||s.phase==Phase::Withdrawn){return std::unexpected(Error{Code::Conflict,"External I2P positions unavailable"});}
  for(const auto& [binding,position]:rows){auto* m=s.find(binding.hash);if(!m||m->data().admission!=binding.admission||m->retired.requested||!position||position>std::numeric_limits<std::int32_t>::max()){
    return std::unexpected(Error{Code::Conflict,"External I2P position binding changed"});
  }}
  for(const auto& [binding,position]:rows){auto* m=s.find(binding.hash);m->livePosition=position;m->completionPositionUnavailable=false;}return {};
}
std::expected<void,Error> OwnedProfile::applyExternalRateLimits(const core::SessionRateLimits& limits){
  auto& s=*impl_;if(!s.onThread()||!s.externalQueue||s.phase==Phase::Withdrawn||!engine::validExternalRateLimits(limits)){
    return std::unexpected(Error{Code::InvalidFormat,"Invalid coordinated I2P rate caps"});
  }
  s.externalRates=limits;
  if(s.session){lt::settings_pack pack;pack.set_int(lt::settings_pack::download_rate_limit,static_cast<int>(limits.download.bytesPerSecond));
    pack.set_int(lt::settings_pack::upload_rate_limit,static_cast<int>(limits.upload.bytesPerSecond));s.session->apply_settings(pack);
    const auto actual=s.session->get_settings();
    if(core::effectiveSessionRateLimit(actual.get_int(lt::settings_pack::download_rate_limit))!=limits.download
        ||core::effectiveSessionRateLimit(actual.get_int(lt::settings_pack::upload_rate_limit))!=limits.upload){return std::unexpected(Error{Code::Io,"Coordinated I2P rate caps unavailable"});}
  }return {};
}
std::expected<void,Error> OwnedProfile::add(std::unique_ptr<Prepared> p){
  auto& s=*impl_;if(!s.onThread()||s.phase==Phase::Withdrawn||!p||s.members.size()>=32||s.members.contains(p->impl_->hash.hex())){return std::unexpected(Error{Code::Conflict,"I2P member is duplicate, stale or exceeds the32-torrent bound"});}
  s.externalSnapshotValid=false;
  const auto key=p->impl_->hash.hex();
  s.makeRoomForDownload(key);
  auto m=std::make_unique<Impl::Member>();m->prepared=std::move(p);m->livePosition=m->data().storage.capturedDownload().position;m->forceStart=m->data().storage.capturedDownload().forceStart;auto [i,ok]=s.members.emplace(key,std::move(m));(void)ok;
  if(s.phase==Phase::Active){s.memberInstallRequested=true;s.requestStartup();}else if(s.phase==Phase::Starting){s.requestInstall();}return {};
}
bool OwnedProfile::retire(const core::TorrentHash& hash,std::chrono::steady_clock::time_point end){
  auto& s=*impl_;auto* m=s.find(hash);if(!s.onThread()||!m||m->retired.requested||end<=std::chrono::steady_clock::now()){return false;}s.alerts();if(s.bridge){s.bridge->cancelDiscovery(m->data().admission);}s.externalSnapshotValid=false;m->retired.requested=true;m->deadline=end;
  if(!m->installed){
    if(m->handle.is_valid()){
      if(!s.session){return false;}m->unchecked=true;m->removing=true;m->retired.failure.reset();m->failure.clear();s.session->remove_torrent(m->handle);return true;
    }
    if(m->registered){const auto tuple=s.registration(*m);if(!s.session||lt::biglybtc_revoke_private_i2p_torrent_v1(s.session.get(),&tuple)!=0){s.fail(*m,{Code::Conflict,"Cannot withdraw unused I2P registration"});return false;}m->registered=false;}
    m->retired.failure.reset();m->retired.readyForJoin=true;m->retired.joined=true;return true;
  }if(!s.session||!m->handle.is_valid()||!m->failure.empty()){s.fail(*m,{Code::Conflict,"I2P member cannot certify retirement"});return false;}
  // Preserve a genuine settled pause across policy holds. A paused status bit
  // alone may precede async_stop_torrent and cannot authorize the snapshot.
  const auto actual=m->handle.status();m->handle.unset_flags(lt::torrent_flags::auto_managed|lt::torrent_flags::stop_when_ready);
  if(m->pauseSettled&&!m->pausePending&&(actual.flags&lt::torrent_flags::paused)){m->pauseSeen=true;m->saveRequested=true;m->handle.save_resume_data(lt::torrent_handle::flush_disk_cache);}
  else if(!(actual.flags&lt::torrent_flags::paused)&&!m->pausePending){m->pauseSettled=false;m->pausePending=true;m->handle.pause();} // Otherwise await its genuine outstanding pause callback.
  return true;
}
OwnedTorrent::RetirementObservation OwnedProfile::retirement(const core::TorrentHash& h){impl_->alerts();auto* m=impl_->find(h);return m?m->retired:OwnedTorrent::RetirementObservation{.failure=Error{Code::Missing,"I2P member is absent"}};}
std::expected<bool,Error> OwnedProfile::rebaseCategory(const storage::ProfileLease& lease,const core::TorrentHash& hash){
  auto& s=*impl_;
  if(std::this_thread::get_id()!=s.ioWorker||!s.source->lease()->sharesOwnership(lease)){
    return std::unexpected(Error{Code::Conflict,"Category rebase requires the original I2P IO worker and profile OFD"});
  }
  // Host control serializes add/release. Qt callbacks neither erase members nor
  // access retained TorrentStateFile; their parameters/queue/counters stay intact.
  auto* member=s.find(hash);if(!member||member->checkpointed.load(std::memory_order_acquire)){return false;}
  if(member->data().hash!=hash||!member->data().admission){
    s.source->revoke();return std::unexpected(Error{Code::Conflict,"I2P category admission is stale"});
  }
  auto result=member->data().storage.rebaseCategory(lease);
  if(!result){s.source->revoke();return std::unexpected(result.error());}return true;
}
std::expected<void,Error> OwnedProfile::checkpoint(const core::TorrentHash& h){
  auto& s=*impl_;auto* m=s.find(h);if(!m||!m->retired.readyForJoin||!m->retired.joined||m->retired.failure||m->checkpointed.load()){return std::unexpected(Error{Code::Conflict,"Target I2P checkpoint requires exact flush/removal/storage expiration"});}
  if(m->installed&&!m->unchecked){auto result=s.source->publish(h,[&]()->std::expected<void,Error>{
      auto valid=m->data().source->validateRetiredSnapshot();if(!valid){return valid;}
      std::optional<engine::detail::ManagedBadPayloadBytes> bad;if(m->finalCounters.badPrefix){bad={{m->finalCounters.badPrefix->discarded,m->finalCounters.badPrefix->hashFailed}};}
      const auto durations=m->finalDurations;
      return m->data().storage.checkpoint(*s.source->lease(),m->pieces,m->complete,
          m->finalCounters.received,m->finalCounters.sent,durations?durations->downloading:0,
          durations?durations->seeding:0,{},bad,m->finalCounters.bad.has_value(),
          {},false,false,nullptr,m->tagState.lastActiveMillis());
    });if(!result){return result;}}
  auto released=s.source->releaseChild(h);if(!released){return released;}m->checkpointed.store(true);return {};
}
std::expected<void,Error> OwnedProfile::release(const core::TorrentHash& h){auto& s=*impl_;auto* m=s.find(h);if(!s.onThread()||!m||!m->checkpointed.load()){return std::unexpected(Error{Code::Conflict,"I2P member storage still retained"});}s.members.erase(h.hex());s.schedule();return {};}
std::expected<void,Error> OwnedProfile::publish(const core::TorrentHash& h,const std::function<std::expected<void,Error>()>& f){return impl_->source->publish(h,f);}
std::expected<void,Error> OwnedProfile::publishPositions(const std::function<std::expected<void,Error>()>& f){return impl_->source->publishPositions(f);}
std::expected<std::uint64_t,Error> OwnedProfile::validateExternalQueueSource(){
  auto access=impl_->source->lease()->sourceAccess();if(!access){return std::unexpected(access.error());}
  auto current=impl_->source->validate();if(!current){return std::unexpected(current.error());}
  return access->revision(); // SAME OFD/source epoch; no returned effect permit.
}
std::expected<std::unique_ptr<OwnedProfile::QueueState>,Error> OwnedProfile::readQueueState(){
  auto& s=*impl_;auto access=s.source->lease()->sourceAccess();if(!access){return std::unexpected(access.error());}auto valid=s.source->validate();if(!valid){return std::unexpected(valid.error());}
  auto file=storage::ConfigFile::load(*s.source->lease(),"downloads.config");if(!file){return std::unexpected(file.error());}auto library=core::readLibrary(file->document());if(!library){return std::unexpected(Error{Code::InvalidFormat,"Invalid saved I2P queue"});}
  auto state=std::unique_ptr<QueueState>(new QueueState(std::make_unique<QueueState::Impl>()));state->impl_->revision=access->revision();
  for(const auto& [key,m]:s.members){(void)key;auto row=std::ranges::find_if(library->downloads,[&](const auto& r){return r.hash==m->data().hash;});if(row==library->downloads.end()){return std::unexpected(Error{Code::Conflict,"I2P queue member disappeared"});}state->impl_->rows.push_back({m->data().hash,m->data().admission,row->position,row->forceStart});}return state;
}
bool OwnedProfile::applyQueueState(std::unique_ptr<QueueState> state){
  auto& s=*impl_;if(!s.onThread()||!state||state->impl_->revision!=s.source->lease()->sourceRevision()||state->impl_->rows.size()!=s.members.size()){return false;}
  for(const auto& row:state->impl_->rows){auto* m=s.find(row.hash);if(!m||m->data().admission!=row.admission){return false;}}
  for(const auto& row:state->impl_->rows){auto* m=s.find(row.hash);m->livePosition=row.position;m->forceStart=row.force;}s.schedule();return true;
}
std::vector<core::TorrentHash> OwnedProfile::completedForceStarts()const{std::vector<core::TorrentHash> rows;for(const auto& [key,m]:impl_->members){(void)key;if(m->clearCompletedForce&&!m->retired.requested){rows.push_back(m->data().hash);}}return rows;}
void OwnedProfile::acknowledgeCompletion(const core::TorrentHash& h){if(auto* m=impl_->find(h)){m->clearCompletedForce=false;}}
std::vector<OwnedProfile::Observation> OwnedProfile::observations(){
  auto& s=*impl_;s.schedule();std::vector<Observation> out;for(auto& [key,m]:s.members){(void)key;if(m->retired.readyForJoin){continue;}OwnedTorrent::Observation v;v.phase=s.phase==Phase::Active&&!m->installed?Phase::Starting:s.phase;v.publicIdentity=s.source->destination().hash;v.admission=m->data().admission;v.generation=s.generation;v.failure=m->failure.empty()?s.failure:m->failure;
    if(s.channel->failure()!=i2cp::InitialChannel::Failure::None){const auto* reason=QMetaEnum::fromType<i2cp::InitialChannel::Failure>().valueToKey(static_cast<int>(s.channel->failure()));v.failure=std::string("I2CP: ")+(reason?reason:"Unknown");}
    if(s.bridge){auto b=s.bridge->observation();v.bridgePort=b.port;v.bridgeSockets=b.sockets;v.rejectedClients=b.rejectedClients;}
    if(const auto disconnected=s.channel->routerDisconnect()){v.failure="I2CP router disconnected: "+disconnected->reason.substr(0,256);}
    bool stateObserved = false;
    int dl=0,ul=0;bool selectedComplete=false;if(m->handle.is_valid()){lt::torrent_status actual;const auto counts=s.sample(*m,actual);if(counts.state==engine::detail::ManagedPayloadCounters::Read::Accepted){v.payloadDownloaded=counts.received;v.payloadUploaded=counts.sent;stateObserved=!actual.errc;}else{actual=m->handle.status();}
      if(qEnvironmentVariableIsSet("BIGLYBTC_I2P_DIAGNOSTICS")&&nowMillis()>=m->nextDiagnostic){
        m->nextDiagnostic=nowMillis()+1000;
        qInfo().noquote().nospace()<<"I2P_TARGET hash="<<QString::fromStdString(m->data().hash.hex())<<" admission="<<m->data().admission<<" queued="<<m->queued<<" checked="<<m->counters->checked.load()<<" paused="<<bool(actual.flags&lt::torrent_flags::paused)<<" stop_ready="<<bool(actual.flags&lt::torrent_flags::stop_when_ready)<<" announcing="<<actual.announcing_to_trackers<<" peers="<<actual.num_peers<<" done="<<actual.total_done<<" payload="<<actual.total_payload_download<<" announces="<<m->alerts.announces<<" replies="<<m->alerts.trackerReplies<<" tracker_errors="<<m->alerts.trackerErrors<<" error="<<actual.errc.value()<<" manager_streams="<<s.channel->streamManagerObservation().streams<<" bridge_sockets="<<(s.bridge?s.bridge->observation().sockets:0);
        for(const auto& tracker:m->handle.trackers()){for(const auto& endpoint:tracker.endpoints){for(const auto& hash:endpoint.info_hashes){qInfo().noquote().nospace()<<"I2P_TRACKER admission="<<m->data().admission<<" updating="<<hash.updating<<" started="<<hash.start_sent<<" fails="<<unsigned(hash.fails)<<" error="<<hash.last_error.value()<<" category="<<hash.last_error.category().name()<<" next="<<hash.next_announce.time_since_epoch().count()<<" minimum="<<hash.min_announce.time_since_epoch().count();}}}
      }
      selectedComplete=actual.is_finished;v.seeding=actual.is_seeding;v.verifiedBytes=actual.total_done;v.checked=m->counters->checked.load()&&!actual.errc;v.downloadRate=actual.download_payload_rate;v.uploadRate=actual.upload_payload_rate;v.peers=actual.num_peers;v.seeds=actual.num_seeds;v.diagnostics.statusAvailable=true;v.diagnostics.paused=bool(actual.flags&lt::torrent_flags::paused);v.diagnostics.alerts=m->alerts;dl=m->handle.download_limit();ul=m->handle.upload_limit();}
    if(m->retired.requested){v.phase=Phase::Retiring;}Observation row{.hash=m->data().hash,.torrent=std::move(v),.queued=m->queued,.selectedComplete=selectedComplete,.downloadLimit=dl,.uploadLimit=ul};row.queueUnavailable=s.queueUnavailable;row.completionPositionUnavailable=m->completionPositionUnavailable;
    const auto& original=m->data().storage.capturedDownload();
    row.cumulativeReceived=engine::cumulativePayload(original.downloaded,row.torrent.payloadDownloaded);
    row.cumulativeSent=engine::cumulativePayload(original.uploaded,row.torrent.payloadUploaded);
    if (stateObserved && row.torrent.phase == Phase::Active && row.torrent.failure.empty()
        && m->data().source->prepared() && s.source->prepared()
        && ((row.cumulativeReceived && *row.cumulativeReceived > original.downloaded.value_or(0))
            || (row.cumulativeSent && *row.cumulativeSent > original.uploaded.value_or(0)))) {
      if (!m->data().backupBaseline) { m->data().backupBaseline=std::make_shared<const core::SavedDownload>(original); }
      row.backupBaseline=m->data().backupBaseline;
    }
    row.lastActiveTagMillis = m->tagState.lastActiveMillis();
    if (stateObserved && row.torrent.phase == Phase::Active && row.torrent.failure.empty()
        && s.source->tagManagerEnabled()) {
      row.downloadTagMembers = m->tagState.members();
    }
    if(s.session){const auto settings=s.session->get_settings();row.globalDownloadLimit=settings.get_int(lt::settings_pack::download_rate_limit);row.globalUploadLimit=settings.get_int(lt::settings_pack::upload_rate_limit);row.loopbackPeerClasses=s.session->get_peer_class_filter().access(lt::address_v4::loopback());}
    out.push_back(std::move(row));
  }return out;
}
std::vector<std::array<std::uint8_t,20>> OwnedProfile::peerIds(const core::TorrentHash& h)const{
  try{
    const auto& s=*impl_;if(!s.onThread()||s.phase!=Phase::Active){return {};}const auto* m=s.find(h);
    if(!m||!m->installed||m->retired.requested
        ||!m->failure.empty()||!s.failure.empty()||!m->handle.is_valid()){return {};}
    std::vector<lt::peer_info> peers;m->handle.get_peer_info(peers);
    std::vector<std::array<std::uint8_t,20>> out;
    for(const auto& peer:peers){
      if(!(peer.flags&lt::peer_info::i2p_socket)||peer.pid.is_all_zeros()){continue;}
      std::array<std::uint8_t,20> id{};std::copy_n(peer.pid.begin(),id.size(),id.begin());
      if(std::ranges::find(out,id)==out.end()){out.push_back(id);if(out.size()==4){break;}}
    }return out;
  }catch(...){return {};}
}
std::optional<std::size_t> OwnedProfile::peerSetCount(const core::TorrentHash& hash,
    std::uint64_t admission,std::uint64_t generation)const {
  constexpr std::size_t maximum=512;
  const auto& s=*impl_;
  if(!s.onThread()||s.phase!=Phase::Active||s.sourcePaused||!s.session||
      !admission||!generation||generation!=s.generation||!s.source->prepared()||!s.failure.empty()){return {};}
  const auto* member=s.find(hash);
  if(!member||member->data().admission!=admission||!member->installed||member->queued||
      member->retired.requested||!member->failure.empty()||!member->handle.is_valid()||
      !member->data().source->prepared()||!member->counters->checked.load()){return {};}
  try{
    const auto actual=member->handle.status(lt::torrent_handle::query_torrent_file);
    if(actual.errc||(actual.flags&lt::torrent_flags::paused)||actual.num_peers<0||
        std::size_t(actual.num_peers)>maximum||
        (actual.state!=lt::torrent_status::downloading&&actual.state!=lt::torrent_status::finished&&
         actual.state!=lt::torrent_status::seeding)){return {};}
    const auto info=actual.torrent_file.lock();
    if(!info||info->num_pieces()<0||
        (std::uint64_t(info->num_pieces())+7)/8>2*1024*1024/maximum){return {};}
    std::vector<lt::peer_info> peers;
    member->handle.get_peer_info(peers);
    if(peers.size()>maximum||!member->data().source->prepared()||!s.source->prepared()){return {};}
    std::size_t count=0;
    for(const auto& peer:peers){
      if(peer.flags&(lt::peer_info::connecting|lt::peer_info::handshake)){continue;}
      if(!(peer.flags&lt::peer_info::i2p_socket)){return {};}
      ++count;
    }
    return count;
  }catch(...){return {};}
}
OwnedProfile::Phase OwnedProfile::phase()const noexcept{return impl_->phase;}
bool OwnedProfile::sourceCurrent()const noexcept{return impl_->source->prepared();}
void OwnedProfile::revokeSource()noexcept{impl_->source->revoke();}
void OwnedProfile::close(){if(impl_->onThread()){impl_->close();}}
void OwnedProfile::join(){auto& s=*impl_;s.joining.reset();if(s.bridge){s.bridge->joinSource();}if(s.permit){s.permit->join();}}
bool OwnedProfile::event(QEvent* e){if(e->type()==SourcePermit::wakeEvent()){auto& s=*impl_;if(s.permit){const auto p=s.permit->phase();if(p==SourcePermit::Phase::Ready||p==SourcePermit::Phase::Failed||p==SourcePermit::Phase::Stopped){s.startup();}else if(p==SourcePermit::Phase::Idle&&(s.startRequested||s.memberInstallRequested||(s.bridgeRequested&&s.channel->ownedTransportIdle())||s.channel->sourceEffectsPending())){s.requestStartup();}}return true;}return QObject::event(e);}
}
