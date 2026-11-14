// SPDX-License-Identifier: GPL-2.0-or-later
// Original NAT lifecycle: Copyright (C) Azureus Software, Inc.
#pragma once
#include "client.hpp"
#include "../azureus_udp/owned_transport.hpp"
#include <QPointer>
#include <QTimer>
namespace biglybtc::services::azureus_nat {
namespace transport=azureus_udp;
struct SessionHooks {
 std::function<bool(std::move_only_function<void()>)> afterRelease;
 std::function<bool(std::chrono::steady_clock::time_point,std::move_only_function<void()>)> withPermit;
 // Evaluated only while the source owner's permit is held.
 std::function<std::optional<std::uint64_t>()> revision;
};
enum class SessionFailure {Input,Capacity,Rejected,Timeout,Source,Closed,Cancelled};
struct TunnelResult {RoutedContact contact;bool directObserved=false;};
using TunnelOutcome=std::expected<TunnelResult,SessionFailure>;
using TunnelCallback=std::move_only_function<void(TunnelOutcome)>;
struct SessionFacts {
 std::size_t bindings=0,punches=0,relays=0,connects=0;
 std::uint64_t earlyInbound=0,rejected=0;bool rendezvousActive=false,closed=false;
};
// One source epoch / shared UDP lane. This actor never admits a peer, opens a
// socket, changes routing authority or publishes values. Explicit rendezvous
// selection and existing DHT admission or finite DATA peer leases are required.
class Session final:public QObject {
public:
 static Result<std::unique_ptr<Session>> open(transport::OwnedPingTransport&,
     std::uint64_t heldSourceRevision,SessionHooks,bool secondary=false);
 ~Session()override;
 bool selectRendezvous(RoutedContact);
 Result<std::uint64_t> punch(RoutedContact relay,RoutedContact target,QObject*,TunnelCallback);
 bool cancel(std::uint64_t);
 SessionFacts facts()const;
 void close();
private:
 using Clock=std::chrono::steady_clock;
 struct Pending {
  RoutedContact relay,target;QPointer<QObject> receiver;TunnelCallback callback;
  Clock::time_point end;std::optional<transport::Ticket> rpc,raw;
  std::optional<udp::Address> early;std::optional<Punch> policy;
  std::optional<TunnelBurst> burst;std::optional<nodes::Contact> indirect;
  bool firstOutboundDone=false;
 };
 struct Relay {std::shared_ptr<const transport::TransferRequest> request;BindingLease lease;std::optional<transport::Ticket> ticket;};
 struct Connect {
  std::shared_ptr<const transport::TransferRequest> request;RoutedContact target;
  TunnelBurst burst;Clock::time_point end;std::optional<transport::Ticket> ticket;
  bool answered=false;
 };
 Session(transport::OwnedPingTransport&,std::uint64_t,SessionHooks,bool);
 bool guard(Clock::time_point,std::move_only_function<void()>);
 void incoming(std::shared_ptr<const transport::TransferRequest>);
 void dispatch(std::shared_ptr<const transport::TransferRequest>,Message,RoutedContact);
 void answer(const transport::TransferRequest&,Message);
 void relayDone(std::uint64_t,transport::TransferOutcome);
 void punchDone(std::uint64_t,transport::TransferOutcome);
 void rawPunch(std::uint64_t);
 void rawConnect(std::uint64_t);
 void finish(std::uint64_t,TunnelOutcome);
 void finishReady(std::uint64_t);
 void tick();
 void bind();
 void shutdown(SessionFailure);
 static Milliseconds now();
 QPointer<transport::OwnedPingTransport> lane_;std::uint64_t epoch_=0,serial_=0;
 SessionHooks hooks_;QTimer timer_;nodes::NodeCache cache_;Bindings bindings_;
 ClientBinding client_;std::optional<RoutedContact> rendezvous_;
 std::optional<transport::Ticket> bindTicket_;
 std::map<std::uint64_t,Pending> pending_;std::map<std::uint64_t,Relay> relays_;
 std::map<std::uint64_t,Connect> connects_;
 Milliseconds nextExpiry_=0;std::uint64_t earlyInbound_=0,rejected_=0;bool closed_=false;
};
}
