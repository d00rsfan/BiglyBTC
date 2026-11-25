// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#if !defined(__cpp_lib_atomic_shared_ptr)
#include <mutex>
#endif
#include <utility>
#include <vector>
namespace biglybtc::services::shared_udp {class Provider;}
namespace biglybtc::core {
struct SharedUdpEndpoint {
  std::array<std::uint8_t,16> address{};std::uint16_t port=0;std::uint8_t length=4;
  bool operator==(const SharedUdpEndpoint&)const=default;
};
struct SharedUdpListener {SharedUdpEndpoint local,external;};
// Numeric observations minted only by the qualified SDK attachment. This is
// socket lifetime/observation evidence, never a profile or network permission.
class SharedUdpWitness final {
public:
 bool current(const SharedUdpEndpoint& local,const SharedUdpEndpoint& advertised)const noexcept{
  if(!state_->active||!state_->active->load(std::memory_order_acquire)){return false;}
  const auto values=state_->readListeners();if(!values){return false;}
  for(const auto& value:*values){if(value.local==local&&(advertised==local||value.external==advertised)){return true;}}return false;
 }
 // Revocation is safe from the external lifetime thread; it cannot grant or
 // resurrect a socket. The SDK observer sees this same atomic before send.
 void revoke()const noexcept{if(state_->active){state_->active->store(false,std::memory_order_release);}}
private:
 struct State {
  using Listeners = std::shared_ptr<const std::vector<SharedUdpListener>>;
  std::shared_ptr<std::atomic<bool>> active;
  std::shared_ptr<void> imagePin;

  Listeners readListeners() const {
#ifdef __cpp_lib_atomic_shared_ptr
   return listeners.load(std::memory_order_acquire);
#else
   const std::lock_guard lock(mutex);
   return listeners;
#endif
  }

  void setListeners(Listeners values) {
#ifdef __cpp_lib_atomic_shared_ptr
   listeners.store(std::move(values), std::memory_order_release);
#else
   const std::lock_guard lock(mutex);
   listeners.swap(values);
#endif
  }

 private:
#ifdef __cpp_lib_atomic_shared_ptr
  std::atomic<Listeners> listeners;
#else
  mutable std::mutex mutex;
  Listeners listeners;
#endif
 };
 explicit SharedUdpWitness(std::shared_ptr<State> state):state_(std::move(state)){}
 std::shared_ptr<State> state_;
 friend class biglybtc::services::shared_udp::Provider;
};
}
