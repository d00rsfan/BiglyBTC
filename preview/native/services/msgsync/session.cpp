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
azureus_dht::Address peerAddress(const udp::Peer& peer){azureus_dht::Address a;a.port=peer.port;if(peer.address.protocol()==QAbstractSocket::IPv4Protocol){a.length=4;auto ip=peer.address.toIPv4Address();for(unsigned i=0;i<4;++i){a.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}}else{a.length=16;auto ip=peer.address.toIPv6Address();std::copy_n(ip.c,16,a.bytes.begin());}return a;}
udp::Peer peerEndpoint(const azureus_dht::Address& a){QHostAddress ip;if(a.length==4){ip=QHostAddress((quint32(a.bytes[0])<<24)|(quint32(a.bytes[1])<<16)|(quint32(a.bytes[2])<<8)|a.bytes[3]);}else{Q_IPV6ADDR v{};std::copy_n(a.bytes.begin(),16,v.c);ip=QHostAddress(v);}return {ip,a.port};}
bool sameExternal(const azureus_nodes::Snapshot& a,const azureus_nodes::Snapshot& c){return a.udp&&c.udp&&a.protocol==c.protocol&&a.address==c.address;}
std::string b(std::string_view value){return std::to_string(value.size())+":"+std::string(value);}
}
RoomSession::RoomSession(std::shared_ptr<Room> room,RoomSessionHooks hooks):room_(std::move(room)),hooks_(std::move(hooks)){lanes_.reserve(2);peers_.reserve(128);}
Result<std::unique_ptr<RoomSession>> RoomSession::open(std::shared_ptr<Room> room,std::span<udp::OwnedPingTransport* const> lanes,RoomSessionHooks hooks,std::optional<RoomPeer> privatePeer){
 if(!room||bool(privatePeer)!=room->isPrivate()||lanes.empty()||lanes.size()>2||!hooks.afterRelease||!hooks.withPermit||!hooks.revision){return std::unexpected(Error::Input);}
 auto self=std::unique_ptr<RoomSession>(new RoomSession(std::move(room),std::move(hooks)));self->privatePeer_=std::move(privatePeer);
 if(!self->room_->isPrivate()){auto peek=PeekChannel::open(self->room_->key());if(!peek){return std::unexpected(peek.error());}self->peek_.emplace(std::move(*peek));}
 for(std::size_t i=0;i<lanes.size();++i){
  auto* lane=lanes[i];if(!lane||lane->thread()!=QThread::currentThread()
      ||std::ranges::find(lanes.first(i),lane)!=lanes.first(i).end()){return std::unexpected(Error::Input);}
  const auto registered=lane->registerTransferHandler(std::string(self->room_->handler()),true,self.get(),
    [owner=self.get(),i](auto request){owner->incoming(i,std::move(request));},self->hooks_.revision);
  if(!registered){return std::unexpected(Error::Resources);}self->lanes_.push_back(lane);
  if(self->peek_){const auto peekRegistered=lane->registerTransferHandler(std::string(self->peek_->handler()),true,self.get(),[owner=self.get(),i](auto request){owner->incomingPeek(i,std::move(request));},self->hooks_.revision,udp::TransferMode::Read);
  if(!peekRegistered){return std::unexpected(Error::Resources);}}
 }
 if(self->privatePeer_&&self->privatePeer_->lane>=self->lanes_.size()){return std::unexpected(Error::Input);}
 if(self->privatePeer_&&!self->lanes_[self->privatePeer_->lane]->isTransferPeer(self->privatePeer_->endpoint,self->room_->handler())&&!self->ensurePeer(*self->privatePeer_)){return std::unexpected(Error::Input);}
 if(self->privatePeer_&&!self->addPeer(*self->privatePeer_)){return std::unexpected(Error::Input);}
 return self;
}
RoomSession::~RoomSession(){close();}
RoomSession::Entry* RoomSession::find(std::uint64_t serial){const auto at=std::ranges::find_if(peers_,[&](const auto& p){return p.serial==serial;});return at==peers_.end()?nullptr:&*at;}
bool RoomSession::addPeer(RoomPeer value){
 if(privatePeer_&&(value.lane!=privatePeer_->lane||value.endpoint!=privatePeer_->endpoint)){return false;}
 if(closed_||QThread::currentThread()!=thread()||value.lane>=lanes_.size()||!lanes_[value.lane]
     ||!lanes_[value.lane]->isTransferPeer(value.endpoint,room_->handler())||value.uid==room_->uid()
     ||value.version<51||value.version>55){return false;}
 const auto prior=std::ranges::find_if(peers_,[&](const auto& p){return p.peer.lane==value.lane&&p.peer.endpoint==value.endpoint&&p.peer.uid==value.uid;});
 if(prior!=peers_.end()){prior->recency=++recency_;if(value.alive){prior->peer.alive=true;prior->peer.failures=0;nodeAlive(prior->peer);}return true;}
 if(serial_==std::numeric_limits<std::uint64_t>::max()){return false;}
 if(peers_.size()==128){const auto oldest=std::ranges::min_element(peers_,{},[](const auto& p){return p.ticket||p.queued?std::numeric_limits<std::uint64_t>::max():p.recency;});if(oldest==peers_.end()||(oldest->ticket||oldest->queued)){return false;}peers_.erase(oldest);}
 peers_.push_back({++serial_,std::move(value),{},++recency_});if(peers_.back().peer.alive){nodeAlive(peers_.back().peer);}changed();return true;
}
unsigned RoomSession::syncFactor(std::int64_t age,bool sleeping)noexcept{
 // Original final else-if(<5min) is unreachable after <60min: >=1h stays1.
 const unsigned factor=age<120?1:age<300?2:age<3600?3:1;return factor*(sleeping?2:1);
}
RoomSession::Entry* RoomSession::choose(){
 std::vector<Entry*> healthy,failed,live;std::size_t failingActive=0;
 for(auto& entry:peers_){
  if(entry.ticket||entry.queued){if(entry.peer.failures){++failingActive;}continue;}
  if(std::ranges::any_of(peers_,[&](const auto& p){return (p.ticket||p.queued)&&p.peer.endpoint==entry.peer.endpoint;})){continue;}
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
 if(closed_||QThread::currentThread()!=thread()){return false;}const auto now=Clock::now();for(auto& p:peers_){if(p.queued&&now>=p.queuedEnd){p.queued=false;p.peer.alive=false;if(p.peer.failures<std::numeric_limits<unsigned>::max()){++p.peer.failures;}}}
 estimate_.tick(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());auto messages=room_->messages(wall);if(!messages){return false;}
 const auto age=messages->empty()?std::chrono::duration_cast<std::chrono::seconds>(now-created_).count():messages->back().ageSeconds;
 const auto state=facts();if(count%syncFactor(age,sleeping)||state.active>=6){return false;}auto* selected=choose();if(!selected){return false;}const auto serial=selected->serial;
 if(hooks_.tunnel&&(selected->peer.failures||!selected->peer.alive)&&(room_->isPrivate()||state.alive==0||(state.peers<=5&&state.alive<2)||(state.peers<=10&&state.alive<3))){const auto peer=selected->peer;hooks_.tunnel(peer.lane,peer,room_->isPrivate());selected=find(serial);if(!selected){return false;}}
 auto request=room_->request(wall);if(!request||!lanes_[selected->peer.lane]){return false;}const auto end=now+std::chrono::seconds(30);auto external=externalPeer(selected->peer);auto* lane=lanes_[selected->peer.lane].data();
 // Ordinary admitted endpoints retain their existing single actor preflight.
 // Only a DATA-only/aliased endpoint needs a source job to renew its lease.
 if(lane->isRoutingPeer(selected->peer.endpoint)&&(!external||external->address==std::optional{peerAddress(selected->peer.endpoint)})){dispatch(serial,std::move(*request),end,false);selected=find(serial);return selected&&selected->ticket.has_value();}
 selected->queued=true;selected->queuedEnd=end;QPointer<RoomSession> self(this);const bool queued=hooks_.withPermit(end,[self,serial,end,request=std::move(*request)]()mutable{if(self&&!self->closed_){self->dispatch(serial,std::move(request),end,true);}});if(!queued){if(auto* p=find(serial)){p->queued=false;}}return queued;
}
void RoomSession::dispatch(std::uint64_t serial,std::string request,Clock::time_point end,bool renew){
 if(closed_){return;}auto* selected=find(serial);if(!selected){return;}selected->queued=false;if(Clock::now()>=end||(renew&&!ensurePeer(selected->peer))){selected->peer.alive=false;if(selected->peer.failures<std::numeric_limits<unsigned>::max()){++selected->peer.failures;}changed();return;}
 auto* lane=lanes_[selected->peer.lane].data();if(!lane){return;}QPointer<RoomSession> self(this);auto ticket=lane->callTransfer(selected->peer.endpoint,selected->peer.version,std::string(room_->handler()),std::move(request),this,[self,serial](auto result){if(self){self->completed(serial,std::move(result));}},end);
 if(ticket){selected=find(serial);if(selected){selected->ticket=*ticket;}incoming_.attempt(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());}changed();
}
Result<azureus_nodes::Snapshot> RoomSession::externalPeer(const RoomPeer& p){NodeHint hint{p.uid,p.contact};auto decoded=contactHint(p.lane,hint);if(!decoded){return std::unexpected(decoded.error());}return azureus_nodes::Snapshot{true,decoded->version,peerAddress(decoded->endpoint)};}
Result<std::string> RoomSession::externalContact(const udp::TransferRequest& r){if(!r.advertised.udp||!r.advertised.address||(r.advertised.address->length!=4&&r.advertised.address->length!=16)){return std::unexpected(Error::Input);}return contactMap(peerEndpoint(*r.advertised.address),r.advertised.protocol);}
bool RoomSession::ensurePeer(const RoomPeer& p){
 if(closed_||p.lane>=lanes_.size()||!lanes_[p.lane]){return false;}auto revision=hooks_.revision();if(!revision){return false;}auto external=externalPeer(p);auto* lane=lanes_[p.lane].data();if(lane->isRoutingPeer(p.endpoint)&&(!external||external->address==std::optional{peerAddress(p.endpoint)})){return true;}return external&&bool(lane->leaseTransferPeer(p.endpoint,room_->handler(),*external,this,*revision,Clock::now()+std::chrono::seconds(120)));
}
bool RoomSession::observeRoute(std::size_t lane,const azureus_nodes::Snapshot& external,udp::Peer actual){
 if(closed_){return false;}const auto found=std::ranges::find_if(peers_,[&](const auto& p){auto known=externalPeer(p.peer);return p.peer.lane==lane&&known&&sameExternal(*known,external);});
 return found!=peers_.end()&&updateRoute(found->peer,actual);
}
bool RoomSession::acceptsRoutedPeer(std::size_t lane,udp::Peer actual,const azureus_nodes::Snapshot& external)const{
 if(closed_||lane>=lanes_.size()){return false;}return std::ranges::any_of(peers_,[&](const auto& p){if(p.peer.lane!=lane||p.peer.endpoint!=actual){return false;}auto known=externalPeer(p.peer);return known&&sameExternal(*known,external);});
}
bool RoomSession::renewRoutedPeer(std::size_t lane,udp::Peer actual,const azureus_nodes::Snapshot& external){
 if(!acceptsRoutedPeer(lane,actual,external)){return false;}const auto p=std::ranges::find_if(peers_,[&](const auto& p){return p.peer.lane==lane&&p.peer.endpoint==actual;});return p!=peers_.end()&&ensurePeer(p->peer);
}
bool RoomSession::updateRoute(RoomPeer logical,udp::Peer actual){
 if(closed_||QThread::currentThread()!=thread()||logical.lane>=lanes_.size()||!lanes_[logical.lane]||logical.endpoint.address!=actual.address){return false;}auto external=externalPeer(logical);auto revision=hooks_.revision();if(!external||!revision){return false;}auto* lane=lanes_[logical.lane].data();const auto end=Clock::now()+std::chrono::seconds(120);
 if(!lane->leaseTransferPeer(logical.endpoint,room_->handler(),*external,this,*revision,end)||!lane->leaseTransferPeer(actual,room_->handler(),*external,this,*revision,end)){return false;}if(!lane->rerouteTransferPeer(room_->handler(),logical.endpoint,actual,this,*revision)){return false;}
 for(auto& entry:peers_){auto known=externalPeer(entry.peer);if(entry.peer.lane==logical.lane&&known&&sameExternal(*known,*external)){entry.peer.endpoint=actual;}}
 if(privatePeer_&&privatePeer_->lane==logical.lane){auto known=externalPeer(*privatePeer_);if(known&&sameExternal(*known,*external)){privatePeer_->endpoint=actual;}}
 logical.endpoint=actual;const bool accepted=addPeer(std::move(logical));changed();return accepted;
}
void RoomSession::completed(std::uint64_t serial,udp::TransferOutcome outcome){
 if(closed_){return;}auto* entry=find(serial);if(!entry){return;}entry->ticket.reset();
 if(!outcome){entry->peer.alive=false;if(entry->peer.failures<std::numeric_limits<unsigned>::max()){++entry->peer.failures;}changed();return;}
 QPointer<RoomSession> self(this);
 if(!hooks_.afterRelease([self,serial,envelope=std::move(*outcome)]()mutable{
  if(!self||self->closed_){return;}auto* peer=self->find(serial);if(!peer){return;}
  auto received=self->room_->receive(std::move(envelope),QDateTime::currentMSecsSinceEpoch());
  if(!received){peer->peer.alive=false;++self->rejected_;}
  else{self->incoming_.reply(received->more,std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());peer->peer.alive=true;peer->peer.failures=0;self->nodeAlive(peer->peer);peer->recency=++self->recency_;++self->exchanges_;if(received->more>0){self->biasedOut_=serial;}if(received->added){self->preferLive_=true;}if(received->hint&&self->hooks_.hint){self->hooks_.hint(peer->peer.lane,*received->hint);}}
  self->changed();
 })){++rejected_;changed();}
}
std::optional<NodeHint> RoomSession::randomHint(std::optional<Uid> excluded){
 if(privatePeer_){return {};}std::vector<const Entry*> choices;for(const auto& p:peers_){if(!p.peer.failures&&(!excluded||p.peer.uid!=*excluded)&&p.peer.uid!=room_->uid()&&!p.peer.contact.empty()){choices.push_back(&p);}}
 if(choices.empty()){return {};}
 const auto& chosen=*choices[QRandomGenerator::system()->bounded(static_cast<quint32>(choices.size()))];return NodeHint{chosen.peer.uid,chosen.peer.contact};
}
void RoomSession::nodeAlive(const RoomPeer& peer){
 QString address;if(peer.endpoint.address.protocol()==QAbstractSocket::IPv6Protocol){const auto ip=peer.endpoint.address.toIPv6Address();for(unsigned n=0;n<8;++n){if(n){address+=QLatin1Char(':');}address+=QString::number((unsigned(ip.c[n*2])<<8)|ip.c[n*2+1],16);}}else{address=peer.endpoint.address.toString();}address+=QLatin1Char(':');address+=QString::number(peer.endpoint.port);estimate_.alive(address.toStdString(),std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
}
void RoomSession::incomingPeek(std::size_t lane,std::shared_ptr<const udp::TransferRequest> request){
 if(closed_||!peek_||lane>=lanes_.size()){return;}QPointer<RoomSession> self(this);
 if(!hooks_.afterRelease([self,lane,request=std::move(request)]{
  if(!self||self->closed_||!self->lanes_[lane]||Clock::now()>=request->deadline){return;}const auto facts=self->facts();auto reply=self->room_->peekReply(static_cast<std::int32_t>(facts.peers),static_cast<std::int32_t>(facts.alive),self->estimate_.value(),QDateTime::currentMSecsSinceEpoch());if(!reply){return;}auto answer=self->peek_->answer(request->request,*reply);if(!answer){++self->rejected_;return;}
  if(!self->hooks_.withPermit(request->deadline,[self,lane,request,envelope=std::move(*answer)]()mutable{if(self&&!self->closed_&&self->lanes_[lane]){static_cast<void>(self->lanes_[lane]->answerTransfer(request->receipt,std::move(envelope)));}})){++self->rejected_;}
 })){++rejected_;}
}
void RoomSession::incoming(std::size_t lane,std::shared_ptr<const udp::TransferRequest> request){
 if(closed_||lane>=lanes_.size()||(privatePeer_&&(lane!=privatePeer_->lane||request->peer!=privatePeer_->endpoint))){return;}QPointer<RoomSession> self(this);
 if(!hooks_.afterRelease([self,lane,request=std::move(request)]{
  if(!self||self->closed_||!self->lanes_[lane]||Clock::now()>=request->deadline){return;}
  if(!self->privatePeer_&&self->hooks_.privateRequest){auto negotiation=self->room_->privateRequest(request->request);if(negotiation){self->hooks_.privateRequest(lane,request,std::move(*negotiation));return;}}
  auto contact=contactMap(self->lanes_[lane]->endpoint());if(!contact){return;}self->room_->updateContact(std::move(*contact));
  auto answer=self->room_->answerDetailed(request->request,QDateTime::currentMSecsSinceEpoch(),self->randomHint({}));
  if(!answer||answer->self){++self->rejected_;self->changed();return;}
  auto peerContact=externalContact(*request);
  if(peerContact){self->addPeer({lane,request->peer,request->advertised.protocol,answer->requester,std::move(*peerContact),true,0});const auto p=std::ranges::find_if(self->peers_,[&](const auto& p){return p.peer.lane==lane&&p.peer.endpoint==request->peer&&p.peer.uid==answer->requester;});if(p!=self->peers_.end()){self->biasedIn_=p->serial;}}
  const auto queued=self->hooks_.withPermit(request->deadline,[self,lane,request,envelope=std::move(answer->envelope)]()mutable{
    if(self&&!self->closed_&&self->lanes_[lane]){static_cast<void>(self->lanes_[lane]->answerTransfer(request->receipt,std::move(envelope)));}
  });if(!queued){++self->rejected_;}self->changed();
 })){++rejected_;changed();}
}
Result<PreparedSend> RoomSession::prepareSend(std::string text,std::string nickname,std::int64_t wall,std::optional<std::int32_t> origin,std::optional<std::int32_t> type){
 if(closed_||QThread::currentThread()!=thread()){return std::unexpected(Error::Input);}return room_->prepareSend(std::move(text),std::move(nickname),wall,origin,type);
}
Result<MessageId> RoomSession::commitSend(PreparedSend&& prepared,std::int64_t wall){
 if(QThread::currentThread()!=thread()||closed_){auto discarded=std::move(prepared);(void)discarded;return std::unexpected(Error::Input);}auto result=room_->commitSend(std::move(prepared),wall);if(result){preferLive_=true;changed();}return result;
}
Result<MessageId> RoomSession::send(std::string text,std::string nickname,std::int64_t wall,std::optional<std::int32_t> origin,std::optional<std::int32_t> type){
 if(closed_||QThread::currentThread()!=thread()){return std::unexpected(Error::Input);}auto result=room_->send(std::move(text),std::move(nickname),wall,origin,type);if(result){preferLive_=true;changed();}return result;
}
RoomSessionFacts RoomSession::facts()const{
 RoomSessionFacts out{peers_.size(),0,0,exchanges_,rejected_,incoming_.state(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count())};for(const auto& p:peers_){out.alive+=p.peer.alive;out.active+=p.ticket.has_value()||p.queued;}return out;
}
void RoomSession::changed(){if(hooks_.changed){hooks_.changed();}}
void RoomSession::close(){
 if(closed_){return;}closed_=true;for(const auto& lane:lanes_){if(lane){lane->unregisterTransferHandler(room_->handler());if(peek_){lane->unregisterTransferHandler(peek_->handler());}}}peers_.clear();
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
