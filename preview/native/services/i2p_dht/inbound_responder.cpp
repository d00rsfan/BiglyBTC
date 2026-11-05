// SPDX-License-Identifier: GPL-2.0-or-later
// Original policy: Paul Gardner, Copyright (C) Azureus Software, Inc.
#include "inbound_responder.hpp"
#include <algorithm>
#include <new>
#include <limits>
#include <openssl/crypto.h>
namespace biglybtc::services::i2p::dht::inbound {
namespace {
constexpr std::uint64_t increment(std::uint64_t value)noexcept{
  return value==std::numeric_limits<std::uint64_t>::max()?value:value+1;
}
static_assert(increment(0)==1);
static_assert(increment(std::numeric_limits<std::uint64_t>::max()-1)==std::numeric_limits<std::uint64_t>::max());
static_assert(increment(std::numeric_limits<std::uint64_t>::max())==std::numeric_limits<std::uint64_t>::max());
ResponderError stateError(StateError value){
  switch(value){
  case StateError::Invalid:return ResponderError::Invalid;case StateError::Thread:return ResponderError::Thread;
  case StateError::Closed:return ResponderError::Closed;case StateError::Capacity:return ResponderError::Capacity;
  case StateError::Crypto:return ResponderError::Crypto;case StateError::Resource:return ResponderError::Resource;case StateError::Clock:return ResponderError::Clock;
  }return ResponderError::Invalid;
}
ReplyResult<Contact> signedContact(const Destination& sender,const Id& id,std::uint16_t port){
  auto actual=decodeDestination(sender.wire);if(!actual){return std::unexpected(ResponderError::Invalid);}
  Contact result{id,actual->hash,port,std::move(*actual)};if(!validContact(result)){return std::unexpected(ResponderError::Invalid);}return result;
}
bool closer(const Id& a,const Id& b,const Id& target){
  for(std::size_t i=0;i<a.size();++i){const auto x=Byte(a[i]^target[i]),y=Byte(b[i]^target[i]);if(x!=y){return x<y;}}return false;
}
bool validRouting(const RoutingSnapshot& routing,const Id& target){
  if(routing.closest.size()>10){return false;}
  for(std::size_t i=0;i<routing.closest.size();++i){
    if(!validContact(routing.closest[i])){return false;}
    if(i&&closer(routing.closest[i].id,routing.closest[i-1].id,target)){return false;}
    for(std::size_t j=0;j<i;++j){if(routing.closest[i].id==routing.closest[j].id){return false;}}
  }return true;
}
}
Responder::Responder(Contact local,std::unique_ptr<TokenBindings> tokens,std::unique_ptr<PeerStore> peers):local_(std::move(local)),tokens_(std::move(tokens)),peers_(std::move(peers)){}
Responder::~Responder(){close();}
ReplyResult<std::unique_ptr<Responder>> Responder::create(Contact local,StoreLimits limits,std::optional<azureus_auth::Key> supplied){
  struct Clean {std::optional<azureus_auth::Key>& key;~Clean(){if(key){OPENSSL_cleanse(key->data(),key->size());}}} clean{supplied};
  try{
    if(!local.destination){return std::unexpected(ResponderError::Invalid);}auto actual=signedContact(*local.destination,local.id,local.port);if(!actual||actual->hash!=local.hash){return std::unexpected(ResponderError::Invalid);}
    auto tokens=TokenBindings::create(supplied);if(!tokens){return std::unexpected(stateError(tokens.error()));}
    auto peers=PeerStore::create(limits);if(!peers){return std::unexpected(stateError(peers.error()));}
    return std::unique_ptr<Responder>(new Responder(std::move(*actual),std::move(*tokens),std::move(*peers)));
  }catch(...){return std::unexpected(ResponderError::Resource);}
}
ReplyResult<std::optional<Response>> Responder::receive(const Incoming& incoming,std::int64_t wall,RequestPolicy policy,const Current& current,const Route& route){
  if(std::this_thread::get_id()!=thread_){return std::unexpected(ResponderError::Thread);}if(closed_){return std::unexpected(ResponderError::Closed);}if(busy_){return std::unexpected(ResponderError::Reentry);}
  busy_=true;struct Reset {bool& value;~Reset(){value=false;}} reset{busy_};
  try{
    const auto live=[&]{if(closed_||!current){return false;}const bool allowed=current();return allowed&&!closed_;};
    const auto unavailable=[&]{return closed_?ResponderError::Closed:ResponderError::NoAuthority;};
    if(!live()){return std::unexpected(unavailable());}if(!route){return std::unexpected(ResponderError::Routing);}
    auto request=decode(incoming.payload);if(!request||request->kind!=Kind::Query){return std::unexpected(ResponderError::Invalid);}
    if(policy.sleeping&&!request->wakeRequested){return std::optional<Response>{};}
    Contact originator;
    if(incoming.signedSender){
      if(incoming.toPort!=local_.port){return std::optional<Response>{};}
      if(!request->idIsTwentyBytes){return std::unexpected(ResponderError::Invalid);}
      auto actual=signedContact(*incoming.signedSender,request->id,incoming.fromPort);if(!actual){return std::unexpected(actual.error());}originator=std::move(*actual);
    }else{
      if(incoming.toPort!=static_cast<unsigned>(local_.port)+1||request->method!=Method::AnnouncePeer){return std::optional<Response>{};}
      if(!live()){return std::unexpected(unavailable());}auto saved=tokens_->resolve(request->token);if(!saved){return std::unexpected(stateError(saved.error()));}if(!*saved){
        // The immediately preceding live guard encloses this non-callback
        // binding lookup. No routing/store/response is attempted on a miss.
        requests_=increment(requests_);tokenRejected_=increment(tokenRejected_);
        return std::optional<Response>{};
      }originator=std::move(**saved);
      // Raw a.id and observed fromPort are not identity evidence. Only the
      // original token-map saved full contact supplies the originator/reply Q.
    }
    bool existing=false;std::optional<Read> found;
    if(request->method==Method::GetPeers){
      if(!live()){return std::unexpected(unavailable());}auto result=peers_->read(request->target,wall,16);if(!result){return std::unexpected(stateError(result.error()));}existing=result->keyExists;found=std::move(*result);
    }else if(request->method==Method::AnnouncePeer&&!incoming.signedSender){
      auto known=peers_->contains(request->target);if(!known){return std::unexpected(stateError(known.error()));}existing=*known;
    }
    if(!live()){return std::unexpected(unavailable());}
    auto routing=route(originator,request->method,request->target,existing);if(!live()){return std::unexpected(unavailable());}
    if(!routing||!validRouting(*routing,request->target)){return std::unexpected(ResponderError::Routing);}
    Message response;response.kind=Kind::Reply;response.transaction=request->transaction;response.id=local_.id;
    if(request->method==Method::FindNode){response.hasNodes=true;if(!request->findNodeTarget){response.nodes=std::move(routing->closest);}}
    else if(request->method==Method::GetPeers){
      if(!live()){return std::unexpected(unavailable());}auto token=tokens_->remember(originator);if(!token){return std::unexpected(stateError(token.error()));}response.token.assign(token->begin(),token->end());
      if(found->keyExists){
        response.hasPeers=true;
        for(const auto& peer:found->peers){
          if(request->version==0&&((request->noSeed&&peer.seed)||peer.hash==originator.hash)){continue;}response.peers.push_back(peer);
        }
      }else{response.hasNodes=true;response.nodes=std::move(routing->closest);}
    }else if(request->method==Method::AnnouncePeer&&!incoming.signedSender&&routing->allowStore&&!policy.sleeping&&!policy.suspended){
      if(!live()){return std::unexpected(unavailable());}auto stored=peers_->store(request->target,originator,request->seed,wall);
      // Original receiveStore ACKs distance/quota/sleep declines. Capacity is
      // an explicit local decline, not a successful durable-storage receipt.
      if(!stored&&stored.error()!=StateError::Capacity){return std::unexpected(stateError(stored.error()));}
    }
    if(!live()){return std::unexpected(unavailable());}
    auto payload=encodeReply(response,request->version==0?PeerReplyMode::Ordinary:PeerReplyMode::Azureus);if(!payload){return std::unexpected(ResponderError::Invalid);}
    if(!live()){return std::unexpected(unavailable());}
    const auto replyPort=static_cast<std::uint16_t>(originator.port+1);
    auto result=std::optional<Response>{Response{std::move(originator),local_.port,replyPort,std::move(*payload)}};
    // Full owning response construction and the final live guard succeeded.
    // No counters are committed on a later-returning NoAuthority path.
    requests_=increment(requests_);replyPrepared_=increment(replyPrepared_);
    return result;
  }catch(const std::bad_alloc&){return std::unexpected(ResponderError::Resource);}catch(...){return std::unexpected(ResponderError::Callback);}
}
ReplyResult<StateCounts> Responder::counts()const noexcept{
  if(std::this_thread::get_id()!=thread_){return std::unexpected(ResponderError::Thread);}if(closed_){return std::unexpected(ResponderError::Closed);}
  auto tokens=tokens_->size();if(!tokens){return std::unexpected(stateError(tokens.error()));}auto peers=peers_->counts();if(!peers){return std::unexpected(stateError(peers.error()));}return StateCounts{*tokens,*peers,requests_,replyPrepared_,tokenRejected_};
}
void Responder::close()noexcept{closed_=true;if(tokens_){tokens_->close();}if(peers_){peers_->close();}}
} // namespace biglybtc::services::i2p::dht::inbound
