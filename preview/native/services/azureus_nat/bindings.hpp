// SPDX-License-Identifier: GPL-2.0-or-later
// Original rendezvous binding policy: Copyright (C) Azureus Software, Inc.
#pragma once
#include "contact.hpp"
#include <map>
namespace biglybtc::services::azureus_nat {
using Milliseconds=std::int64_t;
struct BindReply {bool ok=false;std::optional<std::uint16_t> port;bool operator==(const BindReply&)const=default;};
struct BindingLease {std::string key;std::uint64_t serial=0;nodes::Contact contact;udp::Address transport;};
// Single owner, memory only. The caller validates source epoch, actual endpoint,
// lane, request role and deadline before invoking policy or emitting effects.
class Bindings final {
public:
 explicit Bindings(bool secondary=false):secondary_(secondary){}
 Result<BindReply> bind(const nodes::Contact&,Milliseconds now);
 Result<BindReply> bind(const RoutedContact&,Milliseconds now);
 std::optional<BindingLease> candidate(std::string_view target,Milliseconds now)const;
 bool outcome(const BindingLease&,bool success,Milliseconds now);
 std::vector<BindingLease> expire(Milliseconds now);
 void clear(){entries_.clear();} // Keep serials monotonic across source epochs.
 std::size_t size()const noexcept{return entries_.size();}
private:
 struct Entry {BindingLease lease;Milliseconds bound=0,lastConnect=0;std::uint32_t failures=0;};
 Result<BindReply> bind(const nodes::Contact&,const udp::Address&,Milliseconds);
 static bool healthy(const Entry&,Milliseconds);
 bool secondary_=false;std::uint64_t serial_=0;
 std::map<std::string,Entry,std::less<>> entries_;
};
}
