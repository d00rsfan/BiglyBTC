// SPDX-License-Identifier: GPL-2.0-or-later
#include "actor.hpp"
#include "routing_maintenance.hpp"
#include "transaction_history.hpp"
#include "owned_i2p_profile_fixture.hpp"
#include "source_permit.hpp"
#if defined(BIGLYBTC_TEST_LIVE_I2P_BRIDGE)
#include "bridge.hpp"
#include <sys/inotify.h>
#include <cstring>
#include <future>
#include <condition_variable>
#endif
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
namespace i2p=biglybtc::services::i2p;namespace cp=i2p::i2cp;using Channel=cp::InitialChannel;
namespace {
constexpr std::uint64_t Now=1761739200123ULL;
i2p::ClientIdentity signer(const char* family){
  auto read=[&](const char* suffix){const auto path=QString::fromUtf8(BIGLYBTC_I2P_TEST_KEYS_DIR)+QLatin1Char('/')+QString::fromLatin1(family)+QString::fromLatin1(suffix);QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()<1||file.size()>1024){qFatal("Cannot open bounded signer %s: %s",qPrintable(path),qPrintable(file.errorString()));}const auto bytes=file.readAll();if(file.error()!=QFileDevice::NoError){qFatal("Cannot read signer %s: %s",qPrintable(path),qPrintable(file.errorString()));}return i2p::Bytes(reinterpret_cast<const i2p::Byte*>(bytes.constData()),reinterpret_cast<const i2p::Byte*>(bytes.constData())+bytes.size());};
  auto result=i2p::makeClientIdentity(read(".sender"),i2p::Bytes(256,0x51),read(".known-test-key"));if(!result){qFatal("Cannot import synthetic signer: %s",result.error().message.c_str());}return std::move(*result);
}
struct Router {
  QTcpServer server;QPointer<QTcpSocket> peer;QByteArray bytes;qsizetype offset=1;QString failure;
  Router(){QObject::connect(&server,&QTcpServer::newConnection,&server,[this]{if(peer){failure="Unexpected second client";return;}peer=server.nextPendingConnection();QObject::connect(peer,&QTcpSocket::readyRead,&server,[this]{collect();});collect();});}
  ~Router(){QObject::disconnect(&server,nullptr,&server,nullptr);if(peer){QObject::disconnect(peer,nullptr,&server,nullptr);peer->abort();}}
  void collect(){const auto n=peer->bytesAvailable();if(n<0||n>131077-bytes.size()){failure="Unbounded owned router input";peer->abort();return;}bytes+=peer->read(n);}
  bool listen(bool ipv6=false){return server.listen(ipv6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost,0);}
  Channel::Options options(int budget=3000)const{return {server.serverAddress(),server.serverPort(),"2.13.0",false,budget};}
  bool send(i2p::View data){return peer&&peer->write(reinterpret_cast<const char*>(data.data()),static_cast<qint64>(data.size()))==static_cast<qint64>(data.size());}
  std::optional<cp::Frame> next()const{if(bytes.size()-offset<5){return std::nullopt;}const auto* at=reinterpret_cast<const cp::Byte*>(bytes.constData()+offset);std::uint32_t n=0;for(unsigned i=0;i<4;++i){n=(n<<8)|at[i];}if(n>cp::MaximumPayloadBytes){qFatal("Unbounded actual client frame");}if(bytes.size()-offset<5+static_cast<qsizetype>(n)){return std::nullopt;}auto decoded=cp::decodeFrame(cp::View(at,n+5));if(!decoded){qFatal("Malformed client frame: %s",decoded.error().message.c_str());}return std::move(*decoded);}
  cp::Frame take(){auto frame=next();if(!frame){qFatal("Missing actual client frame");}offset+=5+static_cast<qsizetype>(frame->payload.size());return std::move(*frame);}
  bool status(std::uint32_t nonce,std::uint32_t message,cp::Byte code,std::uint16_t session=7){auto data=cp::encodeMessageStatus({session,message,code,0,nonce});return data&&send(*data);}
  bool lease(std::uint64_t expiry=Now+600000){cp::RequestedLease l;l.gateway.fill(0x51);l.tunnelId=33;l.expiresMilliseconds=expiry;auto data=cp::encodeLeaseRequest({7,{l}});return data&&send(*data);}
};
}

namespace d=i2p::dht;namespace provider=i2p::provider;
// The genuine worker SourcePermit acquires and validates the generated profile;
// the driver only consumes actual Ready and ACKs it. There is no grant setter.
class SourceDriver final:public QObject {
public:
  Channel& channel;provider::SourcePermit permit;d::Actor* actor=nullptr;bool hold=false;
  SourceDriver(std::shared_ptr<provider::ProfileSource> source,Channel& c):channel(c),permit(std::move(source),this){
    QObject::connect(&c,&Channel::sourceEffectRequested,this,[this]{if(permit.phase()==provider::SourcePermit::Phase::Idle){permit.request();}});
  }
  ~SourceDriver()override{actor=nullptr;if(permit.phase()==provider::SourcePermit::Phase::Executing){permit.acknowledge();}permit.stop();permit.join();}
  void release(){hold=false;drain();if(actor){actor->resumeAuthorizedInbound();}}
  void drain(){
    if(hold||permit.phase()!=provider::SourcePermit::Phase::Ready){return;}auto ready=permit.take();if(!ready){if(actor){actor->resumeAuthorizedInbound();}return;}
    channel.resumeAuthorizedEffects();if(actor){actor->resumeAuthorizedInbound();}permit.acknowledge();
  }
  bool event(QEvent* e)override{
    if(e->type()!=provider::SourcePermit::wakeEvent()){return QObject::event(e);}
    drain();if(permit.phase()==provider::SourcePermit::Phase::Idle&&channel.sourceEffectsPending()){permit.request();}return true;
  }
};
class RoutingMaintenanceTests final:public QObject {
  Q_OBJECT
private slots:
  void sourceHeldPingRefreshAndClientReplyOwnership_data(){QTest::addColumn<bool>("revoke");QTest::newRow("real-maintenance-and-client")<<false;QTest::newRow("revoke-before-held-init")<<true;}
  void sourceHeldPingRefreshAndClientReplyOwnership(){
    QFETCH(bool,revoke);
    owned_i2p_test::Fixture fixture;auto source=provider::ProfileSource::open(*fixture.lease,fixture.hash);QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY(storage);
    Router router;QVERIFY(router.listen());Channel channel;QVERIFY(channel.bindEffectAuthority((*source)->effectAuthority()));SourceDriver driver(*source,channel);
    QVERIFY(channel.begin(router.options()));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::GetDateType);auto date=cp::encodeMessage(cp::SetDate{Now,std::string("2.13.0")});QVERIFY(date&&router.send(*date));QTRY_VERIFY(channel.routerDate());auto identity=(*source)->takeIdentity();QVERIFY(identity);
    QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;QVERIFY(driver.permit.request());QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
    {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});QVERIFY(channel.createLeaseSession(std::move(*identity),{{"i2cp.fastReceive","true"}},Now));}driver.release();
    QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateSessionType);auto created=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});QVERIFY(created&&router.send(*created));QTRY_VERIFY(channel.sessionId());QVERIFY(router.lease());QTRY_VERIFY(router.next());QVERIFY(cp::decodeCreateLeaseSet2(router.take()));QTRY_VERIFY(channel.leaseSetSubmission());
    auto remote=signer("dsa");d::Contact seed;seed.hash=remote.destination()->hash;seed.port=10002;seed.destination=*remote.destination();auto nid=d::makeNodeId(seed.hash,seed.port,i2p::Bytes(14,0x32));QVERIFY(nid);seed.id=*nid;auto local=d::makeNodeId((*source)->destination().hash,10000,i2p::Bytes(14,0x31));QVERIFY(local);
    QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;d::Actor actor(channel);driver.actor=&actor;const auto cleanup=qScopeGuard([&]{driver.actor=nullptr;actor.close();});d::Actor::Options options{*local,10000,{seed},3,true};options.routingMaintenance=true;QVERIFY(actor.begin(options));QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);QVERIFY(!channel.dhtScopeObservation(channel.dhtScopeGeneration()).granted);QVERIFY(!router.next());
    if(revoke){(*source)->revoke();QVERIFY(!actor.resumeAuthorizedInbound());driver.release();QCOMPARE(channel.dhtScopeGeneration(),std::uint64_t(0));QVERIFY(!actor.inboundEnabled());QVERIFY(!router.next());return;}driver.release();
    QTRY_VERIFY(router.next());auto sent=cp::decodeDhtSendDatagram(router.take());QVERIFY(sent);auto mux=cp::decodeDhtMuxDatagram(sent->payload);QVERIFY(mux&&mux->protocol==17&&mux->fromPort==10000&&mux->toPort==10002);auto signedPing=i2p::decode1(mux->datagram);QVERIFY(signedPing);auto ping=d::decode(signedPing->payload);QVERIFY(ping&&ping->method==d::Method::Ping);
    const auto answer=[&](const d::Message& query,std::uint32_t message){d::Message response;response.kind=d::Kind::Reply;response.id=seed.id;response.transaction=query.transaction;response.hasNodes=query.method==d::Method::FindNode||query.method==d::Method::GetPeers;auto payload=d::encode(response);if(!payload){return false;}auto envelope=cp::encodeDhtMuxDatagram({18,10002,10001,*payload},cp::GzipMode::Deflate);if(!envelope){return false;}auto frame=cp::encodeMessagePayload({7,message,*envelope});return frame&&router.send(*frame);};
    QVERIFY(answer(*ping,811));QCoreApplication::processEvents();QCOMPARE(actor.observation().phase,d::Phase::Idle);
    // First real10s maintenance delivery produces FIND_NODE, not get_peers;
    // this is a genuine timer, signed packet and worker-held source path.
    QTRY_VERIFY_WITH_TIMEOUT(router.next(),15000);sent=cp::decodeDhtSendDatagram(router.take());QVERIFY(sent);mux=cp::decodeDhtMuxDatagram(sent->payload);QVERIFY(mux&&mux->protocol==17);auto signedFind=i2p::decode1(mux->datagram);QVERIFY(signedFind);auto find=d::decode(signedFind->payload);QVERIFY(find&&find->method==d::Method::FindNode);
    d::Id target{};target.fill(0x5a);auto next=actor.lookupPeers(target,false,3000);QVERIFY(next);QTRY_VERIFY(router.next());sent=cp::decodeDhtSendDatagram(router.take());QVERIFY(sent);mux=cp::decodeDhtMuxDatagram(sent->payload);QVERIFY(mux);auto signedGet=i2p::decode1(mux->datagram);QVERIFY(signedGet);auto get=d::decode(signedGet->payload);QVERIFY(get&&get->method==d::Method::GetPeers);QVERIFY(answer(*find,813));QCoreApplication::processEvents();QCOMPARE(actor.observation().phase,d::Phase::Running);QCOMPARE(actor.observation().rejected,std::uint64_t(0));QVERIFY(answer(*get,814));QTRY_COMPARE(actor.observation().phase,d::Phase::Complete);
  }
  void refreshPolicyKeepsAbsoluteEndsAndClosestFive(){
    auto remote=signer("dsa");d::Contact seed;seed.hash=remote.destination()->hash;seed.destination=*remote.destination();seed.port=10002;seed.id=*d::makeNodeId(seed.hash,seed.port,i2p::Bytes(14,0x32));d::Id target{};target.fill(0x55);
    d::detail::RoutingRefresh refresh;QVERIFY(refresh.begin({target,300000},{&seed,1},0));auto next=refresh.next(0);QVERIFY(next);auto& node=refresh.nodes()[*next];node.phase=d::NodePhase::Sent;node.end=30000;node.ticket=1;node.transaction={1,2,3,4,5,6,7,8};refresh.pause();QCOMPARE(refresh.target().end,std::int64_t(300000));QCOMPARE(node.end,std::int64_t(30000));QCOMPARE(node.phase,d::NodePhase::Candidate);refresh.tick(30000);QVERIFY(!refresh.running());QCOMPARE(node.phase,d::NodePhase::Failed); // No new attempt or extended end after preemption.
    QVERIFY(!refresh.begin({target,300001},{&seed,1},0));QVERIFY(refresh.begin({target,300000},{&seed,1},0));refresh.tick(300000);QVERIFY(!refresh.running());
    std::vector<d::Contact> contacts;for(unsigned i=1;i<=7;++i){i2p::Bytes wire(387);for(std::size_t n=0;n<384;++n){wire[n]=i2p::Byte(n*29+i*7);}wire[0]=i2p::Byte(i);wire[384]=wire[385]=wire[386]=0;auto destination=i2p::decodeDestination(wire);QVERIFY(destination);d::Contact contact;contact.hash=destination->hash;contact.port=static_cast<std::uint16_t>(12000+i);contact.id=*d::makeNodeId(contact.hash,contact.port,i2p::Bytes(14,static_cast<i2p::Byte>(i)));contacts.push_back(contact);}
    QVERIFY(refresh.begin({target,300000},contacts,0));for(unsigned i=0;i<5;++i){auto candidate=refresh.next(0);QVERIFY(candidate);refresh.nodes()[*candidate].phase=d::NodePhase::Replied;}refresh.settle();QVERIFY(!refresh.running());QCOMPARE(std::count_if(refresh.nodes().begin(),refresh.nodes().end(),[](const auto& node){return node.phase==d::NodePhase::Candidate;}),2);
  }
};
QTEST_GUILESS_MAIN(RoutingMaintenanceTests)
#include "i2p_dht_routing_maintenance_tests.moc"
