// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_service.hpp"
#ifndef BIGLYBTC_HOST_I2P_OWNER
#define BIGLYBTC_HOST_I2P_OWNER 0
#endif
#if BIGLYBTC_HOST_I2P_OWNER
#include "../services/i2p_provider/owner.hpp"
#include <QCoreApplication>
#include <QHostAddress>
#include <QMetaObject>
#include <QThread>
#include <mutex>
#include <chrono>
#include <optional>

namespace biglybtc::app {
namespace provider=services::i2p::provider;
struct OwnedI2pService::Impl {
  explicit Impl(std::unique_ptr<provider::OwnedTorrent> value)
      :owner(std::move(value)),caller(QThread::currentThread()){}
  QThread thread;
  std::unique_ptr<provider::OwnedTorrent> owner;
  QThread* caller;
  mutable std::mutex mutex;
  mutable Observation last;
  bool closed=false;
  std::optional<storage::Error> closeError;

  void capture()const {
    const auto actual=owner->observation();
    switch(actual.phase){
    case provider::OwnedTorrent::Phase::Prepared:last.phase=Phase::Prepared;break;
    case provider::OwnedTorrent::Phase::Starting:last.phase=Phase::Starting;break;
    case provider::OwnedTorrent::Phase::Active:last.phase=Phase::Active;break;
    case provider::OwnedTorrent::Phase::Withdrawn:last.phase=Phase::Withdrawn;break;
    case provider::OwnedTorrent::Phase::Retiring:last.phase=Phase::Retiring;break;
    }
    last.publicIdentity=actual.publicIdentity;
    last.admission=actual.admission;last.generation=actual.generation;
    last.current=actual.phase==provider::OwnedTorrent::Phase::Active;
    if(last.current||actual.phase==provider::OwnedTorrent::Phase::Retiring){
      last.verifiedBytes=actual.verifiedBytes;last.seeding=actual.seeding;
      last.payloadDownloaded=actual.payloadDownloaded;last.payloadUploaded=actual.payloadUploaded;
      last.downloadRate=actual.downloadRate;last.uploadRate=actual.uploadRate;
      last.peers=actual.peers;last.seeds=actual.seeds;last.checked=actual.checked;
      last.trackerAnnounces=actual.diagnostics.alerts.announces;
      last.trackerReplies=actual.diagnostics.alerts.trackerReplies;
      last.trackerErrors=actual.diagnostics.alerts.trackerErrors;
    }else{last.downloadRate=0;last.uploadRate=0;last.peers=0;last.seeds=0;}
    if(!actual.failure.empty()){last.failure=actual.failure;}
    else if(actual.diagnostics.error.value){last.failure=actual.diagnostics.error.message;}
    else if(actual.phase==provider::OwnedTorrent::Phase::Withdrawn&&last.failure.empty()){
      last.failure="I2P owner withdrawn";
    }
  }
  void finishAndMove(QThread* target,bool immediate)noexcept {
    try{capture();}catch(...){last.current=false;}
    if(immediate){try{owner->close();}catch(...){last.current=false;}}
    last.phase=Phase::Withdrawn;last.current=false;
    last.downloadRate=0;last.uploadRate=0;last.peers=0;last.seeds=0;
    // OwnedTorrent is parentless; its channel/bridge/children move together.
    if(owner->thread()!=target&&!owner->moveToThread(target)){
      try{last.failure="Thread move failed";}catch(...){}
    }
  }
  template<typename Function> bool onOwner(Function&& function){
    if(owner->thread()==QThread::currentThread()){function();return true;}
    return thread.isRunning()&&QMetaObject::invokeMethod(owner.get(),
        std::forward<Function>(function),Qt::BlockingQueuedConnection);
  }
  std::expected<void,storage::Error> shutdown(bool graceful){
    if(closed){if(closeError){return std::unexpected(*closeError);}return {};}
    if(QThread::currentThread()!=caller){
      return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"I2P close requires its Host worker"});
    }
    bool ready=false,uninstalled=false;
    std::optional<storage::Error> failure;
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds{5};
    if(graceful&&owner){
      try{
        bool began=false;
        if(!onOwner([&]{
          try{
            capture();
            uninstalled=last.generation==0&&(last.phase==Phase::Prepared||last.phase==Phase::Starting);
            if(!uninstalled){
              began=owner->beginRetirement(end);
              if(!began){failure=owner->retirement().failure.value_or(storage::Error{
                  storage::ErrorCode::Conflict,"I2P admission cannot certify retirement; recheck required"});}
            }
          }catch(...){failure=storage::Error{storage::ErrorCode::Io,"I2P retirement setup failed"};}
        })){failure=storage::Error{storage::ErrorCode::Io,"I2P event thread unavailable for retirement"};}
        while(began&&!failure&&!ready&&std::chrono::steady_clock::now()<end){
          if(!onOwner([&]{
            try{
              const auto retired=owner->retirement();
              ready=retired.readyForJoin;failure=retired.failure;
              if(!ready){capture();}
            }catch(...){failure=storage::Error{storage::ErrorCode::Io,"I2P retirement observation failed"};}
          })){failure=storage::Error{storage::ErrorCode::Io,"I2P event thread stopped during retirement"};}
          if(!ready&&!failure){QThread::msleep(10);}
        }
        if(!uninstalled&&!ready&&!failure){failure=storage::Error{
            storage::ErrorCode::Io,"I2P retirement deadline expired; recheck required"};}
      }catch(...){failure=storage::Error{storage::ErrorCode::Io,"I2P retirement failed; recheck required"};}
    }
    // A successful retirement retains its exact snapshot for checkpointRetired.
    // All other paths explicitly revoke before the QObject tree leaves Qt.
    if(owner){
      try{
        if(!onOwner([&]{finishAndMove(caller,!ready);})){ready=false;failure=storage::Error{
            storage::ErrorCode::Io,"Cannot return I2P owner to its Host worker"};}
      }catch(...){ready=false;failure=storage::Error{storage::ErrorCode::Io,"I2P thread cleanup failed"};}
    }
    thread.quit();thread.wait();
    // finished's direct fallback closes/moves any tree left on the Qt thread.
    // Actual OS/provider/source joins always run externally, even on failure.
    if(owner){
      try{owner->join();last.joined=true;}
      catch(...){ready=false;failure=storage::Error{storage::ErrorCode::Io,"I2P external join failed"};}
      if(graceful&&ready&&!failure){
        try{auto saved=owner->checkpointRetired();if(!saved){failure=saved.error();}}
        catch(...){failure=storage::Error{storage::ErrorCode::Io,"I2P checkpoint failed; recheck required"};}
      }
      owner.reset();
    }
    closed=true;last.phase=Phase::Withdrawn;last.current=false;
    last.downloadRate=0;last.uploadRate=0;last.peers=0;last.seeds=0;
    closeError=std::move(failure);
    if(closeError){last.failure=closeError->message;return std::unexpected(*closeError);}
    last.failure.clear();return {};
  }
};
OwnedI2pService::OwnedI2pService(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
bool OwnedI2pService::available()noexcept{return true;}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::start(
    const storage::ProfileLease& lease,const core::TorrentHash& hash,I2pServiceOptions options){
  if(!QCoreApplication::instance()||QCoreApplication::closingDown()){
    return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Qt application required for I2P"});
  }
  if(QThread::currentThread()==QCoreApplication::instance()->thread()){
    return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"I2P start requires the Host worker"});
  }
  if(!options.routerPort||!options.peerPort){
    return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Nonzero I2P router/peer ports required"});
  }
  try{
    // Filesystem preparation and SAME-OFD/key/full-metadata checks stay on the
    // external Host worker, before the private Qt socket loop exists.
    auto prepared=provider::OwnedTorrent::prepare(lease,hash);
    if(!prepared){return std::unexpected(prepared.error());}
    auto service=std::unique_ptr<OwnedI2pService>(new OwnedI2pService(std::make_unique<Impl>(std::move(*prepared))));
    auto& state=*service->impl_;
    provider::OwnedTorrent::Options actual;
    // I2CP negotiates client protocol features here; product version0.1.0
    // requests legacy LeaseSet21. Use the already-qualified protocol profile.
    actual.router={QHostAddress::LocalHost,options.routerPort,"2.13.0",false,30000,false};
    actual.session=std::move(options.sessionOptions);actual.ports={options.peerPort,options.peerPort};
    // Also handles a failed queued invocation: finished runs on the owning
    // thread and returns the closed QObject tree before external destruction.
    QObject::connect(&state.thread,&QThread::finished,state.owner.get(),[&state]{
      if(state.owner&&state.owner->thread()==QThread::currentThread()){
        state.finishAndMove(state.caller,true);
      }
    },Qt::DirectConnection);
    state.thread.start();
    if(!state.thread.isRunning()||!state.owner->moveToThread(&state.thread)){
      (void)service->impl_->shutdown(false);return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot start I2P event thread"});
    }
    bool started=false;
    const bool posted=QMetaObject::invokeMethod(state.owner.get(),[&state,&actual,&started]{
      try{started=state.owner->begin(actual);state.capture();}catch(...){started=false;}
    },Qt::BlockingQueuedConnection);
    if(!posted||!started){
      (void)service->impl_->shutdown(false);return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot begin owned I2P session"});
    }
    return service;
  }catch(...){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot prepare I2P service"});}
}
OwnedI2pService::Observation OwnedI2pService::observation()const {
  auto& state=*impl_;std::lock_guard lock(state.mutex);
  if(state.closed||!state.owner){return state.last;}
  if(QThread::currentThread()!=state.caller){
    auto value=state.last;value.current=false;value.failure="Host worker required";return value;
  }
  try{
    if(!state.thread.isRunning()){state.last.current=false;state.last.failure="I2P event thread stopped";return state.last;}
    if(!QMetaObject::invokeMethod(state.owner.get(),[&state]{
      try{state.capture();}catch(...){state.last.current=false;state.last.failure="Unavailable";}
    },Qt::BlockingQueuedConnection)){
      state.last.current=false;state.last.failure="Unavailable";
    }
    return state.last;
  }catch(...){Observation value;value.phase=Phase::Withdrawn;value.failure="Unavailable";return value;}
}
std::expected<void,storage::Error> OwnedI2pService::close(){
  auto& state=*impl_;std::lock_guard lock(state.mutex);return state.shutdown(true);
}
OwnedI2pService::~OwnedI2pService(){
  try{auto& state=*impl_;std::lock_guard lock(state.mutex);(void)state.shutdown(false);}catch(...){}
}
}
#else
namespace biglybtc::app {
struct OwnedI2pService::Impl {};
OwnedI2pService::OwnedI2pService(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
OwnedI2pService::~OwnedI2pService()=default;
bool OwnedI2pService::available()noexcept{return false;}
std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> OwnedI2pService::start(
    const storage::ProfileLease&,const core::TorrentHash&,I2pServiceOptions){
  return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional native I2P owner is unavailable"});
}
OwnedI2pService::Observation OwnedI2pService::observation()const{
  Observation value;value.phase=Phase::Withdrawn;value.failure="Optional native I2P owner is unavailable";return value;
}
std::expected<void,storage::Error> OwnedI2pService::close(){return {};}
}
#endif
