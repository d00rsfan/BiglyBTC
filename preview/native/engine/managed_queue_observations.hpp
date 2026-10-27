#pragma once
#include "biglybtc/queue_observations.hpp"
#include <array>
#include <atomic>
#include <thread>
#include <vector>
namespace biglybtc::engine::detail {
// Exactly one producer: the actual torrent's LT executor. Exactly one reader:
// this admission's worker, captured before attachment. No callback wait, heap
// allocation, LT pointer or IO; an overrun withdraws every optional fact.
class ManagedQueueObservations final {
public:
  static constexpr std::uint64_t Capacity=256;
  explicit ManagedQueueObservations(unsigned protocol):facts_(protocol),worker_(std::this_thread::get_id()){}
  void restore(std::optional<std::string_view> aggregate,std::optional<std::int64_t> legacy) noexcept {
    if(std::this_thread::get_id()!=worker_||installed_.load(std::memory_order_acquire)){failed_.store(true,std::memory_order_release);return;}
    if(aggregate){static_cast<void>(facts_.restoreAggregate(*aggregate));}
    else if(legacy){facts_.restoreLegacy(*legacy);}
  }
  void trackerOrder(const std::optional<std::vector<std::array<unsigned char,32>>>& order) noexcept {
    if(std::this_thread::get_id()!=worker_||installed_.load(std::memory_order_acquire)){failed_.store(true,std::memory_order_release);return;}
    if(order){static_cast<void>(facts_.trackerOrder(*order));}
  }
  void installed() noexcept {installed_.store(true,std::memory_order_release);}
  void observe(const core::QueueObservation& event) noexcept {
    if(failed_.load(std::memory_order_acquire)){return;}
    const auto write=written_.load(std::memory_order_relaxed);
    const auto read=read_.load(std::memory_order_acquire);
    if(write==UINT64_MAX||write-read>=Capacity){failed_.store(true,std::memory_order_release);return;}
    events_[write%Capacity]=event;
    written_.store(write+1,std::memory_order_release);
  }
  core::QueueObservedFacts facts(std::int64_t now) const noexcept {
    if(!drain()){return {};}
    return facts_.facts(now);
  }
  std::optional<core::QueueObservedDurations> settledDurations() const noexcept {
    if(!drain()){return {};}
    return facts_.settledDurations();
  }
private:
  bool drain() const noexcept {
    if(std::this_thread::get_id()!=worker_||!installed_.load(std::memory_order_acquire)
        ||failed_.load(std::memory_order_acquire)){return false;}
    auto read=read_.load(std::memory_order_relaxed);
    const auto end=written_.load(std::memory_order_acquire);
    while(read<end){static_cast<void>(facts_.observe(events_[read%Capacity]));++read;}
    read_.store(read,std::memory_order_release);
    return !failed_.load(std::memory_order_acquire);
  }
  mutable core::QueueOwnerObservations facts_;
  const std::thread::id worker_;
  std::array<core::QueueObservation,Capacity> events_{};
  mutable std::atomic<std::uint64_t> read_{0};
  std::atomic<std::uint64_t> written_{0};
  std::atomic<bool> installed_{false},failed_{false};
};
}
