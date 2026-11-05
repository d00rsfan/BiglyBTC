// SPDX-License-Identifier: GPL-2.0-or-later
// SOURCE-CANDIDATE ONLY: synthetic pure-state inputs, no network/source grant.
#include "inbound_state.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
namespace i=biglybtc::services::i2p;namespace d=i::dht;namespace s=d::inbound;
void require(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
template<class T,class E>T take(std::expected<T,E> value){if(!value){throw std::runtime_error("Unexpected candidate refusal");}return std::move(*value);}
d::Contact contact(unsigned index){
  i::Bytes wire(387);for(std::size_t n=0;n<384;++n){wire[n]=i::Byte(n*29+index*7);}wire[0]=i::Byte(index>>8);wire[1]=i::Byte(index);wire[384]=wire[385]=wire[386]=0;
  auto destination=take(i::decodeDestination(wire));i::Bytes random(14,i::Byte(index));random[0]=i::Byte(index>>8);random[1]=i::Byte(index);
  d::Contact result;result.hash=destination.hash;result.port=10000;result.id=take(d::makeNodeId(result.hash,result.port,random));result.destination=std::move(destination);return result;
}
void tokens(){
  biglybtc::services::azureus_auth::Key secret{};for(std::size_t n=0;n<secret.size();++n){secret[n]=i::Byte(n);}
  auto table=take(s::TokenBindings::create(secret));const auto a=contact(1);auto token=take(table->remember(a));auto found=take(table->resolve(token));require(found&&found->id==a.id&&found->destination->wire==a.destination->wire,"Token must retain exact full originator");
  auto malformed=a;malformed.destination.reset();require(!table->remember(malformed),"Unresolved contact cannot become a raw announce target");
  auto wrong=token;wrong[0]^=1;require(!take(table->resolve(wrong))&&!take(table->resolve(i::View(token).first(7))),"Missing/wrong-length bearer token accepted");
  auto alias=a;alias.id.back()^=1;const auto collision=take(table->remember(alias));require(collision==token,"Original type2 uses first16 NID bytes");found=take(table->resolve(token));require(found&&found->id==alias.id&&take(table->size())==1,"Transport token-map collision must bind latest original full node");
  bool foreign=false;std::thread other([&]{auto result=table->remember(a);foreign=!result&&result.error()==s::StateError::Thread;});other.join();require(foreign,"Token state crossed owner thread");table->close();require(!table->resolve(token),"Closed token table retained a lookup");
  table=take(s::TokenBindings::create(secret));std::array<biglybtc::services::azureus_auth::Token2,514> issued{};
  for(unsigned n=0;n<512;++n){issued[n]=take(table->remember(contact(n+10)));}require(take(table->size())==512,"Token capacity differs from original512");
  require(take(table->resolve(issued[0])).has_value(),"Oldest live token missing");issued[512]=take(table->remember(contact(522)));
  require(!take(table->resolve(issued[1]))&&take(table->resolve(issued[0])).has_value(),"Resolve must touch original access LRU");
  issued[513]=take(table->remember(contact(523)));require(!take(table->resolve(issued[2]))&&take(table->size())==512,"Token LRU evicted incorrectly");
}
void lifetime(){
  auto store=take(s::PeerStore::create());d::Id key{};key.fill(0x51);const auto a=contact(1);
  require(take(store->store(key,a,false,1000))==s::StoreResult::Inserted,"Initial direct store refused");
  require(take(store->store(key,a,true,4000))==s::StoreResult::StoreTimeRefreshed,"Original equal0 update not recognised");auto row=take(store->read(key,4000));require(row.peers.size()==1&&!row.peers[0].seed,"Equal version0 changed original seed flag");
  require(take(store->expire(1000+s::PeerLifetimeMillis,true))==0,"Exact original expiry boundary must survive");
  require(take(store->expire(1001+s::PeerLifetimeMillis,true))==1,"Repeat version0 refreshed original creation time");
  row=take(store->read(key,1002+s::PeerLifetimeMillis));require(row.keyExists&&row.peers.empty(),"Newly empty mapping must survive until next scan");
  (void)take(store->expire(1001+s::PeerLifetimeMillis+s::ExpiryScanMillis));row=take(store->read(key,1002+s::PeerLifetimeMillis+s::ExpiryScanMillis));require(!row.keyExists,"Previously empty mapping did not retire at next scan");
  auto backward=take(s::PeerStore::create());(void)take(backward->store(key,a,false,1000));(void)take(backward->store(key,a,true,500));require(take(backward->expire(501+s::PeerLifetimeMillis,true))==1,"Original reset must clamp creation after backwards wall clock");
  require(!backward->read(key,-1)&&!backward->store(key,a,false,std::numeric_limits<std::int64_t>::max()),"Unsupported/overflowing wall clock accepted");
}
void cyclingAndBounds(){
  auto store=take(s::PeerStore::create());d::Id key{};key.fill(0x52);std::array<d::Contact,18> peers;
  for(unsigned n=0;n<peers.size();++n){peers[n]=contact(n+1);(void)take(store->store(key,peers[n],n%2==0,1000));}
  auto first=take(store->read(key,1000));auto second=take(store->read(key,1001));require(first.peers.size()==16&&second.peers.size()==16&&first.peers[0].hash==peers[0].hash&&second.peers[0].hash==peers[16].hash&&second.peers[1].hash==peers[17].hash,"Original16-value access-order cycle lost peers");
  auto duplicate=take(s::PeerStore::create());auto alias=peers[0];alias.id.back()^=1;(void)take(duplicate->store(key,peers[0],true,1000));(void)take(duplicate->store(key,alias,false,1000));require(take(duplicate->counts()).values==2&&take(duplicate->read(key,1000)).peers.size()==1,"Distinct NID values must deduplicate returned peer hashes");
  auto limited=take(s::PeerStore::create({1,2}));(void)take(limited->store(key,peers[0],false,1000));(void)take(limited->store(key,peers[1],true,1000));
  auto full=limited->store(key,peers[2],true,1000);require(!full&&full.error()==s::StateError::Capacity,"Per-key bound did not refuse new entry");
  (void)take(limited->store(key,peers[0],true,1001));auto another=key;another[0]^=1;require(!limited->store(another,peers[2],true,1001),"Key capacity was evicted implicitly");
  auto counts=take(limited->counts());require(counts.keys==1&&counts.values==2,"Refused store mutated bounded contents");
  bool foreign=false;std::thread other([&]{auto result=limited->read(key,1001);foreign=!result&&result.error()==s::StateError::Thread;});other.join();require(foreign,"Peer store crossed owner thread");limited->close();require(!limited->read(key,1001),"Closed peer store served values");
}
int main(){try{tokens();lifetime();cyclingAndBounds();std::cout<<"PASS detached candidate token/peer state controls\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
