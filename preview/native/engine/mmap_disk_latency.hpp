#pragma once
#include "disk_latency.hpp"
#include <atomic>
#include <cstdint>
#include <memory>

namespace biglybtc::engine::detail {
// An optional exact admission observation. Registries/slots/callbacks hold weak
// references only, never a torrent/owner. No disk operation depends on success.
class MmapDiskLatency final:public std::enable_shared_from_this<MmapDiskLatency> {
public:
  explicit MmapDiskLatency(std::uint64_t admission):admission_(admission){}
  std::uint64_t admission() const noexcept {return admission_;}
  bool active() const noexcept {return active_.load(std::memory_order_acquire);}
  bool claim() noexcept {bool expected=false;return active()&&claimed_.compare_exchange_strong(expected,true,std::memory_order_acq_rel);}
  void retire() noexcept {active_.store(false,std::memory_order_release);}
  void lost() noexcept {lost_.store(true,std::memory_order_release);}
  DiskLatencySnapshot snapshot(std::int64_t now) const noexcept {
    auto result=latency_.snapshot(now);
    if(!active()||!claimed_.load(std::memory_order_acquire)||lost_.load(std::memory_order_acquire)){result.readMillis.reset();result.writeMillis.reset();result.measurementLost|=lost_.load(std::memory_order_acquire)||!active();}
    return result;
  }
  class Request final {
  public:
    Request(std::weak_ptr<MmapDiskLatency> owner,std::uint64_t token):owner_(std::move(owner)),token_(token){}
    ~Request(){if(!completed_.exchange(true,std::memory_order_acq_rel)){if(auto owner=owner_.lock()){owner->lost();owner->latency_.finish(token_,false);}}}
    void complete(bool success) noexcept {
      if(auto owner=owner_.lock()){
        if(completed_.exchange(true,std::memory_order_acq_rel)){owner->lost();return;}
        owner->latency_.finish(token_,success);
      }
    }
  private:
    std::weak_ptr<MmapDiskLatency> owner_;
    std::uint64_t token_;
    std::atomic<bool> completed_{false};
  };
  std::shared_ptr<Request> begin(DiskIoAxis axis) noexcept {
    if(!active()||!claimed_.load(std::memory_order_acquire)||lost_.load(std::memory_order_acquire)){return {};}
    const auto token=latency_.begin(axis);if(!token){lost();return {};}
    try{return std::make_shared<Request>(weak_from_this(),token);}
    catch(...){lost();latency_.finish(token,false);return {};}
  }
private:
  const std::uint64_t admission_;
  DiskLatency latency_;
  std::atomic<bool> active_{true},claimed_{false},lost_{false};
};
}
