// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace biglybtc::storage { struct OriginalMldhtSettings; }
namespace biglybtc::engine {
struct NetworkPolicy;
struct MainlineBindOptions {
  std::string pluginDirectory;
  int port=49001;
  bool loaded=true,enabled=true,autostart4=true,autostart6=true;
  bool multihoming4=false,multihoming6=true;
  // Resolved original core restriction. Any wildcard permits BOTH families,
  // matching the wrapper predicate; an empty explicit list permits neither.
  std::vector<std::string> originalBinds;
  bool operator==(const MainlineBindOptions&) const = default;
};
std::expected<MainlineBindOptions,storage::Error> mainlineBindOptions(
    const storage::OriginalMldhtSettings&,std::span<const std::string> resolvedOriginalBinds);
struct MainlineInterfaceFact {
  std::string name,address;
  unsigned index=0,scope=0;
  bool up=false,loopback=false;
  bool operator==(const MainlineInterfaceFact&) const = default;
};
enum class MainlineRouteState { NotCaptured,Selected,Unavailable };
struct MainlineRouteFact {
  MainlineRouteState state=MainlineRouteState::NotCaptured;
  std::string address;
  unsigned scope=0;
  bool operator==(const MainlineRouteFact&) const = default;
};
struct MainlineBindFacts {
  // OS enumeration order is significant for the original v4 last resort.
  std::vector<MainlineInterfaceFact> interfaces;
  std::array<MainlineRouteFact,2> routes; // v4,v6; unavailable != not captured.
};
enum class MainlineFamilyBindState {
  Disabled,Ready,NoEligibleAddress,MissingRouteFact,UnsupportedWildcardFallback
};
struct MainlineDesiredBind {
  unsigned family=4,scope=0,slot=0;
  std::string address;
  std::uint16_t port=0;
  bool operator==(const MainlineDesiredBind&) const = default;
};
struct MainlineFamilyBindPlan {
  unsigned family=4;
  MainlineFamilyBindState state=MainlineFamilyBindState::Disabled;
  std::vector<MainlineDesiredBind> bindings;
  bool operator==(const MainlineFamilyBindPlan&) const = default;
};
struct MainlineBindPlan {
  std::array<MainlineFamilyBindPlan,2> families{{{4,MainlineFamilyBindState::Disabled,{}},{6,MainlineFamilyBindState::Disabled,{}}}};
  bool operator==(const MainlineBindPlan&) const = default;
};
constexpr std::size_t MaximumMainlineInterfaceFacts=4096,MaximumMainlineFamilyBinds=64;
// Cold original family only: no active IDs. Slots are per-family first-free
// registration order; they are desired values, NEVER provider activation proof.
std::expected<MainlineBindPlan,storage::Error> selectMainlineStartupBindPlan(
    const MainlineBindOptions&,const MainlineBindFacts&);
// Current-server retention: single-home ignores unrelated additions; multihome
// changes require a new explicit plan/join, never silent ID reassignment.
// Complete-plan projection only. Never truncates multiple original sockets,
// unresolved fallbacks or derived slots the exact one-per-family executor lacks.
std::expected<NetworkPolicy,storage::Error> projectMainlineBindPlan(
    const NetworkPolicy&,const MainlineBindPlan&);
std::expected<void,storage::Error> mainlineBindInterfacesCurrent(
    const MainlineBindOptions&,const MainlineBindPlan&,const MainlineBindFacts& captured,
    std::span<const MainlineInterfaceFact> current);
class MainlineBindCapture final {
public:
  // Owned worker, explicit Normal+dht+no-proxy activation only. Retains SAME
  // actual OFD. No DNS, UDP data I/O, extra session/timer or profile writes.
  static std::expected<MainlineBindCapture,storage::Error> capture(
      const storage::ProfileLease&,const NetworkPolicy&,const MainlineBindOptions&);
  const MainlineBindFacts& facts() const;
  const MainlineBindPlan& plan() const;
  const MainlineBindOptions& options() const;
  // Call at existing startup/interface-change fences, not from a new sampler.
  // Rechecks held lock+selected interface facts; never repeats route connects.
  std::expected<void,storage::Error> validate() const;
private:
  struct Impl;
  explicit MainlineBindCapture(std::shared_ptr<const Impl> impl):impl_(std::move(impl)){}
  std::shared_ptr<const Impl> impl_;
};
}
