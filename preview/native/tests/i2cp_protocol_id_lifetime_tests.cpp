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
#include <algorithm>
#include <array>
#include <zlib.h>
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
namespace st=i2p::streaming;
namespace {
std::uint32_t word(const i2p::Bytes& bytes,std::size_t at){if(bytes.size()<at+4){qFatal("Truncated real tracker packet");}std::uint32_t value=0;for(unsigned i=0;i<4;++i){value=(value<<8)|bytes[at+i];}return value;}
QByteArray words(std::initializer_list<std::uint32_t> input){QByteArray out;for(const auto value:input){for(int i=3;i>=0;--i){out.append(static_cast<char>(value>>(8*i)));}}return out;}
i2p::Bytes rawTracker(const QByteArray& payload){
  z_stream z{};if(deflateInit2(&z,3,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)!=Z_OK){qFatal("Generated tracker gzip init");}
  i2p::Bytes result(2048);z.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(payload.constData()));z.avail_in=static_cast<uInt>(payload.size());z.next_out=result.data();z.avail_out=static_cast<uInt>(result.size());
  const auto done=deflate(&z,Z_FINISH);result.resize(z.total_out);deflateEnd(&z);if(done!=Z_STREAM_END||result.size()<18){qFatal("Generated tracker gzip bounds");}
  result[4]=static_cast<i2p::Byte>(6969>>8);result[5]=static_cast<i2p::Byte>(6969);result[6]=static_cast<i2p::Byte>(9000>>8);result[7]=static_cast<i2p::Byte>(9000);result[9]=18;return result;
}
}
class ProtocolIdLifetimeTests final:public QObject {
  Q_OBJECT
  void registered(Router& router,Channel& channel,SourceDriver& driver,std::shared_ptr<provider::ProfileSource> source){
    QVERIFY(channel.begin(router.options(30000)));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::GetDateType);
    auto date=cp::encodeMessage(cp::SetDate{Now,std::string("2.13.0")});QVERIFY(date&&router.send(*date));QTRY_VERIFY(channel.routerDate());
    auto identity=source->takeIdentity();QVERIFY(identity);QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;QVERIFY(driver.permit.request());QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
    {QVERIFY(driver.permit.take());const auto release=qScopeGuard([&]{driver.permit.acknowledge();});auto created=channel.createLeaseSession(std::move(*identity),{{"i2cp.fastReceive","true"}},Now);QVERIFY2(created,created?"":created.error().message.c_str());}
    driver.release();QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateSessionType);auto created=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});QVERIFY(created&&router.send(*created));QTRY_VERIFY(channel.sessionId());
    QVERIFY(router.lease());QTRY_VERIFY(router.next());QVERIFY(cp::decodeCreateLeaseSet2(router.take()));QTRY_VERIFY(channel.leaseSetSubmission());
  }
  void holdReady(SourceDriver& driver){
    driver.hold=true;QTRY_VERIFY(driver.permit.phase()==provider::SourcePermit::Phase::Ready||driver.permit.phase()==provider::SourcePermit::Phase::Idle);
    if(driver.permit.phase()==provider::SourcePermit::Phase::Idle){QVERIFY(driver.permit.request());}QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
  }
  cp::HostLookup lookup(Router& router){auto value=cp::decodeHostLookup(router.take());if(!value){qFatal("Malformed actual tracker lookup");}return *value;}
  void answerLookup(Router& router,const cp::HostLookup& lookup,const i2p::ClientIdentity& peer){auto bytes=cp::encodeHostReply({7,lookup.request,0,*peer.destination()});QVERIFY(bytes&&router.send(*bytes));}
  st::Packet streamPacket(Router& router,const i2p::Destination& local,const i2p::Destination& remote){
    auto frame=cp::decodeSendDatagram(router.take());if(!frame||frame->sessionId!=7||frame->nonce||frame->recipient.wire!=remote.wire||!frame->expirationMilliseconds||frame->skipLeaseSet){qFatal("Malformed actual stream SendMessage");}auto mux=st::decodeMux(frame->payload);if(!mux||mux->fromPort!=9000||mux->toPort!=80){qFatal("Wrong actual stream tuple");}auto packet=st::decode(mux->packet,local);if(!packet){qFatal("Actual stream signature failed");}return *packet;
  }
  void deliverStream(Router& router,const st::Packet& packet,const i2p::ClientIdentity& peer,std::uint32_t message){
    auto bytes=st::encode(packet,peer);QVERIFY(bytes);auto mux=st::encodeMux({80,9000,std::move(*bytes)});QVERIFY(mux);auto frame=cp::encodeMessagePayload({7,message,std::move(*mux)});QVERIFY(frame&&router.send(*frame));
  }
  std::uint32_t trackerTransaction(Router& router,const i2p::Destination& local,const i2p::Destination& remote,bool connect){
    auto frame=cp::decodeSendDatagram(router.take());if(!frame||frame->nonce||frame->recipient.wire!=remote.wire||!frame->expirationMilliseconds||frame->skipLeaseSet==connect){qFatal("Actual tracker envelope changed");}
    auto mux=cp::decodeMuxDatagram(frame->payload);if(!mux||mux->fromPort!=9000||mux->toPort!=6969||mux->protocol!=(connect?19:20)){qFatal("Actual tracker tuple changed");}
    i2p::Bytes data;
    if(connect){auto body=i2p::decode2(mux->datagram,remote.hash);if(!body||body->sender.wire!=local.wire){qFatal("Actual connect identity/signature differs");}data=std::move(body->payload);}
    else{auto body=i2p::decode3(mux->datagram);if(!body||body->claimedSender!=local.hash){qFatal("Actual scrape identity differs");}data=std::move(body->payload);}
    if(data.size()!=(connect?16:36)||word(data,8)!=(connect?0:2)){qFatal("Actual tracker action/length differs");}
    if(connect){if(word(data,0)!=0x417||word(data,4)!=0x27101980){qFatal("Actual BEP15 connect magic differs");}}
    else if(word(data,0)!=0x12345678||word(data,4)!=0x9abcdef0||!std::all_of(data.begin()+16,data.end(),[](auto value){return value==0x51;})){qFatal("Actual scrape connection/hash differs");}
    return word(data,12);
  }
  void deliverTracker(Router& router,const QByteArray& packet,std::uint32_t message){auto frame=cp::encodeMessagePayload({7,message,rawTracker(packet)});QVERIFY(frame&&router.send(*frame));}
private slots:
  void managedWireIdsCross1024WithRealReadyAndOldSignedResetCannotSettleNew(){
    owned_i2p_test::Fixture fixture;auto source=provider::ProfileSource::open(*fixture.lease,fixture.hash);QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY(storage);
    const auto config=owned_i2p_test::read(fixture.path/"biglybt.config"),key=owned_i2p_test::read(fixture.key),state=owned_i2p_test::read(fixture.state());
    Router router;QVERIFY(router.listen());Channel channel;QVERIFY(channel.bindEffectAuthority((*source)->effectAuthority()));SourceDriver driver(*source,channel);registered(router,channel,driver,*source);QVERIFY(channel.leaseSetSubmission());
    const auto local=channel.ownedLeaseDestination();QVERIFY(local);auto peer=signer("dsa");QVERIFY(channel.enableStreamManager({}));holdReady(driver);QVERIFY(driver.permit.phase()==provider::SourcePermit::Phase::Ready);QVERIFY(!(*source)->effectAuthority()->granted());
    const Channel::StreamOptions options{9000,80,30000,st::MaximumWindow};std::uint64_t first=0,last=0;
    // 1032 accepted operations, bounded four slots. Ready is deliberately not
    // consumed: real source is current, but no physical SYN is authorized.
    for(unsigned batch=0;batch<258;++batch){
      std::array<std::uint64_t,4> operations{};
      for(auto& operation:operations){auto accepted=channel.connectStream(peer.destination()->wire,options);QVERIFY2(accepted,accepted?"":accepted.error().message.c_str());operation=*accepted;QVERIFY(operation>last);last=operation;if(!first){first=operation;}const auto current=channel.managedStreamObservation(operation);QVERIFY(current&&current->stream.transmittedPackets==0);}
      QCOMPARE(channel.streamManagerObservation().streams,std::size_t(4));QVERIFY(!channel.connectStream(peer.destination()->wire,options));
      for(const auto operation:operations){QVERIFY(channel.cancelStream(operation));QVERIFY(channel.releaseStream(operation));QVERIFY(!channel.managedStreamObservation(operation));}
      QCOMPARE(channel.streamManagerObservation().streams,std::size_t(0));QVERIFY(!channel.cancelStream(first));QVERIFY(!channel.releaseStream(first));
    }
    QVERIFY(last>1024);QVERIFY(!router.next());QVERIFY(!(*source)->effectAuthority()->granted());
    auto retired=channel.connectStream(peer.destination()->wire,options);QVERIFY(retired&&*retired>last);driver.release();QTRY_VERIFY(router.next());const auto oldSyn=streamPacket(router,*local,*peer.destination());QVERIFY((oldSyn.flags&st::Syn)&&oldSyn.receive!=0);
    QVERIFY(channel.cancelStream(*retired));QVERIFY(channel.releaseStream(*retired));QTRY_VERIFY(channel.ownedTransportIdle());
    auto current=channel.connectStream(peer.destination()->wire,options);QVERIFY(current&&*current>*retired);QTRY_VERIFY(router.next());const auto newSyn=streamPacket(router,*local,*peer.destination());QVERIFY((newSyn.flags&st::Syn)&&newSyn.receive!=0&&newSyn.receive!=oldSyn.receive);
    const auto rejected=channel.streamManagerObservation().rejectedPackets;
    st::Packet stale;stale.send=oldSyn.receive;stale.receive=77;stale.flags=st::Reset|st::Signature;deliverStream(router,stale,peer,701);
    st::Packet valid;valid.send=newSyn.receive;valid.receive=78;valid.flags=st::Syn|st::From|st::Signature;valid.from=*peer.destination();deliverStream(router,valid,peer,702);
    QTRY_VERIFY(channel.managedStreamObservation(*current)&&channel.managedStreamObservation(*current)->stream.phase==st::Phase::Established);
    QVERIFY(channel.streamManagerObservation().rejectedPackets>rejected);QVERIFY(!channel.managedStreamObservation(*retired));QVERIFY(!channel.cancelStream(*retired));
    QVERIFY(channel.cancelStream(*current));QVERIFY(channel.releaseStream(*current));QCOMPARE(channel.state(),Channel::State::SessionCreated);QVERIFY(router.failure.isEmpty());
    QCOMPARE(owned_i2p_test::read(fixture.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(fixture.key),key);QCOMPARE(owned_i2p_test::read(fixture.state()),state);
  }
  void trackerTransactionsCross2048WithRealReadyAndLateReplyCannotSettleNew(){
    owned_i2p_test::Fixture fixture;auto source=provider::ProfileSource::open(*fixture.lease,fixture.hash);QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY(storage);
    const auto config=owned_i2p_test::read(fixture.path/"biglybt.config"),key=owned_i2p_test::read(fixture.key),state=owned_i2p_test::read(fixture.state());
    Router router;QVERIFY(router.listen());Channel channel;QVERIFY(channel.bindEffectAuthority((*source)->effectAuthority()));SourceDriver driver(*source,channel);registered(router,channel,driver,*source);QVERIFY(channel.leaseSetSubmission());
    const auto local=channel.ownedLeaseDestination();QVERIFY(local);auto peer=signer("dsa");holdReady(driver);QVERIFY(driver.permit.phase()==provider::SourcePermit::Phase::Ready);
    cp::TrackerRequest request;request.destination=peer.destination()->hash;request.infoHash.fill(0x51);request.port=6969;request.deadlineMilliseconds=30000;
    std::uint64_t first=0,last=0;
    for(unsigned i=0;i<1030;++i){
      auto operation=channel.scrapeTracker(request);QVERIFY2(operation,operation?"":operation.error().message.c_str());QVERIFY(*operation>last);last=*operation;if(!first){first=*operation;}
      QVERIFY(!channel.scrapeTracker(request));QVERIFY(channel.trackerObservation()&&channel.trackerObservation()->operation==*operation&&channel.trackerObservation()->phase==cp::TrackerPhase::LookingUp);
      QVERIFY(channel.cancelTracker(*operation));QVERIFY(!channel.cancelTracker(*operation));QVERIFY(channel.trackerObservation()&&channel.trackerObservation()->phase==cp::TrackerPhase::LocalCancelled);QVERIFY(!channel.trackerObservation()->counts);
    }
    QVERIFY(last>1024);QVERIFY(!router.next());QVERIFY(!(*source)->effectAuthority()->granted());QVERIFY(!channel.cancelTracker(first));
    constexpr std::uint64_t connection=0x123456789abcdef0ULL;
    auto old=channel.scrapeTracker(request);QVERIFY(old&&*old>last);driver.release();QTRY_VERIFY(router.next());const auto oldLookup=lookup(router);QVERIFY(oldLookup.request>1024&&oldLookup.request<=0x7fffffffU);answerLookup(router,oldLookup,peer);
    QTRY_VERIFY(router.next());const auto oldConnect=trackerTransaction(router,*local,*peer.destination(),true);deliverTracker(router,words({0,oldConnect,static_cast<std::uint32_t>(connection>>32),static_cast<std::uint32_t>(connection)}),801);
    QTRY_VERIFY(router.next());const auto oldScrape=trackerTransaction(router,*local,*peer.destination(),false);QVERIFY(oldConnect!=oldScrape);QVERIFY(channel.cancelTracker(*old));QTRY_VERIFY(channel.ownedTransportIdle());
    auto current=channel.scrapeTracker(request);QVERIFY(current&&*current>*old);QTRY_VERIFY(router.next());const auto currentLookup=lookup(router);QVERIFY(currentLookup.request>oldLookup.request&&currentLookup.request<=0x7fffffffU);answerLookup(router,currentLookup,peer);
    QTRY_VERIFY(router.next());const auto newConnect=trackerTransaction(router,*local,*peer.destination(),true);QVERIFY(newConnect!=oldConnect&&newConnect!=oldScrape);
    // Old scrape cannot settle Connecting; old CONNECT cannot drive the newer
    // stage either. The valid later CONNECT is an ordered TCP parser barrier.
    deliverTracker(router,words({2,oldScrape,99,99,99}),802);deliverTracker(router,words({0,oldConnect,0,1}),803);
    deliverTracker(router,words({0,newConnect,static_cast<std::uint32_t>(connection>>32),static_cast<std::uint32_t>(connection)}),804);
    QTRY_VERIFY(router.next());const auto newScrape=trackerTransaction(router,*local,*peer.destination(),false);QVERIFY(newScrape!=oldConnect&&newScrape!=oldScrape&&newScrape!=newConnect);
    deliverTracker(router,words({2,oldScrape,99,99,99}),805);deliverTracker(router,words({2,newScrape,3,4,5}),806);
    QTRY_VERIFY(channel.trackerObservation()&&channel.trackerObservation()->operation==*current&&channel.trackerObservation()->phase==cp::TrackerPhase::Counts);
    const auto result=channel.trackerObservation();QVERIFY(result->counts&&result->rejectedReplies>=3);QCOMPARE(result->counts->seeds,std::int32_t(3));QCOMPARE(result->counts->downloaded,std::int32_t(4));QCOMPARE(result->counts->peers,std::int32_t(5));QVERIFY(!channel.cancelTracker(*old));
    QCOMPARE(channel.state(),Channel::State::SessionCreated);QVERIFY(router.failure.isEmpty());QCOMPARE(owned_i2p_test::read(fixture.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(fixture.key),key);QCOMPARE(owned_i2p_test::read(fixture.state()),state);
  }
};
QTEST_GUILESS_MAIN(ProtocolIdLifetimeTests)
#include "i2cp_protocol_id_lifetime_tests.moc"
