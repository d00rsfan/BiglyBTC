// SPDX-License-Identifier: GPL-2.0-or-later
// I2PHelper33d97: KRPC/NodeInfo/DHTI2P; detached bounded policy, no network grant.
#pragma once
#include "datagram.hpp"
#include <array>
#include <optional>
#include <vector>
namespace biglybtc::services::i2p::dht {
using Id=std::array<Byte,20>;
inline constexpr std::size_t MaximumPacket=10*1024,MaximumContacts=128,MaximumPeers=64;
struct Contact {Id id{};Hash hash{};std::uint16_t port=0;std::optional<Destination> destination;};
bool validContact(const Contact&)noexcept;
Result<Id> makeNodeId(const Hash&,std::uint16_t,View random14);
Result<Contact> decodeContact(View);
Result<Bytes> encodeContact(const Contact&);
enum class Method {Ping,FindNode,GetPeers,AnnouncePeer};
enum class Kind {Query,Reply,Error};
struct Peer {Hash hash{};bool seed=false;};
struct Message {
  Kind kind=Kind::Query;Method method=Method::Ping;Bytes transaction;
  Id id{},target{};bool seed=false,noSeed=false;unsigned version=0;
  Bytes token;std::vector<Contact> nodes;std::vector<Peer> peers;
  bool hasNodes=false,hasPeers=false;int errorCode=0;
  bool wakeRequested=false; // Root z presence only; no source/storage grant.
  std::optional<Bytes> findNodeTarget; // Engaged only for non20-byte decoded/explicit find_node target.
  bool idIsTwentyBytes=true; // Raw announce ignores id; signed originators still require exact20.
};
Result<Message> decode(View);
Result<Bytes> encode(const Message&);
enum class PeerReplyMode {Azureus,Ordinary};
// Local _v stays AZ4 for every caller, exactly as original sendResponse.
// Ordinary get_peers omits flags; filtering is the responder's separate policy.
Result<Bytes> encodeReply(const Message&,PeerReplyMode);
struct LookupOptions {Id target{};bool seed=false,announce=false;unsigned timeoutMilliseconds=120000,parallelism=3;};
enum class Phase {Idle,Running,Complete,TimedOut,Cancelled,Closed,Failed};
enum class NodePhase {Candidate,Resolving,Queued,Sent,Replied,Failed};
struct Node {Contact contact;NodePhase phase=NodePhase::Candidate;Bytes transaction,token;std::uint64_t ticket=0;std::int64_t end=0;bool announcing=false,announced=false,publish=false;};
struct Snapshot {std::uint64_t operation=0;Phase phase=Phase::Idle;Id target{};std::vector<Peer> peers;std::size_t contacts=0,replied=0,announced=0;std::uint64_t rejected=0;};
// Owns only observations/intents. The Qt actor supplies actual dispatch/reply
// facts; a candidate contact or raw reply is never a source/effect grant.
class Lookup final {
public:
  bool start(std::uint64_t,const LookupOptions&,std::span<const Contact>,std::int64_t now);
  void cancel()noexcept;
  void close()noexcept;
  void tick(std::int64_t now)noexcept;
  std::optional<std::size_t> next(std::int64_t now);
  bool resolving(std::size_t);
  bool resolved(std::size_t,const Destination&);
  bool queued(std::size_t,Bytes transaction,std::uint64_t ticket,std::int64_t now);
  bool dispatched(std::uint64_t,bool written,std::int64_t now);
  bool reply(const Message&,std::uint16_t remotePort,std::int64_t now);
  void failed(std::size_t)noexcept;
  const std::vector<Node>& nodes()const noexcept{return nodes_;}
  const LookupOptions& options()const noexcept{return options_;}
  Snapshot snapshot()const;
  bool running()const noexcept{return phase_==Phase::Running;}
  std::int64_t end()const noexcept{return end_;}
private:
  void settle()noexcept;
  bool offer(const Contact&);
  LookupOptions options_;std::vector<Node> nodes_;std::vector<Peer> peers_;
  std::uint64_t operation_=0,rejected_=0;Phase phase_=Phase::Idle;
  std::int64_t end_=0,last_=0;bool publishing_=false;
};
} // namespace biglybtc::services::i2p::dht
