// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
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
  const auto values=state_->listeners.load(std::memory_order_acquire);if(!values){return false;}
  for(const auto& value:*values){if(value.local==local&&(advertised==local||value.external==advertised)){return true;}}return false;
 }
 // Revocation is safe from the external lifetime thread; it cannot grant or
 // resurrect a socket. The SDK observer sees this same atomic before send.
 void revoke()const noexcept{if(state_->active){state_->active->store(false,std::memory_order_release);}}
private:
 struct State {
  std::shared_ptr<std::atomic<bool>> active;
  std::atomic<std::shared_ptr<const std::vector<SharedUdpListener>>> listeners;
  std::shared_ptr<void> imagePin;
 };
 explicit SharedUdpWitness(std::shared_ptr<State> state):state_(std::move(state)){}
 std::shared_ptr<State> state_;
 friend class biglybtc::services::shared_udp::Provider;
};
}
