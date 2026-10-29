// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Private to the explicit Azureus service. Concrete bounded use of the existing
// Executor; no actor, router, DB or QObject is accessed by an I/O operation.
#include "azureus_value_versions.hpp"
#include "local_rating.hpp"
#include "biglybtc/executor.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace biglybtc::app::detail {
class AzureusProfileIo final {
public:
  struct Data {
    std::optional<std::int32_t> version;
    std::shared_ptr<const storage::DistributedRatingSnapshot> rating;
    std::uint64_t revision=0;
  };
  using Result=std::expected<Data,storage::Error>;
  using Operation=std::move_only_function<Result(std::stop_token)>;
  enum class Phase {Queued,Running,Ready,Executing,Committed,Released};
  struct Job final {
    mutable std::mutex mutex;
    std::condition_variable changed;
    Phase phase=Phase::Queued;
    std::atomic<bool> revoked{false};
    std::optional<Result> result{std::unexpected(storage::Error{storage::ErrorCode::Io,"Profile IO did not complete"})};
    std::optional<core::Ticket> ticket;
  };
  using Handle=std::shared_ptr<Job>;
  static QEvent::Type wakeEvent(){static const auto type=[] {const int value=QEvent::registerEventType();if(value<QEvent::User){throw std::runtime_error("No profile IO event type");}return static_cast<QEvent::Type>(value);}();return type;}
private:
  struct Mailbox final {
    std::mutex mutex;QObject* receiver=nullptr;bool queued=false;
    void signal(){
      std::lock_guard lock(mutex);if(!receiver||queued){return;}
      // Queue only: no user/reentrant callback can run while the worker holds
      // a source gate. Unbind serializes with posting before QObject teardown.
      auto event=std::make_unique<QEvent>(wakeEvent());QCoreApplication::postEvent(receiver,event.release());queued=true;
    }
  };
public:
  void bind(QObject* receiver){std::lock_guard lock(mailbox_->mutex);mailbox_->receiver=receiver;}
  void unbind()noexcept{std::lock_guard lock(mailbox_->mutex);mailbox_->receiver=nullptr;mailbox_->queued=false;}
  void drainWake(){std::lock_guard lock(mailbox_->mutex);mailbox_->queued=false;}
  explicit AzureusProfileIo(std::shared_ptr<const storage::ProfileLease> lease)
      :lease_(std::move(lease)),executor_(std::make_unique<core::Executor>(7)){
    static_cast<void>(wakeEvent());
    if(!lease_||!executor_->start()){throw std::runtime_error("Cannot start profile IO executor");}
  }
  ~AzureusProfileIo(){requestStop();join();}
  AzureusProfileIo(const AzureusProfileIo&)=delete;
  // Only the external create/lifetime caller may wait. No DHT owner runs here.
  bool initialize(const std::shared_ptr<storage::AzureusValueVersions>& versions){
    auto result=std::make_shared<std::optional<bool>>();
    auto submitted=executor_->submit([result,versions](std::stop_token stop)->core::WorkResult{
      *result=!stop.stop_requested()&&bool(versions->validate());return {};
    });if(!submitted){return false;}if(!submitted->result().get()){return false;}return result->value_or(false);
  }
  std::expected<Handle,core::SubmitError> submit(Operation operation){
    std::lock_guard slots(slotsMutex_);
    if(stopping_){return std::unexpected(core::SubmitError::Stopped);}
    Handle* free=nullptr;for(auto& item:slots_){if(!item){free=&item;break;}}
    if(!free){return std::unexpected(core::SubmitError::Overloaded);}
    auto job=std::make_shared<Job>();*free=job;
    std::size_t held=0;for(const auto& item:slots_){held+=bool(item);}highWater_=std::max(highWater_,held);
    const auto lease=lease_;const auto mailbox=mailbox_;
    auto submitted=executor_->submit([job,lease,mailbox,operation=std::move(operation)](std::stop_token stop)mutable->core::WorkResult{
      bool entering=false;
      {std::lock_guard lock(job->mutex);if(job->phase==Phase::Released){return {};}entering=!stop.stop_requested()&&!job->revoked.load();if(entering){job->phase=Phase::Running;}}
      const auto signal=[&]()noexcept{try{mailbox->signal();}catch(...){std::lock_guard lock(job->mutex);job->revoked=true;job->changed.notify_all();}};
      try{if(entering){
        // The worker retains and releases its own lock, including while the
        // actor consumes a one-shot permit. It never runs actor callbacks.
        auto access=lease->sourceAccess();
        Result result=std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Profile IO cancelled"});
        try{if(access&&!stop.stop_requested()){
          result=operation(stop);if(result){result->revision=access->revision();}
        }else if(!access){result=std::unexpected(access.error());}}
        catch(...){result=std::unexpected(storage::Error{storage::ErrorCode::Io,"Profile IO failed"});}
        {
          std::lock_guard lock(job->mutex);job->result=std::move(result);
          if(stop.stop_requested()){job->revoked=true;}
          job->phase=Phase::Ready;
        }
        signal();
        std::unique_lock lock(job->mutex);
        std::stop_callback stopped(stop,[&]{job->revoked.store(true);job->changed.notify_all();});
        job->changed.wait(lock,[&]{return job->phase==Phase::Committed||(job->revoked&&job->phase!=Phase::Executing);});
      }}catch(...){job->revoked.store(true);} // Source gate released first.
      {std::lock_guard lock(job->mutex);job->phase=Phase::Released;}signal();return {};
    });
    if(!submitted){free->reset();return std::unexpected(submitted.error());}
    job->ticket.emplace(std::move(*submitted));return job;
  }
  static std::optional<Result> take(const Handle& job){
    std::lock_guard lock(job->mutex);if(job->phase!=Phase::Ready||job->revoked||!job->result){return {};}
    job->phase=Phase::Executing;return *job->result;
  }
  static void acknowledge(const Handle& job)noexcept{
    std::lock_guard lock(job->mutex);if(job->phase==Phase::Executing){job->phase=Phase::Committed;}job->changed.notify_all();
  }
  static void revoke(const Handle& job)noexcept{
    {std::lock_guard lock(job->mutex);job->revoked=true;if(job->phase==Phase::Queued){job->phase=Phase::Released;}job->changed.notify_all();}
    if(job->ticket){job->ticket->cancel();}
  }
  static storage::Error failure(const Handle& job){
    std::lock_guard lock(job->mutex);if(job->result&&!*job->result){return job->result->error();}
    return {storage::ErrorCode::Cancelled,"Profile completion was revoked before execution"};
  }
  struct Facts {std::size_t pending=0,running=0,ready=0,highWater=0;};
  Facts facts(){
    std::lock_guard slots(slotsMutex_);Facts result;result.highWater=highWater_;
    for(const auto& slot:slots_){if(!slot){continue;}++result.pending;std::lock_guard lock(slot->mutex);result.running+=slot->phase==Phase::Running;result.ready+=slot->phase==Phase::Ready||slot->phase==Phase::Executing;}
    return result;
  }
  std::size_t highWater(){std::lock_guard lock(slotsMutex_);return highWater_;}
  std::size_t queued()const{return executor_?executor_->pending():0;}
  static bool released(const Handle& job){std::lock_guard lock(job->mutex);return job->phase==Phase::Released;}
  void release(const Handle& job){
    if(!released(job)){return;}
    std::lock_guard lock(slotsMutex_);for(auto& slot:slots_){if(slot==job){slot.reset();return;}}
  }
  void requestStop()noexcept{
    {std::lock_guard lock(slotsMutex_);stopping_=true;for(const auto& slot:slots_){if(slot){revoke(slot);}}}
    if(executor_){executor_->requestShutdown();}
  }
  // External lifetime only. An uninterruptible OS syscall can delay this join;
  // network close/deadlines have already progressed on their separate thread.
  void join()noexcept{executor_.reset();}
  const std::shared_ptr<const storage::ProfileLease>& lease()const noexcept{return lease_;}
private:
  std::shared_ptr<Mailbox> mailbox_=std::make_shared<Mailbox>();
  std::shared_ptr<const storage::ProfileLease> lease_;
  std::mutex slotsMutex_;
  std::array<Handle,8> slots_{};
  bool stopping_=false;
  std::size_t highWater_=0;
  std::unique_ptr<core::Executor> executor_;
};
}
