// SPDX-License-Identifier: GPL-2.0-or-later
#include "session.hpp"
#include <QThread>
#include <QDateTime>
#include "../azureus_transfer/data.hpp"
#include <algorithm>
#include <limits>
namespace biglybtc::services::azureus_nat {
namespace {
using Clock=std::chrono::steady_clock;
transport::Peer peer(const udp::Address& a){
 QHostAddress ip;if(a.length==4){ip=QHostAddress((quint32(a.bytes[0])<<24)|(quint32(a.bytes[1])<<16)|(quint32(a.bytes[2])<<8)|a.bytes[3]);}
 else if(a.length==16){Q_IPV6ADDR bytes{};std::copy_n(a.bytes.begin(),16,bytes.c);ip=QHostAddress(bytes);}
 return {ip,a.port};
}
udp::Address address(const transport::Peer& p){
 udp::Address out;out.port=p.port;if(p.address.protocol()==QAbstractSocket::IPv4Protocol){out.length=4;auto ip=p.address.toIPv4Address();for(unsigned i=0;i<4;++i){out.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}}
 else if(p.address.protocol()==QAbstractSocket::IPv6Protocol){out.length=16;auto ip=p.address.toIPv6Address();std::copy_n(ip.c,16,out.bytes.begin());}
 return out;
}
bool sameIp(udp::Address a,udp::Address b){a=canonicalAddress(a);b=canonicalAddress(b);a.port=b.port=1;return a==b;}
bool chatData(const Message& m){return !m.clientData||*m.clientData=="de";}
Message reply(Type type,bool ok){Message m;m.type=type;m.ok=ok;return m;}
}
Milliseconds Session::now(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
Session::Session(transport::OwnedPingTransport& lane,std::uint64_t epoch,SessionHooks hooks,bool secondary):lane_(&lane),epoch_(epoch),hooks_(std::move(hooks)),bindings_(secondary),nextExpiry_(now()+150000){
 timer_.setInterval(100);QObject::connect(&timer_,&QTimer::timeout,this,[this]{tick();});
}
Result<std::unique_ptr<Session>> Session::open(transport::OwnedPingTransport& lane,std::uint64_t epoch,SessionHooks hooks,bool secondary){
 if(lane.thread()!=QThread::currentThread()||!hooks.afterRelease||!hooks.withPermit||!hooks.revision){return std::unexpected(Error::Input);}
 auto self=std::unique_ptr<Session>(new Session(lane,epoch,std::move(hooks),secondary));QPointer<Session> owner(self.get());
 auto registered=lane.registerTransferHandler(std::string(handlerKey()),false,self.get(),[owner](auto request){if(owner){owner->incoming(std::move(request));}},[owner]()->std::optional<std::uint64_t>{
  if(!owner||owner->closed_){return {};}auto r=owner->hooks_.revision();return r==std::optional{owner->epoch_}?r:std::nullopt;
 },transport::TransferMode::Call,true,transport::TransferOriginator::SameIp);
 if(!registered){self->closed_=true;return std::unexpected(Error::Input);}self->timer_.start();return self;
}
Session::~Session(){close();}
bool Session::guard(Clock::time_point end,std::move_only_function<void()> effect){
 if(closed_||!lane_||QThread::currentThread()!=thread()||Clock::now()>=end){return false;}QPointer<Session> self(this);
 const bool queued=hooks_.withPermit(end,[self,end,effect=std::move(effect)]()mutable{
  if(!self||self->closed_){return;}if(!self->lane_||self->hooks_.revision()!=std::optional{self->epoch_}){self->shutdown(SessionFailure::Source);return;}
  if(Clock::now()<end){effect();}
 });if(!queued){shutdown(SessionFailure::Source);}return queued;
}
bool Session::route(const RoutedContact& contact,Clock::time_point end){
 if(!lane_||Clock::now()>=end){return false;}return hooks_.peer?hooks_.peer(contact,std::min(end,Clock::now()+std::chrono::seconds(60))):lane_->isTransferPeer(peer(contact.transport()),handlerKey());
}
bool Session::expects(const Message& m,const RoutedContact& origin)const{
 if(m.type==Type::Bind||m.type==Type::Punch||m.type==Type::Query||m.type==Type::Close){return true;}
 if(m.type==Type::Connect){return rendezvous_&&origin.external().address==rendezvous_->external().address;}
 if(m.type==Type::TunnelInbound){return std::ranges::any_of(pending_,[&](const auto& row){return Clock::now()<row.second.end&&sameIp(row.second.target.external().address,origin.transport());});}
 if(m.type==Type::TunnelOutbound){return std::ranges::any_of(connects_,[&](const auto& row){return Clock::now()<row.second.end&&sameIp(row.second.target.external().address,origin.transport());});}return false;
}
bool Session::ingress(transport::Peer actual,std::string bytes){
 if(closed_||!lane_||QThread::currentThread()!=thread()||bytes.size()>azureus_transfer::MaximumDatagram){return false;}
 azureus_dht::Context context;context.network=lane_->network();context.localVersion=55;context.now=QDateTime::currentMSecsSinceEpoch();auto packet=azureus_transfer::decode(bytes,context);
 if(!packet||packet->data.handler!=handlerKey()){return false;}
 if(lane_->isTransferPeer(actual,handlerKey())){return lane_->receiveShared(actual,std::move(bytes));}
 if(!hooks_.peer||ingressQueued_>=16||packet->data.type!=azureus_transfer::Type::WriteRequest||packet->data.start!=0||packet->data.length!=packet->data.total||packet->data.payload.size()!=static_cast<std::size_t>(packet->data.total)||!packet->header.fields().originator){return false;}
 auto external=nodes::Snapshot{true,packet->header.fields().originatorVersion,packet->header.fields().originator};auto payload=std::move(packet->data.payload);const bool empty=packet->data.key.empty();if(!empty&&packet->data.key.size()!=20){return false;}
 ++ingressQueued_;QPointer<Session> self(this);const auto end=Clock::now()+std::chrono::seconds(30);
 if(!hooks_.afterRelease([self,actual,external,payload=std::move(payload),bytes=std::move(bytes),empty,end]()mutable{
  if(!self){return;}--self->ingressQueued_;if(self->closed_||Clock::now()>=end){return;}auto m=decode(std::move(payload),self->cache_);if(!m){++self->rejected_;return;}const bool raw=m->type==Type::TunnelInbound||m->type==Type::TunnelOutbound;if(raw!=empty){++self->rejected_;return;}
  nodes::Contact contact;contact.protocol=external.protocol;contact.address=*external.address;auto observed=RoutedContact::observed(std::move(contact),address(actual),self->cache_);if(!observed){++self->rejected_;return;}
  self->guard(end,[self,m=std::move(*m),observed=std::move(*observed),actual,bytes=std::move(bytes),end]()mutable{
   if(!self->expects(m,observed)||!self->route(observed,end)){++self->rejected_;return;}static_cast<void>(self->lane_->receiveShared(actual,std::move(bytes)));
  });
 })){--ingressQueued_;return false;}return true;
}
bool Session::selectRendezvous(RoutedContact contact){
 if(closed_||!lane_||(!hooks_.peer&&!lane_->isTransferPeer(peer(contact.transport()),handlerKey()))){return false;}
 return guard(Clock::now()+std::chrono::seconds(30),[this,contact=std::move(contact)]()mutable{
  if(!route(contact,Clock::now()+std::chrono::seconds(30))){return;}if(bindTicket_){lane_->cancelTransfer(*bindTicket_);bindTicket_.reset();}client_.clear();rendezvous_=std::move(contact);
  if(!client_.select(rendezvous_->external(),now())){rendezvous_.reset();return;}bind();
 });
}
void Session::bind(){
 if(!rendezvous_||bindTicket_){return;}auto attempt=client_.begin(now());if(!attempt){return;}if(!route(*rendezvous_,Clock::time_point(std::chrono::milliseconds(attempt->end)))){auto failure=client_.complete(*attempt,BindStatus::Failed,now());if(failure.failedTarget){rendezvous_.reset();}return;}Message request;request.type=Type::Bind;auto bytes=encode(request);if(!bytes){return;}
 QPointer<Session> self(this);
 auto ticket=lane_->callTransfer(peer(rendezvous_->transport()),rendezvous_->external().protocol,std::string(handlerKey()),std::move(*bytes),this,[self,attempt=*attempt](auto result)mutable{
  if(!self||self->closed_){return;}self->guard(Clock::now()+std::chrono::seconds(1),[self,attempt,result=std::move(result)]()mutable{
   // ClientBinding serials survive clear/reselection; an old callback cannot
   // clear the newly selected relay's ticket or operational state.
   BindStatus status=BindStatus::Failed;if(result){auto m=decode(std::move(*result),self->cache_);if(m&&m->type==Type::BindReply){status=*m->ok?BindStatus::Ok:BindStatus::Denied;}}
   auto done=self->client_.complete(attempt,status,now());if(!done.accepted){return;}self->bindTicket_.reset();if(done.failedTarget){self->rendezvous_.reset();}
  });
 },Clock::time_point(std::chrono::milliseconds(attempt->end)));
 if(ticket){bindTicket_=*ticket;}else{auto done=client_.complete(*attempt,BindStatus::Failed,now());if(done.failedTarget){rendezvous_.reset();}}
}
Result<std::uint64_t> Session::punch(RoutedContact relay,RoutedContact target,QObject* receiver,TunnelCallback callback){
 if(closed_||!lane_||QThread::currentThread()!=thread()||!receiver||receiver->thread()!=thread()||!callback||(!hooks_.peer&&(!lane_->isTransferPeer(peer(relay.transport()),handlerKey())||!lane_->isTransferPeer(peer(target.transport()),handlerKey())))){return std::unexpected(Error::Input);}
 if(pending_.size()>=3||serial_==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(Error::Limit);}
 // Register before the RPC: target's inbound write normally precedes the relay reply.
 const auto id=++serial_;auto end=Clock::now()+std::chrono::seconds(45);
 pending_.emplace(id,Pending{std::move(relay),std::move(target),receiver,std::move(callback),end,{},{},{},{},{},{},false});
 if(!guard(end,[this,id]{auto i=pending_.find(id);if(i==pending_.end()){return;}if(!route(i->second.relay,std::min(i->second.end,Clock::now()+std::chrono::seconds(30)))||!route(i->second.target,i->second.end)){finish(id,std::unexpected(SessionFailure::Rejected));return;}Message m;m.type=Type::Punch;m.target=socketAddress(i->second.target.external().address).value();auto bytes=encode(m);if(!bytes){finish(id,std::unexpected(SessionFailure::Input));return;}QPointer<Session> self(this);
  auto t=lane_->callTransfer(peer(i->second.relay.transport()),i->second.relay.external().protocol,std::string(handlerKey()),std::move(*bytes),this,[self,id](auto result){if(self){self->punchDone(id,std::move(result));}},std::min(i->second.end,Clock::now()+std::chrono::seconds(30)));
  if(!t){finish(id,std::unexpected(SessionFailure::Rejected));}else{i->second.rpc=*t;}
 })){finish(id,std::unexpected(SessionFailure::Source));}return id;
}
void Session::incoming(std::shared_ptr<const transport::TransferRequest> request){
 if(closed_){return;}QPointer<Session> self(this);
 if(!hooks_.afterRelease([self,request=std::move(request)]{
  if(!self||self->closed_||Clock::now()>=request->deadline){return;}auto message=decode(request->request,self->cache_);
  if(!message||request->handler!=handlerKey()||!request->advertised.address){++self->rejected_;return;}
  nodes::Contact external;external.protocol=request->advertised.protocol;external.address=*request->advertised.address;auto contact=RoutedContact::observed(std::move(external),address(request->peer),self->cache_);
  if(!contact){++self->rejected_;return;}
  self->guard(request->deadline,[self,request,message=std::move(*message),contact=std::move(*contact)]()mutable{
   if(request->sourceRevision!=self->epoch_){++self->rejected_;return;}self->dispatch(request,std::move(message),std::move(contact));
  });
 })){++rejected_;}
}
void Session::answer(const transport::TransferRequest& request,Message message){auto bytes=encode(message);if(bytes){static_cast<void>(lane_->answerTransfer(request.receipt,std::move(*bytes)));}}
void Session::dispatch(std::shared_ptr<const transport::TransferRequest> request,Message m,RoutedContact origin){
 const bool raw=m.type==Type::TunnelInbound||m.type==Type::TunnelOutbound;
 if((raw&&request->mode!=transport::TransferMode::Write)||(!raw&&request->mode!=transport::TransferMode::Call)){++rejected_;return;}
 if(m.type==Type::Bind){auto result=bindings_.bind(origin,now());auto response=reply(Type::BindReply,result&&result->ok);if(result){response.port=result->port;}answer(*request,std::move(response));return;}
 if(m.type==Type::Query){auto response=reply(Type::QueryReply,true);auto text=socketAddress(origin.transport()).value();response.ip=text[1]=='['?text.substr(2,text.find(']')-2):text.substr(1,text.rfind(':')-1);response.port=origin.transport().port;answer(*request,std::move(response));return;}
 if(m.type==Type::Close){if(rendezvous_&&origin.external().id==rendezvous_->external().id){client_.clear();rendezvous_.reset();if(bindTicket_){lane_->cancelTransfer(*bindTicket_);bindTicket_.reset();}}answer(*request,reply(Type::CloseReply,true));return;}
 if(m.type==Type::TunnelInbound){
  bool accepted=false;for(auto& [id,p]:pending_){(void)id;if(Clock::now()<p.end&&sameIp(p.target.external().address,origin.transport())){accepted=true;if(!p.early){p.early=origin.transport();if(!p.indirect){++earlyInbound_;}}if(p.policy){p.policy->inbound(origin.transport(),now());}}}
  if(!accepted){++rejected_;}std::vector<std::uint64_t> ids;for(const auto& [id,p]:pending_){if(p.policy){ids.push_back(id);}}for(auto id:ids){finishReady(id);}return;
 }
 if(m.type==Type::TunnelOutbound){bool expected=false;for(const auto& [id,c]:connects_){(void)id;if(Clock::now()<c.end&&sameIp(c.target.external().address,origin.transport())){expected=true;break;}}if(!expected){++rejected_;}return;}
 if(m.type==Type::Punch){
  auto lease=bindings_.candidate(*m.target,now());if(!chatData(m)||!lease||relays_.size()>=3||serial_==std::numeric_limits<std::uint64_t>::max()){answer(*request,reply(Type::PunchReply,false));return;}
  auto exported=nodes::encodeContact(origin.exportSnapshot());if(!exported){answer(*request,reply(Type::PunchReply,false));return;}
  Message connect;connect.type=Type::Connect;connect.origin=std::move(*exported);connect.port=origin.transport().port;auto bytes=encode(connect);if(!bytes){return;}
  auto targetRoute=RoutedContact::observed(lease->contact,lease->transport,cache_);if(!targetRoute||!route(*targetRoute,request->deadline)){answer(*request,reply(Type::PunchReply,false));return;}auto id=++serial_;relays_.emplace(id,Relay{request,*lease,{}});QPointer<Session> self(this);
  auto ticket=lane_->callTransfer(peer(lease->transport),lease->contact.protocol,std::string(handlerKey()),std::move(*bytes),this,[self,id](auto result){if(self){self->relayDone(id,std::move(result));}},std::min(request->deadline,Clock::now()+std::chrono::seconds(30)));
  if(ticket){relays_.at(id).ticket=*ticket;}else{bindings_.outcome(*lease,false,now());relays_.erase(id);answer(*request,reply(Type::PunchReply,false));}return;
 }
 if(m.type==Type::Connect){
  if(!chatData(m)||!rendezvous_||origin.external().address!=rendezvous_->external().address||connects_.size()>=3||serial_==std::numeric_limits<std::uint64_t>::max()){answer(*request,reply(Type::ConnectReply,false));return;}
  auto decoded=nodes::decodeContact(*m.origin,cache_);if(!decoded){answer(*request,reply(Type::ConnectReply,false));return;}auto physical=decoded->contact.address;if(m.port&&*m.port){physical.port=*m.port;}
  auto target=RoutedContact::observed(std::move(decoded->contact),physical,cache_);auto burst=TunnelBurst::start(now());
  if(!target||!burst||!route(*target,request->deadline)){answer(*request,reply(Type::ConnectReply,false));return;}
  auto id=++serial_;connects_.emplace(id,Connect{request,std::move(*target),std::move(*burst),std::min(request->deadline,Clock::now()+std::chrono::seconds(12)),{},false});rawConnect(id);return;
 }
 ++rejected_;
}
void Session::relayDone(std::uint64_t id,transport::TransferOutcome result){
 if(closed_){return;}auto i=relays_.find(id);if(i==relays_.end()){return;}auto end=i->second.request->deadline;
 if(Clock::now()>=end){relays_.erase(i);return;}guard(end,[this,id,result=std::move(result)]()mutable{auto i=relays_.find(id);if(i==relays_.end()){return;}auto item=std::move(i->second);relays_.erase(i);bool ok=false;if(result){auto response=decode(std::move(*result),cache_);ok=response&&response->type==Type::ConnectReply&&*response->ok&&chatData(*response);}
  const bool current=bindings_.outcome(item.lease,ok,now());auto response=reply(Type::PunchReply,ok&&current);if(ok&&current){response.port=item.lease.transport.port;response.clientData="de";}answer(*item.request,std::move(response));
 });
}
void Session::punchDone(std::uint64_t id,transport::TransferOutcome result){
 if(closed_){return;}auto i=pending_.find(id);if(i==pending_.end()){return;}guard(i->second.end,[this,id,result=std::move(result)]()mutable{auto i=pending_.find(id);if(i==pending_.end()){return;}auto& p=i->second;p.rpc.reset();if(!result){finish(id,std::unexpected(result.error()==transport::Failure::Timeout?SessionFailure::Timeout:SessionFailure::Rejected));return;}auto response=decode(std::move(*result),cache_);
  if(!response||response->type!=Type::PunchReply||!*response->ok||!chatData(*response)){finish(id,std::unexpected(SessionFailure::Rejected));return;}
  auto target=p.target.external();target.address=p.target.transport();if(response->port&&*response->port){target.address.port=*response->port;}p.indirect=std::move(target);auto burst=TunnelBurst::start(now());if(!burst){finish(id,std::unexpected(SessionFailure::Input));return;}p.burst=std::move(*burst);rawPunch(id);
 });
}
void Session::rawPunch(std::uint64_t id){
 auto i=pending_.find(id);if(i==pending_.end()){return;}auto& p=i->second;if(!p.burst||!p.indirect||p.raw){return;}auto attempt=p.burst->begin(now());if(!attempt){return;}auto contact=p.target.withTransport(p.indirect->address);if(!contact||!route(*contact,p.end)){finish(id,std::unexpected(SessionFailure::Rejected));return;}
 Message m;m.type=Type::TunnelOutbound;auto bytes=encode(m);if(!bytes){return;}QPointer<Session> self(this);
 auto done=[self,id](transport::TransferOutcome result)mutable{if(!self||self->closed_){return;}auto i=self->pending_.find(id);if(i==self->pending_.end()){return;}self->guard(i->second.end,[self,id,result=std::move(result)]()mutable{auto i=self->pending_.find(id);if(i==self->pending_.end()){return;}auto& p=i->second;p.raw.reset();if(result){p.burst->acknowledged();}
   if(!p.firstOutboundDone){p.firstOutboundDone=true;auto policy=Punch::accepted(*p.indirect,{},now());if(!policy){self->finish(id,std::unexpected(SessionFailure::Input));return;}p.policy=std::move(*policy);if(p.early){p.policy->inbound(*p.early,now());}}self->finishReady(id);
  });};
 auto ticket=lane_->writeTransfer(peer(p.indirect->address),p.target.external().protocol,std::string(handlerKey()),std::move(*bytes),this,std::move(done),std::min(p.end,Clock::time_point(std::chrono::milliseconds(attempt->end))));
 if(ticket){p.raw=*ticket;}else{p.firstOutboundDone=true;auto policy=Punch::accepted(*p.indirect,{},now());if(policy){p.policy=std::move(*policy);if(p.early){p.policy->inbound(*p.early,now());}finishReady(id);}else{finish(id,std::unexpected(SessionFailure::Input));}}
}
void Session::rawConnect(std::uint64_t id){
 auto i=connects_.find(id);if(i==connects_.end()){return;}auto& c=i->second;if(c.ticket){return;}if(!route(c.target,c.end)){connects_.erase(i);return;}auto attempt=c.burst.begin(now());if(!attempt){return;}Message m;m.type=Type::TunnelInbound;auto bytes=encode(m);if(!bytes){return;}QPointer<Session> self(this);
 auto ticket=lane_->writeTransfer(peer(c.target.transport()),c.target.external().protocol,std::string(handlerKey()),std::move(*bytes),this,[self,id](auto result){if(!self||self->closed_){return;}auto i=self->connects_.find(id);if(i==self->connects_.end()){return;}self->guard(i->second.end,[self,id,result=std::move(result)]()mutable{auto i=self->connects_.find(id);if(i==self->connects_.end()){return;}auto& c=i->second;c.ticket.reset();if(result){c.burst.acknowledged();}if(!c.answered){auto response=reply(Type::ConnectReply,true);response.clientData="de";self->answer(*c.request,std::move(response));c.answered=true;}});},std::min(c.end,Clock::time_point(std::chrono::milliseconds(attempt->end))));
 if(ticket){c.ticket=*ticket;}else if(!c.answered){auto response=reply(Type::ConnectReply,true);response.clientData="de";answer(*c.request,std::move(response));c.answered=true;}
}
void Session::finishReady(std::uint64_t id){auto i=pending_.find(id);if(i==pending_.end()||!i->second.policy){return;}auto result=i->second.policy->finish(now());if(!result){return;}auto target=i->second.target.withTransport(result->target.address);if(!target){finish(id,std::unexpected(SessionFailure::Input));return;}finish(id,TunnelResult{std::move(*target),result->directObserved});}
void Session::finish(std::uint64_t id,TunnelOutcome result){
 auto i=pending_.find(id);if(i==pending_.end()){return;}auto item=std::move(i->second);pending_.erase(i);if(lane_){if(item.rpc){lane_->cancelTransfer(*item.rpc);}if(item.raw){lane_->cancelTransfer(*item.raw);}}
 if(item.receiver&&item.callback){QPointer<Session> self(this);hooks_.afterRelease([self,receiver=item.receiver,callback=std::move(item.callback),result=std::move(result)]()mutable{
  if(!self||!receiver){return;}QMetaObject::invokeMethod(self,[self,receiver,callback=std::move(callback),result=std::move(result)]()mutable{if(self&&receiver){if(self->closed_&&result){result=std::unexpected(self->closedReason_);}callback(std::move(result));}},Qt::QueuedConnection);
 });}
}
bool Session::cancel(std::uint64_t id){if(closed_||QThread::currentThread()!=thread()||!pending_.contains(id)){return false;}finish(id,std::unexpected(SessionFailure::Cancelled));return true;}
void Session::tick(){
 if(closed_){return;}const auto n=now();if(pending_.empty()&&relays_.empty()&&connects_.empty()&&(!client_.next()||n<*client_.next())&&n<nextExpiry_){return;}
 guard(Clock::now()+std::chrono::seconds(1),[this]{const auto n=now();if(n>=nextExpiry_){bindings_.expire(n);nextExpiry_=n+150000;}bind();
  std::vector<std::uint64_t> ids;for(const auto& [id,p]:pending_){(void)p;ids.push_back(id);}for(auto id:ids){auto i=pending_.find(id);if(i==pending_.end()){continue;}if(Clock::now()>=i->second.end){finish(id,std::unexpected(SessionFailure::Timeout));continue;}finishReady(id);if(pending_.contains(id)){rawPunch(id);}}
  ids.clear();for(const auto& [id,c]:connects_){(void)c;ids.push_back(id);}for(auto id:ids){auto i=connects_.find(id);if(Clock::now()>=i->second.end){if(i->second.ticket){lane_->cancelTransfer(*i->second.ticket);}connects_.erase(i);}else{rawConnect(id);}}
  std::erase_if(relays_,[this](const auto& row){if(Clock::now()<row.second.request->deadline){return false;}if(row.second.ticket){lane_->cancelTransfer(*row.second.ticket);}return true;});
 });
}
SessionFacts Session::facts()const{return {bindings_.size(),pending_.size(),relays_.size(),connects_.size(),earlyInbound_,rejected_,client_.active(),closed_};}
void Session::shutdown(SessionFailure failure){
 if(closed_){return;}closed_=true;closedReason_=failure;timer_.stop();if(lane_){lane_->unregisterTransferHandler(handlerKey());}client_.clear();rendezvous_.reset();bindings_.clear();bindTicket_.reset();relays_.clear();connects_.clear();while(!pending_.empty()){finish(pending_.begin()->first,std::unexpected(failure));}
}
void Session::close(){shutdown(SessionFailure::Closed);}
}
