// SPDX-License-Identifier: GPL-2.0-or-later
// MsgSyncHandler.getSharedSecret/handleDHRequest/privateMessageEncrypt formats.
#include "private_channel.hpp"
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <algorithm>
#include <set>
namespace biglybtc::services::msgsync {
namespace {
std::span<const std::uint8_t> span(std::string_view v){return {reinterpret_cast<const std::uint8_t*>(v.data()),v.size()};}
template<std::size_t N>std::string_view view(const std::array<std::uint8_t,N>& v){return {reinterpret_cast<const char*>(v.data()),v.size()};}
std::string b(std::string_view v){return std::to_string(v.size())+":"+std::string(v);}
Result<core::Document> parse(std::string data){
 if(data.size()>MaximumWireBytes){return std::unexpected(Error::Limit);}
 auto doc=core::Document::parse(std::move(data));
 if(!doc||doc->root().type!=core::Node::Type::Dictionary){return std::unexpected(Error::Input);}
 std::set<std::string_view> keys;const auto& children=doc->root().children;
 for(std::size_t i=0;i<children.size();i+=2){if(!keys.insert(children[i].bytes).second){return std::unexpected(Error::Input);}}
 return std::move(*doc);
}
const core::Node* bytes(const core::Document& d,std::string_view k){const auto* v=d.find(k);return v&&v->type==core::Node::Type::Bytes?v:nullptr;}
bool type(const core::Document& d,std::int64_t t){const auto* v=d.find("t");return v&&v->type==core::Node::Type::Integer&&v->integer==t;}
bool packet(std::string_view v){return !v.empty()&&v.size()<=MaximumWireBytes-128;}
}
Result<PrivateRequest> decodePrivateRequest(std::string data){
 auto d=parse(std::move(data));if(!d){return std::unexpected(d.error());}
 const auto* i=bytes(*d,"i"),*u=bytes(*d,"u"),*k=bytes(*d,"k"),*a=bytes(*d,"a"),*v=d->find("v");
 if(!type(*d,2)||!v||v->type!=core::Node::Type::Integer||v->integer<4||v->integer>5
    ||!i||i->bytes.size()!=8||!u||u->bytes.size()!=16||bool(k)==bool(a)
    ||(d->find("k")&&!k)||(d->find("a")&&!a)||!packet(k?k->bytes:a->bytes)){
  return std::unexpected(Error::Input);
 }
 PrivateRequest out;std::copy(i->bytes.begin(),i->bytes.end(),out.activity.begin());
 std::copy(u->bytes.begin(),u->bytes.end(),out.userKey.begin());out.phase=k?PrivatePhase::Keys:PrivatePhase::Auth;out.packet=k?k->bytes:a->bytes;return out;
}
Result<std::string> encodePrivateRequest(const PrivateRequest& r){
 if(!packet(r.packet)){return std::unexpected(Error::Limit);}
 std::string out="d";if(r.phase==PrivatePhase::Auth){out+="1:a"+b(r.packet);}out+="1:i"+b(view(r.activity));if(r.phase==PrivatePhase::Keys){out+="1:k"+b(r.packet);}return out+"1:ti2e1:u"+b(view(r.userKey))+"1:vi5ee";
}
Result<PrivateReply> decodePrivateReply(std::string data){
 auto d=parse(std::move(data));if(!d){return std::unexpected(d.error());}
 if(!type(*d,3)){return std::unexpected(Error::Input);}PrivateReply out;
 for(auto [key,dest]:{std::pair{"k",&out.keys},std::pair{"a",&out.auth},std::pair{"error",&out.error}}){
  if(const auto* v=d->find(key)){if(v->type!=core::Node::Type::Bytes||!packet(v->bytes)){return std::unexpected(Error::Input);}*dest=v->bytes;}
 }
 if(out.error){if(out.keys||out.auth||out.error->size()>1024){return std::unexpected(Error::Input);}}
 else if(bool(out.keys)!=bool(out.auth)){return std::unexpected(Error::Input);}
 return out;
}
Result<std::string> encodePrivateReply(const PrivateReply& r){
 if((r.error&&(r.keys||r.auth||r.error->empty()||r.error->size()>1024))||(!r.error&&bool(r.keys)!=bool(r.auth))
    ||(r.keys&&(!packet(*r.keys)||!packet(*r.auth)))){return std::unexpected(Error::Input);}
 std::string out="d";if(r.auth){out+="1:a"+b(*r.auth);}if(r.error){out+="5:error"+b(*r.error);}if(r.keys){out+="1:k"+b(*r.keys);}out+="1:si1e1:ti3ee";
 if(out.size()>MaximumWireBytes){return std::unexpected(Error::Limit);}return out;
}
PrivateChannel::PrivateChannel(PrivateUserKey u,std::string peer,std::array<std::uint8_t,24> secret,std::array<std::uint8_t,20> handler)
 :user_(u),peer_(std::move(peer)),secret_(secret),handler_(handler){OPENSSL_cleanse(secret.data(),secret.size());}
PrivateChannel::~PrivateChannel(){clear();}
void PrivateChannel::clear()noexcept{OPENSSL_cleanse(secret_.data(),secret_.size());alive_=false;}
PrivateChannel::PrivateChannel(PrivateChannel&& o)noexcept:user_(o.user_),peer_(std::move(o.peer_)),secret_(o.secret_),handler_(o.handler_),alive_(o.alive_){o.clear();}
PrivateChannel& PrivateChannel::operator=(PrivateChannel&& o)noexcept{if(this!=&o){clear();user_=o.user_;peer_=std::move(o.peer_);secret_=o.secret_;handler_=o.handler_;alive_=o.alive_;o.clear();}return *this;}
std::string_view PrivateChannel::handler()const noexcept{return alive_?view(handler_):std::string_view{};}
Result<PrivateChannel> PrivateChannel::authenticated(plugins::msgsync::LegacySts& sts,PrivateUserKey u,std::string_view peer){
 auto secret=sts.authenticatedSecret(peer);if(!secret){return std::unexpected(Error::Signature);}
 auto fixed=plugins::msgsync::fixSharedSecret(secret->view());auto keys=plugins::msgsync::deriveChannelKeys(span(view(u)));
 if(!fixed||!keys){if(fixed){OPENSSL_cleanse(fixed->data(),fixed->size());}return std::unexpected(Error::Crypto);}
 PrivateChannel out(u,std::string(peer),*fixed,keys->listen);OPENSSL_cleanse(fixed->data(),fixed->size());OPENSSL_cleanse(keys->general.data(),keys->general.size());return out;
}
Result<std::string> PrivateChannel::encrypt(std::string_view data)const{
 if(!alive_){return std::unexpected(Error::Input);}if(data.size()>MaximumWireBytes){return std::unexpected(Error::Limit);}
 auto result=plugins::msgsync::encrypt(plugins::msgsync::Kind::Private,secret_,span(data));if(!result){return std::unexpected(Error::Crypto);}return std::move(*result);
}
Result<std::string> PrivateChannel::decrypt(std::string_view data)const{
 if(!alive_){return std::unexpected(Error::Input);}if(data.size()>MaximumTransferBytes){return std::unexpected(Error::Limit);}
 auto result=plugins::msgsync::decrypt(plugins::msgsync::Kind::Private,secret_,span(data));if(!result){return std::unexpected(Error::Crypto);}return std::move(*result);
}
PrivateInitiator::PrivateInitiator(plugins::msgsync::LegacySts sts,std::string peer,PrivateActivity a,PrivateUserKey u)
 :sts_(std::move(sts)),peer_(std::move(peer)),activity_(a),user_(u){}
Result<PrivateInitiator> PrivateInitiator::create(plugins::msgsync::LegacySts sts,std::string peer){
 if(peer.empty()||sts.state().keysDerived||sts.state().remotePublic){return std::unexpected(Error::Input);}PrivateActivity a;PrivateUserKey u;
 if(RAND_bytes(a.data(),a.size())!=1||RAND_bytes(u.data(),u.size())!=1){return std::unexpected(Error::Crypto);}return PrivateInitiator(std::move(sts),std::move(peer),a,u);
}
Result<PrivateRequest> PrivateInitiator::first()const{
 if(!sts_||waitingFinal_){return std::unexpected(Error::Input);}auto keys=sts_->keysPacket();if(!keys){return std::unexpected(Error::Crypto);}return PrivateRequest{activity_,user_,PrivatePhase::Keys,std::move(*keys)};
}
Result<PrivateRequest> PrivateInitiator::keysReply(const PrivateReply& r){
 if(!sts_){return std::unexpected(Error::Input);}auto sts=std::move(*sts_);sts_.reset();
 if(waitingFinal_||r.error||!r.keys||!r.auth){return std::unexpected(Error::Input);}
 auto keys=sts.receiveKeys(*r.keys);if(!keys||*keys!=r.keys->size()){return std::unexpected(Error::Signature);}
 auto auth=sts.authPacket();auto verified=sts.receiveAuth(*r.auth);
 if(!auth||!verified||*verified!=r.auth->size()||!sts.authenticatedSecret(peer_)){return std::unexpected(Error::Signature);}
 sts_.emplace(std::move(sts));waitingFinal_=true;return PrivateRequest{activity_,user_,PrivatePhase::Auth,std::move(*auth)};
}
Result<PrivateChannel> PrivateInitiator::finish(const PrivateReply& r){
 if(!sts_){return std::unexpected(Error::Input);}auto sts=std::move(*sts_);sts_.reset();
 if(!waitingFinal_||r.error||r.keys||r.auth){return std::unexpected(Error::Input);}return PrivateChannel::authenticated(sts,user_,peer_);
}
PrivateResponder::PrivateResponder(plugins::msgsync::LegacySts sts,PrivateActivity a,PrivateUserKey u):sts_(std::move(sts)),activity_(a),user_(u){}
Result<PrivateResponder> PrivateResponder::create(plugins::msgsync::LegacySts sts,const PrivateRequest& r){
 if(r.phase!=PrivatePhase::Keys||sts.state().keysDerived||sts.state().remotePublic){return std::unexpected(Error::Input);}
 auto keys=sts.receiveKeys(r.packet);if(!keys||*keys!=r.packet.size()){return std::unexpected(Error::Signature);}
 return PrivateResponder(std::move(sts),r.activity,r.userKey);
}
Result<PrivateReply> PrivateResponder::first()const{
 if(!sts_){return std::unexpected(Error::Input);}auto keys=sts_->keysPacket(),auth=sts_->authPacket();if(!keys||!auth){return std::unexpected(Error::Crypto);}return PrivateReply{std::move(*keys),std::move(*auth),{}};
}
Result<PrivateChannel> PrivateResponder::finish(const PrivateRequest& r){
 if(!sts_){return std::unexpected(Error::Input);}auto sts=std::move(*sts_);sts_.reset();
 if(r.phase!=PrivatePhase::Auth||r.activity!=activity_||r.userKey!=user_){return std::unexpected(Error::Input);}
 auto auth=sts.receiveAuth(r.packet);const auto peer=sts.state().remotePublic;
 if(!auth||*auth!=r.packet.size()||!peer){return std::unexpected(Error::Signature);}return PrivateChannel::authenticated(sts,user_,*peer);
}
}
