// SPDX-License-Identifier: GPL-2.0-only
#include "peek.hpp"
#include "message.hpp"
#include <algorithm>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <bit>
#include <unordered_set>
namespace biglybtc::services::msgsync {
namespace {
std::string bytes(std::string_view value){return std::to_string(value.size())+":"+std::string(value);}
std::string integer(std::int32_t value){return "i"+std::to_string(value)+"e";}
std::int32_t narrow(std::int64_t value){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));}
Result<core::Document> dictionary(std::string value){
 if(value.size()>MaximumWireBytes){return std::unexpected(Error::Limit);}auto parsed=core::Document::parse(std::move(value),{MaximumWireBytes,MaximumWireBytes,64,8});if(!parsed||parsed->root().type!=core::Node::Type::Dictionary||parsed->root().children.size()%2){return std::unexpected(Error::Input);}std::unordered_set<std::string_view> names;for(std::size_t n=0;n<parsed->root().children.size();n+=2){if(!names.insert(parsed->root().children[n].bytes).second){return std::unexpected(Error::Input);}}return std::move(*parsed);
}
Result<void> replace(core::Document& document,std::string_view key,std::string value){if(!document.replaceValue(key,std::move(value))){return std::unexpected(Error::Limit);}return {};}
Result<std::string> serialise(const core::Document& document){auto encoded=document.serialize();if(encoded.size()>MaximumWireBytes){return std::unexpected(Error::Limit);}return encoded;}
}
Result<std::string> encodePeekRequest(const PeekKey& key,std::string_view random){
 if(random.size()>MaximumWireBytes){return std::unexpected(Error::Limit);}auto document=dictionary("de");if(!document){return std::unexpected(document.error());}if(!replace(*document,"k",bytes({reinterpret_cast<const char*>(key.data()),key.size()}))||!replace(*document,"r",bytes(random))){return std::unexpected(Error::Limit);}return serialise(*document);
}
Result<PeekRequest> decodePeekRequest(std::string value,const PeekKey& expected){
 auto document=dictionary(std::move(value));if(!document){return std::unexpected(document.error());}const auto* key=document->find("k");const auto* random=document->find("r");if(!key||key->type!=core::Node::Type::Bytes||key->bytes!=std::string_view(reinterpret_cast<const char*>(expected.data()),expected.size())||!random||random->type!=core::Node::Type::Bytes){return std::unexpected(Error::Input);}return PeekRequest{std::string(random->bytes)};
}
Result<std::string> encodePeekReply(const PeekReply& value){
 if((value.signature&&value.signature->size()>MaximumWireBytes)||(value.publicKey&&value.publicKey->size()>MaximumWireBytes)){return std::unexpected(Error::Limit);}auto document=dictionary("de");if(!document){return std::unexpected(document.error());}
 for(const auto& entry:{std::pair{"e",value.estimate},std::pair{"l",value.live},std::pair{"m",value.messages}}){if(!replace(*document,entry.first,integer(entry.second))){return std::unexpected(Error::Limit);}}
 if(value.publicKey&&!replace(*document,"p",bytes(*value.publicKey))){return std::unexpected(Error::Limit);}if(value.signature&&!replace(*document,"s",bytes(*value.signature))){return std::unexpected(Error::Limit);}if(value.total&&!replace(*document,"t",integer(*value.total))){return std::unexpected(Error::Limit);}return serialise(*document);
}
Result<PeekReply> decodePeekReply(std::string value){
 auto document=dictionary(std::move(value));if(!document){return std::unexpected(document.error());}PeekReply result;
 for(const auto& entry:{std::pair{"m",&result.messages},std::pair{"l",&result.live},std::pair{"e",&result.estimate}}){const auto* field=document->find(entry.first);if(!field||field->type!=core::Node::Type::Integer){return std::unexpected(Error::Input);}*entry.second=narrow(field->integer);}
 // Original client ignores t; a malformed optional t must not reject valid m/l/e.
 if(const auto* total=document->find("t");total&&total->type==core::Node::Type::Integer){result.total=narrow(total->integer);}
 for(const auto& entry:{std::pair{"s",&result.signature},std::pair{"p",&result.publicKey}}){if(const auto* field=document->find(entry.first)){if(field->type!=core::Node::Type::Bytes){return std::unexpected(Error::Input);}*entry.second=std::string(field->bytes);}}
 return result;
}
LiveNodeEstimate::LiveNodeEstimate(std::int64_t now):bloom_(*AddOnlyBloom::create(1000)),start_(now){}
void LiveNodeEstimate::clear(std::int64_t now){bloom_=*AddOnlyBloom::create(1000);start_=now;}
void LiveNodeEstimate::alive(std::string_view address,std::int64_t now){
 if(bloom_.count()>=100){estimate_=100;clear(now);}if(bloom_.contains(address)){if(lastNew_>0&&now-lastNew_>120000){estimate_=static_cast<std::int32_t>(bloom_.count());clear(now);bloom_.add(address);lastNew_=now;}}
 else{bloom_.add(address);lastNew_=now;estimate_=std::max(estimate_,static_cast<std::int32_t>(bloom_.count()));}
}
void LiveNodeEstimate::tick(std::int64_t now){if((lastNew_>0&&now-lastNew_>=120000)||now-start_>=600000){estimate_=static_cast<std::int32_t>(bloom_.count());clear(now);}}
namespace {
std::span<const std::uint8_t> raw(std::string_view value){return {reinterpret_cast<const std::uint8_t*>(value.data()),value.size()};}
}
Result<PeekChannel> PeekChannel::open(std::string_view userKey){
 if(userKey.empty()||userKey.size()>4096){return std::unexpected(Error::Input);}auto keys=plugins::msgsync::deriveChannelKeys(raw(userKey));if(!keys){return std::unexpected(Error::Crypto);}return PeekChannel(*keys);
}
PeekChannel::~PeekChannel(){OPENSSL_cleanse(keys_.general.data(),keys_.general.size());}
Result<std::string> PeekChannel::request()const{
 std::array<std::uint8_t,16> random{};if(RAND_bytes(random.data(),random.size())!=1){return std::unexpected(Error::Crypto);}auto plain=encodePeekRequest(keys_.peek,{reinterpret_cast<const char*>(random.data()),random.size()});if(!plain){return std::unexpected(plain.error());}auto encoded=plugins::msgsync::encrypt(plugins::msgsync::Kind::General,keys_.general,raw(*plain));if(!encoded){return std::unexpected(Error::Crypto);}return std::move(*encoded);
}
Result<PeekReply> PeekChannel::receive(std::string envelope)const{
 if(envelope.size()>MaximumTransferBytes){return std::unexpected(Error::Limit);}auto plain=plugins::msgsync::decrypt(plugins::msgsync::Kind::General,keys_.general,raw(envelope));if(!plain){return std::unexpected(Error::Crypto);}return decodePeekReply(std::move(*plain));
}
Result<std::string> PeekChannel::answer(std::string envelope,const PeekReply& reply)const{
 if(envelope.size()>MaximumTransferBytes){return std::unexpected(Error::Limit);}auto plain=plugins::msgsync::decrypt(plugins::msgsync::Kind::General,keys_.general,raw(envelope));if(!plain){return std::unexpected(Error::Crypto);}if(!decodePeekRequest(std::move(*plain),keys_.peek)){return std::unexpected(Error::Input);}auto response=encodePeekReply(reply);if(!response){return std::unexpected(response.error());}auto encoded=plugins::msgsync::encrypt(plugins::msgsync::Kind::General,keys_.general,raw(*response));if(!encoded){return std::unexpected(Error::Crypto);}return std::move(*encoded);
}
bool PeekAggregate::add(const PeekReply& reply){
 if(summary_.results>=3){return false;}++summary_.results;const bool more=reply.messages>summary_.messages;summary_.messages=std::max(summary_.messages,reply.messages);live_=std::max(live_,reply.live);estimate_=std::max(estimate_,reply.estimate);summary_.nodes=std::max(live_,estimate_);
 if(reply.signature&&reply.publicKey&&(more||!summary_.signature)){summary_.signature=reply.signature;summary_.publicKey=reply.publicKey;}return summary_.results<3;
}
}
