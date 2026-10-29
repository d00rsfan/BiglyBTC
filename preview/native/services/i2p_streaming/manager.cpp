// SPDX-License-Identifier: GPL-2.0-or-later
#include "manager.hpp"
#include <algorithm>
#include <limits>

namespace biglybtc::services::i2p::streaming {
namespace {
auto refused(const char* text){return std::unexpected(Error{ErrorCode::Unsupported,text});}
bool window(unsigned value){return value>=2&&value<=MaximumWindow;}
bool ports(Ports value){return value.local&&value.remote;}
void count(std::uint64_t& value)noexcept{if(value!=std::numeric_limits<std::uint64_t>::max()){++value;}}
bool endFits(std::uint64_t now,unsigned period){return now<=std::numeric_limits<std::uint64_t>::max()-period;}
}
Manager::Manager(Destination local,ManagerOptions options,Connection::Entropy entropy)
  :local_(std::move(local)),options_(options),entropy_(std::move(entropy)){}
bool Manager::valid()const noexcept{
  return !local_.wire.empty()&&options_.inactivityMilliseconds>=1&&options_.inactivityMilliseconds<=3600000
    &&(options_.idleAction==IdleAction::Send||options_.idleAction==IdleAction::Disconnect);
}
Manager::Entry* Manager::find(std::uint64_t op)noexcept{for(auto& item:streams_){if(item&&item->connection.observation().operation==op){return &*item;}}return nullptr;}
const Manager::Entry* Manager::find(std::uint64_t op)const noexcept{for(const auto& item:streams_){if(item&&item->connection.observation().operation==op){return &*item;}}return nullptr;}
bool Manager::available(std::uint64_t op,std::uint32_t id)const noexcept{
  return op&&id&&!find(op)&&std::ranges::none_of(streams_,[&](const auto& item){return item&&item->id==id;})
    &&std::ranges::any_of(streams_,[](const auto& item){return !item;});
}
bool Manager::active()const noexcept{return std::ranges::any_of(streams_,[](const auto& item){return item&&item->connection.active();});}
bool Manager::receives()const noexcept{
  return listener_.has_value()||std::ranges::any_of(streams_,[](const auto& item){return item&&(item->connection.active()||item->connection.observation().phase==Phase::Closed);});
}
Result<void> Manager::connect(std::uint64_t op,std::uint32_t id,Destination peer,const ConnectOptions& input,std::uint64_t now,std::optional<std::uint64_t> absoluteHandshakeEnd){
  tick(now);
  if(!valid()||!available(op,id)||!ports(input.ports)||!window(input.maximumWindow)||input.handshakeMilliseconds<1||input.handshakeMilliseconds>120000||!endFits(now,input.handshakeMilliseconds)||peer.wire.empty()||peer.hash==local_.hash){return refused("Bounded explicit stream connection unavailable");}
  const auto end=absoluteHandshakeEnd.value_or(now+input.handshakeMilliseconds);
  if(end<=now||end-now>input.handshakeMilliseconds){return refused("Original finite handshake deadline expired/invalid");}
  Connection prepared(op,id,local_,std::move(peer),input.maximumWindow,now,entropy_);
  if(!prepared.active()){return refused("Invalid connection options");}
  const auto jitter=entropy_?entropy_(10000):0;if(jitter>=10000){return refused("Invalid idle jitter entropy");}
  auto free=std::ranges::find_if(streams_,[](const auto& item){return !item;});
  free->emplace(Entry{std::move(prepared),input.ports,false,false,id,jitter,end,now,{}});
  return {};
}
Result<void> Manager::listen(const ListenOptions& provided,std::uint64_t now){
  tick(now);
  if(provided.peers.empty()||provided.peers.size()>MaximumStreams){return refused("Finite listener peer bound required");}
  ListenOptions input;input.ports=provided.ports;input.maximumWindow=provided.maximumWindow;
  for(const auto& peer:provided.peers){auto decoded=decodeDestination(peer.wire);if(!decoded){return std::unexpected(decoded.error());}input.peers.push_back(std::move(*decoded));}
  if(!valid()||listener_||!pending_.empty()||!ports(input.ports)||!window(input.maximumWindow)||input.peers.empty()||input.peers.size()>MaximumStreams){return refused("Explicit finite listener unavailable");}
  for(std::size_t i=0;i<input.peers.size();++i){
    if(input.peers[i].wire.empty()||input.peers[i].hash==local_.hash){return refused("Listener peer must be an explicit other Destination");}
    for(std::size_t j=0;j<i;++j){if(input.peers[j].hash==input.peers[i].hash){return refused("Duplicate or ambiguous listener peer");}}
  }
  listener_.emplace(std::move(input));return {};
}
void Manager::stopListening()noexcept{listener_.reset();pending_.clear();pendingBytes_=0;}
Result<void> Manager::accept(std::uint64_t op,std::uint32_t id,std::uint64_t now){
  tick(now);
  if(!listener_||pending_.empty()||!available(op,id)){return refused("No current bounded SYN/stream slot");}
  auto& first=pending_.front();const auto policy=*listener_;
  auto prepared=Connection::accept(op,id,local_,*first.syn.from,first.syn,policy.maximumWindow,now,entropy_);
  if(!prepared){return std::unexpected(prepared.error());}
  const auto jitter=entropy_?entropy_(10000):0;if(jitter>=10000){return refused("Invalid idle jitter entropy");}
  auto free=std::ranges::find_if(streams_,[](const auto& item){return !item;});
  free->emplace(Entry{std::move(*prepared),first.ports,true,false,id,jitter,first.end,now,{}});
  pendingBytes_-=first.bytes;pending_.pop_front();return {};
}
void Manager::receive(const Mux& mux,std::uint64_t now,std::function<std::uint64_t()> afterVerification,std::function<bool()> current){
  tick(now);if(!valid()||mux.packet.size()<22){count(rejected_);return;}
  std::uint32_t id=0;for(unsigned i=0;i<4;++i){id=(id<<8)|mux.packet[i];}
  if(id){
    for(auto& item:streams_){if(item&&item->id==id){
      if(item->ports.local!=mux.toPort||item->ports.remote!=mux.fromPort||(!item->connection.active()&&item->connection.observation().phase!=Phase::Closed)){count(rejected_);return;}
      auto decoded=decode(mux.packet,item->connection.peer());if(!decoded){count(rejected_);return;}
      if(current&&!current()){count(rejected_);return;}if(afterVerification){now=afterVerification();tick(now);}if(current&&!current()){count(rejected_);return;}
      if(!item->connection.active()&&item->connection.observation().phase!=Phase::Closed){count(rejected_);return;}
      item->connection.receive(std::move(*decoded),now);
      if(item->connection.lastReceiveAccepted()){item->lastActivity=now;item->idlePending=false;}
      settleWait(*item,now);return;
    }}
    count(rejected_);return;
  }
  if(!listener_||listener_->ports.local!=mux.toPort||listener_->ports.remote!=mux.fromPort){count(rejected_);return;}
  auto syn=decodeInitialSyn(mux.packet,local_);if(!syn){count(rejected_);return;}
  if(current&&!current()){count(rejected_);return;}if(afterVerification){now=afterVerification();tick(now);}if(current&&!current()){count(rejected_);return;}
  if(!listener_){count(rejected_);return;}
  if(std::ranges::none_of(listener_->peers,[&](const auto& peer){return peer.wire==syn->from->wire;})){count(rejected_);return;}
  // A duplicate SYN cannot create a second accepted stream. Its original
  // SYN/ACK retains the ordinary retransmission clock if the peer missed it.
  for(const auto& item:streams_){if(item&&item->inbound&&item->connection.peer().wire==syn->from->wire&&item->connection.remoteId()==syn->receive&&item->ports.local==mux.toPort&&item->ports.remote==mux.fromPort){count(rejected_);return;}}
  for(const auto& item:pending_){if(item.syn.receive==syn->receive&&item.syn.from->wire==syn->from->wire){count(rejected_);return;}}
  const auto weight=mux.packet.size()+syn->from->wire.size()+syn->from->signingPublicKey.size()+syn->payload.size()+4*syn->nacks.size();
  if(pending_.size()==MaximumPendingSyns||weight>MaximumPendingSynBytes-pendingBytes_||!endFits(now,3000)){count(rejected_);return;}
  pending_.push_back({std::move(*syn),Ports{mux.toPort,mux.fromPort},now+3000,weight});pendingBytes_+=weight;
}
void Manager::settleWait(Entry& entry,std::uint64_t now)noexcept{
  if(!entry.wait||entry.wait->observed.state!=WaitState::Pending){return;}
  auto& wait=*entry.wait;const auto observed=entry.connection.observation();
  if(now>=wait.end){wait.observed.state=WaitState::TimedOut;return;}
  if(!entry.connection.active()&&observed.phase!=Phase::Closed){wait.observed.state=WaitState::Cancelled;return;}
  if(wait.observed.kind==WaitFor::Readable){
    if(entry.connection.readableBytes()>=wait.observed.minimumBytes||observed.phase==Phase::ReadEof||observed.phase==Phase::Closed){wait.observed.state=WaitState::Ready;}
  }else if(entry.connection.outputOpen()&&wait.observed.minimumBytes<=MaximumBuffered-observed.queuedTx){wait.observed.state=WaitState::Ready;}
}
void Manager::tick(std::uint64_t now){
  while(!pending_.empty()&&now>=pending_.front().end){pendingBytes_-=pending_.front().bytes;pending_.pop_front();count(expired_);}
  for(auto& item:streams_){if(!item){continue;}auto& entry=*item;const auto observed=entry.connection.observation();
    if(entry.connection.active()&&(observed.phase==Phase::Connecting||(entry.inbound&&!observed.transmittedPackets))&&now>=entry.handshakeEnd){entry.connection.terminate(Phase::Timeout);}
    else if(entry.connection.active()&&observed.phase!=Phase::Connecting&&!entry.idlePending&&now>=entry.lastActivity&&now-entry.lastActivity>=static_cast<std::uint64_t>(options_.inactivityMilliseconds)+entry.idleJitter&&observed.inFlightPackets==0){
      if(options_.idleAction==IdleAction::Send&&entry.connection.keepalive()){entry.idlePending=true;}
      else{entry.connection.terminate(Phase::IdleTimeout);}
    }
    settleWait(entry,now);
  }
}
std::optional<Outbound> Manager::next(std::uint64_t now){
  tick(now);
  for(std::size_t step=0;step<MaximumStreams;++step){const auto index=(cursor_+step)%MaximumStreams;auto& item=streams_[index];if(!item){continue;}
    auto packet=item->connection.next(now);if(packet){cursor_=(index+1)%MaximumStreams;return Outbound{item->connection.observation().operation,std::move(*packet),item->ports};}
  }
  return std::nullopt;
}
void Manager::dispatched(const Outbound& outbound,std::uint64_t now){
  if(auto* item=find(outbound.operation)){item->connection.dispatched(outbound.packet,now);item->lastActivity=now;item->idlePending=false;settleWait(*item,now);}
}
const Destination* Manager::peer(std::uint64_t op)const noexcept{const auto* item=find(op);return item?&item->connection.peer():nullptr;}
std::optional<unsigned> Manager::transmitTimeout(const Outbound& outgoing)const noexcept{const auto* item=find(outgoing.operation);return item?item->connection.transmitTimeout(outgoing.packet):std::nullopt;}
Result<void> Manager::write(std::uint64_t op,View bytes,std::uint64_t now){
  tick(now);auto* item=find(op);if(!item){return refused("Current stream operation required");}auto result=item->connection.write(bytes);settleWait(*item,now);return result;
}
Result<Bytes> Manager::read(std::uint64_t op,std::size_t maximum,std::uint64_t now){
  tick(now);auto* item=find(op);if(!item||(!item->connection.active()&&item->connection.observation().phase!=Phase::Closed)){return refused("Readable current stream required");}auto result=item->connection.read(std::min(maximum,MaximumBuffered));settleWait(*item,now);return result;
}
bool Manager::finishWrite(std::uint64_t op,std::uint64_t now){tick(now);auto* item=find(op);if(!item||!item->connection.active()){return false;}item->connection.finishWrite();settleWait(*item,now);return true;}
bool Manager::cancel(std::uint64_t op)noexcept{auto* item=find(op);if(!item||!item->connection.active()){return false;}item->connection.terminate(Phase::Cancelled);if(item->wait&&item->wait->observed.state==WaitState::Pending){item->wait->observed.state=WaitState::Cancelled;}return true;}
bool Manager::release(std::uint64_t op)noexcept{for(auto& item:streams_){if(item&&item->connection.observation().operation==op){if(!item->connection.drained()){return false;}item.reset();return true;}}return false;}
bool Manager::wait(std::uint64_t op,WaitFor kind,std::size_t minimum,unsigned period,std::uint64_t now){
  tick(now);auto* item=find(op);if(!item||(item->wait&&item->wait->observed.state==WaitState::Pending)||minimum<1||minimum>MaximumBuffered||period<1||period>120000||!endFits(now,period)||(kind!=WaitFor::Readable&&kind!=WaitFor::Writable)){return false;}
  item->wait=Wait{{kind,WaitState::Pending,minimum},now+period};settleWait(*item,now);return true;
}
void Manager::terminate(Phase phase)noexcept{stopListening();for(auto& item:streams_){if(item){item->connection.terminate(phase);if(item->wait&&item->wait->observed.state==WaitState::Pending){item->wait->observed.state=WaitState::Cancelled;}}}}
std::optional<std::uint64_t> Manager::hardWakeup()const noexcept{
  std::optional<std::uint64_t> result;const auto add=[&](std::uint64_t value){if(!result||value<*result){result=value;}};
  if(!pending_.empty()){add(pending_.front().end);}
  for(const auto& item:streams_){if(!item){continue;}const auto observed=item->connection.observation();if(item->connection.active()&&(observed.phase==Phase::Connecting||(item->inbound&&!observed.transmittedPackets))){add(item->handshakeEnd);}
    else if(item->connection.active()&&!item->idlePending&&!observed.inFlightPackets&&endFits(item->lastActivity,options_.inactivityMilliseconds+item->idleJitter)){add(item->lastActivity+options_.inactivityMilliseconds+item->idleJitter);}
    if(item->wait&&item->wait->observed.state==WaitState::Pending){add(item->wait->end);}
  }
  return result;
}
std::optional<std::uint64_t> Manager::wakeup()const noexcept{
  auto result=hardWakeup();
  for(const auto& item:streams_){if(item){if(auto at=item->connection.wakeup();at&&(!result||*at<*result)){result=*at;}}}
  return result;
}
std::optional<ManagedObservation> Manager::observation(std::uint64_t op)const noexcept{const auto* item=find(op);return item?std::optional(ManagedObservation{item->connection.observation(),item->ports,item->inbound,item->idleJitter,item->wait?std::optional(item->wait->observed):std::nullopt}):std::nullopt;}
ManagerObservation Manager::observation()const noexcept{
  ManagerObservation out;out.listening=listener_.has_value();out.pendingSyns=pending_.size();out.pendingSynBytes=pendingBytes_;out.rejectedPackets=rejected_;out.expiredSyns=expired_;
  for(const auto& item:streams_){if(item){++out.streams;const auto observed=item->connection.observation();out.queuedTx+=observed.queuedTx;out.queuedRx+=observed.queuedRx;}}return out;
}
} // namespace biglybtc::services::i2p::streaming
