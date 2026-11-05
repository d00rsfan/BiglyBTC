// SPDX-License-Identifier: GPL-2.0-or-later
// Original policy: Paul Gardner, Copyright (C) Azureus Software, Inc.
// Detached SOURCE-CANDIDATE; no channel consumer or source/provider grant.
#pragma once
#include "inbound_state.hpp"
#include <cstdint>
#include <functional>
namespace biglybtc::services::i2p::dht::inbound {
enum class ResponderError {Invalid,NoAuthority,Routing,Capacity,Crypto,Clock,Thread,Reentry,Closed,Resource,Callback};
template<class T>using ReplyResult=std::expected<T,ResponderError>;
struct Incoming {View payload;const Destination* signedSender=nullptr;std::uint16_t fromPort=0,toPort=0;};
struct RequestPolicy {bool sleeping=false,suspended=false;};
struct RoutingSnapshot {std::vector<Contact> closest;bool allowStore=false;}; // Already distance-sorted/truncated liveOnlyK10; not source authority.
struct Response {Contact recipient;std::uint16_t fromPort=0,toPort=0;Bytes payload;}; // RAW18 to exact saved Q+1; ACK is not storage certification.
// Guarded terminal outcomes per Responder instance, not all received packets.
// requests counts only a fully prepared reply or an exact raw token-binding
// miss. replyPrepared is local construction, never socket enqueue/delivery.
// tokenRejected counts only that binding miss, not malformed/NoAuthority/etc.
// Owning passive copies confer no identity/source/store/provider permission.
struct StateCounts {
  std::size_t tokenBindings=0;Counts peers;
  std::uint64_t requests=0,replyPrepared=0,tokenRejected=0;
};
// Caller reserves one bounded response slot and retains the object through the
// call. Current must reflect the actual held source permit/session/lease, and
// cannot itself grant authority. Route records actual contactAlive then supplies
// an actual bounded snapshot/localInClosest-or-existing-key decision. Callbacks
// do no network/FS waits; reentry is refused. All mutations stay on owner thread.
using Current=std::function<bool()>;
using Route=std::function<std::optional<RoutingSnapshot>(const Contact&,Method,const Id& target,bool existingKey)>;
class Responder final {
public:
  static ReplyResult<std::unique_ptr<Responder>> create(Contact local,StoreLimits={},std::optional<azureus_auth::Key> supplied={});
  ~Responder();Responder(const Responder&)=delete;Responder& operator=(const Responder&)=delete;
  ReplyResult<std::optional<Response>> receive(const Incoming&,std::int64_t wallMillis,RequestPolicy,const Current&,const Route&);
  ReplyResult<StateCounts> counts()const noexcept;
  void close()noexcept;
private:
  Responder(Contact,std::unique_ptr<TokenBindings>,std::unique_ptr<PeerStore>);
  Contact local_;std::unique_ptr<TokenBindings> tokens_;std::unique_ptr<PeerStore> peers_;
  std::uint64_t requests_=0,replyPrepared_=0,tokenRejected_=0;
  const std::thread::id thread_=std::this_thread::get_id();bool closed_=false,busy_=false;
};
} // namespace biglybtc::services::i2p::dht::inbound
