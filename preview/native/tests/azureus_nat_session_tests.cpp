// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_nat/session.hpp"
#include "shared_udp_fixture.hpp"
#include <QScopeGuard>
namespace nat=services::azureus_nat;
namespace {
nat::udp::Address addr(udp::Peer p){nat::udp::Address a;a.port=p.port;if(p.address.protocol()==QAbstractSocket::IPv4Protocol){a.length=4;auto ip=p.address.toIPv4Address();for(unsigned i=0;i<4;++i){a.bytes[i]=static_cast<std::uint8_t>(ip>>(24-8*i));}}else{a.length=16;auto ip=p.address.toIPv6Address();std::copy_n(ip.c,16,a.bytes.begin());}return a;}
nat::RoutedContact contact(Actor& a){nat::nodes::Contact external;external.protocol=55;external.address=addr(a.transport.endpoint());nat::nodes::NodeCache cache;auto c=nat::RoutedContact::observed(external,addr(a.actualEndpoint),cache);need(bool(c),"Owned external/physical contact");return std::move(*c);}
nat::SessionHooks hooks(Actor& a,QObject* context){nat::SessionHooks h;
 h.revision=[&a]()->std::optional<std::uint64_t>{need(a.sourceHeld,"Revision read only under source permit");return a.permit?std::optional{a.revision}:std::nullopt;};
 h.afterRelease=[&a,context](auto fn){return QMetaObject::invokeMethod(context,[&a,fn=std::move(fn)]()mutable{need(!a.sourceHeld,"Decode after source release");fn();},Qt::QueuedConnection);};
 h.withPermit=[&a](auto end,auto fn){need(!a.sourceHeld,"Never nest source permit acquisition");if(!a.permit||std::chrono::steady_clock::now()>=end){return false;}a.sourceHeld=true;auto reset=qScopeGuard([&a]{a.sourceHeld=false;});fn();return true;};return h;
}
std::unique_ptr<nat::Session> session(Actor& a,QObject* ctx){auto s=nat::Session::open(a.transport,a.revision,hooks(a,ctx));need(bool(s),"Owned NAT registration");return std::move(*s);}
void admit(Actor& a,Actor& b){need(bool(a.transport.allowPeer(b.actualEndpoint)),"Owned existing peer admission");need(bool(b.transport.allowPeer(a.actualEndpoint)),"Owned existing reverse admission");}
}
class AzureusNatSessionTests final:public QObject {
 Q_OBJECT
private slots:
 void threePartyEarlyInboundKeepsExternalIdentity_data(){QTest::addColumn<bool>("v6");QTest::addColumn<bool>("roles");QTest::newRow("IPv4")<<false<<false;QTest::newRow("IPv6")<<true<<false;QTest::newRow("IPv4-external-port")<<false<<true;QTest::newRow("IPv6-external-port")<<true<<true;}
 void threePartyEarlyInboundKeepsExternalIdentity(){
  QFETCH(bool,v6);QFETCH(bool,roles);Actor caller(QHostAddress(v6?"::1":"127.121.29.1"),v6,0,roles),relay(QHostAddress(v6?"::1":"127.121.29.2"),v6,0,roles),target(QHostAddress(v6?"::1":"127.121.29.3"),v6,0,roles);admit(caller,relay);admit(relay,target);admit(caller,target);
  auto c=session(caller,this),r=session(relay,this),t=session(target,this);auto before=contact(target);QVERIFY(t->selectRendezvous(contact(relay)));QTRY_VERIFY_WITH_TIMEOUT(t->facts().rendezvousActive,2000);QCOMPARE(r->facts().bindings,std::size_t{1});
  std::optional<nat::TunnelOutcome> result;auto id=c->punch(contact(relay),before,this,[&](auto value){result.emplace(std::move(value));});QVERIFY(id);QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),3000);QVERIFY(*result);QVERIFY((**result).directObserved);QCOMPARE((**result).contact.transport(),addr(target.actualEndpoint));QCOMPARE((**result).contact.external().address,before.external().address);QCOMPARE((**result).contact.external().id,before.external().id);QCOMPARE(c->facts().punches,std::size_t{0});QVERIFY(c->facts().earlyInbound>=1);QCOMPARE(r->facts().relays,std::size_t{0});
  nat::Message query;query.type=nat::Type::Query;auto encoded=nat::encode(query);QVERIFY(encoded);std::optional<udp::TransferOutcome> queried;QVERIFY(caller.transport.callTransfer(relay.actualEndpoint,55,std::string(nat::handlerKey()),*encoded,this,[&](auto v){queried.emplace(std::move(v));},std::chrono::steady_clock::now()+1s));QTRY_VERIFY_WITH_TIMEOUT(queried.has_value(),1500);QVERIFY(*queried);nat::nodes::NodeCache queryCache;auto value=nat::decode(**queried,queryCache);QVERIFY(value&&value->type==nat::Type::QueryReply&&*value->ok);QCOMPARE(*value->ip,std::string(v6?"0:0:0:0:0:0:0:1":"127.121.29.1"));QCOMPARE(*value->port,caller.actualEndpoint.port);
QVERIFY(!caller.transport.isRoutingPeer({QHostAddress(v6?"::1":"127.121.29.9"),54321}));
 }
 void wrongRelayAndOpaqueDataAreRejectedWithoutApplicationEffects(){
  Actor caller(QHostAddress("127.121.30.1"),false),relay(QHostAddress("127.121.30.2"),false),target(QHostAddress("127.121.30.3"),false);admit(caller,relay);admit(relay,target);admit(caller,target);auto c=session(caller,this),r=session(relay,this),t=session(target,this);QVERIFY(t->selectRendezvous(contact(relay)));QTRY_VERIFY_WITH_TIMEOUT(t->facts().rendezvousActive,2000);
  nat::Message m;m.type=nat::Type::Connect;m.origin=nat::nodes::encodeContact(contact(caller).exportSnapshot()).value();m.port=caller.actualEndpoint.port;auto bytes=nat::encode(m);QVERIFY(bytes);std::optional<udp::TransferOutcome> response;
  QVERIFY(caller.transport.callTransfer(target.actualEndpoint,55,std::string(nat::handlerKey()),*bytes,this,[&](auto v){response.emplace(std::move(v));},std::chrono::steady_clock::now()+1s));QTRY_VERIFY_WITH_TIMEOUT(response.has_value(),1500);QVERIFY(*response);nat::nodes::NodeCache cache;auto reply=nat::decode(**response,cache);QVERIFY(reply&&reply->type==nat::Type::ConnectReply&&!*reply->ok);QCOMPARE(t->facts().connects,std::size_t{0});
  m={};m.type=nat::Type::Punch;m.target=nat::socketAddress(contact(target).external().address).value();m.clientData="d9:_notunneli1ee";bytes=nat::encode(m);QVERIFY(bytes);response.reset();QVERIFY(caller.transport.callTransfer(relay.actualEndpoint,55,std::string(nat::handlerKey()),*bytes,this,[&](auto v){response.emplace(std::move(v));},std::chrono::steady_clock::now()+1s));QTRY_VERIFY_WITH_TIMEOUT(response.has_value(),1500);QVERIFY(*response);reply=nat::decode(**response,cache);QVERIFY(reply&&reply->type==nat::Type::PunchReply&&!*reply->ok);QCOMPARE(r->facts().relays,std::size_t{0});
  m={};m.type=nat::Type::TunnelInbound;bytes=nat::encode(m);response.reset();auto rejected=c->facts().rejected;QVERIFY(relay.transport.writeTransfer(caller.actualEndpoint,55,std::string(nat::handlerKey()),*bytes,this,[&](auto v){response.emplace(std::move(v));},std::chrono::steady_clock::now()+1s));QTRY_VERIFY_WITH_TIMEOUT(response.has_value()&&c->facts().rejected>rejected,1500);QVERIFY(*response);QCOMPARE(c->facts().punches,std::size_t{0});
 }
 void boundedCancelCloseAndChangedSourceCannotCompleteAnOldPunch(){
  Actor caller(QHostAddress("127.121.31.1"),false),relay(QHostAddress("127.121.31.2"),false),target(QHostAddress("127.121.31.3"),false);admit(caller,relay);admit(relay,target);admit(caller,target);auto c=session(caller,this),r=session(relay,this),t=session(target,this);QVERIFY(t->selectRendezvous(contact(relay)));QTRY_VERIFY_WITH_TIMEOUT(t->facts().rendezvousActive,2000);
  unsigned cancelled=0;std::vector<std::uint64_t> ids;for(int i=0;i<3;++i){auto id=c->punch(contact(relay),contact(target),this,[&](auto result){need(!result&&result.error()==nat::SessionFailure::Cancelled,"Exactly cancelled result");++cancelled;});QVERIFY(id);ids.push_back(*id);}QVERIFY(!c->punch(contact(relay),contact(target),this,[](auto){}));for(auto id:ids){QVERIFY(c->cancel(id));QVERIFY(!c->cancel(id));}QTRY_COMPARE_WITH_TIMEOUT(cancelled,3U,1000);QCOMPARE(c->facts().punches,std::size_t{0});
  std::optional<nat::TunnelOutcome> result;QVERIFY(c->punch(contact(relay),contact(target),this,[&](auto value){result.emplace(std::move(value));}));++caller.revision;QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),1500);QVERIFY(!*result&&result->error()==nat::SessionFailure::Source);QVERIFY(c->facts().closed);QCOMPARE(c->facts().punches,std::size_t{0});QVERIFY(!c->selectRendezvous(contact(relay)));QVERIFY(!c->punch(contact(relay),contact(target),this,[](auto){}));c->close();
  // Destruction unregisters only its own handler, leaving the admitted routing
  // peer and independent data handler usable.
  const std::string other(20,'O');QVERIFY(caller.room(other,[](auto v){return v;}));QVERIFY(relay.room(other,[](auto v){return v;}));std::optional<udp::TransferOutcome> ordinary;QVERIFY(relay.transport.callTransfer(caller.actualEndpoint,55,other,"still independent",this,[&](auto v){ordinary.emplace(std::move(v));},std::chrono::steady_clock::now()+1s));QTRY_VERIFY_WITH_TIMEOUT(ordinary.has_value(),1500);QVERIFY(*ordinary&&**ordinary=="still independent");
 }
};
QTEST_GUILESS_MAIN(AzureusNatSessionTests)
#include "azureus_nat_session_tests.moc"
