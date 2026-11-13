// SPDX-License-Identifier: GPL-2.0-or-later
// General MsgSyncHandler SYNC and BuddyPluginBeta payload/history formats.
#include "room.hpp"
#include "message_origin.hpp"
#include "ordering.hpp"
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <algorithm>
#include <numeric>
#include <limits>
namespace biglybtc::services::msgsync {
namespace {
std::string bytes(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
std::string number(std::int64_t n){return "i"+std::to_string(n)+"e";}
template<std::size_t N>std::string_view view(const std::array<std::uint8_t,N>& a){return {reinterpret_cast<const char*>(a.data()),N};}
std::span<const std::uint8_t> span(std::string_view s){return {reinterpret_cast<const std::uint8_t*>(s.data()),s.size()};}
Result<core::Document> dictionary(std::string s){auto d=core::Document::parse(std::move(s));if(!d||d->root().type!=core::Node::Type::Dictionary){return std::unexpected(Error::Input);}return std::move(*d);}
const core::Node* field(const core::Node& n,std::string_view key,core::Node::Type t){const auto* v=n.find(key);return v&&v->type==t?v:nullptr;}
Result<std::string> encoded(const core::Node& n){auto v=core::encodeValue(n);if(!v){return std::unexpected(Error::Input);}return std::move(*v);}
}
Result<RoomPolicy> roomPolicy(std::string_view key){
 RoomPolicy out;out.baseKey=key;const auto at=key.rfind('[');
 if(at==std::string_view::npos||!key.ends_with(']')){return out;}
 const auto suffix=key.substr(at+1,key.size()-at-2);
 if(suffix.find("pk=")==std::string_view::npos&&suffix.find("ro=")==std::string_view::npos){return out;}
 out.baseKey=key.substr(0,at);bool havePk=false,haveRo=false;
 for(std::size_t pos=0;pos<=suffix.size();){
  const auto end=suffix.find('&',pos);const auto arg=suffix.substr(pos,end==std::string_view::npos?suffix.size()-pos:end-pos);
  const auto equal=arg.find('=');if(equal==std::string_view::npos){return std::unexpected(Error::Input);}
  const auto name=arg.substr(0,equal),value=arg.substr(equal+1);
  if(name=="pk"){
   if(havePk||value.empty()){return std::unexpected(Error::Input);}havePk=true;
   std::string decoded;unsigned buffer=0,bits=0;
   for(char c:value){if(c>='a'&&c<='z'){c=static_cast<char>(c-'a'+'A');}
    const auto n=std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567").find(c);if(n==std::string_view::npos){return std::unexpected(Error::Input);}
    buffer=(buffer<<5)|static_cast<unsigned>(n);bits+=5;if(bits>=8){bits-=8;decoded+=static_cast<char>(buffer>>bits);}
   }
   const bool compressed=decoded.size()==25&&(decoded.front()==2||decoded.front()==3);
   const bool uncompressed=decoded.size()==49&&decoded.front()==4;
   if((bits&&(buffer&((1U<<bits)-1U)))||(!compressed&&!uncompressed)){return std::unexpected(Error::Input);}
   out.manager=std::move(decoded);
  }else if(name=="ro"){
   if(haveRo){return std::unexpected(Error::Input);}haveRo=true;out.readOnly=value=="1";
  }
  if(end==std::string_view::npos){break;}pos=end+1;
 }
 if(out.readOnly&&!out.manager){return std::unexpected(Error::Input);}
 return out;
}
Room::Room(std::string key,Uid uid,plugins::msgsync::MessageSigner signer,plugins::msgsync::ChannelKeys keys,std::string contact,bool ro)
 :userKey_(std::move(key)),uid_(uid),signer_(std::move(signer)),keys_(keys),localContact_(std::move(contact)),readOnly_(ro){}
Room::~Room(){OPENSSL_cleanse(keys_.general.data(),keys_.general.size());}
Room::Room(Room&&)noexcept=default;Room& Room::operator=(Room&&)noexcept=default;
Result<Room> Room::open(std::string key,Uid uid,plugins::msgsync::MessageSigner signer,std::string contact,bool ro){
 if(key.empty()||key.size()>4096||signer.publicKey().empty()||!dictionary(contact)){return std::unexpected(Error::Input);}
 auto policy=roomPolicy(key);if(!policy){return std::unexpected(policy.error());}
 const bool managedReadOnly=policy->readOnly;
 ro=ro||(managedReadOnly&&policy->manager!=std::optional<std::string>{std::string(signer.publicKey())});
 auto keys=plugins::msgsync::deriveChannelKeys(span(key));if(!keys){return std::unexpected(Error::Crypto);}
 Room out(std::move(key),uid,std::move(signer),*keys,std::move(contact),ro);out.managedReadOnly_=managedReadOnly;out.manager_=std::move(policy->manager);return out;
}
Result<std::string> Room::request(std::int64_t now){
 auto entries=ledger_.snapshot(now);if(!entries){return std::unexpected(entries.error());}auto bloom=ledger_.buildBloom({});if(!bloom){return std::unexpected(bloom.error());}
 SyncRequest r{5,uid_,std::array<std::uint8_t,6>{},bloom->random,std::move(bloom->filter),static_cast<std::int32_t>(entries->size()),ledger_.newMessages(),{}};
 if(RAND_bytes(r.requestId->data(),static_cast<int>(r.requestId->size()))!=1){return std::unexpected(Error::Crypto);}
 if(!entries->empty()){r.oldestAgeSeconds=std::max(0,entries->front().message.ageSeconds);}
 auto wire=encodeSyncRequest(r);if(!wire){return std::unexpected(wire.error());}auto result=plugins::msgsync::encrypt(plugins::msgsync::Kind::General,keys_.general,span(*wire));if(!result){return std::unexpected(Error::Crypto);}return std::move(*result);
}
Result<std::string> Room::answer(std::string envelope,std::int64_t now){
 auto result=answerDetailed(std::move(envelope),now);if(!result){return std::unexpected(result.error());}return std::move(result->envelope);
}
Result<SyncAnswer> Room::answerDetailed(std::string envelope,std::int64_t now,std::optional<NodeHint> hint){
 if(envelope.size()>MaximumTransferBytes){return std::unexpected(Error::Limit);}auto plain=plugins::msgsync::decrypt(plugins::msgsync::Kind::General,keys_.general,span(envelope));if(!plain){return std::unexpected(Error::Crypto);}
 auto req=decodeSyncRequest(std::move(*plain));if(!req){return std::unexpected(req.error());}auto fresh=replay_.remember(*req);if(!fresh||!*fresh){return std::unexpected(Error::Input);}
 SyncReply reply;reply.hint=std::move(hint);if(req->uid==uid_){reply.status=2;}else{
  auto messages=ledger_.snapshot(now);if(!messages){return std::unexpected(messages.error());}std::size_t content=0;
  for(auto& entry:*messages){auto& message=entry.message;auto masked=maskBloomKey(message.signature,req->random);if(!masked){return std::unexpected(masked.error());}
   if(req->bloom.contains(*masked)){continue;}for(char& c:*masked){c=static_cast<char>(static_cast<unsigned char>(c)^255U);}if(req->bloom.contains(*masked)){continue;}
   if(req->oldestAgeSeconds&&*req->oldestAgeSeconds>0&&static_cast<std::int64_t>(message.ageSeconds)-*req->oldestAgeSeconds>=300){continue;}
   if(req->version<5&&message.control){continue;}const auto size=message.content.size()+(message.control?message.control->size():0);
   if(size>MaximumReplyContentBytes-content){++reply.moreToCome;continue;}content+=size;message.publicKey=entry.senderKey;
   if(message.uid==uid_&&entry.senderKey==signer_.publicKey()){message.encodedContact=localContact_;}
   else if(!message.encodedContact){for(const auto& key:known_){if(key.uid==message.uid&&key.key==entry.senderKey){message.encodedContact=key.contact;break;}}}
   reply.messages.push_back(std::move(message));
  }
 }
 if(reply.hint&&(reply.hint->uid==req->uid||reply.hint->uid==uid_)){reply.hint.reset();}
 auto wire=encodeSyncReply(reply);if(!wire){return std::unexpected(wire.error());}auto result=plugins::msgsync::encrypt(plugins::msgsync::Kind::General,keys_.general,span(*wire));if(!result){return std::unexpected(Error::Crypto);}return SyncAnswer{std::move(*result),req->uid,req->uid==uid_};
}
Result<ReceiveSummary> Room::admit(std::vector<Message> incoming,std::int64_t now,bool loading){
 ReceiveSummary out;
 for(auto& message:incoming){
  std::vector<std::string_view> candidates;
  if(message.publicKey){candidates.push_back(*message.publicKey);}else{
   if(message.uid==uid_){candidates.push_back(signer_.publicKey());}
   for(const auto& k:known_){if(k.uid==message.uid){candidates.push_back(k.key);}}
   for(const auto& k:known_){if(k.uid!=message.uid){candidates.push_back(k.key);}}
  }
  bool accepted=false;
  for(const auto candidate:candidates){
   if(managedReadOnly_&&(!manager_||candidate!=*manager_)){continue;}
   if(!message.encodedContact){if(candidate==signer_.publicKey()){message.encodedContact=localContact_;}else{for(const auto& old:known_){if(old.key==candidate){message.encodedContact=old.contact;break;}}}}
   if(!message.encodedContact||!dictionary(*message.encodedContact)){continue;}
   auto verified=verifyMessage(message,candidate);if(!verified){continue;}
   const std::string key(candidate);const auto contact=message.encodedContact.value_or(std::string{});auto added=ledger_.add(std::move(*verified),now,loading);if(!added){return std::unexpected(added.error());}
   if(*added==AddOutcome::Added){++out.added;}else{++out.duplicates;}accepted=true;
   if(!contact.empty()&&dictionary(contact)){auto found=std::find_if(known_.begin(),known_.end(),[&](const auto& k){return k.uid==message.uid&&k.key==key;});
    if(found!=known_.end()){known_.erase(found);}if(known_.size()==128){known_.pop_front();}known_.push_back({message.uid,key,contact});}
   break;
  }
  if(!accepted){++out.rejected;}
 }
 return out;
}
Result<ReceiveSummary> Room::receive(std::string envelope,std::int64_t now){
 if(envelope.size()>MaximumTransferBytes){return std::unexpected(Error::Limit);}auto plain=plugins::msgsync::decrypt(plugins::msgsync::Kind::General,keys_.general,span(envelope));if(!plain){return std::unexpected(Error::Crypto);}
 auto reply=decodeSyncReply(std::move(*plain));if(!reply||reply->status!=1){return std::unexpected(reply?Error::Input:reply.error());}auto result=admit(std::move(reply->messages),now);if(result){result->more=reply->moreToCome;result->hint=std::move(reply->hint);}return result;
}
Result<PreparedSend> Room::prepareSend(std::string text,std::string nickname,std::int64_t now,std::optional<std::int32_t> origin){
 if(readOnly_||text.empty()||nickname.size()>MaximumContentBytes){return std::unexpected(Error::Input);}
 auto payload=dictionary("de");if(!payload){return std::unexpected(payload.error());}
 if(!payload->replaceValue("msg",bytes(text))||!payload->replaceValue("nick",bytes(nickname))){return std::unexpected(Error::Input);}
 if(origin&&!payload->replaceValue("f","d1:o"+number(*origin)+"e")){return std::unexpected(Error::Input);}
 auto conversation=messages(now);if(!conversation){return std::unexpected(conversation.error());}
 if(!conversation->empty()){
  std::int64_t sequence=-1;std::int64_t missing=0;for(auto at=conversation->rbegin();at!=conversation->rend();++at){sequence=at->sequence.value_or(0);if(sequence>0){break;}++missing;}
  if(sequence>std::numeric_limits<std::int64_t>::max()-missing-1){return std::unexpected(Error::Limit);}sequence+=missing+1;
  if(!payload->replaceValue("pre",bytes(view(conversation->back().id)))||!payload->replaceValue("seq",number(sequence))){return std::unexpected(Error::Input);}
 }
 auto wire=payload->serialize();if(wire.size()>MaximumContentBytes){return std::unexpected(Error::Limit);}auto message=signMessage(signer_,uid_,std::move(wire));if(!message){return std::unexpected(message.error());}
 message->publicKey=std::string(signer_.publicKey());message->encodedContact=localContact_;
 auto verified=verifyMessage(*message,signer_.publicKey());if(!verified){return std::unexpected(verified.error());}return PreparedSend{sendOwner_,ledger_.mutations(),std::move(*verified)};
}
Result<MessageId> Room::commitSend(PreparedSend&& prepared,std::int64_t now){
 auto value=std::move(prepared.value_);if(!value||readOnly_||prepared.owner_.lock()!=sendOwner_||prepared.mutation_!=ledger_.mutations()){return std::unexpected(Error::Input);}
 const auto id=value->message().id;auto added=ledger_.add(std::move(*value),now);if(!added){return std::unexpected(added.error());}return id;
}
Result<MessageId> Room::send(std::string text,std::string nickname,std::int64_t now,std::optional<std::int32_t> origin){
 auto prepared=prepareSend(std::move(text),std::move(nickname),now,origin);if(!prepared){return std::unexpected(prepared.error());}return commitSend(std::move(*prepared),now);
}
Result<std::vector<RoomMessage>> Room::messages(std::int64_t now){
 auto snapshot=ledger_.snapshot(now);if(!snapshot){return std::unexpected(snapshot.error());}std::vector<RoomMessage> out;out.reserve(snapshot->size());std::vector<DisplayMessage> display;display.reserve(snapshot->size());
 for(const auto& row:*snapshot){auto payload=dictionary(row.message.content);if(!payload){continue;}const auto* msg=field(payload->root(),"msg",core::Node::Type::Bytes);const auto* nick=field(payload->root(),"nick",core::Node::Type::Bytes);if(!msg){continue;}
  RoomMessage item{row.message.id,row.message.uid,row.senderKey,std::string(msg->bytes),nick?std::string(nick->bytes):std::string{},row.message.ageSeconds,row.senderKey==signer_.publicKey(),{}, {}};
  item.timestamp=row.timestamp;
  std::optional<std::int64_t> origin;if(const auto* flags=field(payload->root(),"f",core::Node::Type::Dictionary)){if(const auto* value=field(*flags,"o",core::Node::Type::Integer)){origin=value->integer;}}item.origin=messageOrigin(item.text,origin);
  if(const auto* p=field(payload->root(),"pre",core::Node::Type::Bytes);p&&p->bytes.size()==8){MessageId id;std::copy(p->bytes.begin(),p->bytes.end(),id.begin());item.previous=id;}
  if(const auto* q=field(payload->root(),"seq",core::Node::Type::Integer)){item.sequence=q->integer;}display.push_back({item.id,item.previous,item.sequence.value_or(0),row.timestamp,row.ordinal});out.push_back(std::move(item));
 }
 // The original UI preserves the preceding display list before sorting new
 // arrivals. Stable ordinals distinguish records even when IDs collide.
 std::vector<std::size_t> input(out.size());std::iota(input.begin(),input.end(),0);
 std::ranges::sort(input,[&](auto a,auto b){const auto aa=std::ranges::find(displayOrder_,display[a].ordinal),bb=std::ranges::find(displayOrder_,display[b].ordinal);if(aa!=displayOrder_.end()||bb!=displayOrder_.end()){return aa<bb;}return display[a].ordinal<display[b].ordinal;});
 std::vector<DisplayMessage> candidates;for(const auto row:input){candidates.push_back(display[row]);}auto order=displayMessageOrder(candidates);if(!order){return std::unexpected(order.error());}
 std::vector<RoomMessage> sorted;sorted.reserve(out.size());displayOrder_.clear();for(const auto row:*order){const auto original=input[row];displayOrder_.push_back(display[original].ordinal);sorted.push_back(std::move(out[original]));}return sorted;
}
Result<std::string> Room::exportHistory(std::int64_t now){
 auto snapshot=ledger_.snapshot(now);if(!snapshot){return std::unexpected(snapshot.error());}std::string nodes="d",messages="l";std::size_t n=0;
 for(const auto& entry:*snapshot){auto node=dictionary("de");if(!node){return std::unexpected(node.error());}auto contact=entry.message.encodedContact.value_or(localContact_);
  if(!node->replaceValue("u",bytes(view(entry.message.uid)))||!node->replaceValue("p",bytes(entry.senderKey))||!node->replaceValue("c",contact)){return std::unexpected(Error::Input);}nodes+=bytes(std::to_string(n))+node->serialize();
  auto message=dictionary("de");auto ciphertext=plugins::msgsync::encrypt(plugins::msgsync::Kind::General,keys_.general,span(entry.message.content));if(!message||!ciphertext){return std::unexpected(Error::Crypto);}
  for(auto [key,value]:{std::pair{"n",number(static_cast<std::int64_t>(n++))},{"i",bytes(view(entry.message.id))},{"c",bytes(*ciphertext)},{"s",bytes(entry.message.signature)},{"a",number(entry.message.ageSeconds)},{"h",bytes(entry.message.history)}}){if(!message->replaceValue(key,std::move(value))){return std::unexpected(Error::Input);}}
  if(entry.message.control&&!message->replaceValue("$",bytes(*entry.message.control))){return std::unexpected(Error::Input);}messages+=message->serialize();
 }
 nodes+='e';messages+='e';auto root=dictionary("de");if(!root||!root->replaceValue("time",number(now))||!root->replaceValue("nodes",nodes)||!root->replaceValue("messages",messages)){return std::unexpected(Error::Input);}return root->serialize();
}
Result<ReceiveSummary> Room::importHistory(std::string encodedHistory,std::int64_t now){
 if(encodedHistory.size()>1024*1024||now<0){return std::unexpected(Error::Limit);}auto root=dictionary(std::move(encodedHistory));if(!root){return std::unexpected(root.error());}
 const auto* nodes=field(root->root(),"nodes",core::Node::Type::Dictionary);const auto* rows=field(root->root(),"messages",core::Node::Type::List);const auto* time=field(root->root(),"time",core::Node::Type::Integer);
 if(!nodes||!rows||!time||time->integer<0||rows->children.size()>128||nodes->children.size()>256){return std::unexpected(Error::Input);}std::vector<Message> messages;messages.reserve(rows->children.size());
 for(const auto& r:rows->children){const auto* index=field(r,"n",core::Node::Type::Integer);if(!index){return std::unexpected(Error::Input);}const auto* node=nodes->find(std::to_string(index->integer));if(!node){return std::unexpected(Error::Input);}
  const auto* uid=field(*node,"u",core::Node::Type::Bytes);const auto* pub=field(*node,"p",core::Node::Type::Bytes);const auto* contact=field(*node,"c",core::Node::Type::Dictionary);const auto* content=field(r,"c",core::Node::Type::Bytes);
  if(!uid||uid->bytes.size()!=8||!pub||!contact||!content){return std::unexpected(Error::Input);}auto message=dictionary("de");if(!message){return std::unexpected(message.error());}
  for(const auto key:{"i","s","a","h","$"}){if(const auto* v=r.find(key)){auto raw=encoded(*v);if(!raw||!message->replaceValue(key,std::move(*raw))){return std::unexpected(Error::Input);}}}
  auto plain=plugins::msgsync::decrypt(plugins::msgsync::Kind::General,keys_.general,span(content->bytes));auto rawContact=encoded(*contact);if(!plain||!rawContact){return std::unexpected(Error::Crypto);}
  if(!message->replaceValue("u",bytes(uid->bytes))||!message->replaceValue("p",bytes(pub->bytes))||!message->replaceValue("k",*rawContact)||!message->replaceValue("c",bytes(*plain))){return std::unexpected(Error::Input);}
  auto decoded=decodeMessage(message->serialize());if(!decoded){return std::unexpected(decoded.error());}
  const auto elapsed=now>time->integer?(static_cast<std::uint64_t>(now)-static_cast<std::uint64_t>(time->integer))/1000:0;
  decoded->ageSeconds=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(decoded->ageSeconds)+static_cast<std::uint32_t>(elapsed));messages.push_back(std::move(*decoded));
 }
 return admit(std::move(messages),now,true);
}
}
