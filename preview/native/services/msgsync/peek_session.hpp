// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "peek.hpp"
#include "session.hpp"
#include <QTimer>
namespace biglybtc::services::msgsync {
struct PeekSessionFacts {PeekSummary summary;std::size_t active=0,pending=0,attempted=0,rejected=0;bool finished=false;};
// One transient read, no Room/UID/signer/history/profile creation. The owner
// supplies genuine DHT originators after routing admission and fresh source
// permits. Shared room encryption is not participant identity authentication.
class PeekSession final:public QObject {
public:
 using Clock=std::chrono::steady_clock;
 static Result<std::unique_ptr<PeekSession>> open(std::string_view,
  std::span<azureus_udp::OwnedPingTransport* const>,RoomSessionHooks,Clock::time_point end);
 ~PeekSession()override;
 bool addPeer(std::size_t lane,azureus_udp::Peer,std::int8_t version);
 void discoveryDone();
 PeekSessionFacts facts()const;
 void close();
private:
 struct Peer {std::size_t lane;azureus_udp::Peer endpoint;std::int8_t version;};
 struct Request {std::uint64_t serial;Peer peer;std::optional<azureus_udp::Ticket> ticket;};
 PeekSession(PeekChannel,RoomSessionHooks,Clock::time_point);
 void pump();void send(std::uint64_t);void completed(std::uint64_t,azureus_udp::TransferOutcome);
 void retired(std::uint64_t);void finish();void changed();
 PeekChannel channel_;RoomSessionHooks hooks_;QTimer timer_;Clock::time_point end_,grace_;
 std::vector<QPointer<azureus_udp::OwnedPingTransport>> lanes_;
 std::vector<Peer> seen_,pending_;std::vector<Request> active_;PeekAggregate aggregate_;
 std::uint64_t serial_=0;std::size_t attempted_=0,rejected_=0;bool discoveryDone_=false,finished_=false,pumping_=false;
};
}
