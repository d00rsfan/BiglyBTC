// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_owner.hpp"
#include "owner_runtime_guard.hpp"
#include "source_permit.hpp"
#include "retirement_counters.hpp"
#include "metadata_disk_router.hpp"
#include "managed_queue_observations.hpp"
#include "biglybtc/queue_observations.hpp"
#include "biglybtc/queue_positions.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/biglybtc_private_i2p_owner.hpp>
#include <libtorrent/biglybtc_queue_observer.hpp>
#include <libtorrent/ip_filter.hpp>
#include <QDateTime>
#include <QMetaEnum>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <openssl/rand.h>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <map>

namespace biglybtc::services::i2p::provider {
namespace {
namespace lt=libtorrent;
using Error=storage::Error;using Code=storage::ErrorCode;
std::int64_t nowMillis(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
bool entropy(std::uint64_t& value){return RAND_bytes(reinterpret_cast<unsigned char*>(&value),sizeof(value))==1&&value;}
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
  std::uint64_t admission=0;std::size_t inventoryIndex=0;
  Impl(core::TorrentHash h,std::shared_ptr<ProfileSource> s,engine::detail::ManagedStorage m):hash(h),source(std::move(s)),storage(std::move(m)){}
};
OwnedProfile::Prepared::Prepared(std::unique_ptr<Impl> v):impl_(std::move(v)){}
OwnedProfile::Prepared::~Prepared()=default;
struct OwnedProfile::QueueState::Impl {
  struct Row {core::TorrentHash hash;std::uint64_t admission;std::optional<std::int64_t> position;bool force;};
  std::vector<Row> rows;std::uint64_t revision=0;
};
OwnedProfile::QueueState::QueueState(std::unique_ptr<Impl> p):impl_(std::move(p)){}
OwnedProfile::QueueState::~QueueState()=default;
struct OwnedProfile::Impl {
  struct Member {
    std::unique_ptr<Prepared> prepared;lt::torrent_handle handle;
    std::shared_ptr<detail::RetirementCounters> counters=std::make_shared<detail::RetirementCounters>();
    std::shared_ptr<engine::detail::ManagedQueueObservations> queue;
    core::QueueObservedFacts queueFacts;core::QueueActivityMemory activity;
    std::optional<std::int64_t> livePosition;std::optional<bool> observedComplete;
    bool forceStart=false,clearCompletedForce=false,justCompleted=false,completionPositionUnavailable=false;
    std::atomic<bool> checkpointed{false};
    std::optional<core::QueueObservedDurations> finalDurations;
    OwnedTorrent::RetirementObservation retired;std::chrono::steady_clock::time_point deadline{};
    engine::detail::ManagedPayloadCounters::Sample finalCounters;
    std::vector<std::uint8_t> pieces;std::int64_t nextScrape=0;bool scrapePending=false,weakExpiryObserved=false;
    bool complete=false,unchecked=false,pauseSettled=false,pauseSeen=false,saveRequested=false,removed=false,removing=false,queued=true,installed=false,registered=false;
    OwnedTorrent::AlertDiagnostics alerts;std::string failure;
    auto& data(){return *prepared->impl_;}const auto& data()const{return *prepared->impl_;}
  };
  OwnedProfile* q=nullptr;std::shared_ptr<ProfileSource> source;std::optional<ClientIdentity> identity;
  std::shared_ptr<void> pin;std::shared_ptr<engine::detail::MetadataDiskPolicy> disks=std::make_shared<engine::detail::MetadataDiskPolicy>();
  std::unique_ptr<lt::session> session;std::optional<lt::session_proxy> joining;
  std::unique_ptr<i2cp::InitialChannel> channel;std::unique_ptr<Bridge> bridge;std::unique_ptr<SourcePermit> permit;
  std::map<std::string,std::unique_ptr<Member>> members;
  QTimer timer;Options options;core::QueuePolicy queuePolicy;Rates rates;Phase phase=Phase::Prepared;
  std::uint64_t admission=0,generation=0;bool startRequested=false,bridgeRequested=false,installQueued=false,memberInstallRequested=false;std::int64_t started=nowMillis();
  std::uint32_t queueUnavailable=0;std::string failure;
  bool onThread()const{return QThread::currentThread()==q->thread();}
  Member* find(const core::TorrentHash& h)const{const auto i=members.find(h.hex());return i==members.end()?nullptr:i->second.get();}
  void requestInstall(){
    if(phase!=Phase::Starting||!bridge||installQueued||bridge->observation().phase!=Bridge::Phase::Ready){return;}
    if(std::ranges::none_of(members,[](const auto& entry){return !entry.second->retired.requested;})){return;}
    installQueued=true;if(!bridge->installProvider([this](void* policy){installQueued=false;return installSession(policy);})){installQueued=false;close();}
  }
  void close(const char* reason=nullptr){
    if(reason&&failure.empty()){failure=reason;}if(phase==Phase::Withdrawn){return;}phase=Phase::Withdrawn;timer.stop();source->revoke();identity.reset();
    if(bridge){bridge->close();}if(channel){channel->close();}if(permit){permit->stop();}
    if(session){if(generation){(void)lt::biglybtc_withdraw_private_i2p_owner_v1(session.get(),generation);}joining.emplace(session->abort());session.reset();}
    for(auto& [key,m]:members){(void)key;m->handle={};if(m->retired.requested&&!m->retired.readyForJoin&&!m->retired.failure){m->retired.failure=Error{Code::Conflict,"Shared I2P session withdrew before target retirement; recheck retained"};}}
  }
  void fail(Member& m,Error e){m.failure=e.message;m.retired.failure=std::move(e);/* Preserve siblings; this member cannot checkpoint. */if(m.handle.is_valid()){m.handle.pause();}}
  lt::BiglybtcPrivateI2pTorrentV1 registration(const Member& m)const{
    lt::BiglybtcPrivateI2pTorrentV1 v;v.generation=generation;v.admission=m.data().admission;const auto& full=m.data().storage.identity().full;
    if(full.v1){v.hashes|=1;std::transform(full.v1->begin(),full.v1->end(),v.torrent_v1.begin(),[](std::byte b){return std::to_integer<unsigned char>(b);});}
    if(full.v2){v.hashes|=2;std::transform(full.v2->begin(),full.v2->end(),v.torrent_v2.begin(),[](std::byte b){return std::to_integer<unsigned char>(b);});}return v;
  }
  engine::detail::ManagedPayloadCounters::Sample sample(Member& m,lt::torrent_status& actual,bool pieces=false){
    for(int n=0;n<4;++n){const auto epoch=m.counters->payload.epoch();if(!epoch){break;}actual=m.handle.status(pieces?lt::torrent_handle::query_pieces:lt::status_flags_t{});
      std::optional<engine::detail::ManagedPayloadCounters::Bad> bad;
      if(actual.total_redundant_bytes>=0&&actual.total_failed_bytes>=0&&actual.total_redundant_bytes<std::numeric_limits<std::int64_t>::max()&&actual.total_failed_bytes<std::numeric_limits<std::int64_t>::max()){bad={{actual.total_redundant_bytes,actual.total_failed_bytes}};}
      std::optional<engine::detail::ManagedPayloadCounters::Protocol> protocol;
      if(actual.total_payload_download>=0&&actual.total_payload_upload>=0&&actual.total_download>=actual.total_payload_download&&actual.total_upload>=actual.total_payload_upload){protocol={{actual.total_download-actual.total_payload_download,actual.total_upload-actual.total_payload_upload}};}
      auto v=m.counters->payload.combine(*epoch,actual.total_payload_download,actual.total_payload_upload,protocol,bad);if(v.state!=engine::detail::ManagedPayloadCounters::Read::Retry){return v;}
    }return {};
  }
  bool install(Member& m,bool configured=false){
    if(!session||m.installed||m.retired.requested||!m.data().source->prepared()){return false;}
    const auto tuple=registration(m);if(!configured&&lt::biglybtc_admit_private_i2p_torrent_v1(session.get(),&tuple)!=0){return false;}m.registered=true;
    auto params=m.data().storage.parameters();if(params.total_downloaded||params.total_uploaded){return false;}
    if(auto files=m.data().storage.mappedFiles()){
      const auto path=(std::filesystem::path(params.save_path)/".BiglyBTC-original-storage"/(m.data().hash.hex()+"-owned-i2p-"+std::to_string(m.data().admission))).lexically_normal();
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
    params.flags|=lt::torrent_flags::paused|lt::torrent_flags::stop_when_ready|lt::torrent_flags::duplicate_is_error;params.flags&=~lt::torrent_flags::auto_managed;
    lt::error_code ec;m.handle=session->add_torrent(params,ec);if(ec||!m.handle.is_valid()){return false;}
    m.installed=true;m.pauseSettled=false;m.handle.resume();return true; // stop_when_ready holds after genuine checking.
  }
  bool installSession(void* opaque){
    if(phase!=Phase::Starting){return false;}
    auto firstLive=std::ranges::find_if(members,[](const auto& entry){return !entry.second->retired.requested;});
    // A queued bridge-install turn may race a genuine never-installed Stop.
    // Keep the shared endpoint unconfigured until another live member exists.
    if(firstLive==members.end()){return true;}auto& policy=*static_cast<lt::BiglybtcPrivateI2pOwnerPolicyV1*>(opaque);generation=policy.generation;
    auto& first=*firstLive->second;auto tuple=registration(first);policy.admission=tuple.admission;policy.hashes=tuple.hashes;policy.torrent_v1=tuple.torrent_v1;policy.torrent_v2=tuple.torrent_v2;
    if(lt::biglybtc_configure_private_i2p_owner_v1(session.get(),&policy)!=0){return false;}
    lt::settings_pack network;network.set_str(lt::settings_pack::i2p_hostname,"127.0.0.1");network.set_int(lt::settings_pack::i2p_port,policy.bridge_port);session->apply_settings(network);
    // All I2P peers are subject to the shared global class even though the
    // authenticated private bridge itself is a loopback transport.
    lt::ip_filter filter;filter.add_rule(lt::address_v4::any(),lt::address_v4::broadcast(),1u<<static_cast<unsigned>(lt::session::global_peer_class_id));
    filter.add_rule(lt::address_v6::any(),lt::make_address_v6("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),1u<<static_cast<unsigned>(lt::session::global_peer_class_id));session->set_peer_class_filter(filter);
    bool firstRecord=true;for(auto& [key,m]:members){(void)key;if(m->retired.requested){continue;}if(!install(*m,firstRecord)){return false;}firstRecord=false;}phase=Phase::Active;timer.start(250);return true;
  }
  void alerts(){
    if(!session){return;}std::vector<lt::alert*> batch;session->pop_alerts(&batch);if(batch.size()>4096){failure="I2P alert batch exceeded4096";close();return;}
    for(const auto* alert:batch){
      if(lt::alert_cast<lt::alerts_dropped_alert>(alert)||lt::alert_cast<lt::session_error_alert>(alert)){failure="Shared I2P provider lost an essential observation";close();return;}
      const auto* torrent=dynamic_cast<const lt::torrent_alert*>(alert);if(!torrent){continue;}
      Member* target=nullptr;for(auto& [key,m]:members){(void)key;if(m->handle==torrent->handle){target=m.get();break;}}if(!target){continue;}auto& m=*target;if(m.retired.readyForJoin){continue;}
      if(lt::alert_cast<lt::tracker_announce_alert>(alert)){++m.alerts.announces;}
      if(lt::alert_cast<lt::tracker_reply_alert>(alert)){++m.alerts.trackerReplies;}
      if(lt::alert_cast<lt::tracker_error_alert>(alert)){++m.alerts.trackerErrors;}
      if(lt::alert_cast<lt::scrape_reply_alert>(alert)||lt::alert_cast<lt::scrape_failed_alert>(alert)){m.scrapePending=false;}
      if(lt::alert_cast<lt::file_error_alert>(alert)||lt::alert_cast<lt::torrent_error_alert>(alert)||lt::alert_cast<lt::save_resume_data_failed_alert>(alert)){fail(m,{Code::Io,"Target I2P provider/storage observation failed; recheck retained"});continue;}
      if(lt::alert_cast<lt::torrent_paused_alert>(alert)){m.pauseSettled=true;}
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
  void schedule(){
    if(phase!=Phase::Active||!session){return;}alerts();if(!session){return;}const auto now=nowMillis();std::vector<core::QueueTorrentFacts> facts;std::map<std::uint64_t,Member*> targets;
    core::QueueContext context;context.nowMillis=now;context.ownerStartedMillis=started;std::int64_t down=0,up=0;bool any=false,onlySeeds=true;
    for(auto& [key,m]:members){(void)key;if(!m->installed||m->retired.requested||!m->failure.empty()||!m->handle.is_valid()){continue;}
      lt::torrent_status s;const auto counters=sample(*m,s);if(counters.state!=engine::detail::ManagedPayloadCounters::Read::Accepted){s=m->handle.status();}
      const bool checked=m->counters->checked.load()&&!s.errc;
      if(checked&&m->queued&&!m->scrapePending&&now>=m->nextScrape){m->handle.scrape_tracker();m->scrapePending=true;m->nextScrape=now+10000;}
      core::QueueTorrentFacts f;const auto& saved=m->data().storage.queueMetadata();
      f.token=m->data().admission;f.position=static_cast<std::uint32_t>(std::clamp<std::int64_t>(saved.position,1,std::numeric_limits<std::int32_t>::max()));
      f.complete=s.is_finished;f.state=!checked?core::QueueTorrentState::Checking:m->queued?core::QueueTorrentState::Queued:f.complete?core::QueueTorrentState::Seeding:core::QueueTorrentState::Downloading;
      if(checked){if(m->observedComplete&&!*m->observedComplete&&f.complete){m->justCompleted=true;if(m->forceStart&&!queuePolicy.retainForceStartOnComplete){m->clearCompletedForce=true;}}m->observedComplete=f.complete;}
      f.forceStart=m->forceStart;f.newlyCompleted=m->justCompleted;f.minimumShareRatio=saved.minimumShareRatio;f.maximumShareRatio=saved.maximumShareRatio;f.activity=m->activity;
      f.connectedLeechers=std::max(0,s.num_peers-s.num_seeds);f.downloadBytesPerSecond=s.download_payload_rate;f.uploadBytesPerSecond=s.upload_payload_rate;
      f.sizeBytes=s.total_wanted;f.remainingBytes=std::max<std::int64_t>(0,s.total_wanted-s.total_wanted_done);f.idleUploadSourceKnown=false;
      if(m->queue){m->queueFacts=m->queue->facts(now);const auto& observed=m->queueFacts;f.runningMillis=observed.runningMillis;f.seedingMillis=observed.seedingMillis;
        if(observed.durations){f.secondsDownloading=core::sourceQueueSum(saved.secondsDownloading,observed.durations->downloading);f.secondsSeeding=core::sourceQueueSum(saved.secondsSeeding,observed.durations->seeding);}
        if(observed.swarm){const auto& swarm=*observed.swarm;f.swarm=core::QueueSwarm{swarm.seeds,swarm.peers,swarm.success,swarm.startupSuccess};}}
      if(counters.state==engine::detail::ManagedPayloadCounters::Read::Accepted&&counters.badPrefix&&!saved.badPayloadCountersLost&&counters.received<=std::numeric_limits<std::int64_t>::max()-saved.downloaded&&counters.sent<=std::numeric_limits<std::int64_t>::max()-saved.uploaded&&counters.badPrefix->discarded<=std::numeric_limits<std::int64_t>::max()-saved.discarded&&counters.badPrefix->hashFailed<=std::numeric_limits<std::int64_t>::max()-saved.hashFailed){f.shareRatio=core::sourceQueueShareRatio(saved.downloaded+counters.received,saved.uploaded+counters.sent,saved.discarded+counters.badPrefix->discarded,saved.hashFailed+counters.badPrefix->hashFailed);}
      down+=s.download_payload_rate;up+=s.upload_payload_rate;any=true;onlySeeds&=f.complete;targets.emplace(f.token,m.get());facts.push_back(std::move(f));
    }
    context.seedingOnly=any&&onlySeeds;context.uploadBytesPerSecond=up;context.downloadBytesPerSecond=down;context.downloadLimitBytesPerSecond=rates.download;context.uploadLimitBytesPerSecond=context.seedingOnly&&rates.seedingOnly?rates.seedingUpload:rates.upload;
    lt::settings_pack limits;limits.set_int(lt::settings_pack::download_rate_limit,rates.download);limits.set_int(lt::settings_pack::upload_rate_limit,static_cast<int>(context.uploadLimitBytesPerSecond));session->apply_settings(limits);
    std::vector<std::size_t> order;for(std::size_t i=0;i<facts.size();++i){order.push_back(i);}
    std::ranges::sort(order,[&](auto a,auto b){return targets.at(facts[a].token)->data().inventoryIndex<targets.at(facts[b].token)->data().inventoryIndex;});
    std::vector<core::QueuePositionInput> positions;for(auto i:order){const auto* m=targets.at(facts[i].token);positions.push_back({facts[i].token,m->livePosition,facts[i].complete,true});}
    const auto normalized=core::normalizeQueuePositions(positions);if(!normalized){failure="Original I2P queue positions are ambiguous";close();return;}
    for(std::size_t i=0;i<order.size();++i){facts[order[i]].position=(*normalized)[i];}
    auto plan=core::evaluateQueue(queuePolicy,facts,context);if(!plan){failure="Original I2P queue facts are inconsistent";close();return;}queueUnavailable=plan->unavailableInputs;
    for(auto action:{core::QueueAction::Queue,core::QueueAction::Run}){for(const auto& decision:plan->decisions){auto* m=targets.at(decision.token);m->activity=decision.activity;if(decision.moveNewSeedToTop){m->completionPositionUnavailable=true;}m->justCompleted=false;if(decision.action!=action||!m->counters->checked.load()){continue;}
      m->handle.unset_flags(lt::torrent_flags::auto_managed|lt::torrent_flags::stop_when_ready);
      if(action==core::QueueAction::Queue){if(!m->queued){m->pauseSettled=false;m->handle.pause();m->queued=true;}}
      else if(m->queued&&m->pauseSettled){m->pauseSettled=false;m->handle.resume();m->queued=false;}
    }}
  }
  void requestStartup(){if((phase==Phase::Starting||phase==Phase::Active)&&permit&&permit->phase()==SourcePermit::Phase::Idle){permit->request();}}
  void startup(){
    if((phase!=Phase::Starting&&phase!=Phase::Active)||!permit){return;}auto revision=permit->take();if(!revision){if(permit->phase()==SourcePermit::Phase::Failed||permit->phase()==SourcePermit::Phase::Stopped){close("I2P owner source validation failed");}return;}
    struct Ack{SourcePermit& p;~Ack(){p.acknowledge();}} ack{*permit};
    if(!source->revisionCurrent(*revision)||!channel->resumeAuthorizedEffects()){close("I2P owner effect authority refused");return;}
    if(startRequested){startRequested=false;const auto wall=QDateTime::currentMSecsSinceEpoch();if(!identity||wall<=0||!channel->createLeaseSession(std::move(*identity),options.session,static_cast<std::uint64_t>(wall))){close();return;}identity.reset();}
    if(phase==Phase::Active&&memberInstallRequested){
      memberInstallRequested=false;
      try{for(auto& [key,m]:members){(void)key;if(m->installed||m->retired.requested||!m->failure.empty()){continue;}if(!source->revisionCurrent(*revision)||!install(*m)){fail(*m,{Code::Io,"Cannot install measured I2P member under its fresh source permit"});}}}
      catch(...){failure="I2P member installation ownership became uncertain; recheck retained";close();return;}
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
std::expected<std::unique_ptr<OwnedProfile>,Error> OwnedProfile::prepare(const storage::ProfileLease& lease,const core::TorrentHash& hash){
  try{auto pin=privateOwnerRuntimePin();if(!pin||lt::biglybtc_private_i2p_torrents_abi_v1()!=lt::BiglybtcPrivateI2pTorrentsAbiV1){return std::unexpected(Error{Code::Unsupported,"Approved multi-torrent private I2P provider required"});}
    auto source=ProfileSource::createSession(lease);if(!source){return std::unexpected(source.error());}auto s=std::make_unique<Impl>();s->source=*source;s->pin=std::move(pin);
    auto owner=std::unique_ptr<OwnedProfile>(new OwnedProfile(std::move(s)));auto first=owner->prepareTorrent(hash,{});if(!first){return std::unexpected(first.error());}
    auto identity=(*source)->takeIdentity();if(!identity){return std::unexpected(identity.error());}owner->impl_->identity.emplace(std::move(*identity));owner->impl_->admission=(*first)->impl_->admission;
    auto settings=(*first)->impl_->source->policy().settings();settings.set_int(lt::settings_pack::alert_mask,static_cast<int>(lt::alert_category::error|lt::alert_category::status|lt::alert_category::storage|lt::alert_category::tracker));settings.set_int(lt::settings_pack::alert_queue_size,4096);
    lt::session_params params(std::move(settings));params.disk_io_constructor=engine::detail::metadataDiskConstructor(owner->impl_->disks);owner->impl_->session=std::make_unique<lt::session>(std::move(params));
    auto added=owner->add(std::move(*first));if(!added){return std::unexpected(added.error());}return owner;
  }catch(...){return std::unexpected(Error{Code::Io,"Cannot prepare shared original I2P profile"});}
}
std::expected<std::unique_ptr<OwnedProfile::Prepared>,Error> OwnedProfile::prepareTorrent(const core::TorrentHash& hash,const Retained& retained){
  auto access=impl_->source->lease()->sourceAccess();if(!access){return std::unexpected(access.error());}
  auto child=impl_->source->prepareChild(hash,retained.claims,retained.identities);if(!child){return std::unexpected(child.error());}auto storage=(*child)->prepareStorage();if(!storage){return std::unexpected(storage.error());}
  auto value=std::unique_ptr<Prepared>(new Prepared(std::make_unique<Prepared::Impl>(hash,*child,std::move(*storage))));
  auto inventory=storage::ConfigFile::load(*impl_->source->lease(),"downloads.config");if(!inventory){return std::unexpected(inventory.error());}auto library=core::readLibrary(inventory->document());if(!library){return std::unexpected(Error{Code::InvalidFormat,"Invalid I2P queue inventory"});}
  for(std::size_t n=0;n<library->downloads.size();++n){if(library->downloads[n].hash==hash){value->impl_->inventoryIndex=n;break;}}
  if(!entropy(value->impl_->admission)){return std::unexpected(Error{Code::Io,"I2P member admission entropy unavailable"});}return value;
}
bool OwnedProfile::begin(const Options& options,core::QueuePolicy policy,Rates rates){
  auto& s=*impl_;if(!s.onThread()||s.phase!=Phase::Prepared||!options.ports.peer||!options.ports.localPeer){return false;}s.options=options;s.queuePolicy=std::move(policy);s.rates=rates;s.phase=Phase::Starting;
  s.permit=std::make_unique<SourcePermit>(s.source,this);if(!s.channel->bindEffectAuthority(s.source->effectAuthority())||!s.channel->begin(options.router)){s.close();return false;}return true;
}
OwnedProfile::Retained OwnedProfile::retained()const{
  Retained v;for(const auto& [key,m]:impl_->members){(void)key;const auto claims=m->data().storage.claims();v.claims.insert(v.claims.end(),claims.begin(),claims.end());v.identities.push_back(m->data().storage.identity());}return v;
}
std::expected<void,Error> OwnedProfile::add(std::unique_ptr<Prepared> p){
  auto& s=*impl_;if(!s.onThread()||s.phase==Phase::Withdrawn||!p||s.members.size()>=64||s.members.contains(p->impl_->hash.hex())){return std::unexpected(Error{Code::Conflict,"I2P member is duplicate, stale or exceeds the64-torrent bound"});}
  const auto key=p->impl_->hash.hex();auto m=std::make_unique<Impl::Member>();m->prepared=std::move(p);m->livePosition=m->data().storage.capturedDownload().position;m->forceStart=m->data().storage.capturedDownload().forceStart;auto [i,ok]=s.members.emplace(key,std::move(m));(void)ok;
  if(s.phase==Phase::Active){s.memberInstallRequested=true;s.requestStartup();}else if(s.phase==Phase::Starting){s.requestInstall();}return {};
}
bool OwnedProfile::retire(const core::TorrentHash& hash,std::chrono::steady_clock::time_point end){
  auto& s=*impl_;auto* m=s.find(hash);if(!s.onThread()||!m||m->retired.requested||end<=std::chrono::steady_clock::now()){return false;}s.alerts();m->retired.requested=true;m->deadline=end;
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
  if(m->pauseSettled){m->pauseSeen=true;m->saveRequested=true;m->handle.save_resume_data(lt::torrent_handle::flush_disk_cache);}
  else if(!(actual.flags&lt::torrent_flags::paused)){m->handle.pause();} // Otherwise await its genuine outstanding pause callback.
  return true;
}
OwnedTorrent::RetirementObservation OwnedProfile::retirement(const core::TorrentHash& h){impl_->alerts();auto* m=impl_->find(h);return m?m->retired:OwnedTorrent::RetirementObservation{.failure=Error{Code::Missing,"I2P member is absent"}};}
std::expected<void,Error> OwnedProfile::checkpoint(const core::TorrentHash& h){
  auto& s=*impl_;auto* m=s.find(h);if(!m||!m->retired.readyForJoin||!m->retired.joined||m->retired.failure||m->checkpointed.load()){return std::unexpected(Error{Code::Conflict,"Target I2P checkpoint requires exact flush/removal/storage expiration"});}
  if(m->installed&&!m->unchecked){auto result=s.source->publish(h,[&]()->std::expected<void,Error>{
      auto valid=m->data().source->validateRetiredSnapshot();if(!valid){return valid;}
      std::optional<engine::detail::ManagedBadPayloadBytes> bad;if(m->finalCounters.badPrefix){bad={{m->finalCounters.badPrefix->discarded,m->finalCounters.badPrefix->hashFailed}};}
      const auto durations=m->finalDurations;
      return m->data().storage.checkpoint(*s.source->lease(),m->pieces,m->complete,m->finalCounters.received,m->finalCounters.sent,durations?durations->downloading:0,durations?durations->seeding:0,{},bad,m->finalCounters.bad.has_value());
    });if(!result){return result;}}
  auto released=s.source->releaseChild(h);if(!released){return released;}m->checkpointed.store(true);return {};
}
std::expected<void,Error> OwnedProfile::release(const core::TorrentHash& h){auto& s=*impl_;auto* m=s.find(h);if(!s.onThread()||!m||!m->checkpointed.load()){return std::unexpected(Error{Code::Conflict,"I2P member storage still retained"});}s.members.erase(h.hex());s.schedule();return {};}
std::expected<void,Error> OwnedProfile::publish(const core::TorrentHash& h,const std::function<std::expected<void,Error>()>& f){return impl_->source->publish(h,f);}
std::expected<void,Error> OwnedProfile::publishPositions(const std::function<std::expected<void,Error>()>& f){return impl_->source->publishPositions(f);}
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
    int dl=0,ul=0;bool selectedComplete=false;if(m->handle.is_valid()){lt::torrent_status actual;const auto counts=s.sample(*m,actual);if(counts.state==engine::detail::ManagedPayloadCounters::Read::Accepted){v.payloadDownloaded=counts.received;v.payloadUploaded=counts.sent;}else{actual=m->handle.status();}
      selectedComplete=actual.is_finished;v.seeding=actual.is_seeding;v.verifiedBytes=actual.total_done;v.checked=m->counters->checked.load()&&!actual.errc;v.downloadRate=actual.download_payload_rate;v.uploadRate=actual.upload_payload_rate;v.peers=actual.num_peers;v.seeds=actual.num_seeds;v.diagnostics.statusAvailable=true;v.diagnostics.paused=bool(actual.flags&lt::torrent_flags::paused);v.diagnostics.alerts=m->alerts;dl=m->handle.download_limit();ul=m->handle.upload_limit();}
    if(m->retired.requested){v.phase=Phase::Retiring;}Observation row{.hash=m->data().hash,.torrent=std::move(v),.queued=m->queued,.selectedComplete=selectedComplete,.downloadLimit=dl,.uploadLimit=ul};row.queueUnavailable=s.queueUnavailable;row.completionPositionUnavailable=m->completionPositionUnavailable;
    if(s.session){const auto settings=s.session->get_settings();row.globalDownloadLimit=settings.get_int(lt::settings_pack::download_rate_limit);row.globalUploadLimit=settings.get_int(lt::settings_pack::upload_rate_limit);row.loopbackPeerClasses=s.session->get_peer_class_filter().access(lt::address_v4::loopback());}
    out.push_back(std::move(row));
  }return out;
}
OwnedProfile::Phase OwnedProfile::phase()const noexcept{return impl_->phase;}
void OwnedProfile::close(){if(impl_->onThread()){impl_->close();}}
void OwnedProfile::join(){auto& s=*impl_;s.joining.reset();if(s.bridge){s.bridge->joinSource();}if(s.permit){s.permit->join();}}
bool OwnedProfile::event(QEvent* e){if(e->type()==SourcePermit::wakeEvent()){auto& s=*impl_;if(s.permit){const auto p=s.permit->phase();if(p==SourcePermit::Phase::Ready||p==SourcePermit::Phase::Failed||p==SourcePermit::Phase::Stopped){s.startup();}else if(p==SourcePermit::Phase::Idle&&(s.startRequested||s.memberInstallRequested||(s.bridgeRequested&&s.channel->ownedTransportIdle())||s.channel->sourceEffectsPending())){s.requestStartup();}}return true;}return QObject::event(e);}
}
