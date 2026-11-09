// SPDX-License-Identifier: GPL-2.0-or-later
// Original azmsgsync4209aa5 MsgSyncHandler: signed messages and SYNC v4/5.
#include "message.hpp"
#include <openssl/rand.h>
#include <zlib.h>
#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <utility>

namespace biglybtc::services::msgsync {
namespace {
using Node=core::Node;
constexpr core::Limits WireLimits{MaximumWireBytes,MaximumWireBytes,8192,24};
constexpr core::Limits DecodedLimits{MaximumDecodedBytes,MaximumDecodedBytes,8192,24};
Node bytes(std::string_view value){Node n(Node::Type::Bytes);n.bytes=value;return n;}
Node integer(std::int64_t value){Node n(Node::Type::Integer);n.integer=value;return n;}
template<std::size_t N>std::string_view raw(const std::array<std::uint8_t,N>& value){return {reinterpret_cast<const char*>(value.data()),value.size()};}
bool unique(const Node& node){
  if(node.type==Node::Type::Dictionary){
    if(node.children.size()%2){return false;}
    for(std::size_t n=0;n<node.children.size();n+=2){
      if(node.children[n].type!=Node::Type::Bytes){return false;}
      for(std::size_t m=0;m<n;m+=2){if(node.children[n].bytes==node.children[m].bytes){return false;}}
    }
  }
  for(const auto& child:node.children){if(!unique(child)){return false;}}return true;
}
bool dictionary(const Node& n){return n.type==Node::Type::Dictionary&&unique(n);}
bool boundedBytes(const Node* n,std::size_t maximum){return n&&n->type==Node::Type::Bytes&&n->bytes.size()<=maximum;}
std::int32_t javaInt(std::int64_t value){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));}
template<std::size_t N>bool fixed(const Node* n,std::array<std::uint8_t,N>& output){
  if(!boundedBytes(n,N)||n->bytes.size()!=N){return false;}std::copy(n->bytes.begin(),n->bytes.end(),output.begin());return true;
}
bool valid(const Message& m){
  return m.content.size()<=MaximumContentBytes&&(!m.control||m.control->size()<=MaximumControlBytes)
    &&!m.signature.empty()&&m.signature.size()<=128&&m.history.size()<=MaximumHistoryBytes
    &&(!m.publicKey||(!m.publicKey->empty()&&m.publicKey->size()<=512))
    &&(!m.encodedContact||m.encodedContact->size()<=4096);
}
Result<Message> fromNode(const Node& node){
  if(!dictionary(node)){return std::unexpected(Error::Input);}Message m;
  if(!fixed(node.find("u"),m.uid)||!fixed(node.find("i"),m.id)
    ||!boundedBytes(node.find("c"),MaximumContentBytes)||!boundedBytes(node.find("s"),128)){
    return std::unexpected(Error::Input);
  }
  const auto* age=node.find("a");if(!age||age->type!=Node::Type::Integer){return std::unexpected(Error::Input);}
  m.ageSeconds=std::max<std::int32_t>(0,javaInt(age->integer));
  m.content=node.find("c")->bytes;m.signature=node.find("s")->bytes;
  if(const auto* v=node.find("$")){if(!boundedBytes(v,MaximumControlBytes)){return std::unexpected(Error::Limit);}m.control=std::string(v->bytes);}
  if(const auto* v=node.find("h")){if(!boundedBytes(v,MaximumHistoryBytes)){return std::unexpected(Error::Limit);}m.history=v->bytes;}
  if(const auto* v=node.find("p")){if(!boundedBytes(v,512)||v->bytes.empty()){return std::unexpected(Error::Limit);}m.publicKey=std::string(v->bytes);}
  if(const auto* v=node.find("k")){
    if(!dictionary(*v)){return std::unexpected(Error::Input);}auto encoded=core::encodeValue(*v,{4096,4096,1024,16});
    if(!encoded){return std::unexpected(Error::Limit);}m.encodedContact=std::move(*encoded);
  }
  if(!valid(m)){return std::unexpected(Error::Limit);}return m;
}
// Documents keep any borrowed contact-map subtrees alive until encodeValue
// completes. No Node/StringView outlives the immutable owning source buffer.
Result<Node> toNode(const Message& m,std::vector<core::Document>& backing){
  if(!valid(m)){return std::unexpected(Error::Limit);}Node n(Node::Type::Dictionary);
  if(m.control){n.children.insert(n.children.end(),{bytes("$"),bytes(*m.control)});}
  n.children.insert(n.children.end(),{bytes("a"),integer(m.ageSeconds),bytes("c"),bytes(m.content),bytes("h"),bytes(m.history),bytes("i"),bytes(raw(m.id))});
  if(m.encodedContact){
    auto parsed=core::Document::parse(*m.encodedContact,{4096,4096,1024,16});
    if(!parsed||!dictionary(parsed->root())){return std::unexpected(Error::Input);}
    backing.push_back(std::move(*parsed));n.children.push_back(bytes("k"));n.children.push_back(backing.back().root());
  }
  if(m.publicKey){n.children.insert(n.children.end(),{bytes("p"),bytes(*m.publicKey)});}
  n.children.insert(n.children.end(),{bytes("s"),bytes(m.signature),bytes("u"),bytes(raw(m.uid))});return n;
}
Result<std::string> encoded(const Node& node,core::Limits limits){
  auto value=core::encodeValue(node,limits);if(!value){return std::unexpected(Error::Limit);}return std::move(*value);
}
Result<std::string> gunzip(std::string_view input){
  if(input.empty()||input.size()>MaximumWireBytes){return std::unexpected(Error::Limit);}
  std::string result;std::size_t position=0;unsigned members=0;std::array<char,4096> block{};
  while(position<input.size()){
    if(++members>8){return std::unexpected(Error::Compression);}z_stream stream{};
    if(inflateInit2(&stream,31)!=Z_OK){return std::unexpected(Error::Resources);}
    struct End {z_stream& stream;~End(){inflateEnd(&stream);}} end{stream};
    stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.data()+position));stream.avail_in=static_cast<uInt>(input.size()-position);
    for(;;){
      stream.next_out=reinterpret_cast<Bytef*>(block.data());stream.avail_out=static_cast<uInt>(block.size());
      const auto before=stream.avail_in;const int status=inflate(&stream,Z_NO_FLUSH);const auto produced=block.size()-stream.avail_out;
      if(produced>MaximumDecodedBytes-result.size()){return std::unexpected(Error::Limit);}result.append(block.data(),produced);
      if(status==Z_STREAM_END){break;}
      if(status!=Z_OK||(produced==0&&before==stream.avail_in)){return std::unexpected(Error::Compression);}
    }
    const auto consumed=input.size()-position-stream.avail_in;if(!consumed){return std::unexpected(Error::Compression);}position+=consumed;
  }
  return result;
}
Result<std::string> gzip(std::string_view input){
  if(input.size()>MaximumDecodedBytes){return std::unexpected(Error::Limit);}z_stream stream{};
  if(deflateInit2(&stream,Z_DEFAULT_COMPRESSION,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)!=Z_OK){return std::unexpected(Error::Resources);}
  struct End {z_stream& stream;~End(){deflateEnd(&stream);}} end{stream};
  stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));stream.avail_in=static_cast<uInt>(input.size());
  std::array<char,4096> block{};std::string result;
  for(;;){
    stream.next_out=reinterpret_cast<Bytef*>(block.data());stream.avail_out=static_cast<uInt>(block.size());
    const auto before=stream.avail_in;const int status=deflate(&stream,Z_FINISH);const auto produced=block.size()-stream.avail_out;
    if(produced>MaximumDecodedBytes-result.size()){return std::unexpected(Error::Limit);}result.append(block.data(),produced);
    if(status==Z_STREAM_END){return result;}
    if(status!=Z_OK||(produced==0&&before==stream.avail_in)){return std::unexpected(Error::Compression);}
  }
}
Result<std::vector<Message>> messageList(const Node& list){
  if(list.type!=Node::Type::List||list.children.size()>MaximumMessages){return std::unexpected(Error::Limit);}
  std::vector<Message> result;result.reserve(list.children.size());std::size_t content=0;
  for(const auto& node:list.children){auto m=fromNode(node);if(!m){return std::unexpected(m.error());}
    const auto size=m->content.size()+(m->control?m->control->size():0);
    if(size>MaximumReplyContentBytes-content){return std::unexpected(Error::Limit);}content+=size;result.push_back(std::move(*m));
  }return result;
}
}
VerifiedMessage::VerifiedMessage(Message message,std::string sender):message_(std::move(message)),sender_(std::move(sender)){}
Result<Message> decodeMessage(std::string input){try{
  auto parsed=core::Document::parse(std::move(input),WireLimits);if(!parsed){return std::unexpected(Error::Input);}return fromNode(parsed->root());
}catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}}
Result<std::string> encodeMessage(const Message& m){try{
  std::vector<core::Document> backing;auto n=toNode(m,backing);if(!n){return std::unexpected(n.error());}return encoded(*n,WireLimits);
}catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}}
Result<std::string> signedBytes(const Message& m){try{
  if(m.content.size()>MaximumContentBytes||(m.control&&m.control->size()>MaximumControlBytes)){return std::unexpected(Error::Limit);}
  std::string result;result.reserve(m.uid.size()+m.id.size()+m.content.size()+(m.control?m.control->size():0));result+=raw(m.uid);result+=raw(m.id);result+=m.content;if(m.control){result+=*m.control;}return result;
}catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}}
Result<VerifiedMessage> verifyMessage(Message m,std::string_view candidate){try{
  if(!valid(m)){return std::unexpected(Error::Limit);}auto value=signedBytes(m);if(!value){return std::unexpected(value.error());}
  auto verified=plugins::msgsync::verifyIdentitySignature(candidate,*value,m.signature);
  if(!verified||!*verified){return std::unexpected(Error::Signature);}
  // The unsigned supplied p/k/age/history fields confer no rights and may differ
  // from the actual known-key candidate that authenticated the signed body.
  return VerifiedMessage(std::move(m),std::string(candidate));
}catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}}
Result<Message> signMessage(const plugins::msgsync::MessageSigner& signer,Uid uid,std::string content,std::optional<std::string> control){try{
  Message m;m.uid=uid;m.content=std::move(content);m.control=std::move(control);
  if(RAND_priv_bytes(m.id.data(),static_cast<int>(m.id.size()))!=1){return std::unexpected(Error::Crypto);}
  auto value=signedBytes(m);if(!value){return std::unexpected(value.error());}auto signature=signer.signMessage(*value);
  if(!signature){return std::unexpected(Error::Crypto);}m.signature=std::move(*signature);m.publicKey=std::string(signer.publicKey());
  if(!valid(m)){return std::unexpected(Error::Limit);}return m;
}catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}}
Result<SyncRequest> decodeSyncRequest(std::string input){try{
  auto doc=core::Document::parse(std::move(input),WireLimits);if(!doc||!dictionary(doc->root())){return std::unexpected(Error::Input);}
  const auto* v=doc->find("v");const auto* t=doc->find("t");const auto* m=doc->find("m");const auto* p=doc->find("p");const auto* b=doc->find("b");
  if(!v||v->type!=Node::Type::Integer||!t||t->type!=Node::Type::Integer||javaInt(t->integer)!=0||!m||m->type!=Node::Type::Integer||!p||p->type!=Node::Type::Integer||!b){return std::unexpected(Error::Input);}
  const auto version=javaInt(v->integer);if(version<4){return std::unexpected(Error::Version);}auto bloom=AddOnlyBloom::decode(*b);if(!bloom){return std::unexpected(bloom.error());}
  SyncRequest r{version,{},std::nullopt,{},std::move(*bloom),javaInt(m->integer),javaInt(p->integer),std::nullopt};
  if(!fixed(doc->find("u"),r.uid)||!fixed(doc->find("r"),r.random)){return std::unexpected(Error::Input);}
  if(const auto* q=doc->find("q")){std::array<std::uint8_t,6> value{};if(!fixed(q,value)){return std::unexpected(Error::Input);}r.requestId=value;}
  if(const auto* o=doc->find("o")){if(o->type!=Node::Type::Integer){return std::unexpected(Error::Input);}r.oldestAgeSeconds=javaInt(o->integer);}return r;
}catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}}
Result<std::string> encodeSyncRequest(const SyncRequest& r){try{
  if(r.version<4){return std::unexpected(Error::Version);}auto bloom=r.bloom.encode();if(!bloom){return std::unexpected(bloom.error());}
  auto doc=core::Document::parse(std::move(*bloom),WireLimits);if(!doc){return std::unexpected(Error::Input);}
  Node n(Node::Type::Dictionary);n.children={bytes("b"),doc->root(),bytes("m"),integer(r.messages)};
  if(r.oldestAgeSeconds){n.children.insert(n.children.end(),{bytes("o"),integer(*r.oldestAgeSeconds)});}
  n.children.insert(n.children.end(),{bytes("p"),integer(r.newMessages)});if(r.requestId){n.children.insert(n.children.end(),{bytes("q"),bytes(raw(*r.requestId))});}
  n.children.insert(n.children.end(),{bytes("r"),bytes(raw(r.random)),bytes("t"),integer(0),bytes("u"),bytes(raw(r.uid)),bytes("v"),integer(r.version)});return encoded(n,WireLimits);
}catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}}
Result<SyncReply> decodeSyncReply(std::string input){try{
  auto doc=core::Document::parse(std::move(input),WireLimits);if(!doc||!dictionary(doc->root())){return std::unexpected(Error::Input);}
  const auto* t=doc->find("t");const auto* s=doc->find("s");
  if(!t||t->type!=Node::Type::Integer||javaInt(t->integer)!=1||!s||s->type!=Node::Type::Integer){return std::unexpected(Error::Input);}
  SyncReply r;r.status=javaInt(s->integer);if(r.status!=1&&r.status!=2){return std::unexpected(Error::Version);}
  if(const auto* x=doc->find("x")){if(x->type!=Node::Type::Integer){return std::unexpected(Error::Input);}r.moreToCome=javaInt(x->integer);}
  if(const auto* m=doc->find("m")){auto messages=messageList(*m);if(!messages){return std::unexpected(messages.error());}r.messages=std::move(*messages);}
  else if(const auto* z=doc->find("z")){
    if(!boundedBytes(z,MaximumWireBytes)){return std::unexpected(Error::Input);}auto plain=gunzip(z->bytes);if(!plain){return std::unexpected(plain.error());}
    auto nested=core::Document::parse(std::move(*plain),DecodedLimits);if(!nested||!dictionary(nested->root())){return std::unexpected(Error::Input);}
    const auto* m=nested->find("m");if(!m){return std::unexpected(Error::Input);}auto messages=messageList(*m);if(!messages){return std::unexpected(messages.error());}r.messages=std::move(*messages);
  }return r;
}catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}}
Result<std::string> encodeSyncReply(const SyncReply& r,bool allowGzip){try{
  if(r.status!=1&&r.status!=2){return std::unexpected(Error::Version);}if(r.messages.size()>MaximumMessages){return std::unexpected(Error::Limit);}
  Node list(Node::Type::List);std::vector<core::Document> backing;backing.reserve(r.messages.size());std::size_t content=0;
  for(const auto& m:r.messages){
    const auto size=m.content.size()+(m.control?m.control->size():0);if(size>MaximumReplyContentBytes-content){return std::unexpected(Error::Limit);}content+=size;
    auto n=toNode(m,backing);if(!n){return std::unexpected(n.error());}list.children.push_back(std::move(*n));
  }
  Node root(Node::Type::Dictionary);std::optional<std::string> compressed;
  if(allowGzip&&!r.messages.empty()){
    Node nested(Node::Type::Dictionary);nested.children={bytes("m"),list};auto plain=encoded(nested,DecodedLimits);if(!plain){return std::unexpected(plain.error());}
    auto result=gzip(*plain);if(!result){return std::unexpected(result.error());}if(result->size()<plain->size()){compressed=std::move(*result);}
  }
  if(!r.messages.empty()&&!compressed){root.children={bytes("m"),std::move(list)};}
  root.children.insert(root.children.end(),{bytes("s"),integer(r.status),bytes("t"),integer(1)});
  if(r.moreToCome>0){root.children.insert(root.children.end(),{bytes("x"),integer(r.moreToCome)});}
  if(compressed){root.children.insert(root.children.end(),{bytes("z"),bytes(*compressed)});}return encoded(root,WireLimits);
}catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}}
RequestReplay::RequestReplay(){keys_.reserve(512);}
Result<bool> RequestReplay::remember(const SyncRequest& r){try{
  const auto value=r.requestId?raw(*r.requestId):raw(r.random);auto it=std::find(keys_.begin(),keys_.end(),value);
  if(it!=keys_.end()){std::rotate(it,it+1,keys_.end());return false;}
  std::string key(value);if(keys_.size()==512){keys_.erase(keys_.begin());}keys_.push_back(std::move(key));return true;
}catch(const std::bad_alloc&){return std::unexpected(Error::Resources);}}
}
