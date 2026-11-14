// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "room.hpp"
#include "incoming_sync.hpp"
#include "owned_transport.hpp"
#include <QPointer>
namespace biglybtc::services::msgsync {
struct RoomSessionHooks {
  // Actual owner broker: crypto runs only after its source job releases.
  std::function<bool(std::move_only_function<void()>)> afterRelease;
  // Executes only the small actor effect under a fresh held source permit.
  std::function<bool(std::chrono::steady_clock::time_point,std::move_only_function<void()>)> withPermit;
  std::function<std::optional<std::uint64_t>()> revision;
  std::function<void(std::size_t,const NodeHint&)> hint;
  std::function<void()> changed;
  // Invoked only on the crypto worker after release of the source permit.
  std::function<void(std::size_t,std::shared_ptr<const azureus_udp::TransferRequest>,PrivateRequest)> privateRequest={};
};
struct RoomPeer {
  std::size_t lane=0;azureus_udp::Peer endpoint;std::int8_t version=55;
  Uid uid{};std::string contact;bool alive=false;unsigned failures=0;
};
struct RoomSessionFacts {std::size_t peers=0,alive=0,active=0;std::uint64_t exchanges=0,rejected=0;std::int32_t incomingSync=-1;};
// All state/crypto belongs to one room Qt worker. The two optional lanes share
// one identity/ledger and one six-call budget. A hint never admits an endpoint.
class RoomSession final:public QObject {
public:
 static Result<std::unique_ptr<RoomSession>> open(std::shared_ptr<Room>,
     std::span<azureus_udp::OwnedPingTransport* const>,RoomSessionHooks,std::optional<RoomPeer> privatePeer={});
 ~RoomSession()override;
 bool addPeer(RoomPeer); // Requires the actor's existing routed-peer admission.
 bool tick(std::uint64_t count,std::int64_t wallMillis,bool sleeping=false);
 Result<PreparedSend> prepareSend(std::string text,std::string nickname,std::int64_t wallMillis,std::optional<std::int32_t> origin={},std::optional<std::int32_t> type={});
 Result<MessageId> commitSend(PreparedSend&&,std::int64_t wallMillis);
 Result<MessageId> send(std::string text,std::string nickname,std::int64_t wallMillis,std::optional<std::int32_t> origin={},std::optional<std::int32_t> type={});
 RoomSessionFacts facts()const;
 void close();
 static unsigned syncFactor(std::int64_t ageSeconds,bool sleeping=false)noexcept;
 static Result<std::string> contactMap(azureus_udp::Peer,std::int8_t version=55);
 static Result<RoomPeer> contactHint(std::size_t lane,const NodeHint&);
private:
 struct Entry {std::uint64_t serial=0;RoomPeer peer;std::optional<azureus_udp::Ticket> ticket;std::uint64_t recency=0;};
 RoomSession(std::shared_ptr<Room>,RoomSessionHooks);
 void incomingPeek(std::size_t,std::shared_ptr<const azureus_udp::TransferRequest>);
 void nodeAlive(const RoomPeer&);
 void incoming(std::size_t,std::shared_ptr<const azureus_udp::TransferRequest>);
 void completed(std::uint64_t,azureus_udp::TransferOutcome);
 Entry* find(std::uint64_t);
 Entry* choose();
 std::optional<NodeHint> randomHint(std::optional<Uid> excluded={});
 void changed();
 std::optional<RoomPeer> privatePeer_;
 IncomingSync incoming_;
 std::optional<PeekChannel> peek_;LiveNodeEstimate estimate_{std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()};
 std::shared_ptr<Room> room_;RoomSessionHooks hooks_;
 std::vector<QPointer<azureus_udp::OwnedPingTransport>> lanes_;
 std::vector<Entry> peers_;std::uint64_t serial_=0,recency_=0,exchanges_=0,rejected_=0;
 std::optional<std::uint64_t> biasedIn_,biasedOut_;bool preferLive_=false,closed_=false;
 std::chrono::steady_clock::time_point created_=std::chrono::steady_clock::now();
};
}
