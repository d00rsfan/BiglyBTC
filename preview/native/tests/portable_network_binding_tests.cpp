// SPDX-License-Identifier: GPL-2.0-or-later
#include "../engine/mainline_bind_plan.hpp"
#include "../engine/network_binding.hpp"
#include "../platform/network_interfaces.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
namespace e = biglybtc::engine;
namespace p = biglybtc::platform;

void check(bool passed, const char* message) {
  if (!passed) { throw std::runtime_error(message); }
}

void pinLocalInterface() {
  const auto snapshot = p::readInterfaceAddresses(e::MaximumNetworkInterfaceAddresses);
  check(snapshot.has_value(), "Cannot read operating-system interface addresses");
  const auto loopback = std::ranges::find_if(*snapshot, [](const auto& address) {
    return address.length == 4 && address.up && address.loopback
        && address.address == std::array<std::uint8_t, 16>{127, 0, 0, 1};
  });
  check(loopback != snapshot->end() && loopback->index && !loopback->name.empty(), "IPv4 loopback identity is missing");
  const auto bounded = p::readInterfaceAddresses(0);
  check(!bounded && bounded.error() == p::NetworkInterfacesError::Limit, "Interface snapshot exceeded its bound");

  e::NetworkPolicy policy;
  policy.mode = e::NetworkMode::Normal;
  policy.bindAddresses = {"127.0.0.1"};
  policy.enforceBind = true;
  const auto numeric = e::prepareNetworkBinding(policy);
  check(numeric && numeric->effective.bindAddresses == policy.bindAddresses
      && numeric->pins.size() == 1 && numeric->pins.front().index == loopback->index
      && numeric->pins.front().name == loopback->name, "Numeric bind did not pin the local interface");
  check(e::networkBindingCurrent(*numeric), "Unchanged loopback pin was rejected");

  policy.bindAddresses = {loopback->name + "[v4]"};
  const auto named = e::prepareNetworkBinding(policy);
  check(named && std::ranges::find(named->pins, numeric->pins.front()) != named->pins.end(),
      "Named bind did not resolve the same interface");
  auto changed = numeric->pins;
  changed.front().up = false;
  check(!e::networkBindingCurrent(*numeric, changed), "Down interface retained its pin");
  policy.bindAddresses = {loopback->name + "[4095]"};
  const auto unavailable = e::prepareNetworkBinding(policy);
  check(!unavailable && unavailable.error().code == e::NetworkPolicyErrorCode::BindingUnavailable,
      "Missing interface address was accepted");
}

void selectMainlineAgainstLocalFact() {
  e::MainlineBindOptions options;
  options.pluginDirectory = "mldht";
  options.port = 49001;
  options.loaded = options.enabled = options.autostart4 = true;
  options.autostart6 = false;
  options.multihoming4 = false;
  options.originalBinds = {"127.0.0.1"};
  const auto interfaces = e::readNetworkInterfaces();
  check(interfaces.has_value(), "Cannot read engine interface facts");
  const auto loopback = std::ranges::find_if(*interfaces, [](const auto& address) {
    return address.up && address.address == "127.0.0.1";
  });
  check(loopback != interfaces->end(), "Engine loopback fact is missing");
  e::MainlineBindFacts captured;
  captured.interfaces.push_back({loopback->name, loopback->address, loopback->index, 0, true, true});
  captured.routes[0] = {e::MainlineRouteState::Selected, loopback->address, 0};
  const auto plan = e::selectMainlineStartupBindPlan(options, captured);
  check(plan && plan->families[0].state == e::MainlineFamilyBindState::Ready
      && plan->families[0].bindings.size() == 1 && plan->families[0].bindings.front().address == loopback->address,
      "Mainline route fact did not select the saved numeric bind");
  check(e::mainlineBindInterfacesCurrent(options, *plan, captured, captured.interfaces).has_value(),
      "Mainline rejected the unchanged interface fact");
  auto changed = captured.interfaces;
  ++changed.front().index;
  check(!e::mainlineBindInterfacesCurrent(options, *plan, captured, changed),
      "Mainline retained a route after its interface identity changed");
}

}

int main() {
  try {
    pinLocalInterface();
    selectMainlineAgainstLocalFact();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
