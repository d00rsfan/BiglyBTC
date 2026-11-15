// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owned_transport.hpp"
#include "database.hpp"
#include <QTest>
#include <QDateTime>
#include <QScopeGuard>
#include <QCryptographicHash>
#include <thread>
using namespace biglybtc;using namespace std::chrono_literals;
namespace udp=services::azureus_udp;namespace db=services::azureus_db_runtime;namespace nodes=services::azureus_nodes;
namespace generated_cache {
void need(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
struct Clock final:db::lifecycle::WallClock{std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}};
struct Inputs final:services::azureus_storage_stats::DiversificationControl {std::expected<std::int64_t,services::azureus_storage_stats::Error> expiryJitterMilliseconds()noexcept override{return 0;}std::int64_t suspendedUntilMilliseconds()const noexcept override{return 0;}};
template<class F>bool spin(F condition,int milliseconds=2000){QDeadlineTimer end(milliseconds);while(!condition()&&!end.hasExpired()){QTest::qWait(1);}return condition();}
nodes::Contact contact(udp::Peer peer){nodes::ResolvedEndpoint ep;ep.address.port=peer.port;if(peer.address.protocol()==QAbstractSocket::IPv6Protocol){ep.address.length=16;const auto ip=peer.address.toIPv6Address();std::copy_n(ip.c,16,ep.address.bytes.begin());}else{ep.address.length=4;const auto ip=peer.address.toIPv4Address();for(unsigned i=0;i<4;++i){ep.address.bytes[i]=std::uint8_t(ip>>(24-8*i));}}nodes::NodeCache cache;auto id=cache.lookup(ep,55);need(bool(id),"Generated cache wire identity");nodes::Contact c;c.protocol=55;c.id=id->id;c.address=ep.address;return c;}
struct Actor {
 bool held=false,current=true,hold=false;std::vector<std::move_only_function<void(bool)>> pending;std::function<void(std::string_view)> onKey;
 udp::OwnedPingTransport transport;std::shared_ptr<db::Owner> database;std::shared_ptr<services::azureus_router::RouterOwner> router;nodes::Contact local;
 template<class F>void grant(F fn){const auto old=held;held=true;const auto reset=qScopeGuard([&]{held=old;});fn();}
 bool permit(std::move_only_function<void(bool)> done){if(hold){pending.push_back(std::move(done));return true;}grant([&]{done(current);});return true;}
 Actor(const char* address,unsigned k,unsigned residue=0):transport([address]{udp::Config c;c.network=QHostAddress(QString::fromLatin1(address)).protocol()==QAbstractSocket::IPv6Protocol?3:4;c.servePings=true;c.routingScope=udp::RoutingScope::OwnedLoopback;return c;}()){
  const QHostAddress bind(QString::fromLatin1(address));quint16 port=0;if(bind.protocol()==QAbstractSocket::IPv6Protocol){for(unsigned attempt=0;attempt<256;++attempt){QUdpSocket reserve;need(reserve.bind(bind,0),"Generated IPv6 port reservation");port=reserve.localPort();if(port%8==residue){break;}}need(port%8==residue,"Generated unique original IPv6 ID");}
  need(bool(transport.start(bind,port)),"Generated cache wire socket");local=contact(transport.endpoint());db::Limits limits;limits.storageStats=db::StorageStatsMode::OriginalDiversification;limits.diversificationControl=std::make_shared<Inputs>();auto made=db::Owner::create(limits);need(bool(made),"Generated cache wire DB");database=std::move(*made);need(bool(database->restoreLocalDiversifications({},QDateTime::currentMSecsSinceEpoch()))&&bool(database->bindLocal(local)),"Generated cache wire bind");services::azureus_router::RouterConfig config;config.k=std::uint16_t(k);auto routing=services::azureus_router::RouterOwner::create(local.id,config);need(bool(routing),"Generated cache wire router");router=std::move(*routing);
  grant([&]{need(bool(transport.serveDatabase(router,database,[this]{return current&&held;},[this](auto done){return permit(std::move(done));})),"Generated cache wire service");});need(bool(transport.setLocalReadPreflight([this](std::string key,auto done){return permit([this,key=std::move(key),done=std::move(done)](bool allowed)mutable{if(onKey){onKey(key);}done(allowed);});})),"Generated cache key gate");
 }
 ~Actor(){transport.close();}
};
struct Fixture {
 Actor a,b,c;std::string key;
 explicit Fixture(unsigned k=20,bool v6=false):a(v6?"::1":"127.141.71.1",k,0),b(v6?"::1":"127.142.71.1",k,1),c(v6?"::1":"127.143.71.1",k,2){
  std::array actors{&a,&b,&c};for(auto* local:actors){for(auto* peer:actors){if(local!=peer){local->grant([&]{need(bool(local->transport.attachKnownPeer(peer->transport.endpoint(),55,std::chrono::steady_clock::now()+5s)),"Generated cache peer attachment");});}}}for(auto* actor:actors){actor->grant([&]{need(bool(actor->transport.integrateKnownRouting(2s)),"Generated cache real integration");});}need(spin([&]{return std::ranges::all_of(actors,[](auto* actor){auto s=actor->transport.routingSnapshot();return s&&s->finished&&s->seeded;});}),"Generated cache actual seeded state");key.assign(reinterpret_cast<const char*>(a.local.id.data()),a.local.id.size());
 }
 void chooseOutsideKey(){for(unsigned n=0;n<512;++n){auto hash=QCryptographicHash::hash(QByteArray::number(n),QCryptographicHash::Sha1);nodes::NodeId id;std::copy_n(reinterpret_cast<const std::uint8_t*>(hash.constData()),20,id.begin());const auto closest=a.router->localInClosest(id,2,false);if(closest&&!*closest){key.assign(hash.constData(),hash.size());return;}}throw std::runtime_error("Generated cache outside key");}
 void publish(Actor& publisher,QObject* receiver,char byte){
  std::optional<udp::DetailedFindNodeOutcome> token;publisher.grant([&]{need(bool(publisher.transport.requestStoreToken(a.transport.endpoint(),55,receiver,[&](auto result){token.emplace(std::move(result));},1s)),"Generated direct token request");});need(spin([&]{return token.has_value();})&&bool(*token),"Generated direct token response");const auto* response=std::get_if<udp::find::Decoded>(&**token);need(response!=nullptr,"Generated direct token kind");udp::StoreRequest request;request.randomId=std::get<udp::find::ReplyBody>(response->body()).randomId;request.keys={key};request.valueSets={{{1,QDateTime::currentMSecsSinceEpoch(),std::string(512,byte),publisher.local,0x21,72,-1}}};std::optional<udp::StoreOutcome> stored;publisher.grant([&]{need(bool(publisher.transport.store(a.transport.endpoint(),55,std::move(request),receiver,[&](auto result){stored.emplace(std::move(result));},1s)),"Generated direct STORE request");});need(spin([&]{return stored.has_value();})&&bool(*stored),"Generated direct STORE response");
 }
 void seed(QObject* receiver,bool own=true){publish(b,receiver,'a');publish(c,receiver,'b');if(own){Clock clock;need(bool(a.database->putLocal(key,"generated own value excluded",77,clock)),"Generated own cache exclusion");}}
 void start(std::chrono::milliseconds duration=2s){a.grant([&]{need(bool(a.transport.republishCachedValues(QDateTime::currentMSecsSinceEpoch()+1000000,1000,duration)),"Generated cache start");});}
};
}