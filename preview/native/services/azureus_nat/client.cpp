// SPDX-License-Identifier: GPL-2.0-or-later
#include "client.hpp"
#include <algorithm>
#include <limits>
namespace biglybtc::services::azureus_nat {
namespace {
bool future(Milliseconds now,Milliseconds duration){return now>=0&&duration>0&&now<=std::numeric_limits<Milliseconds>::max()-duration;}
bool sameIp(udp::Address a,udp::Address b){a.port=1;b.port=1;auto aa=socketAddress(a),bb=socketAddress(b);return aa&&bb&&*aa==*bb;}
}
Result<void> ClientBinding::select(nodes::Contact target,Milliseconds now){
 if(!future(now,50000)||!socketAddress(target.address)){return std::unexpected(Error::Input);}clear();target_=std::move(target);next_=now;failures_=2;return {};
}
void ClientBinding::clear(){target_.reset();pending_.reset();operational_=false;failures_=0;}
std::optional<BindAttempt> ClientBinding::begin(Milliseconds now){
 if(!target_||pending_||now<next_||!future(now,50000)||serial_==std::numeric_limits<std::uint64_t>::max()){return {};}
 pending_=BindAttempt{++serial_,*target_,now,now+30000};next_=now+50000;return pending_;
}
BindCompletion ClientBinding::complete(const BindAttempt& attempt,BindStatus status,Milliseconds now){
 if(!pending_||pending_->serial!=attempt.serial||now<pending_->started||(now>=pending_->end&&status!=BindStatus::Failed)){return {};}
 pending_.reset();if(status==BindStatus::Ok){failures_=0;operational_=true;return {true,false};}
 failures_=status==BindStatus::Denied?4:failures_+1;if(failures_==4){target_.reset();operational_=false;return {true,true};}return {true,false};
}
Result<TunnelBurst> TunnelBurst::start(Milliseconds now,Milliseconds budget){
 if(budget>12000||!future(now,budget)){return std::unexpected(Error::Input);}TunnelBurst out;out.next_=now;out.end_=now+budget;return out;
}
std::optional<TunnelAttempt> TunnelBurst::begin(Milliseconds now){
 if(done_||now<next_||now>=end_||sent_==4){return {};}
 ++sent_;next_=now>std::numeric_limits<Milliseconds>::max()-3000?end_:std::min(end_,now+3000);return TunnelAttempt{sent_,std::min(end_,next_)};
}
Result<Punch> Punch::accepted(nodes::Contact target,std::optional<std::uint16_t> indirectPort,Milliseconds now){
 if(!socketAddress(target.address)){return std::unexpected(Error::Contact);}auto burst=TunnelBurst::start(now,10000);if(!burst){return std::unexpected(burst.error());}Punch out;out.target_=std::move(target);if(indirectPort&&*indirectPort){out.target_.address.port=*indirectPort;}out.burst_=std::move(*burst);out.started_=now;out.end_=now+10000;return out;
}
bool Punch::inbound(const udp::Address& actual,Milliseconds now){
 if(closed_||observed_||now<started_||now>=end_||!actual.port||!sameIp(actual,target_.address)){return false;}target_.address.port=actual.port;observed_=true;burst_.close();return true;
}
std::optional<PunchResult> Punch::finish(Milliseconds now)const{
 if(closed_||now<started_||(!observed_&&now<end_)){return {};}return PunchResult{target_,observed_};
}
}
