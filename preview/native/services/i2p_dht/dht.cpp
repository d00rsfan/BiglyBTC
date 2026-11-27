// SPDX-License-Identifier: GPL-2.0-or-later
#include "dht.hpp"
#include "biglybtc/bencode.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <map>
#include <string_view>
#include <utility>
namespace biglybtc::services::i2p::dht {
namespace {
using B=core::Node;
std::unexpected<Error> bad(const char* text){return std::unexpected(Error{ErrorCode::InvalidFormat,text});}
View view(std::string_view s){return {reinterpret_cast<const Byte*>(s.data()),s.size()};}
std::string text(View v){return {reinterpret_cast<const char*>(v.data()),v.size()};}
std::string bytes(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
std::string bytes(View v){return bytes(text(v));}
std::string number(std::int64_t n){return "i"+std::to_string(n)+"e";}
std::string dict(const std::map<std::string,std::string>& fields){std::string out="d";for(const auto& [key,value]:fields){out+=bytes(key);out+=value;}return out+'e';}
bool is(const B* p,B::Type t){return p&&p->type==t;}
template<std::size_t N>bool fixed(const B* p,std::array<Byte,N>& a){if(!is(p,B::Type::Bytes)||p->bytes.size()!=N){return false;}std::copy(p->bytes.begin(),p->bytes.end(),a.begin());return true;}
bool flag(const B* p,bool& value){if(!p){value=false;return true;}if(!is(p,B::Type::Integer)){return false;}value=static_cast<std::uint32_t>(p->integer)!=0;return true;}
bool lessDistance(const Id& a,const Id& b,const Id& target){for(std::size_t i=0;i<a.size();++i){auto x=Byte(a[i]^target[i]),y=Byte(b[i]^target[i]);if(x!=y){return x<y;}}return false;}
bool allZero(const auto& v){return std::all_of(v.begin(),v.end(),[](auto b){return b==0;});}

template<class T> bool readInteger(const B& row,std::string_view key,T& output) {
  const auto* item=row.find(key);
  if (!is(item,B::Type::Integer)||!std::in_range<T>(item->integer)) { return false; }
  output=static_cast<T>(item->integer);
  return true;
}

bool readAzureusContact(const B& row,AzureusContact& output,bool anonymousAllowed) {
  auto& contact=output.contact;
  std::uint8_t protocol=0;
  if (!readInteger(row,"v",protocol)||!readInteger(row,"p",contact.port)
      ||!fixed(row.find("i"),contact.id)||!fixed(row.find("h"),contact.hash)) { return false; }
  output.protocol=protocol;
  if (validContact(contact)) { return true; }
  Id anonymousId{};
  anonymousId[5]=1;
  return anonymousAllowed&&!protocol&&contact.port==1&&contact.id==anonymousId&&allZero(contact.hash);
}

bool readAzureusValue(const B& row,AzureusValue& output) {
  const auto* wire=row.find("w");
  const auto* origin=row.find("o");
  AzureusContact contact;
  if (!is(wire,B::Type::Bytes)||wire->bytes.size()>512||!is(origin,B::Type::Dictionary)
      ||!readInteger(row,"v",output.version)||!readInteger(row,"t",output.created)
      ||!readInteger(row,"f",output.flags)||!readInteger(row,"l",output.lifetime)
      ||!readAzureusContact(*origin,contact,true)) { return false; }
  output.bytes.assign(wire->bytes.begin(),wire->bytes.end());
  if (validContact(contact.contact)) { output.origin=std::move(contact); }
  return true;
}

Result<AzureusMessage> readAzureusMessage(const B& row,Kind kind) {
  AzureusMessage result;
  if (row.type!=B::Type::Dictionary||!readInteger(row,"_i",result.instance)
      ||!readInteger(row,"_f",result.flags)) { return bad("Invalid I2P Azureus envelope"); }
  if (kind==Kind::Query) {
    std::int32_t method=0;
    if (!readInteger(row,"_m",method)||method!=2) {
      return std::unexpected(Error{ErrorCode::Unsupported,"Unsupported I2P Azureus method"});
    }
    AzureusFindValue find;
    if (!fixed(row.find("h"),find.key)||!readInteger(row,"m",find.maximum)||find.maximum<0
        ||!readInteger(row,"f",find.flags)||!readInteger(row,"_t",find.now)) { return bad("Invalid I2P Azureus FIND_VALUE"); }
    result.payload=find;
    return result;
  }

  const auto* contacts=row.find("c");
  const auto* values=row.find("v");
  const auto* blocked=row.find("b");
  if (unsigned(bool(contacts))+unsigned(bool(values))+unsigned(bool(blocked))!=1) {
    return bad("Ambiguous I2P Azureus FIND_VALUE reply");
  }
  if (contacts) {
    if (!is(contacts,B::Type::List)||contacts->children.size()>MaximumContacts) { return bad("Invalid I2P Azureus contact list"); }
    auto& found=result.payload.emplace<std::vector<AzureusContact>>();
    for (const auto& entry:contacts->children) {
      AzureusContact item;
      if (!readAzureusContact(entry,item,false)) { return bad("Invalid I2P Azureus contact"); }
      found.push_back(std::move(item));
    }
  } else if (values) {
    auto& hit=result.payload.emplace<AzureusValueHit>();
    if (!is(values,B::Type::List)||values->children.size()>MaximumPeers||!readInteger(row,"d",hit.diversification)) {
      return bad("Invalid I2P Azureus value list");
    }
    for (const auto& entry:values->children) {
      AzureusValue item;
      if (!readAzureusValue(entry,item)) { return bad("Invalid I2P Azureus value"); }
      hit.values.push_back(std::move(item));
    }
  } else {
    const auto* key=row.find("k");
    const auto* signature=row.find("s");
    if (!is(blocked,B::Type::Integer)||blocked->integer!=1||!is(key,B::Type::Bytes)||key->bytes.empty()
        ||!is(signature,B::Type::Bytes)||signature->bytes.empty()) { return bad("Invalid I2P Azureus key block"); }
    auto& block=result.payload.emplace<AzureusKeyBlock>();
    block.key.assign(key->bytes.begin(),key->bytes.end());
    block.signature.assign(signature->bytes.begin(),signature->bytes.end());
  }
  return result;
}

std::string writeAzureusContact(const AzureusContact& entry) {
  return dict({{"h",bytes(entry.contact.hash)},{"i",bytes(entry.contact.id)},
      {"p",number(entry.contact.port)},{"v",number(entry.protocol)}});
}

std::string writeAzureusValue(const AzureusValue& entry) {
  AzureusContact anonymous;
  anonymous.contact.port=1;
  anonymous.contact.id[5]=1;
  return dict({{"v",number(entry.version)},{"t",number(entry.created)},{"w",bytes(entry.bytes)},
      {"o",writeAzureusContact(entry.origin?*entry.origin:anonymous)},
      {"f",number(entry.flags)},{"l",number(entry.lifetime)}});
}

std::string writeAzureusMessage(const AzureusMessage& message) {
  std::map<std::string,std::string> fields{{"_i",number(message.instance)},{"_f",number(message.flags)}};
  if (const auto* find=std::get_if<AzureusFindValue>(&message.payload)) {
    fields["_m"]=number(2);
    fields["_t"]=number(find->now);
    fields["h"]=bytes(find->key);
    fields["m"]=number(find->maximum);
    fields["f"]=number(find->flags);
  } else if (const auto* contacts=std::get_if<std::vector<AzureusContact>>(&message.payload)) {
    std::string list="l";
    for (const auto& entry:*contacts) { list+=writeAzureusContact(entry); }
    fields["c"]=list+'e';
  } else if (const auto* hit=std::get_if<AzureusValueHit>(&message.payload)) {
    std::string list="l";
    for (const auto& entry:hit->values) { list+=writeAzureusValue(entry); }
    fields["v"]=list+'e';
    fields["d"]=number(hit->diversification);
  } else {
    const auto& blocked=std::get<AzureusKeyBlock>(message.payload);
    fields["b"]=number(1);
    fields["k"]=bytes(blocked.key);
    fields["s"]=bytes(blocked.signature);
  }
  return dict(fields);
}
}
bool validContact(const Contact& c)noexcept{
  if(!c.port||c.port>=65535||allZero(c.hash)||allZero(c.id)){return false;}
  for(unsigned i=0;i<4;++i){if(c.id[i]!=c.hash[i]){return false;}}
  if(Byte(c.id[4]^Byte(c.port>>8))!=c.hash[4]||Byte(c.id[5]^Byte(c.port))!=c.hash[5]){return false;}
  return !c.destination||(c.destination->hash==c.hash&&!c.destination->wire.empty());
}
Result<Id> makeNodeId(const Hash& hash,std::uint16_t port,View random){
  if(!port||port>=65535||random.size()!=14||allZero(hash)){return bad("Secure NID requires hash/port/random14");}
  Id id{};std::copy_n(hash.begin(),6,id.begin());id[4]^=Byte(port>>8);id[5]^=Byte(port);std::copy(random.begin(),random.end(),id.begin()+6);return id;
}
Result<Contact> decodeContact(View wire){
  if(wire.size()!=54){return bad("I2P DHT contact must contain54bytes");}Contact c;
  std::copy_n(wire.begin(),20,c.id.begin());std::copy_n(wire.begin()+20,32,c.hash.begin());c.port=std::uint16_t((unsigned(wire[52])<<8)|wire[53]);
  if(!validContact(c)){return bad("I2P secure NID/hash/port mismatch");}return c;
}
Result<Bytes> encodeContact(const Contact& c){
  if(!validContact(c)){return bad("Invalid I2P DHT contact");}Bytes out(c.id.begin(),c.id.end());out.insert(out.end(),c.hash.begin(),c.hash.end());out.push_back(Byte(c.port>>8));out.push_back(Byte(c.port));return out;
}
Result<Message> decode(View wire){
  if(wire.size()>MaximumPacket){return bad("DHT packet exceeds10KiB policy");}
  auto document=core::Document::parse(text(wire),{MaximumPacket,MaximumPacket,1024,8});if(!document){return bad("Invalid/duplicate-key DHT bencode");}
  const auto& root=document->root();if(root.type!=B::Type::Dictionary){return bad("DHT root must be dictionary");}
  const auto* tx=root.find("t");const auto* type=root.find("y");
  if(!is(tx,B::Type::Bytes)||tx->bytes.empty()||tx->bytes.size()>16||!is(type,B::Type::Bytes)){return bad("DHT correlation/type invalid");}
  Message m;m.wakeRequested=root.find("z")!=nullptr;m.transaction.assign(tx->bytes.begin(),tx->bytes.end());const B* body=nullptr;
  if(type->bytes=="q"){
    m.kind=Kind::Query;const auto* method=root.find("q");body=root.find("a");if(!is(method,B::Type::Bytes)){return bad("DHT query method missing");}
    if(method->bytes=="ping"){m.method=Method::Ping;}
    else if(method->bytes=="find_node"){m.method=Method::FindNode;}
    else if(method->bytes=="get_peers"){m.method=Method::GetPeers;}
    else if(method->bytes=="announce_peer"){m.method=Method::AnnouncePeer;}
    else if(method->bytes=="azrequest"){m.method=Method::AzureusFindValue;}
    else{return std::unexpected(Error{ErrorCode::Unsupported,"Unsupported DHT method"});}
  }else if(type->bytes=="r"){m.kind=Kind::Reply;body=root.find("r");}
  else if(type->bytes=="e"){
    m.kind=Kind::Error;const auto* e=root.find("e");if(!is(e,B::Type::List)||e->children.size()!=2||e->children[0].type!=B::Type::Integer||e->children[0].integer<0||e->children[0].integer>65535||e->children[1].type!=B::Type::Bytes||e->children[1].bytes.size()>256){return bad("DHT error tuple invalid");}m.errorCode=int(e->children[0].integer);return m;
  }else{return bad("Unknown DHT message kind");}
  if(!is(body,B::Type::Dictionary)){return bad("DHT body must be dictionary");}
  if(m.kind==Kind::Query&&m.method==Method::AnnouncePeer){
    const auto* id=body->find("id");if(id&&!is(id,B::Type::Bytes)){return bad("DHT query NID type invalid");}m.idIsTwentyBytes=id&&fixed(id,m.id);
  }else if(!fixed(body->find("id"),m.id)){return bad("DHT responder/query NID invalid");}
  if(const auto* v=body->find("_v")){
    if(!is(v,B::Type::Bytes)){return bad("DHT version must be bytes");}
    if(v->bytes.starts_with("AZ")){const auto s=v->bytes.substr(2);auto result=std::from_chars(s.data(),s.data()+s.size(),m.version);if(s.empty()||result.ec!=std::errc{}||result.ptr!=s.data()+s.size()||m.version>255){return bad("DHT AZ version invalid");}}
  }
  if(const auto* p=body->find("token")){
    if(m.kind==Kind::Query&&m.method==Method::AnnouncePeer){
      // Signed announce ignores this field; raw invalid/absent tokens remain
      // empty and cannot match the responder's original8-byte token map.
      if(is(p,B::Type::Bytes)&&!p->bytes.empty()&&p->bytes.size()<=64){m.token.assign(p->bytes.begin(),p->bytes.end());}
    }else{if(!is(p,B::Type::Bytes)||p->bytes.empty()||p->bytes.size()>64){return bad("DHT token invalid");}m.token.assign(p->bytes.begin(),p->bytes.end());}
  }
  if (m.method==Method::AzureusFindValue || (m.kind==Kind::Reply && body->find("p"))) {
    const auto* payload=body->find("p");
    if (!payload||m.version<MinimumAzureusProtocol||body->find("nodes")||body->find("values")||body->find("flags")) {
      return bad("Invalid I2P Azureus message body");
    }
    auto azureus=readAzureusMessage(*payload,m.kind);
    if (!azureus) { return std::unexpected(azureus.error()); }
    m.method=Method::AzureusFindValue;
    if (const auto* request=std::get_if<AzureusFindValue>(&azureus->payload)) { m.target=request->key; }
    m.azureus=std::move(*azureus);
  }
  if(m.kind==Kind::Query){
    if(m.method==Method::FindNode){const auto* target=body->find("target");if(!is(target,B::Type::Bytes)){return bad("find_node target type invalid");}if(!fixed(target,m.target)){m.findNodeTarget=Bytes(target->bytes.begin(),target->bytes.end());}}
    if((m.method==Method::GetPeers||m.method==Method::AnnouncePeer)&&!fixed(body->find("info_hash"),m.target)){return bad("DHT info_hash invalid");}
    if(m.method==Method::GetPeers&&!flag(body->find("noseed"),m.noSeed)){return bad("get_peers noseed invalid");}
    if(m.method==Method::AnnouncePeer&&!flag(body->find("seed"),m.seed)){return bad("announce_peer seed invalid");} // Advertised TCP port is never read by original transport.
    return m;
  }
  if(const auto* n=body->find("nodes")){
    if(!is(n,B::Type::Bytes)||n->bytes.size()%54||n->bytes.size()/54>MaximumContacts){return bad("DHT nodes geometry invalid");}m.hasNodes=true;
    for(std::size_t i=0;i<n->bytes.size();i+=54){auto c=decodeContact(view(n->bytes.substr(i,54)));if(!c){return std::unexpected(c.error());}m.nodes.push_back(std::move(*c));}
  }
  if(const auto* values=body->find("values")){
    if(m.hasNodes||!is(values,B::Type::List)||values->children.size()>MaximumPeers){return bad("DHT values type/count invalid");}m.hasPeers=true;
    const auto* flags=body->find("flags");if(flags&&(!is(flags,B::Type::Bytes)||flags->bytes.size()!=(values->children.size()+7)/8)){return bad("DHT peer flag bitmap invalid");}
    if(flags&&!values->children.empty()&&values->children.size()%8&&(Byte(flags->bytes.back())&((1U<<(8-values->children.size()%8))-1U))){return bad("DHT unused peer flags nonzero");}
    for(std::size_t i=0;i<values->children.size();++i){Peer p;if(!fixed(&values->children[i],p.hash)||allZero(p.hash)){return bad("DHT peer hash invalid");}p.seed=flags&&(Byte(flags->bytes[i/8])&(1U<<(7-(i%8))));m.peers.push_back(p);}
  }else if(body->find("flags")){return bad("DHT flags without peer values");}
  return m;
}
namespace {
Result<Bytes> encodeVersioned(const Message& m,bool ordinary){
  if(m.transaction.empty()||m.transaction.size()>16||m.token.size()>64||m.nodes.size()>MaximumContacts||m.peers.size()>MaximumPeers){return bad("DHT message capacity invalid");}
  if(m.kind!=Kind::Query&&m.kind!=Kind::Reply&&m.kind!=Kind::Error){return bad("DHT message kind invalid");}
  if(m.kind==Kind::Query&&m.method!=Method::Ping&&m.method!=Method::FindNode&&m.method!=Method::GetPeers&&m.method!=Method::AnnouncePeer&&m.method!=Method::AzureusFindValue){return bad("DHT method invalid");}
  if (m.azureus) {
    if (m.method!=Method::AzureusFindValue || m.kind==Kind::Error || m.hasNodes || m.hasPeers
        || (m.kind==Kind::Query)!=std::holds_alternative<AzureusFindValue>(m.azureus->payload)) {
      return bad("Inconsistent I2P Azureus envelope");
    }
  } else if (m.method==Method::AzureusFindValue) { return bad("Missing I2P Azureus envelope"); }
  if(m.kind==Kind::Query&&m.method==Method::AnnouncePeer&&m.token.empty()){return bad("Outgoing raw announce requires token");} // Preserve existing client encode contract.
  std::map<std::string,std::string> outer{{"t",bytes(m.transaction)}};
  if(m.kind==Kind::Error){outer["y"]=bytes("e");outer["e"]="l"+number(m.errorCode)+bytes("DHT request failed")+"e";}
  else{
    std::map<std::string,std::string> body{{"_v",bytes("AZ4")},{"id",bytes(m.id)}};
    if(!m.token.empty()){body["token"]=bytes(m.token);}
    if (m.azureus) { body["p"]=writeAzureusMessage(*m.azureus); }
    if(m.kind==Kind::Query){
      outer["y"]=bytes("q");if(m.wakeRequested){outer["z"]=number(0);}const char* name="ping";
      if(m.method==Method::FindNode){name="find_node";body["target"]=bytes(m.findNodeTarget?View(*m.findNodeTarget):View(m.target));}
      else if(m.method==Method::GetPeers){name="get_peers";body["info_hash"]=bytes(m.target);if(m.noSeed){body["noseed"]=number(1);}}
      else if(m.method==Method::AnnouncePeer){name="announce_peer";body["info_hash"]=bytes(m.target);body["port"]=number(6881);body["seed"]=number(m.seed?1:0);}
      else if(m.method==Method::AzureusFindValue){name="azrequest";}
      outer["q"]=bytes(name);outer["a"]=dict(body);
    }else{
      outer["y"]=bytes("r");if(m.hasNodes){Bytes contacts;for(const auto& c:m.nodes){auto n=encodeContact(c);if(!n){return std::unexpected(n.error());}contacts.insert(contacts.end(),n->begin(),n->end());}body["nodes"]=bytes(contacts);}
      if(m.hasPeers){std::string values="l";Bytes flags((m.peers.size()+7)/8);for(std::size_t i=0;i<m.peers.size();++i){values+=bytes(m.peers[i].hash);if(m.peers[i].seed){flags[i/8]|=Byte(1U<<(7-(i%8)));}}body["values"]=values+'e';if(!ordinary){body["flags"]=bytes(flags);}}
      outer["r"]=dict(body);
    }
  }
  auto result=dict(outer);Bytes out(result.begin(),result.end());auto checked=decode(out);if(!checked){return std::unexpected(checked.error());}return out;
}
} // namespace
Result<Bytes> encode(const Message& m){return encodeVersioned(m,false);}
Result<Bytes> encodeReply(const Message& m,PeerReplyMode mode){
  if(m.kind!=Kind::Reply||(mode!=PeerReplyMode::Azureus&&mode!=PeerReplyMode::Ordinary)){return bad("Explicit DHT reply projection required");}
  return encodeVersioned(m,mode==PeerReplyMode::Ordinary);
}

bool Lookup::offer(const Contact& contact){
  if(!validContact(contact)){++rejected_;return false;}
  for(const auto& n:nodes_){if(n.contact.id==contact.id||n.contact.hash==contact.hash){if(n.contact.id!=contact.id||n.contact.hash!=contact.hash||n.contact.port!=contact.port){++rejected_;}return false;}}
  if(nodes_.size()==MaximumContacts){++rejected_;return false;}Node n;n.contact=contact;nodes_.push_back(std::move(n));return true;
}
bool Lookup::start(std::uint64_t operation,const LookupOptions& options,std::span<const Contact> seeds,std::int64_t now){
  if(running()||!operation||allZero(options.target)||now<0||!options.timeoutMilliseconds||options.timeoutMilliseconds>120000||!options.parallelism||options.parallelism>3||seeds.empty()||seeds.size()>20||now>std::numeric_limits<std::int64_t>::max()-options.timeoutMilliseconds){return false;}
  if (options.azureus&&(options.announce||!options.azureus->maximum||options.azureus->maximum>MaximumPeers)) { return false; }
  for(const auto& s:seeds){if(!validContact(s)){return false;}}
  options_=options;operation_=operation;phase_=Phase::Running;end_=now+options.timeoutMilliseconds;last_=now;nodes_.clear();peers_.clear();azureusValues_.clear();rejected_=0;publishing_=false;
  nodes_.reserve(MaximumContacts);peers_.reserve(MaximumPeers);for(const auto& s:seeds){offer(s);}return true;
}
void Lookup::cancel()noexcept{if(running()){phase_=Phase::Cancelled;azureusValues_.clear();}}
void Lookup::close()noexcept{phase_=Phase::Closed;azureusValues_.clear();}
void Lookup::tick(std::int64_t now)noexcept{
  if(!running()){return;}if(now<last_){phase_=Phase::Failed;return;}last_=now;if(now>=end_){phase_=Phase::TimedOut;return;}
  for(auto& n:nodes_){if((n.phase==NodePhase::Queued||n.phase==NodePhase::Sent||n.phase==NodePhase::Resolving)&&now>=n.end){n.phase=NodePhase::Failed;}}settle();
}
void Lookup::settle()noexcept{
  if(!running()){return;}
  for(const auto& n:nodes_){if(n.phase==NodePhase::Candidate||n.phase==NodePhase::Resolving||n.phase==NodePhase::Queued||n.phase==NodePhase::Sent){return;}}
  if(options_.announce&&!publishing_){
    // Original helper stores at20; finish candidate discovery before choosing
    // the closest token-owning respondents. No arbitrary token/cached forward.
    publishing_=true;std::array<std::size_t,MaximumContacts> order{};std::size_t count=0;
    for(std::size_t i=0;i<nodes_.size();++i){if(nodes_[i].phase==NodePhase::Replied&&!nodes_[i].token.empty()){order[count++]=i;}}
    std::sort(order.begin(),order.begin()+static_cast<std::ptrdiff_t>(count),[&](auto a,auto b){return lessDistance(nodes_[a].contact.id,nodes_[b].contact.id,options_.target);});
    for(std::size_t i=0;i<std::min<std::size_t>(20,count);++i){nodes_[order[i]].publish=true;}
  }
  for(const auto& n:nodes_){if(n.publish&&n.phase==NodePhase::Replied&&!n.announced&&!n.announcing){return;}}
  phase_=Phase::Complete;
}
std::optional<std::size_t> Lookup::next(std::int64_t now){
  tick(now);if(!running()){return {};}
  const auto active=std::count_if(nodes_.begin(),nodes_.end(),[](const auto& n){return n.phase==NodePhase::Resolving||n.phase==NodePhase::Queued||n.phase==NodePhase::Sent;});if(std::size_t(active)>=options_.parallelism){return {};}
  std::optional<std::size_t> best;
  for(std::size_t i=0;i<nodes_.size();++i){auto& n=nodes_[i];const bool announce=n.publish&&n.phase==NodePhase::Replied&&!n.token.empty()&&!n.announced&&!n.announcing;
    if(n.phase!=NodePhase::Candidate&&!announce){continue;}if(!best||lessDistance(n.contact.id,nodes_[*best].contact.id,options_.target)){best=i;}}
  return best;
}
bool Lookup::resolving(std::size_t index){if(!running()||index>=nodes_.size()||nodes_[index].phase!=NodePhase::Candidate){return false;}auto& n=nodes_[index];n.phase=NodePhase::Resolving;n.end=last_+std::min<std::int64_t>(end_-last_,30000);return true;}
bool Lookup::resolved(std::size_t index,const Destination& destination){
  if(!running()||index>=nodes_.size()||nodes_[index].phase!=NodePhase::Resolving||nodes_[index].contact.hash!=destination.hash){return false;}
  nodes_[index].contact.destination=destination;nodes_[index].phase=NodePhase::Candidate;return true;
}
bool Lookup::queued(std::size_t index,Bytes tx,std::uint64_t ticket,std::int64_t now){
  tick(now);if(!running()||index>=nodes_.size()||!ticket||tx.size()!=8){return false;}auto& n=nodes_[index];
  if(n.phase!=NodePhase::Candidate&&!(n.publish&&n.phase==NodePhase::Replied&&!n.token.empty()&&!n.announced&&!n.announcing)){return false;}
  if(std::any_of(nodes_.begin(),nodes_.end(),[&](const auto& old){return old.ticket==ticket||old.transaction==tx;})){return false;}
  n.announcing=n.phase==NodePhase::Replied;n.transaction=std::move(tx);n.ticket=ticket;n.phase=NodePhase::Queued;n.end=now+std::min<std::int64_t>(end_-now,30000);return true;
}
bool Lookup::dispatched(std::uint64_t ticket,bool written,std::int64_t now){
  tick(now);if(!running()){return false;}for(auto& n:nodes_){if(n.ticket==ticket&&n.phase==NodePhase::Queued){n.phase=written?NodePhase::Sent:NodePhase::Failed;settle();return true;}}return false;
}
void Lookup::failed(std::size_t index)noexcept{if(index<nodes_.size()){nodes_[index].phase=NodePhase::Failed;}settle();}
bool Lookup::reply(const Message& message,std::uint16_t port,std::int64_t now){
  tick(now);if(!running()){return false;}
  if(message.transaction.empty()||message.transaction.size()>16||message.token.size()>64||message.nodes.size()>MaximumContacts||message.peers.size()>MaximumPeers){++rejected_;return false;}
  auto it=std::find_if(nodes_.begin(),nodes_.end(),[&](const auto& n){return n.phase==NodePhase::Sent&&n.transaction==message.transaction&&n.contact.port==port;});
  if(it==nodes_.end()||(message.kind!=Kind::Reply&&message.kind!=Kind::Error)){++rejected_;return false;}
  if(message.kind==Kind::Error){it->phase=NodePhase::Failed;settle();return true;}
  if (bool(message.azureus)!=bool(options_.azureus)) { ++rejected_;return false; }
  if(message.id!=it->contact.id){++rejected_;return false;}it->phase=NodePhase::Replied;
  if (message.azureus) {
    const auto& payload=message.azureus->payload;
    if (const auto* contacts=std::get_if<std::vector<AzureusContact>>(&payload)) {
      for (const auto& candidate:*contacts) {
        if (candidate.protocol>=MinimumAzureusProtocol) { offer(candidate.contact); }
      }
    } else if (const auto* hit=std::get_if<AzureusValueHit>(&payload)) {
      for (const auto& entry:hit->values) {
        if (azureusValues_.size()>=options_.azureus->maximum) { break; }
        if (std::ranges::none_of(azureusValues_,[&](const auto& previous){return previous.bytes==entry.bytes;})) {
          azureusValues_.push_back(entry);
        }
      }
    }
    settle();
    return true;
  }
  if(it->announcing){it->announced=true;it->announcing=false;settle();return true;}
  it->token=message.token;
  // Reserve128 at start prevents contact-vector reallocation under this copy.
  for(const auto& c:message.nodes){offer(c);}
  for(const auto& p:message.peers){if(std::none_of(peers_.begin(),peers_.end(),[&](const auto& old){return p.hash==old.hash;})&&peers_.size()<MaximumPeers){peers_.push_back(p);}}
  settle();return true;
}
Snapshot Lookup::snapshot()const{
  Snapshot s;s.operation=operation_;s.phase=phase_;s.target=options_.target;s.peers=peers_;s.contacts=nodes_.size();s.rejected=rejected_;
  s.azureusValues=azureusValues_;
  for(const auto& n:nodes_){if(n.phase==NodePhase::Replied){++s.replied;}if(n.announced){++s.announced;}}return s;
}
} // namespace biglybtc::services::i2p::dht
