// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHTNATPuncher client/tunnel policy: Copyright (C) Azureus Software, Inc.
#pragma once
#include "bindings.hpp"
namespace biglybtc::services::azureus_nat {
enum class BindStatus {Ok,Denied,Failed};
struct BindAttempt {std::uint64_t serial=0;nodes::Contact target;Milliseconds started=0,end=0;};
struct BindCompletion {bool accepted=false,failedTarget=false;};
class ClientBinding final {
public:
 Result<void> select(nodes::Contact,Milliseconds now);
 std::optional<BindAttempt> begin(Milliseconds now);
 BindCompletion complete(const BindAttempt&,BindStatus,Milliseconds now);
 void clear();
 bool active()const noexcept{return target_.has_value()&&operational_;}
 const std::optional<nodes::Contact>& target()const noexcept{return target_;}
 unsigned failures()const noexcept{return failures_;}
 std::optional<Milliseconds> next()const noexcept{return target_&&!pending_?std::optional{next_}:std::nullopt;}
private:
 std::optional<nodes::Contact> target_;std::optional<BindAttempt> pending_;
 std::uint64_t serial_=0;Milliseconds next_=0;unsigned failures_=0;bool operational_=false;
};
struct TunnelAttempt {unsigned number=0;Milliseconds end=0;};
// Four original send opportunities: immediate, then at3/6/9s. Clock calls can
// be delayed, but never catch up with a burst of queued physical sends.
class TunnelBurst final {
public:
 static Result<TunnelBurst> start(Milliseconds now,Milliseconds budget=12000);
 std::optional<TunnelAttempt> begin(Milliseconds now);
 void acknowledged(){done_=true;}
 void close(){done_=true;}
 bool done(Milliseconds now)const noexcept{return done_||now>=end_;}
private:
 Milliseconds next_=0,end_=0;unsigned sent_=0;bool done_=false;
};
struct PunchResult {nodes::Contact target;bool directObserved=false;};
// Caller first validates the actual NAT handler/type, source epoch and lane.
// An outbound transport ACK alone does not confirm a direct inbound tunnel.
class Punch final {
public:
 static Result<Punch> accepted(nodes::Contact target,std::optional<std::uint16_t> indirectPort,Milliseconds now);
 bool inbound(const udp::Address& actual,Milliseconds now);
 std::optional<TunnelAttempt> outbound(Milliseconds now){return burst_.begin(now);}
 void outboundAcknowledged(){burst_.acknowledged();}
 std::optional<PunchResult> finish(Milliseconds now)const;
 const nodes::Contact& target()const noexcept{return target_;}
 Milliseconds end()const noexcept{return end_;}
 void close(){closed_=true;burst_.close();}
private:
 nodes::Contact target_;TunnelBurst burst_;Milliseconds started_=0,end_=0;bool observed_=false,closed_=false;
};
}
