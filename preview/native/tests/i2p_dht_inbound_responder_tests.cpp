// SPDX-License-Identifier: GPL-2.0-or-later
// SOURCE-CANDIDATE: pure synthetic packet/state controls; no source/network grant.
#include "inbound_responder.hpp"
#include "biglybtc/bencode.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace i=biglybtc::services::i2p;namespace d=i::dht;namespace s=d::inbound;namespace core=biglybtc::core;
void require(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
template<class T,class E>T take(std::expected<T,E> value){if(!value){throw std::runtime_error("Unexpected responder refusal");}return std::move(*value);}
d::Contact contact(unsigned index){
  i::Bytes wire(387);for(std::size_t n=0;n<384;++n){wire[n]=i::Byte(n*29+index*7);}wire[0]=i::Byte(index>>8);wire[1]=i::Byte(index);wire[384]=wire[385]=wire[386]=0;
  auto destination=take(i::decodeDestination(wire));const auto port=static_cast<std::uint16_t>(12000+index);i::Bytes random(14,i::Byte(index));random[0]=i::Byte(index>>8);random[1]=i::Byte(index);
  return {take(d::makeNodeId(destination.hash,port,random)),destination.hash,port,std::move(destination)};
}
bool closer(const d::Contact& a,const d::Contact& b,const d::Id& target){for(std::size_t n=0;n<target.size();++n){auto x=i::Byte(a.id[n]^target[n]),y=i::Byte(b.id[n]^target[n]);if(x!=y){return x<y;}}return false;}
d::Message query(d::Method method,const d::Contact& peer,const d::Id& target){d::Message q;q.method=method;q.id=peer.id;q.target=target;q.transaction={0,1,2,3,4,5,6,7};return q;}
i::Bytes wire(d::Message q,bool ordinary=false,bool azZero=false){
  auto result=take(d::encode(q));const std::string tag="2:_v3:AZ4";auto at=std::search(result.begin(),result.end(),tag.begin(),tag.end());require(at!=result.end(),"Default codec lost AZ4");
  if(ordinary){result.erase(at,at+static_cast<std::ptrdiff_t>(tag.size()));}else if(azZero){*(at+static_cast<std::ptrdiff_t>(tag.size()-1))='0';}return result;
}
core::Document document(const s::Response& reply){return take(core::Document::parse(std::string(reply.payload.begin(),reply.payload.end())));}
bool field(const s::Response& reply,const char* name){auto doc=document(reply);const auto* body=doc.root().find("r");require(body!=nullptr,"Reply body missing");return body->find(name)!=nullptr;}
void advertisedLocal(const s::Response& reply){auto doc=document(reply);const auto* body=doc.root().find("r");const auto* version=body?body->find("_v"):nullptr;require(version&&version->type==core::Node::Type::Bytes&&version->bytes=="AZ4","Original sendResponse must advertise local AZ4 for every caller");}
i::Bytes argument(i::Bytes bytes,const char* name,std::optional<std::string> replacement){
  auto doc=take(core::Document::parse(std::string(bytes.begin(),bytes.end())));const auto* body=doc.root().find("a");require(body!=nullptr,"Query args missing");
  for(std::size_t n=0;n<body->children.size();n+=2){const auto& key=body->children[n];const auto& value=body->children[n+1];if(key.bytes!=name){continue;}
    const auto first=replacement?value.begin:key.begin;const auto last=value.end;bytes.erase(bytes.begin()+static_cast<std::ptrdiff_t>(first),bytes.begin()+static_cast<std::ptrdiff_t>(last));if(replacement){bytes.insert(bytes.begin()+static_cast<std::ptrdiff_t>(first),replacement->begin(),replacement->end());}return bytes;
  }throw std::runtime_error("Expected query field missing");
}
struct Harness {
  d::Contact local=contact(900);std::unique_ptr<s::Responder> responder;bool enabled=true,allow=true;unsigned calls=0;d::Contact last;bool existing=false;s::Route route;
  Harness(){biglybtc::services::azureus_auth::Key key{};responder=take(s::Responder::create(local,{},key));route=[this](const d::Contact& originator,d::Method,const d::Id&,bool known)->std::optional<s::RoutingSnapshot>{++calls;last=originator;existing=known;return s::RoutingSnapshot{{},allow};};}
  s::ReplyResult<std::optional<s::Response>> send(d::Message q,const d::Contact* signedPeer,std::int64_t wall=1000,bool ordinary=false,s::RequestPolicy policy={},std::optional<std::uint16_t> observedPort={},bool azZero=false){
    auto bytes=wire(std::move(q),ordinary,azZero);s::Incoming in{bytes,signedPeer?&*signedPeer->destination:nullptr,observedPort.value_or(signedPeer?signedPeer->port:0),static_cast<std::uint16_t>(local.port+(signedPeer?0:1))};return responder->receive(in,wall,policy,[this]{return enabled;},route);
  }
  i::Bytes token(const d::Contact& peer,const d::Id& key){auto reply=take(send(query(d::Method::GetPeers,peer,key),&peer));require(reply.has_value(),"Missing token response");return take(d::decode(reply->payload)).token;}
  s::Response announce(const d::Contact& claimed,const d::Id& key,i::Bytes token,bool seed,std::int64_t wall=1000,s::RequestPolicy policy={}){auto q=query(d::Method::AnnouncePeer,claimed,key);q.token=std::move(token);q.seed=seed;return *take(send(std::move(q),nullptr,wall,false,policy,55000));}
};
void normalWire(){
  Harness h;auto a=contact(1);d::Id key{};key.fill(0x51);
  auto ping=take(h.send(query(d::Method::Ping,a,key),&a));require(ping&&ping->recipient.destination->wire==a.destination->wire&&ping->fromPort==h.local.port&&ping->toPort==a.port+1,"Signed ping reply recipient/ports changed");advertisedLocal(*ping);require(!field(*ping,"token")&&!field(*ping,"nodes")&&!field(*ping,"values"),"Ping gained fields");
  auto nodes=std::vector<d::Contact>{contact(3),contact(2)};std::sort(nodes.begin(),nodes.end(),[&](const auto& x,const auto& y){return closer(x,y,key);});h.route=[&](const d::Contact& originator,d::Method,const d::Id&,bool)->std::optional<s::RoutingSnapshot>{h.last=originator;return s::RoutingSnapshot{nodes,true};};
  auto found=*take(h.send(query(d::Method::FindNode,a,key),&a));auto body=take(d::decode(found.payload));require(body.hasNodes&&body.nodes.size()==2&&body.token.empty()&&body.nodes.front().id==nodes.front().id,"find_node closest/token behavior differs");
  auto get=*take(h.send(query(d::Method::GetPeers,a,key),&a));body=take(d::decode(get.payload));require(body.hasNodes&&!body.hasPeers&&body.token.size()==8,"get_peers miss must carry token and nodes");
  auto ordinary=*take(h.send(query(d::Method::GetPeers,a,key),&a,1000,false,{}, {},true));require(take(d::decode(ordinary.payload)).token.size()==8,"Explicit AZ0 request refused");advertisedLocal(ordinary);
  auto claim=contact(80);auto stored=h.announce(claim,key,body.token,true);require(stored.recipient.id==a.id&&stored.recipient.destination->wire==a.destination->wire&&stored.toPort==a.port+1,"Raw announce used untrusted id/fromPort instead of saved token contact");require(!field(stored,"values")&&!field(stored,"nodes")&&!field(stored,"token"),"ACK falsely describes storage");require(take(h.responder->counts()).peers.values==1,"Actual bounded direct announce not stored");
}
void filtering(){
  Harness h;auto a=contact(1),b=contact(2),c=contact(3);d::Id key{};key.fill(0x52);
  for(const auto& item:std::array<std::pair<d::Contact,bool>,3>{{{a,false},{b,true},{c,false}}}){auto token=h.token(item.first,key);(void)h.announce(item.first,key,token,item.second);}
  auto q=query(d::Method::GetPeers,a,key);q.noSeed=true;
  auto az=*take(h.send(q,&a));auto peers=take(d::decode(az.payload));require(peers.peers.size()==3&&field(az,"flags")&&std::count_if(peers.peers.begin(),peers.peers.end(),[](const auto& p){return p.seed;})==1,"AZ noseed must retain self and both seed classes");
  q.noSeed=false;auto ordinary=*take(h.send(q,&a,1000,true));peers=take(d::decode(ordinary.payload));require(peers.peers.size()==2&&!field(ordinary,"flags")&&std::none_of(peers.peers.begin(),peers.peers.end(),[&](const auto& p){return p.hash==a.hash;}),"Ordinary caller must exclude itself and bitmap");advertisedLocal(ordinary);
  q.noSeed=true;ordinary=*take(h.send(q,&a,1000,true));peers=take(d::decode(ordinary.payload));require(peers.peers.size()==1&&peers.peers.front().hash==c.hash,"Ordinary noseed filtering differs");
  Harness self;auto token=self.token(a,key);(void)self.announce(a,key,token,true);auto empty=*take(self.send(q,&a,1000,true));peers=take(d::decode(empty.payload));require(peers.hasPeers&&peers.peers.empty()&&!peers.hasNodes&&peers.token.size()==8&&!field(empty,"flags"),"Filtered hit became nodes or lost its token");
  Harness many;for(unsigned n=0;n<18;++n){auto peer=contact(n+10);auto t=many.token(peer,key);(void)many.announce(peer,key,t,n%2==0);}auto limited=*take(many.send(query(d::Method::GetPeers,a,key),&a));require(take(d::decode(limited.payload)).peers.size()==16,"Original server max16 changed");
}
void refusalsAndPolicy(){
  Harness h;auto a=contact(1);d::Id key{};key.fill(0x53);auto q=query(d::Method::GetPeers,a,key);h.enabled=false;auto denied=h.send(q,&a);require(!denied&&denied.error()==s::ResponderError::NoAuthority&&h.calls==0&&take(h.responder->counts()).tokenBindings==0,"Missing authority mutated state or called routing");h.enabled=true;
  auto token=h.token(a,key);auto bad=query(d::Method::AnnouncePeer,a,key);bad.token=token;bad.token[0]^=1;const auto calls=h.calls;require(!take(h.send(bad,nullptr))&&h.calls==calls&&take(h.responder->counts()).peers.values==0,"Bad token reached route/store");
  auto rawPing=query(d::Method::Ping,a,key);require(!take(h.send(rawPing,nullptr)),"Unsigned nonannounce accepted");bad.token=token;auto signedAck=take(h.send(bad,&a));require(signedAck&&signedAck->recipient.id==a.id&&take(h.responder->counts()).peers.values==0,"Signed announce must ACK actual sender without storing");
  h.allow=false;(void)h.announce(a,key,token,true);require(take(h.responder->counts()).peers.values==0,"Distance-declined ACK became a stored value");h.allow=true;
  auto sleeping=h.send(q,&a,1000,false,{true,false});require(take(std::move(sleeping))==std::nullopt,"Sleeping query without z was answered");q.wakeRequested=true;auto awake=*take(h.send(q,&a,1000,false,{true,false}));auto awakeToken=take(d::decode(awake.payload)).token;bad.token=awakeToken;bad.wakeRequested=true;
  require(take(h.send(bad,nullptr,1000,false,{true,false})).has_value()&&take(h.responder->counts()).peers.values==0,"z bypassed sleeping database-store denial");
  require(take(h.send(bad,nullptr,1000,false,{false,true})).has_value()&&take(h.responder->counts()).peers.values==0,"Suspended database-store denial lost ACK");
  auto bytes=wire(q);const std::string z="1:zi0e";auto at=std::search(bytes.begin(),bytes.end(),z.begin(),z.end());require(at!=bytes.end(),"Explicit z not encoded");const auto offset=static_cast<std::size_t>(at-bytes.begin());bytes.erase(at,at+static_cast<std::ptrdiff_t>(z.size()));const std::string any="1:z3:any";bytes.insert(bytes.begin()+static_cast<std::ptrdiff_t>(offset),any.begin(),any.end());s::Incoming incoming{bytes,&*a.destination,a.port,h.local.port};require(take(h.responder->receive(incoming,1000,{true,false},[]{return true;},h.route)).has_value(),"Root z presence was interpreted as a boolean value");
  auto invalid=q;invalid.id[0]^=1;require(!h.send(invalid,&a),"Bad secure NID accepted");h.enabled=false;require(!h.send(q,&a,1000,false,{true,false}),"z granted source authority");
}
void routingAndLifetime(){
  Harness h;auto a=contact(1),b=contact(2);d::Id key{};key.fill(0x54);auto token=h.token(a,key);(void)h.announce(a,key,token,false);d::Id other=key;other[0]^=1;auto bToken=h.token(b,other);
  bool known=false;h.route=[&](const d::Contact&,d::Method,const d::Id&,bool exists)->std::optional<s::RoutingSnapshot>{known=exists;return s::RoutingSnapshot{{},exists};};(void)h.announce(b,key,bToken,true,1001+s::PeerLifetimeMillis);require(known&&take(h.responder->counts()).peers.values==1,"Existing-key route fact expired/cycled before actual store");
  Harness invalid;std::vector<d::Contact> tooMany;for(unsigned n=0;n<11;++n){tooMany.push_back(contact(n+10));}invalid.route=[&](const d::Contact&,d::Method,const d::Id&,bool)->std::optional<s::RoutingSnapshot>{return s::RoutingSnapshot{tooMany,false};};auto refused=invalid.send(query(d::Method::FindNode,a,key),&a);require(!refused&&refused.error()==s::ResponderError::Routing,"Oversize routing snapshot accepted");
  std::vector<d::Contact> reversed{contact(3),contact(2)};std::sort(reversed.begin(),reversed.end(),[&](const auto& x,const auto& y){return closer(y,x,key);});invalid.route=[&](const d::Contact&,d::Method,const d::Id&,bool)->std::optional<s::RoutingSnapshot>{return s::RoutingSnapshot{reversed,false};};require(!invalid.send(query(d::Method::FindNode,a,key),&a),"Unsorted routing snapshot accepted");
  invalid.route=[&](const d::Contact&,d::Method,const d::Id&,bool)->std::optional<s::RoutingSnapshot>{return s::RoutingSnapshot{{a,a},false};};require(!invalid.send(query(d::Method::FindNode,a,key),&a),"Duplicate routing NID accepted");
  Harness lost;lost.route=[&](const d::Contact&,d::Method,const d::Id&,bool)->std::optional<s::RoutingSnapshot>{lost.enabled=false;return s::RoutingSnapshot{};};auto noCurrent=lost.send(query(d::Method::GetPeers,a,key),&a);require(!noCurrent&&noCurrent.error()==s::ResponderError::NoAuthority&&take(lost.responder->counts()).tokenBindings==0,"Current loss in callback still issued token/packet");
  Harness reentry;bool blocked=false;reentry.route=[&](const d::Contact&,d::Method,const d::Id&,bool)->std::optional<s::RoutingSnapshot>{auto nested=reentry.send(query(d::Method::Ping,a,key),&a);blocked=!nested&&nested.error()==s::ResponderError::Reentry;return s::RoutingSnapshot{};};require(take(reentry.send(query(d::Method::Ping,a,key),&a)).has_value()&&blocked,"Callback reentry was not bounded");
  Harness closing;closing.route=[&](const d::Contact&,d::Method,const d::Id&,bool)->std::optional<s::RoutingSnapshot>{closing.responder->close();return s::RoutingSnapshot{};};auto closed=closing.send(query(d::Method::Ping,a,key),&a);require(!closed&&closed.error()==s::ResponderError::Closed,"Close during routing still returned a packet");require(!closing.send(query(d::Method::Ping,a,key),&a),"Closed responder reopened implicitly");
  Harness foreign;bool wrongThread=false;std::thread worker([&]{auto result=foreign.send(query(d::Method::Ping,a,key),&a);wrongThread=!result&&result.error()==s::ResponderError::Thread;});worker.join();require(wrongThread,"Responder crossed owner thread");
}
void originalShapes(){
  Harness h;auto a=contact(1),b=contact(2),c=contact(3);d::Id key{};key.fill(0x55);
  auto call=[&](i::Bytes bytes,const d::Contact* signer)->s::ReplyResult<std::optional<s::Response>>{s::Incoming in{bytes,signer?&*signer->destination:nullptr,signer?signer->port:static_cast<std::uint16_t>(0),static_cast<std::uint16_t>(h.local.port+(signer?0:1))};return h.responder->receive(in,1000,{},[]{return true;},h.route);};
  for(const std::string target:{"0:","3:abc"}){auto bytes=argument(wire(query(d::Method::FindNode,a,key)),"target",target);const auto calls=h.calls;auto result=*take(call(std::move(bytes),&a));auto body=take(d::decode(result.payload));require(h.calls==calls+1&&body.hasNodes&&body.nodes.empty()&&body.token.empty(),"Original non20 find_node must contactAlive and return empty nodes");}
  auto q=query(d::Method::FindNode,a,key);q.findNodeTarget=i::Bytes{1,2,3};require(take(d::decode(take(d::encode(q)))).findNodeTarget==q.findNodeTarget,"Explicit non20 target roundtrip failed");
  auto token=h.token(a,key);q=query(d::Method::AnnouncePeer,a,key);q.token=token;q.seed=true;
  auto signedBytes=argument(argument(wire(q),"token",std::nullopt),"port",std::nullopt);const auto calls=h.calls;auto ack=*take(call(std::move(signedBytes),&a));require(h.calls==calls+1&&ack.recipient.id==a.id&&ack.toPort==a.port+1&&take(h.responder->counts()).peers.values==0,"Signed missing-token/port announce must ACK without a store");
  signedBytes=argument(wire(q),"token",std::string("li1ee"));require(take(call(std::move(signedBytes),&a)).has_value()&&take(h.responder->counts()).peers.values==0,"Signed announce inspected ignored token type");
  auto noToken=q;noToken.token.clear();require(!d::encode(noToken),"Default raw client encode silently lost its token requirement");
  unsigned index=0;for(const std::optional<std::string>& port:std::array<std::optional<std::string>,4>{std::nullopt,std::string("i0e"),std::string("i-1e"),std::string("3:any")}){
    d::Id target=key;target[1]=i::Byte(++index);q.target=target;auto bytes=argument(wire(q),"port",port);bytes=argument(std::move(bytes),"id",index%2?std::optional<std::string>{"0:"}:std::nullopt);
    auto result=*take(call(std::move(bytes),nullptr));require(result.recipient.id==a.id&&result.toPort==a.port+1,"Raw announcement id/port became reply authority");
  }require(take(h.responder->counts()).peers.values==4,"Ignored announce TCP/id shape prevented original direct stores");
  Harness numeric;auto bt=numeric.token(b,key);(void)numeric.announce(b,key,bt,true);auto ct=numeric.token(c,key);(void)numeric.announce(c,key,ct,false);
  for(const auto& item:std::array<std::pair<const char*,std::size_t>,3>{{{"i2e",1},{"i-1e",1},{"i4294967296e",2}}}){
    auto get=query(d::Method::GetPeers,a,key);get.noSeed=true;auto bytes=argument(wire(get,true),"noseed",std::string(item.first));s::Incoming in{bytes,&*a.destination,a.port,numeric.local.port};auto result=*take(numeric.responder->receive(in,1000,{},[]{return true;},numeric.route));require(take(d::decode(result.payload)).peers.size()==item.second&&!field(result,"flags"),"Original numeric intValue noseed semantics changed");
  }
  for(const auto& item:std::array<std::pair<const char*,bool>,2>{{{"i2e",true},{"i4294967296e",false}}}){
    d::Id target=key;target[2]=item.second?1:2;auto t=numeric.token(a,target);auto announce=query(d::Method::AnnouncePeer,a,target);announce.token=t;auto bytes=argument(wire(announce),"seed",std::string(item.first));s::Incoming in{bytes,nullptr,0,static_cast<std::uint16_t>(numeric.local.port+1)};(void)take(numeric.responder->receive(in,1000,{},[]{return true;},numeric.route));auto result=*take(numeric.send(query(d::Method::GetPeers,b,target),&b));const auto values=take(d::decode(result.payload));require(values.peers.size()==1&&values.peers.front().seed==item.second,"Original numeric intValue seed semantics changed");
  }
}
void causalObservationCounts(){
  Harness h;auto a=contact(1),b=contact(2);d::Id key{};key.fill(0x56);
  auto counts=take(h.responder->counts());require(counts.requests==0&&counts.replyPrepared==0&&counts.tokenRejected==0,"Responder observation did not start empty");
  auto token=h.token(a,key);counts=take(h.responder->counts());require(counts.requests==1&&counts.replyPrepared==1&&counts.tokenRejected==0&&counts.tokenBindings==1,"Actual get_peers token outcome was not counted");
  (void)h.announce(a,key,token,true);auto read=*take(h.send(query(d::Method::GetPeers,b,key),&b));auto peers=take(d::decode(read.payload));require(peers.peers.size()==1&&peers.peers.front().hash==a.hash&&peers.peers.front().seed,"Observation baseline did not preserve actual seed readback");
  const auto before=take(h.responder->counts());const auto calls=h.calls;auto bad=query(d::Method::AnnouncePeer,b,key);bad.token=token;bad.token[0]^=1;
  require(!take(h.send(bad,nullptr)),"Bad raw token unexpectedly prepared a reply");counts=take(h.responder->counts());
  require(counts.requests==before.requests+1&&counts.replyPrepared==before.replyPrepared&&counts.tokenRejected==before.tokenRejected+1,"Exact token-binding miss was not isolated in observations");
  require(h.calls==calls&&counts.tokenBindings==before.tokenBindings&&counts.peers.keys==before.peers.keys&&counts.peers.values==before.peers.values,"Bad-token observation changed routing/binding/store state");
  // The old owning snapshot must remain independent of subsequent outcomes.
  require(before.requests==3&&before.replyPrepared==3&&before.tokenRejected==0,"Owning observation changed after receive");
  const auto rejected=counts;
  const auto unchanged=[&]{auto now=take(h.responder->counts());return now.requests==rejected.requests&&now.replyPrepared==rejected.replyPrepared&&now.tokenRejected==rejected.tokenRejected&&now.tokenBindings==rejected.tokenBindings&&now.peers.keys==rejected.peers.keys&&now.peers.values==rejected.peers.values;};
  h.enabled=false;auto noSource=h.send(bad,nullptr);require(!noSource&&noSource.error()==s::ResponderError::NoAuthority&&unchanged(),"NoAuthority changed responder observations");h.enabled=true;
  require(!take(h.send(bad,nullptr,1000,false,{true,false}))&&unchanged(),"Sleeping decline was miscounted as a rejected token");
  auto bytes=wire(bad);s::Incoming wrongPort{bytes,nullptr,0,h.local.port};require(!take(h.responder->receive(wrongPort,1000,{},[]{return true;},h.route))&&unchanged(),"Wrong destination port was miscounted as a rejected token");
  i::Bytes malformed{0xff};s::Incoming invalid{malformed,nullptr,0,static_cast<std::uint16_t>(h.local.port+1)};auto invalidResult=h.responder->receive(invalid,1000,{},[]{return true;},h.route);require(!invalidResult&&invalidResult.error()==s::ResponderError::Invalid&&unchanged(),"Malformed bytes changed guarded outcome counters");
  auto valid=*take(h.send(query(d::Method::GetPeers,b,key),&b));peers=take(d::decode(valid.payload));require(peers.peers.size()==1&&peers.peers.front().hash==a.hash&&peers.peers.front().seed,"Rejected-token observation changed valid store contents");
}
void observationGuardBoundaries(){
  auto a=contact(1);d::Id key{};key.fill(0x57);
  Harness lost;lost.route=[&](const d::Contact&,d::Method,const d::Id&,bool)->std::optional<s::RoutingSnapshot>{lost.enabled=false;return s::RoutingSnapshot{};};
  auto denied=lost.send(query(d::Method::GetPeers,a,key),&a);require(!denied&&denied.error()==s::ResponderError::NoAuthority,"Fixture did not lose actual callback authority");auto counts=take(lost.responder->counts());require(counts.requests==0&&counts.replyPrepared==0&&counts.tokenRejected==0&&counts.tokenBindings==0&&counts.peers.values==0,"Route callback source loss committed observation state");
  Harness nested;bool blocked=false;nested.route=[&](const d::Contact&,d::Method,const d::Id&,bool)->std::optional<s::RoutingSnapshot>{auto denied=nested.send(query(d::Method::Ping,a,key),&a);blocked=!denied&&denied.error()==s::ResponderError::Reentry;auto during=take(nested.responder->counts());require(during.requests==0&&during.replyPrepared==0&&during.tokenRejected==0,"Reentry changed observation counters");return s::RoutingSnapshot{};};require(take(nested.send(query(d::Method::Ping,a,key),&a)).has_value()&&blocked,"Reentry fixture did not finish its legitimate outer reply");counts=take(nested.responder->counts());require(counts.requests==1&&counts.replyPrepared==1&&counts.tokenRejected==0,"Nested refusal was counted as an outer terminal outcome");
  Harness foreign;bool refused=false;std::thread thread([&]{auto result=foreign.send(query(d::Method::Ping,a,key),&a);refused=!result&&result.error()==s::ResponderError::Thread;});thread.join();counts=take(foreign.responder->counts());require(refused&&counts.requests==0&&counts.replyPrepared==0&&counts.tokenRejected==0,"Foreign-thread call committed observation state");
}
int main(){try{normalWire();filtering();refusalsAndPolicy();routingAndLifetime();originalShapes();causalObservationCounts();observationGuardBoundaries();std::cout<<"PASS detached inbound responder controls\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
