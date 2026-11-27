// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_service.hpp"

#include <thread>
#include "magnet_intake.hpp"
#include "managed_download.hpp"
#ifndef BIGLYBTC_HOST_I2P_OWNER
#define BIGLYBTC_HOST_I2P_OWNER 0
#endif
#if BIGLYBTC_HOST_I2P_OWNER
#include "../services/i2p_provider/profile_owner.hpp"
#include "../services/i2p_provider/contact_owner.hpp"
#include "i2p_discovery_options.hpp"
#include "library_commands.hpp"
#include "biglybtc/queue_policy.hpp"
#include <QCoreApplication>
#include <QHostAddress>
#include <QMetaObject>
#include <QThread>
#include <algorithm>
#include <limits>
#include <map>
#include <mutex>

namespace biglybtc::app {
namespace provider=services::i2p::provider;
namespace {
using Error=storage::Error;using Code=storage::ErrorCode;
template<class T>T displaySum(T a,T b,bool& capped){
  if(b>std::numeric_limits<T>::max()-a){capped=true;return std::numeric_limits<T>::max();}return a+b;
}
template<class T>T payloadSum(T a,T b,bool& capped){
  if(a<0||b<0){return -1;}if(b>std::numeric_limits<T>::max()-a){capped=true;return -1;}return a+b;
}
std::expected<provider::OwnedProfile::Rates,Error> savedRates(const core::Document& d){
  provider::OwnedProfile::Rates out;
  for(const auto& [name,target]:{std::pair{"Max Download Speed KBs",&out.download},{"Max Upload Speed KBs",&out.upload},{"Max Upload Speed Seeding KBs",&out.seedingUpload}}){
    if(const auto* n=d.find(name)){if(n->type!=core::Node::Type::Integer||n->integer<0||n->integer>std::numeric_limits<int>::max()/1024){return std::unexpected(Error{Code::InvalidFormat,std::string("Invalid saved I2P rate: ")+name});}*target=static_cast<int>(n->integer)*1024;}
  }
  if(const auto* n=d.find("enable.seedingonly.upload.rate")){if(n->type!=core::Node::Type::Integer||(n->integer!=0&&n->integer!=1)){return std::unexpected(Error{Code::InvalidFormat,"Invalid saved seeding-only I2P rate switch"});}out.seedingOnly=n->integer!=0;}
  return out;
}
}
struct OwnedI2pService::Impl {
  std::unique_ptr<provider::OwnedProfile> owner;std::unique_ptr<provider::ContactOwner> contacts;
  std::shared_ptr<const storage::ProfileLease> lease;
  QThread thread;QThread* caller=QThread::currentThread();
  mutable std::mutex mutex;mutable std::vector<TorrentObservation> last;
  mutable Phase lastPhase=Phase::Prepared;std::map<std::string,engine::PendingMagnet> magnetIntents;
  std::optional<core::TorrentHash> lastSubscriptionAttempt;
  bool closed=false,joined=false,externalQueue=false,checkpointOmitted=false;std::optional<Error> closeError;
  explicit Impl(std::unique_ptr<provider::OwnedProfile> p,std::shared_ptr<const storage::ProfileLease> l,std::unique_ptr<provider::ContactOwner> cache):owner(std::move(p)),contacts(std::move(cache)),lease(std::move(l)){}
  template<class F> bool onOwner(F&& f,bool stopping=false)const{
    if(checkpointOmitted&&!stopping){return false;}
    if(owner->thread()==QThread::currentThread()){f();return true;}
    return thread.isRunning()&&QMetaObject::invokeMethod(owner.get(),std::forward<F>(f),Qt::BlockingQueuedConnection);
  }
  void capture()const{
    lastPhase=static_cast<Phase>(owner->phase());
    std::vector<TorrentObservation> next;
    for(const auto& observed:owner->observations()){
      const auto& a=observed.torrent;Observation v;v.phase=static_cast<Phase>(a.phase);v.publicIdentity=a.publicIdentity;v.admission=a.admission;v.generation=a.generation;
      v.current=a.phase==provider::OwnedProfile::Phase::Active;v.joined=joined;v.verifiedBytes=a.verifiedBytes;v.payloadDownloaded=a.payloadDownloaded;v.payloadUploaded=a.payloadUploaded;
      v.downloadRate=a.downloadRate;v.uploadRate=a.uploadRate;v.peers=a.peers;v.seeds=a.seeds;v.checked=a.checked;v.seeding=a.seeding;
      v.trackerAnnounces=a.diagnostics.alerts.announces;v.trackerReplies=a.diagnostics.alerts.trackerReplies;v.trackerErrors=a.diagnostics.alerts.trackerErrors;
      v.downloadTagMembers = observed.downloadTagMembers;
      v.backupBaseline = observed.backupBaseline;
      v.failure=a.failure;v.cumulativeReceived=observed.cumulativeReceived;v.cumulativeSent=observed.cumulativeSent;v.lastActiveTagMillis=observed.lastActiveTagMillis;v.queued=observed.queued;v.selectedComplete=observed.selectedComplete;v.completionPositionUnavailable=observed.completionPositionUnavailable;v.downloadLimit=observed.downloadLimit;v.uploadLimit=observed.uploadLimit;v.queueUnavailable=observed.queueUnavailable;v.globalDownloadLimit=observed.globalDownloadLimit;v.globalUploadLimit=observed.globalUploadLimit;v.loopbackPeerClasses=observed.loopbackPeerClasses;next.push_back({observed.hash,std::move(v)});
    }last=std::move(next);
  }
  std::expected<void,Error> stop(const core::TorrentHash& h){
    if(closed||checkpointOmitted||QThread::currentThread()!=caller){return std::unexpected(Error{Code::Conflict,"Live I2P Host worker required"});}
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds{5};bool began=false,ready=false;std::optional<Error> error;
    if(!onOwner([&]{began=owner->retire(h,end);if(!began){error=owner->retirement(h).failure.value_or(Error{Code::Conflict,"Cannot start target I2P retirement"});}})){return std::unexpected(Error{Code::Io,"I2P event loop unavailable"});}
    while(began&&!ready&&!error&&std::chrono::steady_clock::now()<end){
      if(!onOwner([&]{const auto r=owner->retirement(h);ready=r.readyForJoin;error=r.failure;})){error=Error{Code::Io,"I2P event loop stopped during target retirement"};}
      if(!ready&&!error){QThread::msleep(10);}
    }
    if(error){return std::unexpected(*error);}if(!ready){return std::unexpected(Error{Code::Io,"Target I2P retirement deadline expired; recheck retained"});}
    // Matching flush+removed alert+actual weak expiry already ended THIS disk
    // lifetime; the shared provider, bridge and siblings remain running.
    auto saved=owner->checkpoint(h);if(!saved){return saved;}
    std::expected<void,Error> released;if(!onOwner([&]{released=owner->release(h);capture();})){return std::unexpected(Error{Code::Io,"Cannot release retired I2P member"});}return released;
  }
  std::expected<void,Error> refreshQueue(){
    auto state=owner->readQueueState();if(!state){return std::unexpected(state.error());}bool applied=false;
    if(!onOwner([&]{applied=owner->applyQueueState(std::move(*state));})||!applied){return std::unexpected(Error{Code::Conflict,"I2P queue source changed before application"});}return {};
  }
  void settleCompletions(){
    if(checkpointOmitted||externalQueue){return;} // No completion publication after terminal omission.
    std::vector<core::TorrentHash> rows;if(!onOwner([&]{rows=owner->completedForceStarts();})){return;}
    for(const auto& h:rows){const auto saved=owner->publish(h,[&]()->std::expected<void,Error>{auto result=storage::clearLibraryForceStart(*lease,h);return result?std::expected<void,Error>{}:std::expected<void,Error>{std::unexpected(result.error())};});
      if(!saved||!refreshQueue()){(void)onOwner([&]{owner->close();});return;}(void)onOwner([&]{owner->acknowledgeCompletion(h);});}
  }
  void moveClosed()noexcept{
    try{owner->close();if(owner->thread()!=caller){(void)owner->moveToThread(caller);}}catch(...){}
  }
  std::expected<void,Error> shutdown(bool graceful,bool saveContacts=true){
    if(QThread::currentThread()!=caller){return std::unexpected(Error{Code::Unsupported,"I2P close requires its Host worker"});}
    if(!saveContacts){checkpointOmitted=true;if(owner){owner->revokeSource();}}
    if(checkpointOmitted){graceful=false;saveContacts=false;}
    if(closed){return closeError?std::expected<void,Error>{std::unexpected(*closeError)}:std::expected<void,Error>{};}
    std::optional<Error> first;
    if(graceful){
      if(onOwner([&]{capture();})){const auto selected=last;for(const auto& entry:selected){auto saved=stop(entry.hash);if(!saved&&!first){first=saved.error();}}}
      else{first=Error{Code::Io,"I2P event loop unavailable for close"};}
    }
    if(!onOwner([&]{moveClosed();},true)){
      first=Error{Code::Io,"Cannot close shared I2P event loop"};
      // Ready/Executing source workers cannot be joined without actual Qt
      // close/ack. Keep this retained service; no forced destruction receipt.
      if(checkpointOmitted){return std::unexpected(*first);}
    }
    thread.quit();thread.wait();owner->join();if(contacts&&saveContacts){contacts->finish();}owner.reset();joined=true;closed=true;
    for(auto& entry:last){entry.value.current=false;entry.value.joined=true;entry.value.phase=Phase::Withdrawn;entry.value.downloadRate=0;entry.value.uploadRate=0;}
    closeError=first;return first?std::expected<void,Error>{std::unexpected(*first)}:std::expected<void,Error>{};
  }
};
OwnedI2pService::OwnedI2pService(std::unique_ptr<Impl> p):impl_(std::move(p)){}
bool OwnedI2pService::available()noexcept{return true;}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::start(const storage::ProfileLease& lease,const core::TorrentHash& hash,I2pServiceOptions options,const engine::RetainedPayloads& external,const storage::TagManagerStartupMode* startup){
  return startImpl(lease,hash,std::move(options),external,startup,nullptr);
}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startWithTagRates(const storage::ProfileLease& lease,const core::TorrentHash& hash,engine::ProfileTagRates& rates,I2pServiceOptions options,const engine::RetainedPayloads& external,const storage::TagManagerStartupMode* startup){
  return startImpl(lease,hash,std::move(options),external,startup,&rates);
}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startImpl(const storage::ProfileLease& lease,const core::TorrentHash& hash,I2pServiceOptions options,const engine::RetainedPayloads& external,const storage::TagManagerStartupMode* startup,engine::ProfileTagRates* rates){
  return startPrepared(lease,std::move(options),[&](std::string_view config) {
    return rates ? provider::OwnedProfile::prepareWithTagRates(lease,hash,*rates,external,startup,config)
                 : provider::OwnedProfile::prepare(lease,hash,external,startup,config);
  });
}
template<class Prepare>
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startPrepared(
    const storage::ProfileLease& lease,I2pServiceOptions options,Prepare&& prepare,bool needsDiscovery,
    const engine::PendingMagnet* firstIntent) {
  if(!QCoreApplication::instance()||QCoreApplication::closingDown()||QThread::currentThread()==QCoreApplication::instance()->thread()||!options.routerPort||!options.peerPort||(options.publicCoexistence&&!options.externalQueue)){return std::unexpected(Error{Code::Unsupported,"Live Qt Host worker, nonzero ports and a union queue for coexistence required"});}
  try{
    struct Bootstrap {
      std::unique_ptr<provider::OwnedProfile> owner;
      std::shared_ptr<const storage::ProfileLease> lease;
      core::QueuePolicy queue;
      provider::OwnedProfile::Rates rates;
      std::optional<services::i2p::dht::Actor::Options> discovery,mixedPublication;
      std::unique_ptr<provider::ContactOwner> contacts;
      std::string configBytes;
    };
    auto bootstrap=[&]()->std::expected<Bootstrap,Error>{
      auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
      auto config=storage::ConfigFile::load(lease,"biglybt.config");if(!config){return std::unexpected(config.error());}const auto configBytes=config->document().serialize();
      auto queue=core::readQueuePolicy(&config->document());if(!queue){return std::unexpected(Error{Code::InvalidFormat,"Invalid original queue setting: "+queue.error().key});}
      auto rates=savedRates(config->document());if(!rates){return std::unexpected(rates.error());}
      auto retained=lease.retain();if(!retained){return std::unexpected(retained.error());}
      auto discovery=readI2pDiscoveryOptions(lease,false,options);if(!discovery){return std::unexpected(discovery.error());}
      auto mixedPublication=*discovery?readMixedI2pPublicationOptions(lease,config->document(),**discovery):std::nullopt;
      return Bootstrap{nullptr,std::move(*retained),std::move(*queue),*rates,std::move(*discovery),
          std::move(mixedPublication),nullptr,configBytes};
    }(); // Release the source gate BEFORE starting or waiting on Qt sockets.
    if(!bootstrap){return std::unexpected(bootstrap.error());}
    // SDK waits belong outside the caller's filesystem/source gate.
    if (needsDiscovery && !bootstrap->discovery) {
      return std::unexpected(Error{Code::Unsupported,"I2P subscription discovery is unavailable for this profile"});
    }
    auto prepared=prepare(bootstrap->configBytes);
    if(!prepared){return std::unexpected(prepared.error());}
    bootstrap->owner=std::move(*prepared);
    auto completed=[&]()->std::expected<void,Error>{
      auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
      auto current=storage::ConfigFile::load(lease,"biglybt.config");if(!current||current->document().serialize()!=bootstrap->configBytes){return std::unexpected(Error{Code::Conflict,"Original I2P queue/rate configuration changed during preparation"});}
      auto contacts=provider::ContactOwner::open(lease,1,bootstrap->discovery);if(!contacts){return std::unexpected(contacts.error());}bootstrap->contacts=std::move(*contacts);
      current=storage::ConfigFile::load(lease,"biglybt.config");if(!current||current->document().serialize()!=bootstrap->configBytes){return std::unexpected(Error{Code::Conflict,"Original I2P configuration changed during contact preparation"});}return {};
    }();if(!completed){return std::unexpected(completed.error());}
    auto service=std::unique_ptr<OwnedI2pService>(new OwnedI2pService(std::make_unique<Impl>(std::move(bootstrap->owner),std::move(bootstrap->lease),std::move(bootstrap->contacts))));auto& s=*service->impl_;
    provider::OwnedProfile::Options actual;actual.router={QHostAddress::LocalHost,options.routerPort,"2.13.0",false,30000,false};actual.session=std::move(options.sessionOptions);actual.ports={options.peerPort,options.peerPort};
    QObject::connect(&s.thread,&QThread::finished,s.owner.get(),[&s]{if(s.owner&&s.owner->thread()==QThread::currentThread()){s.moveClosed();}},Qt::DirectConnection);
    s.thread.start();if(!s.thread.isRunning()||!s.owner->moveToThread(&s.thread)){(void)s.shutdown(false);return std::unexpected(Error{Code::Io,"Cannot start shared I2P event loop"});}
    s.externalQueue=options.externalQueue;
    if (firstIntent) { s.magnetIntents.emplace(firstIntent->hash.hex(),*firstIntent); }
    bool began=false;
    if (!s.onOwner([&] {
      began=(!bootstrap->discovery||s.owner->configureDiscovery(*bootstrap->discovery,std::move(bootstrap->mixedPublication)))
          &&(!s.contacts||!s.contacts->exchange()||s.owner->configureContactExchange(s.contacts->exchange()))
          &&s.owner->configureExternalQueue(options.externalQueue)
          &&s.owner->begin(actual,std::move(bootstrap->queue),bootstrap->rates);
      s.capture();
    })||!began) {
      (void)s.shutdown(false);
      return std::unexpected(Error{Code::Io,"Cannot begin shared I2P session"});
    }
    return service;
  }catch(...){return std::unexpected(Error{Code::Io,"Cannot prepare shared I2P service"});}
}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startPending(const storage::ProfileLease& lease,const engine::PendingMagnet& intent,I2pServiceOptions options,const storage::TagManagerStartupMode* startup){return startPendingImpl(lease,intent,std::move(options),startup,nullptr);}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startPendingWithTagRates(
    const storage::ProfileLease& lease,const engine::PendingMagnet& intent,engine::ProfileTagRates& rates,
    I2pServiceOptions options,const storage::TagManagerStartupMode* startup){return startPendingImpl(lease,intent,std::move(options),startup,&rates);}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startPendingImpl(const storage::ProfileLease& lease,const engine::PendingMagnet& intent,I2pServiceOptions options,const storage::TagManagerStartupMode* startup,engine::ProfileTagRates* rates){
  return startPrepared(lease,std::move(options),[&](std::string_view config) {
    return rates ? provider::OwnedProfile::preparePendingWithTagRates(lease,intent,*rates,startup,config)
                 : provider::OwnedProfile::preparePending(lease,intent,startup,config);
  },false,&intent);
}

std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startSubscription(
    const storage::ProfileLease& lease,const core::TorrentHash& hash,engine::HeldSubscriptionSeedResolver resolve,
    I2pServiceOptions options,const storage::TagManagerStartupMode* startup) {
  return startPrepared(lease,std::move(options),[&](std::string_view config) {
    return provider::OwnedProfile::prepareSubscription(lease,hash,std::move(resolve),startup,config);
  },true);
}
void OwnedI2pService::updateSubscriptionSeeds(std::span<const core::TorrentHash> wanted,
    const engine::HeldSubscriptionSeedResolver& resolve) {
  auto& state=*impl_;
  const std::lock_guard lock(state.mutex);
  if (state.closed || state.checkpointOmitted || QThread::currentThread()!=state.caller) { return; }
  std::vector<core::TorrentHash> retained;
  if (wanted.empty()) { state.lastSubscriptionAttempt.reset(); }
  bool available=false;
  if (!state.onOwner([&]{
        retained=state.owner->retainSubscriptionSeeds(wanted);
        available=resolve && retained.size()<wanted.size()
            && state.owner->subscriptionSeedSlotAvailable(std::chrono::steady_clock::now());
      }) || !available) { return; }
  const auto first=engine::nextSubscriptionSeedCandidate(wanted,state.lastSubscriptionAttempt);
  std::size_t attempted=0;
  for (std::size_t offset=0;offset<wanted.size() && attempted<engine::PrivateSubscriptionCandidateLimit;++offset) {
    const auto& hash=wanted[(first+offset)%wanted.size()];
    if (std::ranges::find(retained,hash)!=retained.end()) { continue; }
    ++attempted;
    auto prepared=state.owner->prepareSubscriptionSeed(hash,resolve);
    if (!prepared) {
      state.lastSubscriptionAttempt=hash;
      continue;
    }
    std::optional<std::expected<void,Error>> added;
    const auto previous=retained.size();
    if (!state.onOwner([&]{
          const auto now=std::chrono::steady_clock::now();
          retained=state.owner->retainSubscriptionSeeds(wanted,now);
          if (retained.size()<engine::PrivateSubscriptionCandidateLimit) {
            added=state.owner->addSubscriptionSeed(std::move(*prepared));
          }
        }) || !added) { break; }
    const bool retired=retained.size()<previous;
    if (*added) { retained.push_back(hash); }
    else if (added->error().code!=Code::Conflict) { break; }
    state.lastSubscriptionAttempt=hash;
    if (retired || retained.size()>=engine::PrivateSubscriptionCandidateLimit) { break; }
  }
}

std::expected<void,storage::Error> OwnedI2pService::addMagnet(const engine::PendingMagnet& intent){return addMagnetImpl(intent,nullptr);}
std::expected<void,storage::Error> OwnedI2pService::addMagnetWithTagRates(const engine::PendingMagnet& intent,engine::ProfileTagRates& rates){return addMagnetImpl(intent,&rates);}
std::expected<void,storage::Error> OwnedI2pService::addMagnetImpl(const engine::PendingMagnet& intent,engine::ProfileTagRates* rates){
  auto& s=*impl_;std::lock_guard lock(s.mutex);
  if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller){return std::unexpected(Error{Code::Conflict,"Live I2P Host worker required"});}
  auto prepared=rates?s.owner->prepareMagnetWithTagRates(intent,*rates):s.owner->prepareMagnet(intent);if(!prepared){return std::unexpected(prepared.error());}
  std::expected<void,Error> result;
  if(!s.onOwner([&]{result=s.owner->startMagnet(std::move(*prepared));})){return std::unexpected(Error{Code::Io,"I2P metadata event loop unavailable"});}
  if(result){s.magnetIntents.insert_or_assign(intent.hash.hex(),intent);}return result;
}
std::expected<void,storage::Error> OwnedI2pService::cancelMagnet(const core::TorrentHash& hash){
  auto& s=*impl_;std::lock_guard lock(s.mutex);bool result=false;
  if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller||!s.onOwner([&]{result=s.owner->cancelMagnet(hash,std::chrono::steady_clock::now()+std::chrono::seconds(30));})||!result){
    return std::unexpected(Error{Code::Conflict,"Cannot retire I2P metadata admission"});}return {};
}
std::vector<engine::PendingMagnetStatus> OwnedI2pService::pendingMagnets()const{
  auto& s=*impl_;std::lock_guard lock(s.mutex);std::vector<engine::PendingMagnetStatus> out;
  if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller){return out;}
  if(!s.onOwner([&]{for(const auto& value:s.owner->magnetObservations()){
    engine::PendingMagnetStatus status;status.hash=value.hash;status.admission=value.admission;
    const auto found=s.magnetIntents.find(value.hash.hex());status.name=found==s.magnetIntents.end()?value.hash.hex():found->second.displayName;
    status.retained=value.retained;status.failure=value.failure;
    status.phase=value.failure?engine::MagnetPhase::Failed:value.cancelled?engine::MagnetPhase::Cancelled:
      value.retiring||value.metadataReady?engine::MagnetPhase::Retiring:engine::MagnetPhase::Resolving;
    out.push_back(std::move(status));
  }})){return {};}
  return out;
}
std::optional<search::I2pSubscriptionUpgrade> OwnedI2pService::takeSubscriptionUpgrade(){
  auto& state=*impl_;
  std::lock_guard lock(state.mutex);
  std::optional<search::I2pSubscriptionUpgrade> result;
  if (state.closed||state.checkpointOmitted||QThread::currentThread()!=state.caller||
      !state.onOwner([&]{result=state.owner->takeSubscriptionUpgrade();})) { return {}; }
  return result;
}
std::expected<void,storage::Error> OwnedI2pService::downloadSubscriptionUpgrade(
    const std::array<char,20>& previous,std::uint32_t version) {
  auto& state=*impl_;
  std::lock_guard lock(state.mutex);
  std::expected<void,Error> result=std::unexpected(Error{Code::Conflict,"The original I2P session is unavailable"});
  if (!state.closed&&!state.checkpointOmitted&&QThread::currentThread()==state.caller) {
    state.onOwner([&]{result=state.owner->downloadSubscriptionUpgrade(previous,version);});
  }
  return result;
}
std::optional<std::expected<std::string,storage::Error>> OwnedI2pService::takeSubscriptionUpgradePayload() {
  auto& state=*impl_;
  std::lock_guard lock(state.mutex);
  std::optional<std::expected<std::string,Error>> result;
  if (state.closed||state.checkpointOmitted||QThread::currentThread()!=state.caller||
      !state.onOwner([&]{result=state.owner->takeSubscriptionUpgradePayload();})) {
    return std::expected<std::string,Error>{std::unexpected(Error{Code::Conflict,"The original I2P session is unavailable"})};
  }
  return result;
}
std::expected<std::string,storage::Error> OwnedI2pService::takeCompletedMetadata(const core::TorrentHash& hash){
  auto& s=*impl_;std::lock_guard lock(s.mutex);std::expected<std::string,Error> result=std::unexpected(Error{Code::Io,"I2P metadata event loop unavailable"});
  if(!(s.closed||s.checkpointOmitted)&&QThread::currentThread()==s.caller){s.onOwner([&]{result=s.owner->takeCompletedMetadata(hash);});}return result;
}
std::expected<void,storage::Error> OwnedI2pService::releaseMagnet(const core::TorrentHash& hash){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller){return std::unexpected(Error{Code::Conflict,"Live I2P Host worker required"});}
  auto result=s.owner->releaseMagnet(hash);if(result){s.magnetIntents.erase(hash.hex());}return result;
}
std::expected<void,storage::Error> OwnedI2pService::publishPendingMagnet(const core::TorrentHash& hash,const std::function<std::expected<void,storage::Error>()>& operation){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller){return std::unexpected(Error{Code::Conflict,"Live I2P Host worker required"});}
  return s.owner->publishPendingMagnet(hash,operation);
}
std::vector<OwnedI2pService::TorrentObservation> OwnedI2pService::observations()const{
  auto& s=*impl_;std::lock_guard lock(s.mutex);
  if(s.checkpointOmitted){for(auto& entry:s.last){entry.value.current=false;entry.value.joined=s.joined;if(!s.joined&&entry.value.failure.empty()){entry.value.failure="Explicit drain incomplete; retained I2P owner is not joined";}}return s.last;}
  if(!s.closed&&s.owner&&QThread::currentThread()==s.caller){s.settleCompletions();if(!s.onOwner([&]{s.capture();})){for(auto& e:s.last){e.value.current=false;e.value.failure="I2P event loop unavailable";}}if(s.contacts){s.contacts->poll();}}return s.last;
}
std::vector<std::array<std::uint8_t,20>> OwnedI2pService::peerIds(const core::TorrentHash& h)const{
  auto& s=*impl_;std::lock_guard lock(s.mutex);
  if((s.closed||s.checkpointOmitted)||!s.owner||QThread::currentThread()!=s.caller){return {};}
  try{std::vector<std::array<std::uint8_t,20>> out;
    if(!s.onOwner([&]{out=s.owner->peerIds(h);})){return {};}return out;
  }catch(...){return {};}
}
std::optional<std::size_t> OwnedI2pService::peerSetCount(const core::TorrentHash& hash,
    std::uint64_t admission,std::uint64_t generation)const {
  auto& s=*impl_;std::lock_guard lock(s.mutex);
  if(s.closed||s.checkpointOmitted||!s.owner||QThread::currentThread()!=s.caller){return {};}
  std::optional<std::size_t> count;
  if(!s.onOwner([&]{count=s.owner->peerSetCount(hash,admission,generation);})){return {};}
  return count;
}
std::optional<storage::Error> OwnedI2pService::contactCacheDiagnostic()const{auto& s=*impl_;std::lock_guard lock(s.mutex);return s.contacts?s.contacts->diagnostic():std::nullopt;}
OwnedI2pService::Observation OwnedI2pService::observation()const{
  const auto rows=observations();Observation out;out.joined=impl_->joined;if(rows.empty()){out.phase=impl_->closed?Phase::Withdrawn:impl_->lastPhase;return out;}out=rows.front().value;
  if(rows.size()>1){bool capped=false;out.backupBaseline.reset();out.lastActiveTagMillis.reset();out.downloadTagMembers.reset();out.verifiedBytes=0;out.payloadDownloaded=0;out.payloadUploaded=0;out.downloadRate=0;out.uploadRate=0;out.peers=0;out.seeds=0;out.checked=true;out.seeding=true;out.cumulativeReceived=0;out.cumulativeSent=0;out.trackerAnnounces=out.trackerReplies=out.trackerErrors=0;
    for(const auto& entry:rows){const auto& v=entry.value;out.verifiedBytes=displaySum(out.verifiedBytes,v.verifiedBytes,capped);out.payloadDownloaded=payloadSum(out.payloadDownloaded,v.payloadDownloaded,capped);out.payloadUploaded=payloadSum(out.payloadUploaded,v.payloadUploaded,capped);out.downloadRate=displaySum(out.downloadRate,v.downloadRate,capped);out.uploadRate=displaySum(out.uploadRate,v.uploadRate,capped);out.peers=displaySum(out.peers,v.peers,capped);out.seeds=displaySum(out.seeds,v.seeds,capped);
      out.cumulativeReceived=out.cumulativeReceived&&v.cumulativeReceived?engine::cumulativePayload(out.cumulativeReceived,*v.cumulativeReceived):std::nullopt;
      out.cumulativeSent=out.cumulativeSent&&v.cumulativeSent?engine::cumulativePayload(out.cumulativeSent,*v.cumulativeSent):std::nullopt;
      out.checked&=v.checked;out.seeding&=v.seeding;out.trackerAnnounces=displaySum(out.trackerAnnounces,v.trackerAnnounces,capped);out.trackerReplies=displaySum(out.trackerReplies,v.trackerReplies,capped);out.trackerErrors=displaySum(out.trackerErrors,v.trackerErrors,capped);}
    if(capped){out.failure="Some aggregate values exceed the display range; individual measurements remain available";}}
  return out;
}
std::expected<void,storage::Error> OwnedI2pService::add(const core::TorrentHash& h,const engine::RetainedPayloads& external){
  return addImpl(h,external,nullptr);
}
std::expected<void,storage::Error> OwnedI2pService::addWithTagRates(const core::TorrentHash& h,engine::ProfileTagRates& rates,const engine::RetainedPayloads& external){
  return addImpl(h,external,&rates);
}
std::expected<void,storage::Error> OwnedI2pService::addImpl(const core::TorrentHash& h,const engine::RetainedPayloads& external,engine::ProfileTagRates* rates){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller){return std::unexpected(Error{Code::Conflict,"Live I2P Host worker required"});}
  provider::OwnedProfile::Retained retained;if(!s.onOwner([&]{retained=s.owner->retained();})){return std::unexpected(Error{Code::Io,"I2P event loop unavailable"});}
  if(retained.claims.size()>engine::detail::MaximumManagedPayloadClaims||external.claims.size()>engine::detail::MaximumManagedPayloadClaims-retained.claims.size()||external.identities.size()>core::MaximumQueueTorrents){return std::unexpected(Error{Code::Unsupported,"Cross-provider claim or identity bound exceeded"});}
  retained.claims.insert(retained.claims.end(),external.claims.begin(),external.claims.end());retained.identities.insert(retained.identities.end(),external.identities.begin(),external.identities.end());
  auto prepared=rates?s.owner->prepareTorrentWithTagRates(h,retained,*rates):s.owner->prepareTorrent(h,retained);if(!prepared){return std::unexpected(prepared.error());}std::expected<void,Error> result;
  if(!s.onOwner([&]{result=s.owner->add(std::move(*prepared));s.capture();})){return std::unexpected(Error{Code::Io,"Cannot add prepared I2P member"});}return result;
}
std::expected<storage::ManualTagCommit,storage::Error> OwnedI2pService::changeRateMembership(engine::ProfileTagRates& rates,
    storage::ManualTagStore& store,const storage::ManualTagSnapshot& snapshot,const core::TorrentHash& hash,
    std::span<const storage::ManualTagMembershipEdit> requests,
    const std::function<std::expected<void,storage::Error>(const storage::ProfileSourceAccess&)>& validate,std::stop_token stop,std::optional<std::pair<std::uint64_t,std::uint64_t>> expectedOwner){
 auto& s=*impl_;std::lock_guard lock(s.mutex);
 if(s.closed||s.checkpointOmitted||QThread::currentThread()!=s.caller){return std::unexpected(Error{Code::Conflict,"Current private Host worker required for rate membership"});}
 auto preflight=s.owner->preflightTagSourceUpdate(rates);if(!preflight){return std::unexpected(preflight.error());}
 std::expected<std::unique_ptr<provider::OwnedProfile::TagSourceUpdate>,Error> update=std::unexpected(Error{Code::Io,"Private event loop unavailable"});
 if(!s.onOwner([&]{update=s.owner->beginTagSourceUpdate();})||!update){return std::unexpected(update?Error{Code::Io,"Cannot quiesce private source readers"}:update.error());}
 auto joined=s.owner->joinTagSourceUpdate(**update);
 std::expected<storage::ManualTagCommit,Error> saved=std::unexpected(Error{Code::Io,"Private source workers did not join"});
 if(joined){try{saved=s.owner->applyRateOnlyMembership(**update,rates,store,snapshot,hash,requests,validate,stop,expectedOwner);}
  catch(...){saved=std::unexpected(Error{Code::Io,"Private rate membership preparation failed"});}}
 auto prepared=joined?s.owner->prepareTagSourceUpdate(**update,rates):joined;
 bool resumed=false;const bool delivered=s.onOwner([&]{resumed=s.owner->finishTagSourceUpdate(std::move(*update));s.capture();});
 if(!delivered){s.owner->revokeSource();}if(!resumed){s.owner->join();}
 // An actual committed save wins even if its runtime must withdraw afterward.
 if(saved&&saved->changed){return saved;}
 if(!delivered||!resumed||!prepared){return std::unexpected(prepared?Error{Code::Conflict,"Private rate membership refresh withdrew"}:prepared.error());}
 return saved;
}
std::expected<void,storage::Error> OwnedI2pService::updateTagSource(engine::ProfileTagRates& rates,const std::function<std::expected<void,storage::Error>()>& operation){
  auto& s=*impl_;std::lock_guard lock(s.mutex);
  if(s.closed||s.checkpointOmitted||QThread::currentThread()!=s.caller||!operation){return std::unexpected(Error{Code::Conflict,"Current private Host worker/source callback required"});}
  auto preflight=s.owner->preflightTagSourceUpdate(rates);if(!preflight){return preflight;}
  std::expected<std::unique_ptr<provider::OwnedProfile::TagSourceUpdate>,Error> update=std::unexpected(Error{Code::Io,"Private event loop unavailable"});
  if(!s.onOwner([&]{update=s.owner->beginTagSourceUpdate();})||!update){return update?std::unexpected(Error{Code::Io,"Cannot quiesce private source readers"}):std::unexpected(update.error());}
  auto joined=s.owner->joinTagSourceUpdate(**update);
  std::expected<void,Error> changed=std::unexpected(Error{Code::Io,"Private source workers did not join"});
  if(joined){try{changed=operation();}catch(...){changed=std::unexpected(Error{Code::Io,"Tag source callback failed"});}}
  // Rebase an unchanged/refused CAS too:cooperative source epochs may advance.
  auto prepared=joined?s.owner->prepareTagSourceUpdate(**update,rates):joined;
  bool resumed=false;
  if(!s.onOwner([&]{resumed=s.owner->finishTagSourceUpdate(std::move(*update));s.capture();})){
    s.owner->revokeSource();return std::unexpected(Error{Code::Io,"Private event loop unavailable after tag source update"});
  }
  if(!resumed){s.owner->join();return prepared?std::unexpected(Error{Code::Conflict,"Private source withdrew during tag refresh"}):prepared;}
  return prepared?changed:prepared;
}
std::expected<void,storage::Error> OwnedI2pService::stop(const core::TorrentHash& h){auto& s=*impl_;std::lock_guard lock(s.mutex);return s.stop(h);}
std::expected<void,storage::Error> OwnedI2pService::publish(const core::TorrentHash& h,const std::function<std::expected<void,storage::Error>()>& f){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller){return std::unexpected(Error{Code::Conflict,"Live I2P Host worker required"});}return s.owner->publish(h,f);
}
std::expected<void,storage::Error> OwnedI2pService::command(const core::TorrentHash& h,core::DownloadAction action,const engine::RetainedPayloads& external){return commandImpl(h,action,external,nullptr);}
std::expected<void,storage::Error> OwnedI2pService::commandWithTagRates(const core::TorrentHash& h,core::DownloadAction action,engine::ProfileTagRates& rates,const engine::RetainedPayloads& external){return commandImpl(h,action,external,&rates);}
std::expected<void,storage::Error> OwnedI2pService::commandImpl(const core::TorrentHash& h,core::DownloadAction action,const engine::RetainedPayloads& external,engine::ProfileTagRates* rates){
  // Serialize on the existing Host worker. A saved-intent edit is allowed only
  // after this target's storage is settled; every sibling keeps its handle.
  bool active=false;for(const auto& row:observations()){active|=row.hash==h;}if(active){auto retired=stop(h);if(!retired){return retired;}}
  bool start=false;auto changed=publish(h,[&]()->std::expected<void,Error>{auto r=storage::applyLibraryCommand(*impl_->lease,h,action,{});if(!r){return std::unexpected(r.error());}start=!r->pauseAfter&&r->after.startup!=core::StartupState::Stopped;return {};});if(!changed){return changed;}
  return start&&(action==core::DownloadAction::Start||action==core::DownloadAction::Resume||action==core::DownloadAction::Queue||action==core::DownloadAction::ForceStart)?(rates?addWithTagRates(h,*rates,external):add(h,external)):std::expected<void,Error>{};
}
std::expected<void,storage::Error> OwnedI2pService::move(const core::TorrentHash& h,core::DownloadAction action,std::span<const storage::LibraryCompletionPool> pools){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller){return std::unexpected(Error{Code::Conflict,"Live I2P Host worker required"});}
  auto changed=s.owner->publishPositions([&]()->std::expected<void,Error>{auto r=storage::applyLibraryCommand(*s.lease,h,action,pools);return r?std::expected<void,Error>{}:std::expected<void,Error>{std::unexpected(r.error())};});if(!changed){return changed;}return s.refreshQueue();
}
std::expected<bool,storage::Error> OwnedI2pService::rebaseCategory(const storage::ProfileLease& lease,const core::TorrentHash& hash){
  // All service control and destruction is serialized on caller. publish()
  // already holds mutex here, so re-locking it would deadlock the FS callback.
  auto& s=*impl_;if(QThread::currentThread()!=s.caller||(s.closed||s.checkpointOmitted)||!s.owner||!s.lease||!s.lease->sharesOwnership(lease)){
    return std::unexpected(Error{Code::Conflict,"Live I2P Host/profile required for category rebase"});
  }
  return s.owner->rebaseCategory(lease,hash);
}
std::expected<void,storage::Error> OwnedI2pService::publishPositions(const engine::SourceMutation& f){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller){return std::unexpected(Error{Code::Conflict,"Live I2P Host worker required"});}
  auto saved=s.owner->publishPositions(f);if(!saved){return saved;}return s.refreshQueue();
}
std::expected<void,storage::Error> OwnedI2pService::publishSchedulerSettings(
    const std::function<std::expected<void,storage::Error>()>& operation) {
  auto& state=*impl_;
  std::lock_guard lock(state.mutex);
  if(state.closed||state.checkpointOmitted||QThread::currentThread()!=state.caller){
    return std::unexpected(Error{Code::Conflict,"Live I2P Host worker required"});
  }
  return state.owner->publishSchedulerSettings(operation);
}
std::expected<bool,storage::Error> OwnedI2pService::pauseForProfileBackup() {
  auto& state=*impl_;
  if (QThread::currentThread()!=state.caller) {
    return std::unexpected(Error{Code::Conflict,"Only the profile worker may pause the private I2P engine"});
  }
  if (state.closed||state.checkpointOmitted) { return false; }
  std::expected<bool,Error> paused;
  if (!state.onOwner([&]{ paused=state.owner->pauseForProfileBackup(); })) {
    return std::unexpected(Error{Code::Io,"The private I2P engine is unavailable for backup"});
  }
  return paused;
}
void OwnedI2pService::resumeAfterProfileBackup() noexcept {
  try {
    auto& state=*impl_;
    if (!state.closed&&!state.checkpointOmitted&&QThread::currentThread()==state.caller) {
      static_cast<void>(state.onOwner([&]{ state.owner->resumeAfterProfileBackup(); }));
    }
  } catch (...) {}
}
std::vector<storage::ProfileBackupTransferStatistics> OwnedI2pService::captureProfileBackupProgress(std::stop_token stop) {
  auto& state=*impl_;
  std::lock_guard lock(state.mutex);
  if (state.closed||state.checkpointOmitted||QThread::currentThread()!=state.caller||stop.stop_requested()) {
    return {};
  }
  try {
    std::unique_ptr<provider::OwnedProfile::BackupCapture> capture;
    if (!state.onOwner([&]{capture=state.owner->beginProfileBackupCapture();})||!capture) { return {}; }
    bool settled=false;
    do {
      if (!state.onOwner([&]{settled=state.owner->settleProfileBackupCapture(*capture,stop.stop_requested());})) { return {}; }
      if (!settled) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    } while (!settled&&!stop.stop_requested());
    return state.owner->readProfileBackupCapture(*capture,stop);
  } catch (...) { return {}; }
}
std::expected<void,storage::Error> OwnedI2pService::configureExternalQueue(bool enabled){
  auto& s=*impl_;std::lock_guard lock(s.mutex);bool done=false;
  if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller||!s.onOwner([&]{done=s.owner->configureExternalQueue(enabled);})||!done){return std::unexpected(Error{Code::Conflict,"Configure external I2P queue before begin"});}
  s.externalQueue=enabled;return {};
}
std::expected<engine::ExternalQueueSnapshot,storage::Error> OwnedI2pService::externalQueueSnapshot(){
  auto& s=*impl_;std::lock_guard lock(s.mutex);std::expected<engine::ExternalQueueSnapshot,Error> out;
  if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller||!s.onOwner([&]{out=s.owner->externalQueueSnapshot();})){return std::unexpected(Error{Code::Conflict,"External I2P snapshot unavailable"});}return out;
}
std::expected<bool,storage::Error> OwnedI2pService::applyExternalQueue(std::uint64_t sequence,
    std::span<const engine::ExternalQueueDecision> rows,std::uint32_t unavailable,core::QueueAction action){
  auto& s=*impl_;std::lock_guard lock(s.mutex);bool current=false;
  if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller||!s.onOwner([&]{current=s.owner->externalQueueCurrent(sequence,rows);})||!current){return std::unexpected(Error{Code::Conflict,"External I2P plan binding changed"});}
  // The Queue phase installs all passive decisions before any Run; these
  // source writes run on the Host worker, outside every Qt/provider wait.
  if(action==core::QueueAction::Queue||action==core::QueueAction::Preserve){
    bool changed=false;
    for(const auto& row:rows){if(!row.decision.clearForceStart){continue;}
      auto saved=s.owner->publish(row.binding.hash,[&]()->std::expected<void,Error>{auto v=storage::clearLibraryForceStart(*s.lease,row.binding.hash);return v?std::expected<void,Error>{}:std::unexpected(v.error());});
      if(!saved){return std::unexpected(saved.error());}changed=true;
    }
    if(changed){auto refreshed=s.refreshQueue();if(!refreshed){return std::unexpected(refreshed.error());}}
  }
  std::optional<std::uint64_t> revision;
  if(action==core::QueueAction::Run){auto current=s.owner->validateExternalQueueSource();if(!current){return std::unexpected(current.error());}revision=*current;}
  std::expected<bool,Error> out;if(!s.onOwner([&]{out=s.owner->applyExternalQueue(sequence,rows,unavailable,action,revision);})){return std::unexpected(Error{Code::Io,"I2P event loop unavailable"});}return out;
}
std::expected<void,storage::Error> OwnedI2pService::applyExternalQueuePositions(std::span<const std::pair<engine::QueueBinding,std::uint32_t>> rows){
  auto& s=*impl_;std::lock_guard lock(s.mutex);std::expected<void,Error> out;
  if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller||!s.onOwner([&]{out=s.owner->applyExternalQueuePositions(rows);})){return std::unexpected(Error{Code::Conflict,"External I2P positions unavailable"});}return out;
}
std::expected<void,storage::Error> OwnedI2pService::applyExternalRateLimits(const core::SessionRateLimits& limits){
  auto& s=*impl_;std::lock_guard lock(s.mutex);std::expected<void,Error> out;
  if((s.closed||s.checkpointOmitted)||QThread::currentThread()!=s.caller||!s.onOwner([&]{out=s.owner->applyExternalRateLimits(limits);})){return std::unexpected(Error{Code::Conflict,"External I2P caps unavailable"});}return out;
}
engine::RetainedPayloads OwnedI2pService::retainedPayloads()const{
  auto& s=*impl_;std::lock_guard lock(s.mutex);engine::RetainedPayloads out;
  if(!(s.closed||s.checkpointOmitted)&&QThread::currentThread()==s.caller){(void)s.onOwner([&]{out=s.owner->retained();});}return out;
}
bool OwnedI2pService::sourceCurrent()const noexcept{auto& s=*impl_;std::lock_guard lock(s.mutex);return !(s.closed||s.checkpointOmitted)&&s.owner&&QThread::currentThread()==s.caller&&s.owner->sourceCurrent();}
void OwnedI2pService::revoke()noexcept{auto& s=*impl_;std::lock_guard lock(s.mutex);if(!s.closed&&s.owner&&QThread::currentThread()==s.caller){s.owner->revokeSource();}}
std::expected<void,storage::Error> OwnedI2pService::close(){auto& s=*impl_;std::lock_guard lock(s.mutex);return s.shutdown(true);}
std::expected<void,storage::Error> OwnedI2pService::closeWithoutCheckpoint(){auto& s=*impl_;std::lock_guard lock(s.mutex);return s.shutdown(false,false);}
bool OwnedI2pService::joined()const noexcept{return QThread::currentThread()==impl_->caller&&impl_->closed&&impl_->joined;}
OwnedI2pService::~OwnedI2pService(){try{auto& s=*impl_;std::lock_guard lock(s.mutex);(void)s.shutdown(false);}catch(...){}}
}
#else
namespace biglybtc::app {
struct OwnedI2pService::Impl {};
OwnedI2pService::OwnedI2pService(std::unique_ptr<Impl> p):impl_(std::move(p)){}
OwnedI2pService::~OwnedI2pService()=default;
bool OwnedI2pService::available()noexcept{return false;}
std::expected<storage::ManualTagCommit,storage::Error> OwnedI2pService::changeRateMembership(engine::ProfileTagRates&,storage::ManualTagStore&,const storage::ManualTagSnapshot&,const core::TorrentHash&,std::span<const storage::ManualTagMembershipEdit>,const std::function<std::expected<void,storage::Error>(const storage::ProfileSourceAccess&)>&,std::stop_token,std::optional<std::pair<std::uint64_t,std::uint64_t>>){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}

std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startPendingWithTagRates(const storage::ProfileLease&,const engine::PendingMagnet&,engine::ProfileTagRates&,I2pServiceOptions,const storage::TagManagerStartupMode*){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::addMagnetWithTagRates(const engine::PendingMagnet&,engine::ProfileTagRates&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::commandWithTagRates(const core::TorrentHash&,core::DownloadAction,engine::ProfileTagRates&,const engine::RetainedPayloads&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}

std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::start(const storage::ProfileLease&,const core::TorrentHash&,I2pServiceOptions,const engine::RetainedPayloads&,const storage::TagManagerStartupMode*){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startWithTagRates(const storage::ProfileLease&,const core::TorrentHash&,engine::ProfileTagRates&,I2pServiceOptions,const engine::RetainedPayloads&,const storage::TagManagerStartupMode*){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::addWithTagRates(const core::TorrentHash&,engine::ProfileTagRates&,const engine::RetainedPayloads&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::optional<storage::Error> OwnedI2pService::contactCacheDiagnostic()const{return std::nullopt;}
OwnedI2pService::Observation OwnedI2pService::observation()const{Observation v;v.phase=Phase::Withdrawn;v.failure="Optional native I2P owner is unavailable";return v;}
std::vector<OwnedI2pService::TorrentObservation> OwnedI2pService::observations()const{return {};}
std::vector<std::array<std::uint8_t,20>> OwnedI2pService::peerIds(const core::TorrentHash&)const{return {};}
std::optional<std::size_t> OwnedI2pService::peerSetCount(
    const core::TorrentHash&,std::uint64_t,std::uint64_t)const{return {};}
std::expected<void,storage::Error> OwnedI2pService::add(const core::TorrentHash&,const engine::RetainedPayloads&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::updateTagSource(engine::ProfileTagRates&,const std::function<std::expected<void,storage::Error>()>&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::stop(const core::TorrentHash&){return {};}
std::expected<void,storage::Error> OwnedI2pService::command(const core::TorrentHash&,core::DownloadAction,const engine::RetainedPayloads&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::publish(const core::TorrentHash&,const std::function<std::expected<void,storage::Error>()>&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::move(const core::TorrentHash&,core::DownloadAction,std::span<const storage::LibraryCompletionPool>){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<bool,storage::Error> OwnedI2pService::rebaseCategory(const storage::ProfileLease&,const core::TorrentHash&){return false;}
std::expected<void,storage::Error> OwnedI2pService::publishPositions(const engine::SourceMutation&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::publishSchedulerSettings(
    const std::function<std::expected<void,storage::Error>()>&) {
  return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Private I2P provider is unavailable"});
}
std::expected<bool,storage::Error> OwnedI2pService::pauseForProfileBackup() { return false; }
void OwnedI2pService::resumeAfterProfileBackup() noexcept {}
std::vector<storage::ProfileBackupTransferStatistics> OwnedI2pService::captureProfileBackupProgress(std::stop_token){return {};}
std::expected<void,storage::Error> OwnedI2pService::configureExternalQueue(bool){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<engine::ExternalQueueSnapshot,storage::Error> OwnedI2pService::externalQueueSnapshot(){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<bool,storage::Error> OwnedI2pService::applyExternalQueue(std::uint64_t,std::span<const engine::ExternalQueueDecision>,std::uint32_t,core::QueueAction){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::applyExternalQueuePositions(std::span<const std::pair<engine::QueueBinding,std::uint32_t>>){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::applyExternalRateLimits(const core::SessionRateLimits&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
engine::RetainedPayloads OwnedI2pService::retainedPayloads()const{return {};}
bool OwnedI2pService::sourceCurrent()const noexcept{return false;}
void OwnedI2pService::revoke()noexcept{}
std::expected<void,storage::Error> OwnedI2pService::close(){return {};}
std::expected<void,storage::Error> OwnedI2pService::closeWithoutCheckpoint(){return {};}
bool OwnedI2pService::joined()const noexcept{return true;}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startPending(const storage::ProfileLease&,const engine::PendingMagnet&,I2pServiceOptions,const storage::TagManagerStartupMode*){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"I2P metadata provider unavailable"});}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::startSubscription(
    const storage::ProfileLease&,const core::TorrentHash&,engine::HeldSubscriptionSeedResolver,I2pServiceOptions,
    const storage::TagManagerStartupMode*) {
  return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Private I2P subscription provider unavailable"});
}
void OwnedI2pService::updateSubscriptionSeeds(std::span<const core::TorrentHash>,const engine::HeldSubscriptionSeedResolver&) {}
std::expected<void,storage::Error> OwnedI2pService::addMagnet(const engine::PendingMagnet&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"I2P metadata provider unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::cancelMagnet(const core::TorrentHash&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"I2P metadata provider unavailable"});}
std::vector<engine::PendingMagnetStatus> OwnedI2pService::pendingMagnets()const{return {};}
std::optional<search::I2pSubscriptionUpgrade> OwnedI2pService::takeSubscriptionUpgrade(){return {};}
std::expected<void,storage::Error> OwnedI2pService::downloadSubscriptionUpgrade(const std::array<char,20>&,std::uint32_t){
  return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Private I2P is unavailable"});
}
std::optional<std::expected<std::string,storage::Error>> OwnedI2pService::takeSubscriptionUpgradePayload(){return {};}
std::expected<std::string,storage::Error> OwnedI2pService::takeCompletedMetadata(const core::TorrentHash&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"I2P metadata provider unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::releaseMagnet(const core::TorrentHash&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"I2P metadata provider unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::publishPendingMagnet(const core::TorrentHash&,const std::function<std::expected<void,storage::Error>()>&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"I2P metadata provider unavailable"});}
}
#endif
