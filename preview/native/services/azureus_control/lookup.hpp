// SPDX-License-Identifier: GPL-2.0-or-later
// Derived GET policy: Azureus Software, Inc. DHTControlImpl, pinned cb8c6cb9.
#pragma once
#include "../azureus_values/values.hpp"
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>
namespace biglybtc::services::azureus_control {
using NodeId=azureus_nodes::NodeId;
using Contact=azureus_nodes::Contact;
using Value=azureus_values::Value;
enum class LookupError {Invalid,Limit,State,Stale,Clock,Overflow,Memory,Unsupported};
enum class LookupState {Idle,Running,Stopping,Complete,TimedOut,Cancelled,Closed,Failed};
enum class LookupStop {Exhausted,Closest,ValueLimit,TwoValueReplies,Deadline,Cancelled,Closed,Capacity,Unsupported,Clock,Memory,Overflow};
enum class LookupNodeState {Candidate,Reserved,Sent,Partial,Replied,Timeout,Cancelled,Closed,Failed,Rejected,Skipped};
enum class LookupKind {Value,Node};
struct LookupOptions {
  NodeId target{},localId{};
  std::size_t parallelism=3,accuracy=20,maximumContacts=128,maximumValues=32;
  std::int64_t timeoutMillis=30000;
  LookupKind kind=LookupKind::Value;
  std::uint8_t flags=0; // Normal0 or source FLAG_STATS8; no obfuscation/diversification.
};
// Explicit adapter policy decision; discovering a contact NEVER grants admission.
struct AdmittedContact {Contact contact;bool owned=false;};
struct QueryIntent {std::uint64_t node=0;Contact contact;NodeId target{};std::uint8_t flags=0,maximumValues=0;};
struct CancelIntent {std::uint64_t node=0,ticket=0;};
struct LookupNode {
  std::uint64_t id=0,parent=0,ticket=0;
  Contact contact;LookupNodeState state=LookupNodeState::Candidate;
  unsigned depth=0,slices=0;
  bool owned=false,cancellationRequested=false,contactReply=false;
  std::int64_t discoveredMillis=0;
  std::optional<std::int64_t> intentMillis,sentMillis,finishedMillis;
  std::size_t values=0;
};
struct LookupValue {std::uint64_t node=0;Value value;};
struct LookupSnapshot {
  std::uint64_t generation=0,lookup=0,revision=0;
  LookupOptions options;
  LookupState state=LookupState::Idle;
  std::optional<LookupStop> stop;
  std::int64_t startedMillis=0,updatedMillis=0;
  std::size_t active=0,valueReplies=0;
  std::uint64_t duplicateContacts=0,rejectedContacts=0,duplicateValues=0;
  std::vector<LookupNode> nodes;
  std::vector<LookupValue> results;
};
inline bool lookupTerminal(LookupState state)noexcept{return state==LookupState::Complete||state==LookupState::TimedOut||state==LookupState::Cancelled||state==LookupState::Closed||state==LookupState::Failed;}
bool validLookupSnapshot(const LookupSnapshot&)noexcept;
// Single-owner, explicit monotonic milliseconds, no Qt/IO/probe/timer. One
// fresh controller per explicit request. All returned snapshots/intents own.
class LookupController final {
public:
  LookupController()=default;
  LookupController(const LookupController&)=delete;
  LookupController& operator=(const LookupController&)=delete;
  LookupController(LookupController&&)noexcept=default;
  LookupController& operator=(LookupController&&)noexcept=default;
  static constexpr std::size_t MaximumContacts=128,MaximumValues=64,MaximumParallelism=8,MaximumSeeds=20,MaximumSlices=64;
  std::expected<void,LookupError> start(std::uint64_t generation,std::uint64_t lookup,const LookupOptions&,std::span<const AdmittedContact>,std::int64_t now);
  std::expected<std::vector<QueryIntent>,LookupError> takeIntents(std::int64_t now);
  std::expected<void,LookupError> sent(std::uint64_t node,std::uint64_t actualTicket,std::int64_t now);
  // Contact-mode continuation is outside the original contact-reply contract:
  // it stops scheduling and requests cancellation, never completes a live RPC.
  std::expected<void,LookupError> contacts(std::uint64_t node,std::span<const AdmittedContact>,bool continuation,std::int64_t now);
  std::expected<void,LookupError> values(std::uint64_t node,std::span<const Value>,bool continuation,std::int8_t diversification,std::int64_t now);
  // Actual dispatch rejection / actor completion only. Do not invent an ack.
  std::expected<void,LookupError> finish(std::uint64_t node,LookupNodeState,std::int64_t now);
  std::expected<void,LookupError> cancel(std::int64_t now);
  std::expected<void,LookupError> close(std::int64_t now);
  std::expected<std::vector<CancelIntent>,LookupError> cancellationIntents()const;
  std::expected<LookupSnapshot,LookupError> snapshot()const;
  LookupState state()const noexcept{return data_.state;}
private:
  std::expected<void,LookupError> clock(std::int64_t now);
  std::expected<void,LookupError> offer(std::span<const AdmittedContact>,std::uint64_t parent,std::int64_t now);
  void stop(LookupStop,std::int64_t now)noexcept;
  void settle(std::int64_t now)noexcept;
  void bump()noexcept;
  LookupNode* node(std::uint64_t id)noexcept;
  LookupSnapshot data_;
  std::int64_t deadline_=0;
};
} // namespace biglybtc::services::azureus_control
