// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile_source.hpp"
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace biglybtc::services::i2p::provider {
// One coalesced bounded operation. The worker alone acquires/releases the
// source gate; the socket thread consumes a one-shot Ready permit, never waits
// for filesystem I/O and never invokes user callbacks under the permit.
class SourcePermit final {
public:
  enum class Phase {Idle,Requested,Reading,Ready,Executing,Consumed,Failed,Stopped};
  static QEvent::Type wakeEvent(){static const auto type=[] {const auto value=QEvent::registerEventType();if(value<QEvent::User){throw std::runtime_error("No owned I2P source event type");}return static_cast<QEvent::Type>(value);}();return type;}
  explicit SourcePermit(std::shared_ptr<ProfileSource> source,QObject* receiver):source_(std::move(source)),receiver_(receiver),worker_([this](std::stop_token stop){run(stop);}){}
  ~SourcePermit(){stop();join();}
  SourcePermit(const SourcePermit&)=delete;
  bool request(){std::lock_guard lock(mutex_);if(phase_!=Phase::Idle){return false;}phase_=Phase::Requested;changed_.notify_all();return true;}
  std::optional<std::uint64_t> take(){std::lock_guard lock(mutex_);if(phase_!=Phase::Ready||stopped_){return {};}if(!source_->effectAuthority()){stopped_=true;phase_=Phase::Failed;changed_.notify_all();return {};}
    if(!source_->effectAuthority()->enter(revision_)){
      // A dirty watch/cooperative epoch can race Ready. Release this actual
      // gate and revalidate; permanent revocation still settles Failed.
      if(source_->effectAuthority()->revoked()){stopped_=true;phase_=Phase::Failed;}else{phase_=Phase::Consumed;}
      changed_.notify_all();return {};
    }phase_=Phase::Executing;return revision_;}
  void acknowledge(){if(source_->effectAuthority()){source_->effectAuthority()->leave();}std::lock_guard lock(mutex_);if(phase_==Phase::Executing){phase_=Phase::Consumed;}changed_.notify_all();}
  Phase phase()const{std::lock_guard lock(mutex_);return phase_;}
  void stop()noexcept{std::lock_guard lock(mutex_);stopped_=true;receiver_=nullptr;changed_.notify_all();worker_.request_stop();}
  // External lifetime only, after network withdrawal. An uninterruptible OS
  // call can delay join; it cannot keep an application socket/permit active.
  void join()noexcept{if(worker_.joinable()){worker_.join();}}
private:
  void wake(){if(receiver_){QCoreApplication::postEvent(receiver_,new QEvent(wakeEvent()));}}
  void run(std::stop_token stop)noexcept{
    for(;;){
      {std::unique_lock lock(mutex_);changed_.wait(lock,[&]{return stopped_||stop.stop_requested()||phase_==Phase::Requested;});if(stopped_||stop.stop_requested()){phase_=Phase::Stopped;return;}phase_=Phase::Reading;}
      bool ok=false;
      try{
        auto access=source_->lease()->sourceAccess();
        if(access&&!stop.stop_requested()&&source_->validate()){
          std::unique_lock lock(mutex_);
          if(!stopped_&&!stop.stop_requested()){
            revision_=access->revision();phase_=Phase::Ready;wake();
            changed_.wait(lock,[&]{return phase_==Phase::Consumed||(stopped_&&phase_!=Phase::Executing);});
            ok=phase_==Phase::Consumed;
          }
        }
      }catch(...){ok=false;}
      // Source access is destroyed on this worker before publishing Idle.
      std::lock_guard lock(mutex_);if(stopped_||stop.stop_requested()){phase_=Phase::Stopped;return;}phase_=ok?Phase::Idle:Phase::Failed;
      try{wake();}catch(...){phase_=Phase::Failed;receiver_=nullptr;return;}
    }
  }
  std::shared_ptr<ProfileSource> source_;
  mutable std::mutex mutex_;std::condition_variable changed_;
  QObject* receiver_;Phase phase_=Phase::Idle;bool stopped_=false;std::uint64_t revision_=0;
  std::jthread worker_;
};
}
