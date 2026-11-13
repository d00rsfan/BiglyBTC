// SPDX-License-Identifier: GPL-2.0-or-later
#include "session.hpp"
#include <QDateTime>
#include <QRandomGenerator>
#include <QThread>
#include <algorithm>
#include <limits>
namespace biglybtc::services::msgsync {
namespace {
using Clock=std::chrono::steady_clock;namespace udp=azureus_udp;
std::string b(std::string_view value){return std::to_string(value.size())+":"+std::string(value);}
}
RoomSession::RoomSession(std::shared_ptr<Room> room,RoomSessionHooks hooks):room_(std::move(room)),hooks_(std::move(hooks)){lanes_.reserve(2);peers_.reserve(128);}
Result<std::unique_ptr<RoomSession>> RoomSession::open(std::shared_ptr<Room> room,std::span<udp::OwnedPingTransport* const> lanes,RoomSessionHooks hooks){
 if(!room||lanes.empty()||lanes.size()>2||!hooks.afterRelease||!hooks.withPermit||!hooks.revision){return std::unexpected(Error::Input);}
 auto self=std::unique_ptr<RoomSession>(new RoomSession(std::move(room),std::move(hooks)));
 for(std::size_t i=0;i<lanes.size();++i){
  auto* lane=lanes[i];if(!lane||lane->thread()!=QThread::currentThread()
      ||std::ranges::find(lanes.first(i),lane)!=lanes.first(i).end()){return std::unexpected(Error::Input);}
  const auto registered=lane->registerTransferHandler(std::string(self->room_->handler()),true,self.get(),
    [owner=self.get(),i](auto request){owner->incoming(i,std::move(request));},self->hooks_.revision);
  if(!registered){return std::unexpected(Error::Resources);}self->lanes_.push_back(lane);
 }
 return self;
}
RoomSession::~RoomSession(){close();}
RoomSession::Entry* RoomSession::find(std::uint64_t serial){const auto at=std::ranges::find_if(peers_,[&](const auto& p){return p.serial==serial;});return at==peers_.end()?nullptr:&*at;}
bool RoomSession::addPeer(RoomPeer value){
 if(closed_||QThread::currentThread()!=thread()||value.lane>=lanes_.size()||!lanes_[value.lane]
     ||!lanes_[value.lane]->isRoutingPeer(value.endpoint)||value.uid==room_->uid()
     ||value.version<51||value.version>55){return false;}
 const auto prior=std::ranges::find_if(peers_,[&](const auto& p){return p.peer.lane==value.lane&&p.peer.endpoint==value.endpoint&&p.peer.uid==value.uid;});
 if(prior!=peers_.end()){prior->recency=++recency_;if(value.alive){prior->peer.alive=true;prior->peer.failures=0;}return true;}
 if(serial_==std::numeric_limits<std::uint64_t>::max()){return false;}
 if(peers_.size()==128){const auto oldest=std::ranges::min_element(peers_,{},[](const auto& p){return p.ticket?std::numeric_limits<std::uint64_t>::max():p.recency;});if(oldest==peers_.end()||oldest->ticket){return false;}peers_.erase(oldest);}
 peers_.push_back({++serial_,std::move(value),{},++recency_});changed();return true;
}
unsigned RoomSession::syncFactor(std::int64_t age,bool sleeping)noexcept{
 // Original final else-if(<5min) is unreachable after <60min: >=1h stays1.
 const unsigned factor=age<120?1:age<300?2:age<3600?3:1;return factor*(sleeping?2:1);
}
RoomSession::Entry* RoomSession::choose(){
 std::vector<Entry*> healthy,failed,live;std::size_t failingActive=0;
 for(auto& entry:peers_){
  if(entry.ticket){if(entry.peer.failures){++failingActive;}continue;}
  if(std::ranges::any_of(peers_,[&](const auto& p){return p.ticket&&p.peer.endpoint==entry.peer.endpoint;})){continue;}
  if(entry.peer.failures){failed.push_back(&entry);}else{healthy.push_back(&entry);if(entry.peer.alive){live.push_back(&entry);}}
 }
 const auto bias=[&](std::optional<std::uint64_t>& which,const std::vector<Entry*>& pool)->Entry*{
  const auto wanted=std::exchange(which,{});if(!wanted){return nullptr;}const auto p=std::ranges::find_if(pool,[&](const auto* e){return e->serial==*wanted;});return p==pool.end()?nullptr:*p;
 };
 if(auto* selected=bias(biasedOut_,live)){return selected;}
 if(auto* selected=bias(biasedIn_,healthy)){return selected;}
 auto candidates=std::move(failed);
 if(preferLive_&&!live.empty()){candidates=live;preferLive_=false;}
 else if(failingActive>=2&&!healthy.empty()){candidates=healthy;}
 else{candidates.insert(candidates.end(),healthy.begin(),healthy.end());}
 // Original selection is uniform by contact address, then UID at that address.
 std::vector<std::vector<Entry*>> groups;
 for(auto* candidate:candidates){const auto group=std::ranges::find_if(groups,[&](const auto& g){return g.front()->peer.endpoint==candidate->peer.endpoint;});if(group==groups.end()){groups.push_back({candidate});}else{group->push_back(candidate);}}
 if(groups.empty()){return nullptr;}auto& group=groups[QRandomGenerator::system()->bounded(static_cast<quint32>(groups.size()))];return group[QRandomGenerator::system()->bounded(static_cast<quint32>(group.size()))];
}
bool RoomSession::tick(std::uint64_t count,std::int64_t wall,bool sleeping){
 if(closed_||QThread::currentThread()!=thread()){return false;}
 auto messages=room_->messages(wall);if(!messages){return false;}
 const auto age=messages->empty()?std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-created_).count():messages->back().ageSeconds;
 if(count%syncFactor(age,sleeping)||facts().active>=6){return false;}auto* selected=choose();if(!selected){return false;}
 auto request=room_->request(wall);if(!request){return false;}const auto serial=selected->serial;auto* lane=lanes_[selected->peer.lane].data();if(!lane){return false;}
 const auto ticket=lane->callTransfer(selected->peer.endpoint,selected->peer.version,std::string(room_->handler()),std::move(*request),this,
   [this,serial](auto result){completed(serial,std::move(result));},Clock::now()+std::chrono::seconds(30));
 if(!ticket){return false;}incoming_.attempt(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());selected=find(serial);if(selected){selected->ticket=*ticket;}changed();return true;
}
void RoomSession::completed(std::uint64_t serial,udp::TransferOutcome outcome){
 if(closed_){return;}auto* entry=find(serial);if(!entry){return;}entry->ticket.reset();
 if(!outcome){entry->peer.alive=false;if(entry->peer.failures<std::numeric_limits<unsigned>::max()){++entry->peer.failures;}changed();return;}
 QPointer<RoomSession> self(this);
 if(!hooks_.afterRelease([self,serial,envelope=std::move(*outcome)]()mutable{
  if(!self||self->closed_){return;}auto* peer=self->find(serial);if(!peer){return;}
  auto received=self->room_->receive(std::move(envelope),QDateTime::currentMSecsSinceEpoch());
  if(!received){peer->peer.alive=false;++self->rejected_;}
  else{self->incoming_.reply(received->more,std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());peer->peer.alive=true;peer->peer.failures=0;peer->recency=++self->recency_;++self->exchanges_;if(received->more>0){self->biasedOut_=serial;}if(received->added){self->preferLive_=true;}if(received->hint&&self->hooks_.hint){self->hooks_.hint(peer->peer.lane,*received->hint);}}
  self->changed();
 })){++rejected_;changed();}
}
std::optional<NodeHint> RoomSession::randomHint(std::optional<Uid> excluded){
 std::vector<const Entry*> choices;for(const auto& p:peers_){if(!p.peer.failures&&(!excluded||p.peer.uid!=*excluded)&&p.peer.uid!=room_->uid()&&!p.peer.contact.empty()){choices.push_back(&p);}}
 if(choices.empty()){return {};}
 const auto& chosen=*choices[QRandomGenerator::system()->bounded(static_cast<quint32>(choices.size()))];return NodeHint{chosen.peer.uid,chosen.peer.contact};
}
void RoomSession::incoming(std::size_t lane,std::shared_ptr<const udp::TransferRequest> request){
 if(closed_||lane>=lanes_.size()){return;}QPointer<RoomSession> self(this);
 if(!hooks_.afterRelease([self,lane,request=std::move(request)]{
  if(!self||self->closed_||!self->lanes_[lane]||Clock::now()>=request->deadline){return;}
  auto contact=contactMap(self->lanes_[lane]->endpoint());if(!contact){return;}self->room_->updateContact(std::move(*contact));
  auto answer=self->room_->answerDetailed(request->request,QDateTime::currentMSecsSinceEpoch(),self->randomHint({}));
  if(!answer||answer->self){++self->rejected_;self->changed();return;}
  auto peerContact=contactMap(request->peer);
  if(peerContact){self->addPeer({lane,request->peer,55,answer->requester,std::move(*peerContact),true,0});const auto p=std::ranges::find_if(self->peers_,[&](const auto& p){return p.peer.lane==lane&&p.peer.endpoint==request->peer&&p.peer.uid==answer->requester;});if(p!=self->peers_.end()){self->biasedIn_=p->serial;}}
  const auto queued=self->hooks_.withPermit(request->deadline,[self,lane,request,envelope=std::move(answer->envelope)]()mutable{
    if(self&&!self->closed_&&self->lanes_[lane]){static_cast<void>(self->lanes_[lane]->answerTransfer(request->receipt,std::move(envelope)));}
  });if(!queued){++self->rejected_;}self->changed();
 })){++rejected_;changed();}
}
Result<PreparedSend> RoomSession::prepareSend(std::string text,std::string nickname,std::int64_t wall,std::optional<std::int32_t> origin){
 if(closed_||QThread::currentThread()!=thread()){return std::unexpected(Error::Input);}return room_->prepareSend(std::move(text),std::move(nickname),wall,origin);
}
Result<MessageId> RoomSession::commitSend(PreparedSend&& prepared,std::int64_t wall){
 if(closed_||QThread::currentThread()!=thread()){return std::unexpected(Error::Input);}auto result=room_->commitSend(std::move(prepared),wall);if(result){preferLive_=true;changed();}return result;
}
Result<MessageId> RoomSession::send(std::string text,std::string nickname,std::int64_t wall,std::optional<std::int32_t> origin){
 if(closed_||QThread::currentThread()!=thread()){return std::unexpected(Error::Input);}auto result=room_->send(std::move(text),std::move(nickname),wall,origin);if(result){preferLive_=true;changed();}return result;
}
RoomSessionFacts RoomSession::facts()const{
 RoomSessionFacts out{peers_.size(),0,0,exchanges_,rejected_,incoming_.state(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count())};for(const auto& p:peers_){out.alive+=p.peer.alive;out.active+=p.ticket.has_value();}return out;
}
void RoomSession::changed(){if(hooks_.changed){hooks_.changed();}}
void RoomSession::close(){
 if(closed_){return;}closed_=true;for(const auto& lane:lanes_){if(lane){lane->unregisterTransferHandler(room_->handler());}}peers_.clear();
}
Result<std::string> RoomSession::contactMap(udp::Peer endpoint,std::int8_t version){
 if(!endpoint.port||version<51||version>55||!endpoint.address.scopeId().isEmpty()){return std::unexpected(Error::Input);}
 std::string address;if(endpoint.address.protocol()==QAbstractSocket::IPv4Protocol){const auto v=endpoint.address.toIPv4Address();for(unsigned i=0;i<4;++i){address+=static_cast<char>(v>>(24-8*i));}}
 else if(endpoint.address.protocol()==QAbstractSocket::IPv6Protocol){const auto v=endpoint.address.toIPv6Address();address.assign(reinterpret_cast<const char*>(v.c),16);}else{return std::unexpected(Error::Input);}
 return "d1:a"+b(address)+"1:pi"+std::to_string(endpoint.port)+"e1:vi"+std::to_string(version)+"ee";
}
Result<RoomPeer> RoomSession::contactHint(std::size_t lane,const NodeHint& hint){
 auto d=core::Document::parse(hint.encodedContact);if(!d||d->root().type!=core::Node::Type::Dictionary){return std::unexpected(Error::Input);}
 const auto* address=d->find("a"),*port=d->find("p"),*version=d->find("v");
 if(!address||address->type!=core::Node::Type::Bytes||(address->bytes.size()!=4&&address->bytes.size()!=16)||!port||port->type!=core::Node::Type::Integer||port->integer<1||port->integer>65535||!version||version->type!=core::Node::Type::Integer||version->integer<51||version->integer>55){return std::unexpected(Error::Input);}
 QHostAddress ip;if(address->bytes.size()==4){const auto* a=reinterpret_cast<const unsigned char*>(address->bytes.data());ip=QHostAddress((quint32(a[0])<<24)|(quint32(a[1])<<16)|(quint32(a[2])<<8)|a[3]);}
 else{Q_IPV6ADDR a{};std::copy_n(address->bytes.begin(),16,a.c);ip=QHostAddress(a);}
 return RoomPeer{lane,{ip,static_cast<quint16>(port->integer)},static_cast<std::int8_t>(version->integer),hint.uid,hint.encodedContact,false,0};
}
}
