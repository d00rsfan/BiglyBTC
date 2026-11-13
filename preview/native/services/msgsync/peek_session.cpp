// SPDX-License-Identifier: GPL-2.0-only
#include "peek_session.hpp"
#include <QThread>
#include <algorithm>
namespace biglybtc::services::msgsync {
namespace udp=azureus_udp;
PeekSession::PeekSession(PeekChannel channel,RoomSessionHooks hooks,Clock::time_point end):channel_(std::move(channel)),hooks_(std::move(hooks)),end_(end),grace_(end){
 timer_.setInterval(25);connect(&timer_,&QTimer::timeout,this,[this]{pump();});
}
Result<std::unique_ptr<PeekSession>> PeekSession::open(std::string_view key,std::span<udp::OwnedPingTransport* const> lanes,RoomSessionHooks hooks,Clock::time_point end){
 if(lanes.empty()||lanes.size()>2||!hooks.afterRelease||!hooks.withPermit||!hooks.revision||end<=Clock::now()||end>Clock::now()+std::chrono::seconds(60)){return std::unexpected(Error::Input);}auto channel=PeekChannel::open(key);if(!channel){return std::unexpected(channel.error());}
 auto self=std::unique_ptr<PeekSession>(new PeekSession(std::move(*channel),std::move(hooks),end));
 for(std::size_t n=0;n<lanes.size();++n){if(!lanes[n]||lanes[n]->thread()!=QThread::currentThread()||std::ranges::find(lanes.first(n),lanes[n])!=lanes.first(n).end()){return std::unexpected(Error::Input);}self->lanes_.push_back(lanes[n]);}self->timer_.start();return self;
}
PeekSession::~PeekSession(){close();}
bool PeekSession::addPeer(std::size_t lane,udp::Peer endpoint,std::int8_t version){
 if(finished_||QThread::currentThread()!=thread()||Clock::now()>=end_||lane>=lanes_.size()||!lanes_[lane]||version<51||version>55||!lanes_[lane]->isRoutingPeer(endpoint)||seen_.size()>=32){return false;}
 if(std::ranges::any_of(seen_,[&](const auto& p){return p.lane==lane&&p.endpoint==endpoint;})){return true;}seen_.push_back({lane,std::move(endpoint),version});pending_.push_back(seen_.back());pump();return true;
}
void PeekSession::discoveryDone(){if(QThread::currentThread()!=thread()){return;}discoveryDone_=true;pump();}
void PeekSession::changed(){if(hooks_.changed){hooks_.changed();}}
void PeekSession::finish(){
 if(finished_){return;}finished_=true;timer_.stop();for(auto& request:active_){if(request.ticket&&lanes_[request.peer.lane]){lanes_[request.peer.lane]->cancelTransfer(*request.ticket);}}active_.clear();pending_.clear();changed();
}
void PeekSession::close(){if(QThread::currentThread()==thread()){finish();}}
void PeekSession::pump(){
 if(finished_||pumping_){return;}pumping_=true;
 if(Clock::now()>=std::min(end_,grace_)||(discoveryDone_&&pending_.empty()&&active_.empty())){pumping_=false;finish();return;}
 while(active_.size()<5&&!pending_.empty()){
  auto peer=std::move(pending_.front());pending_.erase(pending_.begin());const auto id=++serial_;active_.push_back({id,std::move(peer),{}});QPointer<PeekSession> self(this);
  if(!hooks_.afterRelease([self,id]{if(self&&!self->finished_){self->send(id);}})){retired(id);}
 }
 pumping_=false;
}
void PeekSession::send(std::uint64_t id){
 const auto at=std::ranges::find(active_,id,&Request::serial);if(at==active_.end()){return;}auto request=channel_.request();if(!request){++rejected_;retired(id);return;}QPointer<PeekSession> self(this);
 if(!hooks_.withPermit(std::min(end_,grace_),[self,id,payload=std::move(*request)]()mutable{
  if(!self||self->finished_||Clock::now()>=std::min(self->end_,self->grace_)){return;}auto at=std::ranges::find(self->active_,id,&Request::serial);if(at==self->active_.end()){return;}auto* lane=self->lanes_[at->peer.lane].data();if(!lane){self->retired(id);return;}
  ++self->attempted_;auto ticket=lane->readTransfer(at->peer.endpoint,at->peer.version,std::string(self->channel_.handler()),std::move(payload),self,[self,id](auto outcome){if(self){self->completed(id,std::move(outcome));}},std::min({self->end_,self->grace_,Clock::now()+std::chrono::seconds(10)}),self->hooks_.revision);
  if(ticket){at=std::ranges::find(self->active_,id,&Request::serial);if(at!=self->active_.end()){at->ticket=*ticket;}}else{++self->rejected_;self->retired(id);}
 })){++rejected_;retired(id);}
}
void PeekSession::completed(std::uint64_t id,udp::TransferOutcome outcome){
 if(finished_||std::ranges::find(active_,id,&Request::serial)==active_.end()){return;}if(!outcome){++rejected_;retired(id);return;}QPointer<PeekSession> self(this);
 if(!hooks_.afterRelease([self,id,envelope=std::move(*outcome)]()mutable{
  if(!self||self->finished_){return;}auto reply=self->channel_.receive(std::move(envelope));if(!reply){++self->rejected_;self->retired(id);return;}
  if(!self->hooks_.withPermit(std::min(self->end_,self->grace_),[self,id,reply=std::move(*reply)]{
   if(!self||self->finished_||Clock::now()>=std::min(self->end_,self->grace_)||std::ranges::find(self->active_,id,&Request::serial)==self->active_.end()){return;}
   if(!self->aggregate_.summary().results){self->grace_=std::min(self->end_,Clock::now()+std::chrono::seconds(10));}if(!self->aggregate_.add(reply)){self->finish();return;}self->retired(id);if(self){self->changed();}
  })){++self->rejected_;self->retired(id);}
 })){++rejected_;retired(id);}
}
void PeekSession::retired(std::uint64_t id){std::erase_if(active_,[&](const auto& r){return r.serial==id;});pump();}
PeekSessionFacts PeekSession::facts()const{return {aggregate_.summary(),active_.size(),pending_.size(),attempted_,rejected_,finished_};}
}
