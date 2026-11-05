// SPDX-License-Identifier: GPL-2.0-or-later
#include "channel.hpp"
#include "owned_i2p_profile_fixture.hpp"
#include "source_permit.hpp"
#include <QFile>
#include <QPointer>
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

namespace provider=i2p::provider;
// The genuine worker SourcePermit acquires and validates the generated profile;
// the driver only consumes actual Ready and ACKs it. There is no grant setter.
class SourceDriver final:public QObject {
public:
  Channel& channel;provider::SourcePermit permit;bool hold=false;
  SourceDriver(std::shared_ptr<provider::ProfileSource> source,Channel& c):channel(c),permit(std::move(source),this){
    QObject::connect(&c,&Channel::sourceEffectRequested,this,[this]{if(permit.phase()==provider::SourcePermit::Phase::Idle){permit.request();}});
  }
  ~SourceDriver()override{if(permit.phase()==provider::SourcePermit::Phase::Executing){permit.acknowledge();}permit.stop();permit.join();}
  void release(){hold=false;drain();}
  void drain(){
    if(hold||permit.phase()!=provider::SourcePermit::Phase::Ready){return;}auto ready=permit.take();if(!ready){return;}
    channel.resumeAuthorizedEffects();permit.acknowledge();
  }
  bool event(QEvent* e)override{
    if(e->type()!=provider::SourcePermit::wakeEvent()){return QObject::event(e);}
    drain();if(permit.phase()==provider::SourcePermit::Phase::Idle&&channel.sourceEffectsPending()){permit.request();}return true;
  }
};
class OwnedLookupLifetimeTests final:public QObject {
  Q_OBJECT
private slots:
  void realReadyCancellationsCross1024AndOldHostRepliesStayRetired(){
    owned_i2p_test::Fixture fixture;auto source=provider::ProfileSource::open(*fixture.lease,fixture.hash);QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY(storage);
    const auto config=owned_i2p_test::read(fixture.path/"biglybt.config"),key=owned_i2p_test::read(fixture.key),state=owned_i2p_test::read(fixture.state());
    Router router;QVERIFY(router.listen());Channel channel;QVERIFY(channel.bindEffectAuthority((*source)->effectAuthority()));SourceDriver driver(*source,channel);
    QVERIFY(channel.begin(router.options(30000)));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::GetDateType);auto date=cp::encodeMessage(cp::SetDate{Now,std::string("2.13.0")});QVERIFY(date&&router.send(*date));QTRY_VERIFY(channel.routerDate());
    auto identity=(*source)->takeIdentity();QVERIFY(identity);QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;QVERIFY(driver.permit.request());QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
    {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});QVERIFY(channel.createLeaseSession(std::move(*identity),{{"i2cp.fastReceive","true"}},Now));}driver.release();
    QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateSessionType);auto created=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});QVERIFY(created&&router.send(*created));QTRY_VERIFY(channel.sessionId());QVERIFY(router.lease());QTRY_VERIFY(router.next());QVERIFY(cp::decodeCreateLeaseSet2(router.take()));QTRY_VERIFY(channel.leaseSetSubmission());
    QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;QVERIFY(driver.permit.request());QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);QVERIFY((*source)->effectAuthority()->current());QVERIFY(!(*source)->effectAuthority()->granted());
    auto oldPeer=signer("dsa"),freshPeer=signer("ed25519");constexpr std::uint64_t First=0x80000000ULL;std::uint64_t first=0,last=0;
    // No ID hook and no per-ID slow I/O: one genuine Ready is unconsumed.
    // Each cancellation releases the existing one active slot; history stays8.
    for(unsigned i=0;i<1100;++i){auto operation=channel.lookupDestination(oldPeer.destination()->hash,3000);QVERIFY2(operation,operation?"":operation.error().message.c_str());QCOMPARE(*operation,First+i);if(!first){first=*operation;}QVERIFY(*operation>last);last=*operation;
      auto second=channel.lookupDestination(freshPeer.destination()->hash,3000);QVERIFY(!second);auto pending=channel.destinationLookupObservation(*operation);QVERIFY(pending&&pending->phase==Channel::LookupPhase::Pending);
      QVERIFY(channel.cancelDestinationLookup(*operation));QVERIFY(!channel.cancelDestinationLookup(*operation));auto cancelled=channel.destinationLookupObservation(*operation);QVERIFY(cancelled&&cancelled->phase==Channel::LookupPhase::Cancelled);
      if(i>=8){QVERIFY(!channel.destinationLookupObservation(first));}
    }
    QVERIFY(last>First+1024);QVERIFY(!router.next());QVERIFY(!channel.resumeAuthorizedEffects());QVERIFY(!(*source)->effectAuthority()->granted());
    for(std::uint64_t id=last-7;id<=last;++id){const auto terminal=channel.destinationLookupObservation(id);QVERIFY(terminal&&terminal->phase==Channel::LookupPhase::Cancelled);}QVERIFY(!channel.destinationLookupObservation(last-8));
    auto stale=channel.lookupDestination(oldPeer.destination()->hash,3000);QVERIFY(stale&&*stale==last+1);QVERIFY(!router.next());
    {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});QVERIFY(channel.resumeAuthorizedEffects());}
    QTRY_VERIFY(router.next());auto staleWire=cp::decodeHostLookup(router.take());QVERIFY(staleWire&&staleWire->request==*stale&&staleWire->hash==oldPeer.destination()->hash&&staleWire->timeoutMilliseconds==3000);QVERIFY(channel.cancelDestinationLookup(*stale));
    auto current=channel.lookupDestination(freshPeer.destination()->hash,3000);QVERIFY(current&&*current==*stale+1);QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);QVERIFY(!(*source)->effectAuthority()->granted());QVERIFY(!router.next());
    auto old=cp::encodeHostReply({7,static_cast<std::uint32_t>(*stale),0,*oldPeer.destination()});auto premature=cp::encodeHostReply({7,static_cast<std::uint32_t>(*current),0,*freshPeer.destination()});QVERIFY(old&&premature&&router.send(*old)&&router.send(*premature));
    // A disabled fast-receive payload is an ordered TCP parser barrier. Its
    // existing passive drop proves both preceding39frames were processed,
    // without consuming a source grant or relying on a scheduling sleep.
    const auto dropped=channel.receiverObservation().dropped;auto barrier=cp::encodeMessagePayload({7,901,i2p::Bytes{0x51}});QVERIFY(barrier&&router.send(*barrier));QTRY_COMPARE(channel.receiverObservation().dropped,dropped+1);QVERIFY(channel.receiverObservation().lastDrop==Channel::ReceiveDrop::Disabled);
    auto pending=channel.destinationLookupObservation(*current);QVERIFY(pending&&pending->phase==Channel::LookupPhase::Pending);QVERIFY(!router.next());
    {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});QVERIFY(channel.resumeAuthorizedEffects());}
    QTRY_VERIFY(router.next());auto wire=cp::decodeHostLookup(router.take());QVERIFY(wire&&wire->request==*current&&wire->hash==freshPeer.destination()->hash&&wire->timeoutMilliseconds==3000);
    // Same TCP stream processes old first. If it settled the current operation,
    // its DIFFERENT destination hash would fail it before the valid reply.
    auto wrongSession=cp::encodeHostReply({8,static_cast<std::uint32_t>(*current),0,*oldPeer.destination()});auto valid=cp::encodeHostReply({7,static_cast<std::uint32_t>(*current),0,*freshPeer.destination()});QVERIFY(wrongSession&&valid&&router.send(*old)&&router.send(*wrongSession)&&router.send(*valid));
    QTRY_VERIFY(channel.destinationLookupObservation(*current)&&channel.destinationLookupObservation(*current)->phase==Channel::LookupPhase::Resolved);const auto resolved=channel.destinationLookupObservation(*current);QVERIFY(resolved->destination&&resolved->destination->wire==freshPeer.destination()->wire);QCOMPARE(channel.destinationLookupObservation(*stale)->phase,Channel::LookupPhase::Cancelled);QCOMPARE(channel.state(),Channel::State::SessionCreated);
    QCOMPARE(owned_i2p_test::read(fixture.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(fixture.key),key);QCOMPARE(owned_i2p_test::read(fixture.state()),state);
  }
};
QTEST_GUILESS_MAIN(OwnedLookupLifetimeTests)
#include "i2cp_owned_lookup_lifetime_tests.moc"
