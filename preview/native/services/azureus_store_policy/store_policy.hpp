// SPDX-License-Identifier: GPL-2.0-or-later
// Original STORE policy: Copyright (C) Azureus Software, Inc.
#pragma once
#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>
namespace biglybtc::services::azureus_store_policy {
using NodeId=std::array<std::uint8_t,20>;
struct Block { std::optional<std::string> request,signature; };
struct Key {
  std::string bytes;
  bool exists=false,closest=false;
  std::int8_t diversification=1;
  std::optional<Block> block;
};
struct Input {
  NodeId sender{},local{};
  std::int64_t now=0;
  bool seeded=false;
  std::int32_t k=20;
  std::optional<std::int32_t> storageNetwork;
  std::optional<NodeId> furthest; // Last contact of supplied live closest-list snapshot.
  std::vector<Key> keys;
  std::vector<std::vector<NodeId>> valueOriginators;
};
struct State { std::int64_t rejectedAt=0;std::optional<NodeId> rejectedSender; };
enum class EventType { Alive,Mismatch,Verify,VerifyRejected,Closest,HasKey,IsClosest,Store,BlockDetails };
struct Event { EventType type;std::size_t index=0;std::int32_t factor=0;bool direct=false; };
struct Reply {
  std::optional<std::vector<std::int8_t>> diversification;
  std::optional<std::string> blockRequest,blockSignature;
  std::vector<Event> events; // Ordered intentions only; no router/database writes.
  bool blocked() const { return blockRequest.has_value(); }
};
enum class ErrorCode { Thread,Reentry,Input,Limit,Authority,Callback };
struct Error { ErrorCode code;Reply partial; };
using Verify=std::function<std::expected<bool,ErrorCode>(bool direct)>;
// Supplied routing/database snapshots; actual authorization is invoked synchronously
// only at the source policy point. Caller keeps this object alive through callback.
class Admission final {
public:
  explicit Admission(State initial={}):state_(std::move(initial)) {}
  Admission(const Admission&)=delete;Admission& operator=(const Admission&)=delete;
  Admission(Admission&&)=delete;Admission& operator=(Admission&&)=delete;
  std::expected<Reply,Error> admit(const Input&,const Verify&);
  std::expected<State,ErrorCode> state() const;
private:
  const std::thread::id owner_=std::this_thread::get_id();State state_;bool active_=false;
};
}
