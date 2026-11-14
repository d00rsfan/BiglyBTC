// SPDX-License-Identifier: GPL-2.0-or-later
#include "bindings.hpp"
#include <limits>
namespace biglybtc::services::azureus_nat {
namespace {
bool elapsed(Milliseconds now,Milliseconds then,std::uint64_t limit){return now>=then&&static_cast<std::uint64_t>(now)-static_cast<std::uint64_t>(then)>limit;}
}
bool Bindings::healthy(const Entry& e,Milliseconds now){return e.failures<8||elapsed(now,e.lastConnect,30000);}
Result<BindReply> Bindings::bind(const nodes::Contact& c,Milliseconds now){return bind(c,c.address,now);}
Result<BindReply> Bindings::bind(const RoutedContact& c,Milliseconds now){return bind(c.external(),c.transport(),now);}
Result<BindReply> Bindings::bind(const nodes::Contact& c,const udp::Address& transport,Milliseconds now){
 if(now<0){return std::unexpected(Error::Input);}auto key=socketAddress(c.address);if(!key){return std::unexpected(key.error());}if(secondary_){return BindReply{};}
 auto i=entries_.find(*key);if(i==entries_.end()){
  if(entries_.size()==8){return BindReply{};}if(serial_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(Error::Limit);}
  Entry entry;entry.lease={*key,++serial_,c,transport};entry.bound=now;entries_.emplace(*key,std::move(entry));
 }else{if(!healthy(i->second,now)){return BindReply{};}i->second.bound=now;}
 return BindReply{true,c.address.port};
}
std::optional<BindingLease> Bindings::candidate(std::string_view key,Milliseconds now)const{
 if(now<0){return {};}const auto i=entries_.find(key);if(i==entries_.end()||!healthy(i->second,now)){return {};}return i->second.lease;
}
bool Bindings::outcome(const BindingLease& lease,bool success,Milliseconds now){
 if(now<0){return false;}const auto i=entries_.find(lease.key);if(i==entries_.end()||i->second.lease.serial!=lease.serial){return false;}
 auto& e=i->second;e.lastConnect=now;if(success){e.failures=0;}else if(e.failures<std::numeric_limits<std::uint32_t>::max()){++e.failures;}return true;
}
std::vector<BindingLease> Bindings::expire(Milliseconds now){
 std::vector<BindingLease> removed;if(now<0){return removed;}
 for(auto i=entries_.begin();i!=entries_.end();){if(elapsed(now,i->second.bound,300000)){removed.push_back(i->second.lease);i=entries_.erase(i);}else{++i;}}return removed;
}
}
