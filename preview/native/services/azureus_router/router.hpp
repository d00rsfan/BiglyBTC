// SPDX-License-Identifier: GPL-2.0-or-later
// Source-defined router policy: Azureus Software, Inc.; native bounded owner.
#pragma once
#include "nodes.hpp"
#include <array>
#include <expected>
#include <memory>
#include <optional>
#include <span>

namespace biglybtc::services::azureus_router {
using NodeId=azureus_nodes::NodeId;
inline constexpr std::size_t MaximumRouterContacts=2048,MaximumRouterNodes=1023;
inline constexpr std::size_t MaximumRouterBuckets=(MaximumRouterNodes+1)/2,MaximumRouterIntents=64;
struct RouterConfig {std::uint16_t k=20,b=4,replacements=5;bool bootstrapProxy=false;};
struct ContactInput {
  NodeId id{};std::int32_t instance=0;
  std::uint16_t maxFailLive=5,maxFailUnknown=3; // Explicit attachment policy.
  bool sleeping=false;
};
struct RouterContact {
  NodeId id{};std::int32_t instance=0;std::uint32_t failures=0;
  bool local=false,bucket=false,hasBeenAlive=false,pingOutstanding=false;
  bool alive()const noexcept{return hasBeenAlive&&!failures;}
  bool failing()const noexcept{return failures!=0;}
};
enum class RouterIntentKind {Ping,NodeAdded};
struct RouterIntent {RouterIntentKind kind=RouterIntentKind::Ping;NodeId id{};};
// Source findClosestContacts returns a whole terminal bucket, possibly more
// than requested. Replacements are excluded; liveOnly means !failing, including
// unknown bucket members, exactly as DHTRouterImpl 744-795.
struct ClosestContacts {std::size_t count=0;std::array<RouterContact,MaximumRouterContacts> values{};};
struct RouterIntents {std::size_t count=0;std::array<RouterIntent,MaximumRouterIntents> values{};};
struct RouterBucketStatistics {
  NodeId prefix{};std::uint16_t depth=0,contacts=0,replacements=0;bool containsLocal=false;
};
class RouterOwner;
class RouterStatisticsSnapshot final {
public:
  RouterStatisticsSnapshot(const RouterStatisticsSnapshot&)=default;
  RouterStatisticsSnapshot& operator=(const RouterStatisticsSnapshot&)=default;
  std::uint64_t generation()const noexcept{return generation_;}
  std::uint64_t revision()const noexcept{return revision_;}
  // Source order: nodes, leaves, bucket contacts, replacements, live, unknown,
  // failing. Local contact is included; replacement liveness is not counted.
  const std::array<std::uint64_t,7>& counts()const noexcept{return counts_;}
  const RouterConfig& config()const noexcept{return config_;}
  std::span<const RouterBucketStatistics> buckets()const noexcept{return {buckets_.data(),bucketCount_};}
  std::uint64_t capacityDrops()const noexcept{return capacityDrops_;}
  std::uint64_t lostIntents()const noexcept{return lostIntents_;}
  std::uint64_t suppressedKnown()const noexcept{return suppressedKnown_;}
  std::uint32_t consecutiveDead()const noexcept{return consecutiveDead_;}
  bool sleeping()const noexcept{return sleeping_;}bool suspended()const noexcept{return suspended_;}
  bool limited()const noexcept{return capacityDrops_||lostIntents_;}
private:
  friend class RouterOwner;RouterStatisticsSnapshot()=default;
  std::uint64_t generation_=0,revision_=0,capacityDrops_=0,lostIntents_=0,suppressedKnown_=0;
  std::uint32_t consecutiveDead_=0;RouterConfig config_;bool sleeping_=false,suspended_=false;
  std::array<std::uint64_t,7> counts_{};std::size_t bucketCount_=0;
  std::array<RouterBucketStatistics,MaximumRouterBuckets> buckets_{};
};
enum class RouterError {Invalid,Clock,Thread,Memory,Exhausted};
enum class RouterChange {Bucket,Replacement,Updated,Removed,Ignored,Dropped};
// One worker, no locks/timers/network. Times are explicit monotone milliseconds.
// Intents are inert; only a later policy-owning controller may execute them.
class RouterOwner final {
public:
  static std::expected<std::unique_ptr<RouterOwner>,RouterError> create(
      NodeId local,RouterConfig={},std::uint64_t generation=1,std::int64_t nowMilliseconds=0);
  ~RouterOwner();RouterOwner(const RouterOwner&)=delete;RouterOwner& operator=(const RouterOwner&)=delete;
  std::expected<RouterChange,RouterError> contactKnown(const ContactInput&,std::int64_t nowMilliseconds,bool force=false)noexcept;
  std::expected<RouterChange,RouterError> contactAlive(const ContactInput&,std::int64_t nowMilliseconds)noexcept;
  std::expected<RouterChange,RouterError> contactDead(const NodeId&,std::int64_t nowMilliseconds,bool force=false)noexcept;
  std::expected<void,RouterError> setSleeping(bool,std::int64_t nowMilliseconds)noexcept;
  std::expected<void,RouterError> setSuspended(bool,std::int64_t nowMilliseconds)noexcept;
  std::expected<std::optional<RouterContact>,RouterError> find(const NodeId&)const noexcept;
  std::expected<ClosestContacts,RouterError> closest(const NodeId& target,std::size_t requested,bool liveOnly=true)const noexcept;
  std::expected<bool,RouterError> localInClosest(const NodeId& target,std::size_t requested,bool liveOnly=true)const noexcept;
  std::expected<RouterStatisticsSnapshot,RouterError> snapshot()const noexcept;
  std::expected<RouterIntents,RouterError> takeIntents()noexcept;
private:
  struct Impl;explicit RouterOwner(std::unique_ptr<Impl>)noexcept;std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::services::azureus_router
