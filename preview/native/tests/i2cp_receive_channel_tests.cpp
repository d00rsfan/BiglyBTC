// SPDX-License-Identifier: GPL-2.0-or-later
#include "channel.hpp"
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
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

class ReceiveChannelTests final:public QObject {
  Q_OBJECT
  void registered(Router& router,Channel& channel,const char* family,bool fast,int budget=3000){
    auto identity=signer(family);QVERIFY(channel.begin(router.options(budget)));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::GetDateType);QCOMPARE(router.bytes.at(0),'*');auto date=cp::encodeMessage(cp::SetDate{Now,std::string("2.13.0")});QVERIFY(date&&router.send(*date));QTRY_VERIFY(channel.routerDate());QVERIFY(channel.createLeaseSession(std::move(identity),{{"i2cp.fastReceive",fast?"TrUe":"false"}},Now));QVERIFY(channel.enableDatagramReceiver());QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateSessionType);auto ack=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});QVERIFY(ack&&router.send(*ack));QTRY_VERIFY(channel.sessionId());QVERIFY(router.lease());QTRY_VERIFY(router.next());QVERIFY(cp::decodeCreateLeaseSet2(router.take()));QTRY_VERIFY(channel.leaseSetSubmission());QCOMPARE(channel.receiverObservation().fast,fast);
  }
  i2p::Bytes packet(const char* from,const char* local,bool claimed,const i2p::Bytes& payload={0,0xff,0x51}){
    auto sender=signer(from),receiver=signer(local);auto data=claimed?i2p::encode3(sender.destination()->wire,payload):sender.makeDatagram2(receiver.destination()->hash,payload);if(!data){qFatal("Cannot make actual signed/claimed fixture: %s",data.error().message.c_str());}auto gzip=cp::encodeMuxDatagram({claimed?i2p::Datagram3Protocol:i2p::Datagram2Protocol,13,42,std::move(*data)},cp::GzipMode::Deflate);if(!gzip){qFatal("Cannot wrap fixture: %s",gzip.error().message.c_str());}return std::move(*gzip);
  }
  void available(Router& router,std::uint32_t id,const i2p::Bytes& payload){auto encoded=cp::encodeMessageStatus({7,id,0,static_cast<std::uint32_t>(payload.size()),1});QVERIFY(encoded&&router.send(*encoded));}
  void inbound(Router& router,std::uint32_t id,const i2p::Bytes& payload,std::uint16_t session=7){auto encoded=cp::encodeMessagePayload({session,id,payload});QVERIFY(encoded&&router.send(*encoded));}
  void control(Router& router,std::uint32_t id,bool begin){QTRY_VERIFY(router.next());auto decoded=cp::decodeReceiveControl(router.take());QVERIFY(decoded);QCOMPARE(decoded->sessionId,std::uint16_t(7));QCOMPARE(decoded->messageId,id);QCOMPARE(decoded->begin,begin);}
  void outboundBarrier(Router& router,Channel& channel){auto recipient=signer("ed25519");auto sent=channel.submitClaimedDatagram3(recipient.destination()->wire,{},{});QVERIFY2(sent.has_value(),sent?"":sent.error().message.c_str());QTRY_VERIFY(router.next());auto actual=cp::decodeSendDatagram(router.take());QVERIFY(actual);QCOMPARE(actual->nonce,*sent);QVERIFY(router.status(*sent,123,6));QTRY_VERIFY(!channel.datagramPending());QVERIFY(channel.datagramObservation()&&channel.datagramObservation()->routerStatus&&channel.datagramObservation()->routerStatus->code==6);}
private slots:
  void actualLegacyAndFastWireVerifiesBeforeQueuing_data(){
    QTest::addColumn<bool>("fast");QTest::addColumn<bool>("claimed");QTest::addColumn<bool>("ipv6");QTest::addColumn<QByteArray>("local");QTest::addColumn<QByteArray>("remote");
    QTest::newRow("legacy-DSA0-v4")<<false<<false<<false<<QByteArray("ed25519")<<QByteArray("dsa");QTest::newRow("fast-Ed7-v6")<<true<<false<<true<<QByteArray("dsa")<<QByteArray("ed25519");QTest::newRow("legacy-claimed3-v6")<<false<<true<<true<<QByteArray("ed25519")<<QByteArray("dsa");QTest::newRow("fast-claimed3-v4")<<true<<true<<false<<QByteArray("dsa")<<QByteArray("ed25519");
  }
  void actualLegacyAndFastWireVerifiesBeforeQueuing(){
    QFETCH(bool,fast);QFETCH(bool,claimed);QFETCH(bool,ipv6);QFETCH(QByteArray,local);QFETCH(QByteArray,remote);Router router;QVERIFY(router.listen(ipv6));Channel channel;registered(router,channel,local.constData(),fast);QVERIFY(channel.leaseSetSubmission());auto gzip=packet(remote.constData(),local.constData(),claimed);
    // A real outgoing request remains pending while inbound controls/data run.
    auto recipient=signer(remote.constData());auto sent=channel.submitDatagram2(recipient.destination()->wire,{},{});QVERIFY(sent);QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::SendMessageType);
    if(!fast){available(router,11,gzip);control(router,11,true);QVERIFY(channel.receiverObservation().pending);}
    inbound(router,11,gzip);if(!fast){control(router,11,false);}QTRY_COMPARE(channel.receiverObservation().queued,std::size_t(1));QVERIFY(router.status(*sent,77,1));QVERIFY(router.status(*sent,77,6));QTRY_VERIFY(!channel.datagramPending());auto received=channel.takeReceivedDatagram();QVERIFY(received);QCOMPARE(received->messageId,std::uint32_t(11));QCOMPARE(received->fromPort,std::uint16_t(13));QCOMPARE(received->toPort,std::uint16_t(42));
    const i2p::Bytes expected{0,0xff,0x51};if(claimed){QVERIFY(std::holds_alternative<i2p::Datagram3>(received->body));const auto& body=std::get<i2p::Datagram3>(received->body);QVERIFY(body.claimedSender==recipient.destination()->hash&&body.payload==expected);}else{QVERIFY(std::holds_alternative<i2p::Datagram2>(received->body));const auto& body=std::get<i2p::Datagram2>(received->body);QVERIFY(body.sender.wire==recipient.destination()->wire&&body.payload==expected);}
    QCOMPARE(channel.receiverObservation().queued,std::size_t(0));QCOMPARE(channel.receiverObservation().decodedBytes,std::size_t(0));outboundBarrier(router,channel);QVERIFY(router.failure.isEmpty());
  }
  void invalidDatagramDropsPreserveTheActualOutboundSession_data(){QTest::addColumn<int>("kind");QTest::newRow("crc")<<0;QTest::newRow("signature")<<1;QTest::newRow("wrong-recipient")<<2;QTest::newRow("unsupported-protocol")<<3;QTest::newRow("malformed-payload31")<<4;QTest::newRow("wrong-session")<<5;}
  void invalidDatagramDropsPreserveTheActualOutboundSession(){
    QFETCH(int,kind);Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519",true);QVERIFY(channel.leaseSetSubmission());auto gzip=packet("dsa",kind==2?"dsa":"ed25519",false);
    if(kind==0){gzip.at(gzip.size()-8)^=1;}if(kind==1){auto mux=cp::decodeMuxDatagram(gzip);QVERIFY(mux);QVERIFY(!mux->datagram.empty());mux->datagram.at(mux->datagram.size()-1)^=1;auto encoded=cp::encodeMuxDatagram(*mux,cp::GzipMode::Deflate);QVERIFY(encoded);gzip=std::move(*encoded);}if(kind==3){gzip.at(9)=99;}
    if(kind==4){auto encoded=cp::encodeMessagePayload({7,12,gzip});QVERIFY(encoded);(*encoded)[14]^=1;QVERIFY(router.send(*encoded));}else{inbound(router,12,gzip,kind==5?8:7);}
    QTRY_VERIFY(channel.receiverObservation().dropped>=1);outboundBarrier(router,channel);QCOMPARE(channel.state(),Channel::State::SessionCreated);QCOMPARE(channel.receiverObservation().queued,std::size_t(0));QVERIFY(channel.receiverObservation().dropped>=1);QVERIFY(channel.receiverObservation().lastDrop);QVERIFY(!channel.takeReceivedDatagram());
  }
  void fullLegacyQueueReleasesWithoutBeginAndFastQueueStaysWithin160KiB(){
    Router legacy;QVERIFY(legacy.listen());Channel channel;registered(legacy,channel,"ed25519",false);QVERIFY(channel.leaseSetSubmission());auto gzip=packet("dsa","ed25519",false);
    for(std::uint32_t id=1;id<=4;++id){available(legacy,id,gzip);control(legacy,id,true);inbound(legacy,id,gzip);control(legacy,id,false);QTRY_COMPARE(channel.receiverObservation().queued,static_cast<std::size_t>(id));}
    available(legacy,5,gzip);control(legacy,5,false);outboundBarrier(legacy,channel);QCOMPARE(channel.receiverObservation().queued,std::size_t(4));QVERIFY(channel.receiverObservation().lastDrop==Channel::ReceiveDrop::QueueFull);channel.disableDatagramReceiver();QCOMPARE(channel.receiverObservation().decodedBytes,std::size_t(0));QVERIFY(!channel.takeReceivedDatagram());channel.close();
    Router fast;QVERIFY(fast.listen());registered(fast,channel,"ed25519",true);QVERIFY(channel.leaseSetSubmission());const i2p::Bytes maximum(cp::MaximumMuxDatagramBytes-34,0x51);gzip=packet("dsa","ed25519",true,maximum);
    for(std::uint32_t id=21;id<=24;++id){inbound(fast,id,gzip);QTRY_COMPARE(channel.receiverObservation().queued,static_cast<std::size_t>(id-20));}
    QVERIFY(channel.receiverObservation().decodedBytes<=160*1024);QCOMPARE(channel.receiverObservation().decodedBytes,std::size_t(4*(32+maximum.size())));inbound(fast,25,gzip);QTRY_VERIFY(channel.receiverObservation().dropped>=1);outboundBarrier(fast,channel);QCOMPARE(channel.receiverObservation().queued,std::size_t(4));QVERIFY(channel.receiverObservation().lastDrop==Channel::ReceiveDrop::QueueFull);
    // Rotating the finite recent-ID window must not forget unread queue IDs.
    for(std::uint32_t id=100;id<164;++id){inbound(fast,id,gzip);}QTRY_VERIFY(channel.receiverObservation().dropped>=65);auto first=channel.takeReceivedDatagram();QVERIFY(first&&first->messageId==21);inbound(fast,22,gzip);QTRY_VERIFY(channel.receiverObservation().dropped>=66);QCOMPARE(channel.receiverObservation().queued,std::size_t(3));QVERIFY(channel.receiverObservation().lastDrop==Channel::ReceiveDrop::Duplicate);outboundBarrier(fast,channel);
  }
  void timedOutAndDisabledLegacyFetchCannotFinishALaterRequest(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519",false);QVERIFY(channel.leaseSetSubmission());auto gzip=packet("dsa","ed25519",false);available(router,31,gzip);control(router,31,true);QTRY_VERIFY(!channel.receiverObservation().pending);control(router,31,false);QVERIFY(channel.receiverObservation().lastDrop==Channel::ReceiveDrop::PendingTimeout);
    available(router,32,gzip);control(router,32,true);inbound(router,31,gzip);inbound(router,32,gzip);control(router,31,false);control(router,32,false);QTRY_COMPARE(channel.receiverObservation().queued,std::size_t(1));auto got=channel.takeReceivedDatagram();QVERIFY(got&&got->messageId==32);
    available(router,33,gzip);control(router,33,true);channel.disableDatagramReceiver();control(router,33,false);QVERIFY(channel.enableDatagramReceiver());inbound(router,33,gzip);control(router,33,false);available(router,34,gzip);control(router,34,true);inbound(router,34,gzip);control(router,34,false);QTRY_COMPARE(channel.receiverObservation().queued,std::size_t(1));got=channel.takeReceivedDatagram();QVERIFY(got&&got->messageId==34);outboundBarrier(router,channel);
  }
  void modeIsInitialAndCallbackCanConsumeDisableOrDelete_data(){QTest::addColumn<bool>("fast");QTest::addColumn<bool>("destroy");QTest::newRow("legacy-pop-disable")<<false<<false;QTest::newRow("fast-delete")<<true<<true;}
  void modeIsInitialAndCallbackCanConsumeDisableOrDelete(){
    QFETCH(bool,fast);QFETCH(bool,destroy);Router router;QVERIFY(router.listen());QPointer<Channel> channel=new Channel(&router.server);registered(router,*channel,"ed25519",fast);QVERIFY(channel->leaseSetSubmission());QVERIFY(channel->reconfigureLeaseSession({{"i2cp.fastReceive",fast?"false":"true"}},Now));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::ReconfigureSessionType);auto ack=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Updated});QVERIFY(ack&&router.send(*ack));QTRY_COMPARE(channel->state(),Channel::State::SessionCreated);QCOMPARE(channel->receiverObservation().fast,fast);bool consumed=false;
    QObject::connect(channel,&Channel::receiverChanged,&router.server,[&]{if(!channel||channel->receiverObservation().queued==0){return;}if(destroy){delete channel.data();}else{auto got=channel->takeReceivedDatagram();consumed=got.has_value();channel->disableDatagramReceiver();}});
    auto gzip=packet("dsa","ed25519",false);if(!fast){available(router,41,gzip);control(router,41,true);}inbound(router,41,gzip);if(destroy){QTRY_VERIFY(channel.isNull());QTRY_COMPARE(router.peer->state(),QAbstractSocket::UnconnectedState);}else{control(router,41,false);QTRY_VERIFY(consumed);QVERIFY(!channel->receiverObservation().enabled);QCOMPARE(channel->receiverObservation().queued,std::size_t(0));outboundBarrier(router,*channel);}
  }
};
QTEST_GUILESS_MAIN(ReceiveChannelTests)
#include "i2cp_receive_channel_tests.moc"
