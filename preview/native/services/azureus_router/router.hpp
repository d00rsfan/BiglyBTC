// SPDX-License-Identifier: GPL-2.0-or-later
// Source-defined router policy: Azureus Software, Inc.; native bounded owner.
#pragma once
#include "nodes.hpp"
#include <array>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <functional>

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
struct RouterRefreshes {std::size_t count=0;std::array<NodeId,MaximumRouterIntents> targets{};};
struct RouterExportContact {RouterContact contact;std::int64_t timeAlive=0;};
struct RouterBestContacts {std::size_t count=0;std::array<RouterExportContact,MaximumRouterContacts> values{};};
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
// Optional wall time is only original getTimeAlive metadata; routing deadlines
// and failure policy remain monotone. Omitted wall time equals the explicit now.
// Intents are inert; only a later policy-owning controller may execute them.
class RouterOwner final {
public:
  static std::expected<std::unique_ptr<RouterOwner>,RouterError> create(
      NodeId local,RouterConfig={},std::uint64_t generation=1,std::int64_t nowMilliseconds=0,std::optional<std::int64_t> wallMilliseconds=std::nullopt);
  ~RouterOwner();RouterOwner(const RouterOwner&)=delete;RouterOwner& operator=(const RouterOwner&)=delete;
  std::expected<RouterChange,RouterError> contactKnown(const ContactInput&,std::int64_t nowMilliseconds,bool force=false)noexcept;
  std::expected<RouterChange,RouterError> contactAlive(const ContactInput&,std::int64_t nowMilliseconds,std::optional<std::int64_t> wallMilliseconds=std::nullopt)noexcept;
  std::expected<RouterChange,RouterError> contactDead(const NodeId&,std::int64_t nowMilliseconds,bool force=false)noexcept;
  std::expected<void,RouterError> setSleeping(bool,std::int64_t nowMilliseconds)noexcept;
  std::expected<void,RouterError> setSuspended(bool,std::int64_t nowMilliseconds)noexcept;
  std::expected<std::optional<RouterContact>,RouterError> find(const NodeId&)const noexcept;
  std::expected<ClosestContacts,RouterError> closest(const NodeId& target,std::size_t requested,bool liveOnly=true)const noexcept;
  std::expected<bool,RouterError> localInClosest(const NodeId& target,std::size_t requested,bool liveOnly=true)const noexcept;
  std::expected<RouterStatisticsSnapshot,RouterError> snapshot()const noexcept;
  std::expected<RouterIntents,RouterError> takeIntents()noexcept;
  // Explicit clocks and entropy: no timers or IO. Each maintenanceTick is one
  // delivered owner timer turn, not an elapsed-time sampler; it never seeds.
  std::expected<void,RouterError> lookupStarted(std::int64_t wallMilliseconds,std::int64_t nowMilliseconds)noexcept;
  std::expected<void,RouterError> recordLookup(const NodeId&,std::int64_t wallMilliseconds,std::int64_t nowMilliseconds)noexcept;
  std::expected<void,RouterError> scheduleSeedRefresh(std::int64_t nowMilliseconds)noexcept;
  std::expected<RouterRefreshes,RouterError> maintenanceTick(std::int64_t nowMilliseconds,std::int64_t wallMilliseconds,const std::function<void(std::span<std::uint8_t>)>& entropy)noexcept;
  std::expected<RouterBestContacts,RouterError> bestContacts(std::int64_t nowMilliseconds,std::optional<std::int64_t> wallMilliseconds=std::nullopt)const noexcept;

private:
  struct Impl;explicit RouterOwner(std::unique_ptr<Impl>)noexcept;std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::services::azureus_router
