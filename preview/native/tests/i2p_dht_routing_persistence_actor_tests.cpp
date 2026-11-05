// SPDX-License-Identifier: GPL-2.0-or-later
#include "actor.hpp"
#include "owned_i2p_profile_fixture.hpp"
#include "source_permit.hpp"
#include <QFile>
#include <QPointer>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <QDateTime>
#include <limits>
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
class RoutingPersistenceActorTests final:public QObject {
  Q_OBJECT
private slots:
  void actualHeldRestoreExportAndLiveProtocol_data(){QTest::addColumn<bool>("revoke");QTest::newRow("held-restore-export-accepted-query-version")<<false;QTest::newRow("revoke-before-restore")<<true;}
  void actualHeldRestoreExportAndLiveProtocol(){
    QFETCH(bool,revoke);
    owned_i2p_test::Fixture fixture;auto source=provider::ProfileSource::open(*fixture.lease,fixture.hash);QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY(storage);
    const auto config=owned_i2p_test::read(fixture.path/"biglybt.config"),key=owned_i2p_test::read(fixture.key),state=owned_i2p_test::read(fixture.state());
    Router router;QVERIFY(router.listen());Channel channel;QVERIFY(channel.bindEffectAuthority((*source)->effectAuthority()));SourceDriver driver(*source,channel);
    QVERIFY(channel.begin(router.options()));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::GetDateType);auto date=cp::encodeMessage(cp::SetDate{Now,std::string("2.13.0")});QVERIFY(date&&router.send(*date));QTRY_VERIFY(channel.routerDate());auto identity=(*source)->takeIdentity();QVERIFY(identity);
    QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;QVERIFY(driver.permit.request());QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
    {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});QVERIFY(channel.createLeaseSession(std::move(*identity),{{"i2cp.fastReceive","true"}},Now));}driver.release();
    QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateSessionType);auto created=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});QVERIFY(created&&router.send(*created));QTRY_VERIFY(channel.sessionId());QVERIFY(router.lease());QTRY_VERIFY(router.next());QVERIFY(cp::decodeCreateLeaseSet2(router.take()));QTRY_VERIFY(channel.leaseSetSubmission());
    auto remote=signer("dsa");const auto contact=[&](std::uint16_t port,unsigned n){d::Contact value;value.hash=remote.destination()->hash;value.port=port;value.destination=*remote.destination();value.id=*d::makeNodeId(value.hash,port,i2p::Bytes(14,static_cast<i2p::Byte>(n)));return value;};
    const auto seed=contact(10002,0x32),imported=contact(10004,0x33),fresh=contact(10006,0x34);const auto local=d::makeNodeId((*source)->destination().hash,10000,i2p::Bytes(14,0x31));QVERIFY(local);
    QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;d::Actor actor(channel);driver.actor=&actor;const auto cleanup=qScopeGuard([&]{driver.actor=nullptr;actor.close();});d::Actor::Options options{*local,10000,{seed},3,true};
    d::Contact historicalLocal{*d::makeNodeId((*source)->destination().hash,12000,i2p::Bytes(14,0x7a)),(*source)->destination().hash,12000,(*source)->destination()};QVERIFY(historicalLocal.id!=*local);
    options.savedContacts={{historicalLocal,987654321,1,4},{imported,std::numeric_limits<std::int64_t>::max(),1,1}};
    auto invalid=options;invalid.savedContacts.resize(d::MaximumSavedContacts+1);QVERIFY(!actor.begin(invalid));invalid=options;invalid.savedContacts.front().serializationVersion=2;QVERIFY(!actor.begin(invalid));invalid=options;invalid.inbound=false;QVERIFY(!actor.begin(invalid));
    QVERIFY(actor.begin(options));QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);QVERIFY(!actor.exportSavedContacts());QVERIFY(!actor.resumeAuthorizedInbound());QVERIFY(!actor.inboundEnabled());QVERIFY(!router.next());
    if(revoke){(*source)->revoke();QVERIFY(!actor.exportSavedContacts());QVERIFY(!actor.resumeAuthorizedInbound());driver.release();QCOMPARE(channel.dhtScopeGeneration(),std::uint64_t{0});QVERIFY(!actor.inboundEnabled());QVERIFY(!router.next());return;}
    {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});QVERIFY(channel.resumeAuthorizedEffects());QVERIFY(actor.resumeAuthorizedInbound());}
    QVERIFY(actor.inboundEnabled());QVERIFY(!actor.exportSavedContacts());QVERIFY(!router.next());
    // Real wall time advances outside the held grant. Original bestContacts
    // intentionally collapses equal-time rows; do not replace that ordering.
    const auto initialized=QDateTime::currentMSecsSinceEpoch();QTRY_VERIFY(QDateTime::currentMSecsSinceEpoch()>initialized);
    QTRY_VERIFY(driver.permit.phase()==provider::SourcePermit::Phase::Idle||driver.permit.phase()==provider::SourcePermit::Phase::Ready);if(driver.permit.phase()==provider::SourcePermit::Phase::Idle){QVERIFY(driver.permit.request());}QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
    {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});auto rows=actor.exportSavedContacts();QVERIFY(rows);QVERIFY(std::none_of(rows->begin(),rows->end(),[&](const auto& value){return value.contact.id==historicalLocal.id||(value.contact.hash==historicalLocal.hash&&value.contact.id!=*local);}));auto found=std::find_if(rows->begin(),rows->end(),[&](const auto& value){return value.contact.id==imported.id;});QVERIFY(found!=rows->end());QCOMPARE(found->timeAlive,std::int64_t{0});QCOMPARE(found->serializationVersion,std::uint8_t{1});QCOMPARE(found->protocolVersion,std::int8_t{1});auto encoded=d::encodeSavedContacts(*rows);QVERIFY(encoded);auto decoded=d::decodeSavedContacts(*encoded);QVERIFY(decoded);auto roundTrip=d::encodeSavedContacts(*decoded);QVERIFY(roundTrip&&*roundTrip==*encoded);}
    // Two actual signed17 queries enter the existing channel consumer. Ready
    // alone cannot initialize/mutate routing; only fresh take/ACK can reply.
    const auto query=[&](const d::Contact& originator,std::uint32_t message){d::Message request;request.id=originator.id;request.method=originator.id==imported.id?d::Method::FindNode:d::Method::Ping;request.target=*local;request.transaction={1,2,3,4,5,6,7,static_cast<i2p::Byte>(message)};auto payload=d::encode(request);if(!payload){return false;}auto signedPacket=i2p::encode1(remote.destination()->wire,remote.signingPrivateKey(),*payload);if(!signedPacket){return false;}auto envelope=cp::encodeDhtMuxDatagram({17,originator.port,10000,*signedPacket},cp::GzipMode::Deflate);if(!envelope){return false;}auto frame=cp::encodeMessagePayload({7,message,*envelope});return frame&&router.send(*frame);};
    for(const auto& originator:{imported,fresh}){
      QTRY_VERIFY(driver.permit.phase()==provider::SourcePermit::Phase::Idle||driver.permit.phase()==provider::SourcePermit::Phase::Ready);QVERIFY(query(originator,originator.port));QTRY_COMPARE(channel.dhtScopeObservation(channel.dhtScopeGeneration()).queued,std::size_t{1});QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);QVERIFY(!actor.exportSavedContacts());QVERIFY(!router.next());
      {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});QVERIFY(channel.resumeAuthorizedEffects());QVERIFY(actor.resumeAuthorizedInbound());}
      QTRY_VERIFY(router.next());auto frame=cp::decodeDhtSendDatagram(router.take());QVERIFY(frame);auto envelope=cp::decodeDhtMuxDatagram(frame->payload);QVERIFY(envelope&&envelope->protocol==18&&envelope->toPort==originator.port+1);auto reply=d::decode(envelope->datagram);QVERIFY(reply&&reply->kind==d::Kind::Reply&&reply->version==4);if(originator.id==imported.id){QVERIFY(reply->hasNodes);QVERIFY(std::none_of(reply->nodes.begin(),reply->nodes.end(),[&](const auto& value){return value.id==historicalLocal.id||(value.hash==historicalLocal.hash&&value.id!=*local);}));}
      const auto answered=QDateTime::currentMSecsSinceEpoch();QTRY_VERIFY(QDateTime::currentMSecsSinceEpoch()>answered);
    }
    QTRY_VERIFY(driver.permit.phase()==provider::SourcePermit::Phase::Idle||driver.permit.phase()==provider::SourcePermit::Phase::Ready);if(driver.permit.phase()==provider::SourcePermit::Phase::Idle){QVERIFY(driver.permit.request());}QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
    {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});auto rows=actor.exportSavedContacts();QVERIFY(rows);QVERIFY(std::none_of(rows->begin(),rows->end(),[&](const auto& value){return value.contact.id==historicalLocal.id;}));QVERIFY(!rows->empty());QCOMPARE(rows->front().contact.id,imported.id);QCOMPARE(rows->front().protocolVersion,std::int8_t{4});QVERIFY(rows->front().timeAlive>=0&&rows->front().timeAlive<10000);auto live=std::find_if(rows->begin(),rows->end(),[&](const auto& value){return value.contact.id==fresh.id;});QVERIFY(live!=rows->end());QCOMPARE(live->protocolVersion,std::int8_t{4});QVERIFY(live!=rows->begin());for(const auto& value:*rows){QCOMPARE(value.serializationVersion,std::uint8_t{1});}}
    actor.stopInbound();QVERIFY(!actor.exportSavedContacts());QVERIFY(actor.enableInbound());QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
    {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});QVERIFY(actor.resumeAuthorizedInbound());auto rows=actor.exportSavedContacts();QVERIFY(rows);QVERIFY(std::none_of(rows->begin(),rows->end(),[&](const auto& value){return value.contact.id==imported.id||value.contact.id==fresh.id;}));}
    QTRY_VERIFY(driver.permit.phase()==provider::SourcePermit::Phase::Idle||driver.permit.phase()==provider::SourcePermit::Phase::Ready);if(driver.permit.phase()==provider::SourcePermit::Phase::Idle){QVERIFY(driver.permit.request());}QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);(*source)->revoke();QVERIFY(!actor.exportSavedContacts());QVERIFY(!actor.resumeAuthorizedInbound());driver.release();QVERIFY(!actor.exportSavedContacts());QCOMPARE(channel.dhtScopeGeneration(),std::uint64_t{0});
    QCOMPARE(owned_i2p_test::read(fixture.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(fixture.key),key);QCOMPARE(owned_i2p_test::read(fixture.state()),state);
  }
};
QTEST_GUILESS_MAIN(RoutingPersistenceActorTests)
#include "i2p_dht_routing_persistence_actor_tests.moc"
