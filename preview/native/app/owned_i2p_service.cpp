// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_service.hpp"
#ifndef BIGLYBTC_HOST_I2P_OWNER
#define BIGLYBTC_HOST_I2P_OWNER 0
#endif
#if BIGLYBTC_HOST_I2P_OWNER
#include "../services/i2p_provider/profile_owner.hpp"
#include "library_commands.hpp"
#include "biglybtc/queue_policy.hpp"
#include <QCoreApplication>
#include <QHostAddress>
#include <QMetaObject>
#include <QThread>
#include <limits>
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
  std::unique_ptr<provider::OwnedProfile> owner;
  std::shared_ptr<const storage::ProfileLease> lease;
  QThread thread;QThread* caller=QThread::currentThread();
  mutable std::mutex mutex;mutable std::vector<TorrentObservation> last;
  mutable Phase lastPhase=Phase::Prepared;
  bool closed=false,joined=false;std::optional<Error> closeError;
  explicit Impl(std::unique_ptr<provider::OwnedProfile> p,std::shared_ptr<const storage::ProfileLease> l):owner(std::move(p)),lease(std::move(l)){}
  template<class F> bool onOwner(F&& f)const{
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
      v.failure=a.failure;v.queued=observed.queued;v.selectedComplete=observed.selectedComplete;v.completionPositionUnavailable=observed.completionPositionUnavailable;v.downloadLimit=observed.downloadLimit;v.uploadLimit=observed.uploadLimit;v.queueUnavailable=observed.queueUnavailable;v.globalDownloadLimit=observed.globalDownloadLimit;v.globalUploadLimit=observed.globalUploadLimit;v.loopbackPeerClasses=observed.loopbackPeerClasses;next.push_back({observed.hash,std::move(v)});
    }last=std::move(next);
  }
  std::expected<void,Error> stop(const core::TorrentHash& h){
    if(closed||QThread::currentThread()!=caller){return std::unexpected(Error{Code::Conflict,"Live I2P Host worker required"});}
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
    std::vector<core::TorrentHash> rows;if(!onOwner([&]{rows=owner->completedForceStarts();})){return;}
    for(const auto& h:rows){const auto saved=owner->publish(h,[&]()->std::expected<void,Error>{auto result=storage::clearLibraryForceStart(*lease,h);return result?std::expected<void,Error>{}:std::expected<void,Error>{std::unexpected(result.error())};});
      if(!saved||!refreshQueue()){(void)onOwner([&]{owner->close();});return;}(void)onOwner([&]{owner->acknowledgeCompletion(h);});}
  }
  void moveClosed()noexcept{
    try{owner->close();if(owner->thread()!=caller){(void)owner->moveToThread(caller);}}catch(...){}
  }
  std::expected<void,Error> shutdown(bool graceful){
    if(QThread::currentThread()!=caller){return std::unexpected(Error{Code::Unsupported,"I2P close requires its Host worker"});}
    if(closed){return closeError?std::expected<void,Error>{std::unexpected(*closeError)}:std::expected<void,Error>{};}
    std::optional<Error> first;
    if(graceful){
      if(onOwner([&]{capture();})){const auto selected=last;for(const auto& entry:selected){auto saved=stop(entry.hash);if(!saved&&!first){first=saved.error();}}}
      else{first=Error{Code::Io,"I2P event loop unavailable for close"};}
    }
    if(!onOwner([&]{moveClosed();})){first=Error{Code::Io,"Cannot close shared I2P event loop"};}
    thread.quit();thread.wait();owner->join();joined=true;owner.reset();closed=true;
    for(auto& entry:last){entry.value.current=false;entry.value.joined=true;entry.value.phase=Phase::Withdrawn;entry.value.downloadRate=0;entry.value.uploadRate=0;}
    closeError=first;return first?std::expected<void,Error>{std::unexpected(*first)}:std::expected<void,Error>{};
  }
};
OwnedI2pService::OwnedI2pService(std::unique_ptr<Impl> p):impl_(std::move(p)){}
bool OwnedI2pService::available()noexcept{return true;}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::start(const storage::ProfileLease& lease,const core::TorrentHash& hash,I2pServiceOptions options){
  if(!QCoreApplication::instance()||QCoreApplication::closingDown()||QThread::currentThread()==QCoreApplication::instance()->thread()||!options.routerPort||!options.peerPort){return std::unexpected(Error{Code::Unsupported,"Nonzero I2P ports and a live Qt application/Host worker required"});}
  try{
    struct Bootstrap {std::unique_ptr<provider::OwnedProfile> owner;std::shared_ptr<const storage::ProfileLease> lease;core::QueuePolicy queue;provider::OwnedProfile::Rates rates;};
    auto bootstrap=[&]()->std::expected<Bootstrap,Error>{
      auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
      auto config=storage::ConfigFile::load(lease,"biglybt.config");if(!config){return std::unexpected(config.error());}const auto configBytes=config->document().serialize();
      auto queue=core::readQueuePolicy(&config->document());if(!queue){return std::unexpected(Error{Code::InvalidFormat,"Invalid original queue setting: "+queue.error().key});}
      auto rates=savedRates(config->document());if(!rates){return std::unexpected(rates.error());}
      auto retained=lease.retain();if(!retained){return std::unexpected(retained.error());}
      auto prepared=provider::OwnedProfile::prepare(lease,hash);if(!prepared){return std::unexpected(prepared.error());}
      auto current=storage::ConfigFile::load(lease,"biglybt.config");if(!current||current->document().serialize()!=configBytes){return std::unexpected(Error{Code::Conflict,"Original I2P queue/rate configuration changed during preparation"});}
      return Bootstrap{std::move(*prepared),std::move(*retained),std::move(*queue),*rates};
    }(); // Release the source gate BEFORE starting or waiting on Qt sockets.
    if(!bootstrap){return std::unexpected(bootstrap.error());}
    auto service=std::unique_ptr<OwnedI2pService>(new OwnedI2pService(std::make_unique<Impl>(std::move(bootstrap->owner),std::move(bootstrap->lease))));auto& s=*service->impl_;
    provider::OwnedProfile::Options actual;actual.router={QHostAddress::LocalHost,options.routerPort,"2.13.0",false,30000,false};actual.session=std::move(options.sessionOptions);actual.ports={options.peerPort,options.peerPort};
    QObject::connect(&s.thread,&QThread::finished,s.owner.get(),[&s]{if(s.owner&&s.owner->thread()==QThread::currentThread()){s.moveClosed();}},Qt::DirectConnection);
    s.thread.start();if(!s.thread.isRunning()||!s.owner->moveToThread(&s.thread)){(void)s.shutdown(false);return std::unexpected(Error{Code::Io,"Cannot start shared I2P event loop"});}
    bool began=false;if(!s.onOwner([&]{began=s.owner->begin(actual,std::move(bootstrap->queue),bootstrap->rates);s.capture();})||!began){(void)s.shutdown(false);return std::unexpected(Error{Code::Io,"Cannot begin shared I2P session"});}return service;
  }catch(...){return std::unexpected(Error{Code::Io,"Cannot prepare shared I2P service"});}
}
std::vector<OwnedI2pService::TorrentObservation> OwnedI2pService::observations()const{
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(!s.closed&&s.owner&&QThread::currentThread()==s.caller){s.settleCompletions();if(!s.onOwner([&]{s.capture();})){for(auto& e:s.last){e.value.current=false;e.value.failure="I2P event loop unavailable";}}}return s.last;
}
OwnedI2pService::Observation OwnedI2pService::observation()const{
  const auto rows=observations();Observation out;out.joined=impl_->joined;if(rows.empty()){out.phase=impl_->closed?Phase::Withdrawn:impl_->lastPhase;return out;}out=rows.front().value;
  if(rows.size()>1){bool capped=false;out.verifiedBytes=0;out.payloadDownloaded=0;out.payloadUploaded=0;out.downloadRate=0;out.uploadRate=0;out.peers=0;out.seeds=0;out.checked=true;out.seeding=true;out.trackerAnnounces=out.trackerReplies=out.trackerErrors=0;
    for(const auto& entry:rows){const auto& v=entry.value;out.verifiedBytes=displaySum(out.verifiedBytes,v.verifiedBytes,capped);out.payloadDownloaded=payloadSum(out.payloadDownloaded,v.payloadDownloaded,capped);out.payloadUploaded=payloadSum(out.payloadUploaded,v.payloadUploaded,capped);out.downloadRate=displaySum(out.downloadRate,v.downloadRate,capped);out.uploadRate=displaySum(out.uploadRate,v.uploadRate,capped);out.peers=displaySum(out.peers,v.peers,capped);out.seeds=displaySum(out.seeds,v.seeds,capped);out.checked&=v.checked;out.seeding&=v.seeding;out.trackerAnnounces=displaySum(out.trackerAnnounces,v.trackerAnnounces,capped);out.trackerReplies=displaySum(out.trackerReplies,v.trackerReplies,capped);out.trackerErrors=displaySum(out.trackerErrors,v.trackerErrors,capped);}
    if(capped){out.failure="Some aggregate values exceed the display range; individual measurements remain available";}}
  return out;
}
std::expected<void,storage::Error> OwnedI2pService::add(const core::TorrentHash& h){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(s.closed||QThread::currentThread()!=s.caller){return std::unexpected(Error{Code::Conflict,"Live I2P Host worker required"});}
  provider::OwnedProfile::Retained retained;if(!s.onOwner([&]{retained=s.owner->retained();})){return std::unexpected(Error{Code::Io,"I2P event loop unavailable"});}
  auto prepared=s.owner->prepareTorrent(h,retained);if(!prepared){return std::unexpected(prepared.error());}std::expected<void,Error> result;
  if(!s.onOwner([&]{result=s.owner->add(std::move(*prepared));s.capture();})){return std::unexpected(Error{Code::Io,"Cannot add prepared I2P member"});}return result;
}
std::expected<void,storage::Error> OwnedI2pService::stop(const core::TorrentHash& h){auto& s=*impl_;std::lock_guard lock(s.mutex);return s.stop(h);}
std::expected<void,storage::Error> OwnedI2pService::publish(const core::TorrentHash& h,const std::function<std::expected<void,storage::Error>()>& f){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(s.closed||QThread::currentThread()!=s.caller){return std::unexpected(Error{Code::Conflict,"Live I2P Host worker required"});}return s.owner->publish(h,f);
}
std::expected<void,storage::Error> OwnedI2pService::command(const core::TorrentHash& h,core::DownloadAction action){
  // Serialize on the existing Host worker. A saved-intent edit is allowed only
  // after this target's storage is settled; every sibling keeps its handle.
  bool active=false;for(const auto& row:observations()){active|=row.hash==h;}if(active){auto retired=stop(h);if(!retired){return retired;}}
  bool start=false;auto changed=publish(h,[&]()->std::expected<void,Error>{auto r=storage::applyLibraryCommand(*impl_->lease,h,action,{});if(!r){return std::unexpected(r.error());}start=!r->pauseAfter&&r->after.startup!=core::StartupState::Stopped;return {};});if(!changed){return changed;}
  return start&&(action==core::DownloadAction::Start||action==core::DownloadAction::Resume||action==core::DownloadAction::Queue||action==core::DownloadAction::ForceStart)?add(h):std::expected<void,Error>{};
}
std::expected<void,storage::Error> OwnedI2pService::move(const core::TorrentHash& h,core::DownloadAction action,std::span<const storage::LibraryCompletionPool> pools){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(s.closed||QThread::currentThread()!=s.caller){return std::unexpected(Error{Code::Conflict,"Live I2P Host worker required"});}
  auto changed=s.owner->publishPositions([&]()->std::expected<void,Error>{auto r=storage::applyLibraryCommand(*s.lease,h,action,pools);return r?std::expected<void,Error>{}:std::expected<void,Error>{std::unexpected(r.error())};});if(!changed){return changed;}return s.refreshQueue();
}
std::expected<void,storage::Error> OwnedI2pService::close(){auto& s=*impl_;std::lock_guard lock(s.mutex);return s.shutdown(true);}
OwnedI2pService::~OwnedI2pService(){try{auto& s=*impl_;std::lock_guard lock(s.mutex);(void)s.shutdown(false);}catch(...){}}
}
#else
namespace biglybtc::app {
struct OwnedI2pService::Impl {};
OwnedI2pService::OwnedI2pService(std::unique_ptr<Impl> p):impl_(std::move(p)){}
OwnedI2pService::~OwnedI2pService()=default;
bool OwnedI2pService::available()noexcept{return false;}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::start(const storage::ProfileLease&,const core::TorrentHash&,I2pServiceOptions){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
OwnedI2pService::Observation OwnedI2pService::observation()const{Observation v;v.phase=Phase::Withdrawn;v.failure="Optional native I2P owner is unavailable";return v;}
std::vector<OwnedI2pService::TorrentObservation> OwnedI2pService::observations()const{return {};}
std::expected<void,storage::Error> OwnedI2pService::add(const core::TorrentHash&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::stop(const core::TorrentHash&){return {};}
std::expected<void,storage::Error> OwnedI2pService::command(const core::TorrentHash&,core::DownloadAction){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::publish(const core::TorrentHash&,const std::function<std::expected<void,storage::Error>()>&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::move(const core::TorrentHash&,core::DownloadAction,std::span<const storage::LibraryCompletionPool>){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});}
std::expected<void,storage::Error> OwnedI2pService::close(){return {};}
}
#endif
