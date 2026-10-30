// SPDX-License-Identifier: GPL-2.0-or-later
#include "owner.hpp"
#include "owner_runtime_guard.hpp"
#include "source_permit.hpp"
#include "retirement_counters.hpp"
#include "metadata_disk_router.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/biglybtc_private_i2p_owner.hpp>
#include <QDateTime>
#include <QPointer>
#include <QThread>
#include <QMetaObject>
#include <QMetaEnum>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <limits>
#include <vector>

namespace biglybtc::services::i2p::provider {
struct OwnedTorrent::Impl {
  static constexpr int MaximumAlerts=4096;
  OwnedTorrent* q=nullptr;
  std::shared_ptr<ProfileSource> source;
  engine::detail::ManagedStorage storage;
  std::optional<ClientIdentity> identity;
  std::shared_ptr<void> providerPin;
  std::shared_ptr<engine::detail::MetadataDiskPolicy> disks=std::make_shared<engine::detail::MetadataDiskPolicy>();
  std::unique_ptr<i2cp::InitialChannel> channel;
  std::unique_ptr<Bridge> bridge;
  std::unique_ptr<SourcePermit> permit;
  std::unique_ptr<libtorrent::session> session;
  std::optional<libtorrent::session_proxy> joining;
  libtorrent::torrent_handle handle;
  AlertDiagnostics diagnosticAlerts;
  std::shared_ptr<detail::RetirementCounters> counters=std::make_shared<detail::RetirementCounters>();
  RetirementObservation retired;
  std::chrono::steady_clock::time_point retirementDeadline{};
  bool pauseSeen=false,saveRequested=false,flushSeen=false,removing=false,removed=false,checkpointAttempted=false;
  std::vector<std::uint8_t> retiredPieces;
  engine::detail::ManagedPayloadCounters::Sample retiredCounters;
  bool retiredComplete=false;
  const char* retirementUnsafe=nullptr;int retirementUnsafeCode=0;
  Options options;Phase phase=Phase::Prepared;
  std::uint64_t admission=0,providerGeneration=0;bool notifyQueued=false,startRequested=false,bridgeRequested=false;
  Impl(std::shared_ptr<ProfileSource> s,engine::detail::ManagedStorage m,ClientIdentity key,std::shared_ptr<void> pin)
      :source(std::move(s)),storage(std::move(m)),identity(std::move(key)),providerPin(std::move(pin)){}
  bool onThread()const{return QThread::currentThread()==q->thread();}
  void notify(){
    if(notifyQueued){return;}notifyQueued=true;bool ok=false;
    try{const QPointer<OwnedTorrent> owner(q);ok=QMetaObject::invokeMethod(q,[owner]{if(!owner){return;}owner->impl_->notifyQueued=false;emit owner->changed();},Qt::QueuedConnection);}catch(...){ok=false;}
    if(!ok){notifyQueued=false;close();}
  }
  void close(){
    // A second close after successful removal/abort is idempotent: preserve the
    // frozen ready receipt while its external join/checkpoint is still pending.
    if(phase==Phase::Withdrawn){if(!retired.readyForJoin||retired.checkpointed){source->revoke();}return;}
    if(retired.requested&&!retired.readyForJoin&&!retired.failure){retired.failure=storage::Error{storage::ErrorCode::Conflict,"I2P retirement was interrupted; recheck remains required"};}
    phase=Phase::Withdrawn;source->revoke();identity.reset();
    if(bridge){bridge->close();}if(channel){channel->close();}
    if(permit){permit->stop();}
    if(session){
      if(providerGeneration){static_cast<void>(libtorrent::biglybtc_withdraw_private_i2p_owner_v1(session.get(),providerGeneration));}
      joining.emplace(session->abort());session.reset();handle={};
    }
    notify();
  }
  void join(){
    if(phase!=Phase::Withdrawn){return;}
    // Actual provider executor/disk join precedes releasing mapped payload
    // owners and SAME-OFD source authority. No successful checkpoint invented.
    joining.reset();if(bridge){bridge->joinSource();}if(permit){permit->join();}retired.joined=true;
  }
  void retirementFailure(storage::ErrorCode code,const char* message,int engineCode=0){
    if(!retired.failure){
      auto reason=std::string(message);if(engineCode){reason+=" (engine code "+std::to_string(engineCode)+")";}
      retired.failure=storage::Error{code,std::move(reason)};
    }close();
  }
  void markRetirementUnsafe(const char* reason,int engineCode=0)noexcept{
    if(!retirementUnsafe){retirementUnsafe=reason;retirementUnsafeCode=engineCode;}
  }
  engine::detail::ManagedPayloadCounters::Sample sampleCounters(libtorrent::torrent_status& actual,bool pieces){
    for(int attempt=0;attempt<4;++attempt){
      const auto epoch=counters->payload.epoch();if(!epoch){break;}
      actual=handle.status(pieces?libtorrent::torrent_handle::query_pieces:libtorrent::status_flags_t{});
      std::optional<engine::detail::ManagedPayloadCounters::Bad> bad;
      if(actual.total_redundant_bytes>=0&&actual.total_failed_bytes>=0
          &&actual.total_redundant_bytes<std::numeric_limits<std::int64_t>::max()
          &&actual.total_failed_bytes<std::numeric_limits<std::int64_t>::max()){
        bad=engine::detail::ManagedPayloadCounters::Bad{actual.total_redundant_bytes,actual.total_failed_bytes};
      }
      std::optional<engine::detail::ManagedPayloadCounters::Protocol> protocol;
      if(actual.total_payload_download>=0&&actual.total_payload_upload>=0&&actual.total_download>=actual.total_payload_download&&actual.total_upload>=actual.total_payload_upload){
        protocol=engine::detail::ManagedPayloadCounters::Protocol{actual.total_download-actual.total_payload_download,actual.total_upload-actual.total_payload_upload};
      }
      auto value=counters->payload.combine(*epoch,actual.total_payload_download,actual.total_payload_upload,protocol,bad);
      if(value.state!=engine::detail::ManagedPayloadCounters::Read::Retry){return value;}
    }
    return {};
  }
  void beginRetirement(std::chrono::steady_clock::time_point deadline){
    retired.requested=true;retirementDeadline=deadline;phase=Phase::Retiring;
    // Transport closure irreversibly revokes live effects. Only the immutable
    // held snapshot survives, for read-only validation AFTER full removal/join.
    if(bridge){bridge->close();}if(channel){channel->close();}if(permit){permit->stop();}
    handle.unset_flags(libtorrent::torrent_flags::auto_managed);handle.pause();
  }
  void retireAlert(const libtorrent::alert* item){
    if(phase!=Phase::Retiring){return;}
    if(std::chrono::steady_clock::now()>=retirementDeadline){retirementFailure(storage::ErrorCode::Io,"I2P retirement deadline expired before its next fence");return;}
    namespace lt=libtorrent;
    if(retirementUnsafe){retirementFailure(storage::ErrorCode::Io,retirementUnsafe,retirementUnsafeCode);return;}
    if(const auto* value=lt::alert_cast<lt::torrent_paused_alert>(item);value&&value->handle==handle){
      pauseSeen=true;
      if(!saveRequested){saveRequested=true;handle.save_resume_data(lt::torrent_handle::flush_disk_cache);}
    }
    if(const auto* value=lt::alert_cast<lt::save_resume_data_alert>(item);value&&value->handle==handle){
      if(!pauseSeen||!saveRequested||flushSeen){retirementFailure(storage::ErrorCode::Conflict,"Unexpected I2P retirement flush observation");return;}
      lt::torrent_status actual;auto count=sampleCounters(actual,true);
      const int pieces=storage.parameters().ti->num_pieces();
      if(!counters->checked.load(std::memory_order_acquire)||actual.errc||!(actual.flags&lt::torrent_flags::paused)
          ||count.state!=engine::detail::ManagedPayloadCounters::Read::Accepted
          ||value->params.have_pieces.size()!=pieces||actual.pieces.size()!=pieces){
        retirementFailure(storage::ErrorCode::Io,"Exact I2P retirement pieces/counters unavailable; recheck required");return;
      }
      retiredPieces.assign(static_cast<std::size_t>(pieces),0);
      for(int i=0;i<pieces;++i){
        const auto index=lt::piece_index_t{i};
        if(value->params.have_pieces[index]!=actual.pieces[index]){retirementFailure(storage::ErrorCode::Conflict,"I2P flushed-piece snapshot changed");return;}
        retiredPieces[static_cast<std::size_t>(i)]=value->params.have_pieces[index]?1:0;
      }
      if(std::chrono::steady_clock::now()>=retirementDeadline){retirementFailure(storage::ErrorCode::Io,"I2P retirement deadline expired during its snapshot");return;}
      retiredCounters=count;retiredComplete=actual.is_seeding;flushSeen=true;
      session->remove_torrent(handle);removing=true;
    }
    if(const auto* value=lt::alert_cast<lt::torrent_removed_alert>(item);value&&value->handle==handle){if(!removing){retirementFailure(storage::ErrorCode::Conflict,"I2P torrent disappeared before its flush fence");return;}removed=true;}
  }
  void advanceRetirement(){
    if(phase!=Phase::Retiring){return;}
    if(std::chrono::steady_clock::now()>=retirementDeadline){retirementFailure(storage::ErrorCode::Io,"I2P retirement deadline expired; recheck remains required");return;}
    if(flushSeen&&removing&&removed&&!handle.is_valid()){
      // Full session/disk join remains an external operation. Keep the retained
      // profile snapshot for the subsequent read-only validation+original CAS.
      if(providerGeneration){static_cast<void>(libtorrent::biglybtc_withdraw_private_i2p_owner_v1(session.get(),providerGeneration));}
      joining.emplace(session->abort());session.reset();handle={};
      retired.readyForJoin=true;phase=Phase::Withdrawn;notify();
    }
  }
  void drainAlerts(){
    if(!session){return;}
    namespace lt=libtorrent;
    std::vector<lt::alert*> alerts;session->pop_alerts(&alerts);
    auto& totals=diagnosticAlerts;totals.currentShown=0;totals.currentOmitted=0;
    const auto add=[](std::uint64_t& count,std::size_t amount=1){count+=std::min<std::uint64_t>(std::numeric_limits<std::uint64_t>::max()-count,amount);};
    const auto error=[](const lt::error_code& code){return ErrorObservation{code.value(),std::string(code.category().name()).substr(0,64),code.message().substr(0,160)};};
    // The display stays capped at16. Essential retirement observations scan
    // the entire configured bounded queue, independently of display sampling.
    add(totals.drained,alerts.size());const auto inspected=std::min<std::size_t>(alerts.size(),MaximumAlerts);add(totals.uninspected,alerts.size()-inspected);
    if(inspected!=alerts.size()){markRetirementUnsafe("I2P alert batch exceeded4096; original recheck marker retained");}
    for(std::size_t i=0;i<inspected;++i){
      const auto* item=alerts[i];const lt::error_code* code=nullptr;bool selected=true;
      if(lt::alert_cast<lt::alerts_dropped_alert>(item)){markRetirementUnsafe("I2P provider reported alerts_dropped; original recheck marker retained");}
      else if(const auto* entry=lt::alert_cast<lt::session_error_alert>(item)){markRetirementUnsafe("I2P provider reported session_error; original recheck marker retained",entry->error.value());}
      else if(const auto* entry=lt::alert_cast<lt::file_error_alert>(item)){markRetirementUnsafe("I2P provider reported file_error; original recheck marker retained",entry->error.value());}
      else if(const auto* entry=lt::alert_cast<lt::torrent_error_alert>(item)){markRetirementUnsafe("I2P provider reported torrent_error; original recheck marker retained",entry->error.value());}
      else if(const auto* entry=lt::alert_cast<lt::save_resume_data_failed_alert>(item)){markRetirementUnsafe("I2P provider reported save_resume_data_failed; original recheck marker retained",entry->error.value());}
      if(lt::alert_cast<lt::tracker_announce_alert>(item)){add(totals.announces);}
      else if(const auto* entry=lt::alert_cast<lt::tracker_error_alert>(item)){add(totals.trackerErrors);code=&entry->error;}
      else if(lt::alert_cast<lt::tracker_reply_alert>(item)){add(totals.trackerReplies);}
      else if(const auto* entry=lt::alert_cast<lt::i2p_alert>(item)){add(totals.i2pErrors);code=&entry->error;}
      else if(const auto* entry=lt::alert_cast<lt::file_error_alert>(item)){add(totals.fileErrors);code=&entry->error;}
      else if(const auto* entry=lt::alert_cast<lt::torrent_error_alert>(item)){add(totals.torrentErrors);code=&entry->error;}
      else{selected=false;}
      if(selected){if(totals.currentShown<totals.current.size()){totals.current[totals.currentShown++]={item->type(),code?error(*code):ErrorObservation{}};}else{++totals.currentOmitted;}}
      retireAlert(item);
      // pop_alerts pointers belong to the session; a failure may have aborted it.
      if(!session){return;}
    }
    if(phase==Phase::Retiring&&retirementUnsafe){retirementFailure(storage::ErrorCode::Io,retirementUnsafe,retirementUnsafeCode);return;}
    advanceRetirement();
  }
  bool install(void* opaque){
    namespace lt=libtorrent;auto& policy=*static_cast<lt::BiglybtcPrivateI2pOwnerPolicyV1*>(opaque);
    if(phase!=Phase::Starting||!session||!source->prepared()||!source->policy().matches(*source->lease(),source->hash())){return false;}
    const auto& full=storage.identity().full;
    if(full.v1){policy.hashes|=1;std::transform(full.v1->begin(),full.v1->end(),policy.torrent_v1.begin(),[](std::byte b){return std::to_integer<unsigned char>(b);});}
    if(full.v2){policy.hashes|=2;std::transform(full.v2->begin(),full.v2->end(),policy.torrent_v2.begin(),[](std::byte b){return std::to_integer<unsigned char>(b);});}
    if(libtorrent::biglybtc_configure_private_i2p_owner_v1(session.get(),&policy)!=0){return false;}providerGeneration=policy.generation;
    lt::settings_pack network;network.set_str(lt::settings_pack::i2p_hostname,"127.0.0.1");network.set_int(lt::settings_pack::i2p_port,policy.bridge_port);session->apply_settings(network);
    auto params=storage.parameters();
    if(auto mapped=storage.mappedFiles()){
      const auto virtualPath=(std::filesystem::path(params.save_path)/".BiglyBTC-original-storage"/(source->hash().hex()+"-owned-i2p-"+std::to_string(admission))).lexically_normal();
      auto registered=disks->registerOriginalPath(virtualPath,std::move(mapped));if(!registered){return false;}params.save_path=virtualPath.string();
    }
    if(params.total_downloaded||params.total_uploaded){return false;}
    const auto ledger=counters;const auto pin=providerPin;
    params.extensions.push_back([ledger,pin](const lt::torrent_handle& torrent,lt::client_data_t)->std::shared_ptr<lt::torrent_plugin>{
      return std::make_shared<detail::RetirementCounterPlugin>(torrent,ledger,pin);
    });
    lt::error_code error;handle=session->add_torrent(params,error);if(error||!handle.is_valid()){return false;}
    // No seed_mode, resume-piece assertion, synthetic piece or queue epoch.
    // Real LT checking authenticates existing data through qualified storage.
    handle.resume();phase=Phase::Active;notify();return true;
  }
  void requestStartup(){
    if((phase!=Phase::Starting&&phase!=Phase::Active)||!permit){return;}if(permit->phase()==SourcePermit::Phase::Idle){permit->request();}
  }
  void startup(){
    if((phase!=Phase::Starting&&phase!=Phase::Active)||!permit){return;}
    auto revision=permit->take();if(!revision){if(permit->phase()==SourcePermit::Phase::Failed||permit->phase()==SourcePermit::Phase::Stopped){close();}return;}
    struct Ack {SourcePermit& permit;~Ack(){permit.acknowledge();}} ack{*permit};
    try{
      if(!source->revisionCurrent(*revision)){close();return;}
      if(!channel->resumeAuthorizedEffects()){close();return;}
      if(startRequested){
        startRequested=false;const auto now=QDateTime::currentMSecsSinceEpoch();
        if(!identity||now<=0||!channel->createLeaseSession(std::move(*identity),options.session,static_cast<std::uint64_t>(now))){close();return;}identity.reset();
      }
      if(bridgeRequested){
        if(!channel->ownedTransportIdle()){return;}
        bridgeRequested=false;auto actual=channel->ownedLeaseDestination();if(!actual||actual->wire!=source->destination().wire){close();return;}
        bridge=std::make_unique<Bridge>(source,*channel,q);
        QObject::connect(bridge.get(),&Bridge::changed,q,[this]{
          if(!bridge||(phase!=Phase::Starting&&phase!=Phase::Active)){return;}const auto observed=bridge->observation();
          if(observed.phase==Bridge::Phase::Withdrawn){close();return;}
          if(observed.phase==Bridge::Phase::Ready&&phase==Phase::Starting){if(!bridge->installProvider([this](void* policy){return install(policy);})){close();}}
        });
        if(!bridge->begin(options.ports,admission)){close();return;}
      }
    }catch(...){close();}
  }
};
OwnedTorrent::OwnedTorrent(std::unique_ptr<Impl> impl):impl_(std::move(impl)){
  auto& s=*impl_;s.q=this;s.channel=std::make_unique<i2cp::InitialChannel>(this);
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::sourceEffectRequested,this,[this]{impl_->requestStartup();});
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::dateReceived,this,[this]{auto& s=*impl_;if(s.phase==Phase::Starting){s.startRequested=true;s.requestStartup();}});
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::leaseSetSubmitted,this,[this]{auto& s=*impl_;if(s.phase==Phase::Starting&&!s.bridge){s.bridgeRequested=true;s.requestStartup();}});
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::failed,this,[this]{if(impl_->phase==Phase::Starting||impl_->phase==Phase::Active){impl_->close();}});
  QObject::connect(s.channel.get(),&i2cp::InitialChannel::closed,this,[this]{if(impl_->phase==Phase::Starting||impl_->phase==Phase::Active){impl_->close();}});
}
OwnedTorrent::~OwnedTorrent(){impl_->close();impl_->join();}
std::expected<std::unique_ptr<OwnedTorrent>,storage::Error> OwnedTorrent::prepare(const storage::ProfileLease& lease,const core::TorrentHash& hash,std::string directory){
  try{
    auto pin=privateOwnerRuntimePin();if(!pin){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Separately approved private I2P provider image required"});}
    auto source=ProfileSource::open(lease,hash,std::move(directory));if(!source){return std::unexpected(source.error());}
    auto preparedStorage=(*source)->prepareStorage();if(!preparedStorage){return std::unexpected(preparedStorage.error());}
    auto identity=(*source)->takeIdentity();if(!identity){return std::unexpected(identity.error());}
    auto impl=std::make_unique<Impl>(*source,std::move(*preparedStorage),std::move(*identity),std::move(pin));
    if(RAND_bytes(reinterpret_cast<unsigned char*>(&impl->admission),sizeof(impl->admission))!=1||!impl->admission){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Owned admission entropy unavailable"});}
    auto settings=(*source)->policy().settings();
    settings.set_int(libtorrent::settings_pack::alert_mask,static_cast<int>(libtorrent::alert_category::error
        |libtorrent::alert_category::status|libtorrent::alert_category::storage|libtorrent::alert_category::tracker));
    settings.set_int(libtorrent::settings_pack::alert_queue_size,Impl::MaximumAlerts);
    libtorrent::session_params params(std::move(settings));params.disk_io_constructor=engine::detail::metadataDiskConstructor(impl->disks);
    impl->session=std::make_unique<libtorrent::session>(std::move(params));
    return std::unique_ptr<OwnedTorrent>(new OwnedTorrent(std::move(impl)));
  }catch(...){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot prepare dedicated I2P owner"});}
}
bool OwnedTorrent::begin(const Options& options){
  auto& s=*impl_;if(!s.onThread()||s.phase!=Phase::Prepared||!options.ports.peer||!options.ports.localPeer){return false;}
  s.options=options;s.phase=Phase::Starting;
  try{s.permit=std::make_unique<SourcePermit>(s.source,this);if(!s.channel->bindEffectAuthority(s.source->effectAuthority())){s.close();return false;}if(!s.channel->begin(options.router)){s.close();return false;}s.notify();return true;}catch(...){s.close();return false;}
}
bool OwnedTorrent::beginRetirement(std::chrono::steady_clock::time_point deadline){
  auto& s=*impl_;const auto now=std::chrono::steady_clock::now();
  if(!s.onThread()||s.phase!=Phase::Active||s.retired.requested||!s.session||!s.handle.is_valid()||deadline<=now||deadline-now>std::chrono::seconds{30}){return false;}
  try{
    s.drainAlerts();
    if(s.retirementUnsafe){s.retired.requested=true;s.retirementFailure(storage::ErrorCode::Io,s.retirementUnsafe,s.retirementUnsafeCode);return false;}
    s.beginRetirement(deadline);s.notify();return true;
  }catch(...){s.retirementFailure(storage::ErrorCode::Io,"Cannot start I2P retirement; recheck remains required");return false;}
}
OwnedTorrent::RetirementObservation OwnedTorrent::retirement()const{
  auto& s=*impl_;if(s.onThread()){
    try{s.drainAlerts();s.advanceRetirement();}catch(...){s.retirementFailure(storage::ErrorCode::Io,"I2P retirement observation failed; recheck remains required");}
  }return s.retired;
}
std::expected<void,storage::Error> OwnedTorrent::checkpointRetired(){
  auto& s=*impl_;
  if(!s.retired.requested||!s.retired.readyForJoin||!s.retired.joined||s.retired.failure||s.checkpointAttempted){return std::unexpected(s.retired.failure.value_or(storage::Error{storage::ErrorCode::Conflict,"I2P checkpoint requires its exact flushed, removed and joined admission"}));}
  s.checkpointAttempted=true;
  const auto fail=[&](storage::Error error)->std::expected<void,storage::Error>{s.retired.failure=error;s.source->revoke();return std::unexpected(std::move(error));};
  try{
    auto access=s.source->lease()->sourceAccess();if(!access){return fail(access.error());}
    auto current=s.source->validateRetiredSnapshot();if(!current){return fail(current.error());}
    std::optional<engine::detail::ManagedBadPayloadBytes> bad;
    if(s.retiredCounters.badPrefix){bad=engine::detail::ManagedBadPayloadBytes{s.retiredCounters.badPrefix->discarded,s.retiredCounters.badPrefix->hashFailed};}
    auto saved=s.storage.checkpoint(*s.source->lease(),s.retiredPieces,s.retiredComplete,
        s.retiredCounters.received,s.retiredCounters.sent,0,0,{},bad,s.retiredCounters.bad.has_value());
    if(!saved){return fail(saved.error());}
    s.retired.checkpointed=true;s.source->revoke();return {};
  }catch(...){return fail({storage::ErrorCode::Io,"I2P checkpoint failed; original recovery/recheck state retained"});}
}
void OwnedTorrent::close(){if(impl_->onThread()){impl_->close();}}
void OwnedTorrent::join(){impl_->join();}
OwnedTorrent::Observation OwnedTorrent::observation()const{
  auto& s=*impl_;Observation value;value.phase=s.phase;value.publicIdentity=s.source->destination().hash;value.admission=s.admission;value.generation=s.providerGeneration;
  if(s.channel&&s.channel->failure()!=i2cp::InitialChannel::Failure::None){
    const auto* reason=QMetaEnum::fromType<i2cp::InitialChannel::Failure>().valueToKey(static_cast<int>(s.channel->failure()));
    value.failure=std::string("I2CP: ")+(reason?reason:"Unknown failure");
  }
  if(s.bridge){const auto bridge=s.bridge->observation();value.bridgePort=bridge.port;value.rejectedClients=bridge.rejectedClients;value.bridgeSockets=bridge.sockets;}
  if((s.phase==Phase::Active||s.phase==Phase::Retiring)&&s.handle.is_valid()){
    libtorrent::torrent_status actual;const auto counts=s.sampleCounters(actual,false);
    if(counts.state==engine::detail::ManagedPayloadCounters::Read::Accepted){value.payloadDownloaded=counts.received;value.payloadUploaded=counts.sent;}
    else{actual=s.handle.status();}
    value.seeding=actual.is_seeding;value.verifiedBytes=actual.total_done;
    value.downloadRate=actual.download_payload_rate;value.uploadRate=actual.upload_payload_rate;value.peers=actual.num_peers;value.seeds=actual.num_seeds;
    value.checked=s.counters->checked.load(std::memory_order_acquire)&&!actual.errc;

    auto& diagnostic=value.diagnostics;diagnostic.statusAvailable=true;diagnostic.paused=bool(actual.flags&libtorrent::torrent_flags::paused);
    diagnostic.announcingToTrackers=actual.announcing_to_trackers;diagnostic.torrentState=static_cast<int>(actual.state);diagnostic.flags=static_cast<std::uint64_t>(actual.flags);
    // error_code::message is the generic category text; never copy alert.message,
    // tracker message/failure_reason, URLs, endpoints or file names.
    const auto error=[](const libtorrent::error_code& code){return ErrorObservation{code.value(),std::string(code.category().name()).substr(0,64),code.message().substr(0,160)};};
    try{
      diagnostic.error=error(actual.errc);const auto trackers=s.handle.trackers();diagnostic.trackerCount=trackers.size();diagnostic.trackersShown=std::min(trackers.size(),diagnostic.trackers.size());
      for(std::size_t t=0;t<diagnostic.trackersShown;++t){
        auto& output=diagnostic.trackers[t];const auto& input=trackers[t];output.endpointCount=input.endpoints.size();output.endpointsShown=std::min(input.endpoints.size(),output.endpoints.size());
        for(std::size_t e=0;e<output.endpointsShown;++e){
          auto& endpoint=output.endpoints[e];const auto& original=input.endpoints[e];endpoint.enabled=original.enabled;std::size_t h=0;
          for(const auto& hash:original.info_hashes){auto& target=endpoint.hashes[h++];target.updating=hash.updating;target.startSent=hash.start_sent;target.fails=hash.fails;target.error=error(hash.last_error);}
        }
      }
      s.drainAlerts();diagnostic.alerts=s.diagnosticAlerts;
    }catch(...){diagnostic.incomplete=true;}
  }return value;
}
bool OwnedTorrent::event(QEvent* event){
  if(event->type()==SourcePermit::wakeEvent()){
    auto& s=*impl_;if(s.permit){const auto phase=s.permit->phase();if(phase==SourcePermit::Phase::Ready||phase==SourcePermit::Phase::Failed||phase==SourcePermit::Phase::Stopped){s.startup();}else if(phase==SourcePermit::Phase::Idle&&(s.startRequested||(s.bridgeRequested&&s.channel->ownedTransportIdle())||s.channel->sourceEffectsPending())){s.requestStartup();}}return true;
  }return QObject::event(event);
}
}
