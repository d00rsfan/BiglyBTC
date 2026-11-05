// SPDX-License-Identifier: GPL-2.0-or-later
#include "actor.hpp"
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

namespace d=i2p::dht;
class DhtChannelTests final:public QObject {
  Q_OBJECT
  void registered(Router& router,Channel& channel,const char* family,bool fast,int budget=3000){
    auto identity=signer(family);QVERIFY(channel.begin(router.options(budget)));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::GetDateType);QCOMPARE(router.bytes.at(0),'*');auto date=cp::encodeMessage(cp::SetDate{Now,std::string("2.13.0")});QVERIFY(date&&router.send(*date));QTRY_VERIFY(channel.routerDate());QVERIFY(channel.createLeaseSession(std::move(identity),{{"i2cp.fastReceive",fast?"TrUe":"false"}},Now));QVERIFY(channel.enableDatagramReceiver());QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateSessionType);auto ack=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});QVERIFY(ack&&router.send(*ack));QTRY_VERIFY(channel.sessionId());QVERIFY(router.lease());QTRY_VERIFY(router.next());QVERIFY(cp::decodeCreateLeaseSet2(router.take()));QTRY_VERIFY(channel.leaseSetSubmission());QCOMPARE(channel.receiverObservation().fast,fast);
  }

  void inbound(Router& r,std::uint32_t id,const i2p::Bytes& mux,bool fast){
    if(!fast){auto a=cp::encodeMessageStatus({7,id,0,std::uint32_t(mux.size()),0});QVERIFY(a&&r.send(*a));QTRY_VERIFY(r.next());auto begin=cp::decodeReceiveControl(r.take());QVERIFY(begin&&begin->begin&&begin->messageId==id);}
    auto frame=cp::encodeMessagePayload({7,id,mux});QVERIFY(frame&&r.send(*frame));
    if(!fast){QTRY_VERIFY(r.next());auto end=cp::decodeReceiveControl(r.take());QVERIFY(end&&!end->begin&&end->messageId==id);}
  }
private slots:
  void actualScopedLegacyAndFast_data(){QTest::addColumn<bool>("fast");QTest::newRow("legacy")<<false;QTest::newRow("fast")<<true;}
  void actualScopedLegacyAndFast(){
    QFETCH(bool,fast);Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519",fast);QVERIFY(channel.leaseSetSubmission());QVERIFY(!channel.enableDhtDatagrams(8999));QVERIFY(!channel.enableDhtDatagrams(9000));QVERIFY(channel.enableDhtDatagrams(10000));const auto epoch=channel.dhtScopeGeneration();QVERIFY(epoch);
    auto remote=signer("dsa");const i2p::Bytes body{0,0xff,0x51};auto ticket=channel.submitDhtDatagram(remote.destination()->wire,body,true,10002);QVERIFY(ticket);QTRY_VERIFY(router.next());auto frame=cp::decodeDhtSendDatagram(router.take());QVERIFY(frame&&frame->nonce==0&&!frame->skipLeaseSet);auto mux=cp::decodeDhtMuxDatagram(frame->payload);QVERIFY(mux&&mux->protocol==17&&mux->fromPort==10000&&mux->toPort==10002);auto original=i2p::decode1(mux->datagram);QVERIFY(original&&original->payload==body);std::optional<Channel::DhtDispatch> sent;QTRY_VERIFY(sent.has_value()||(sent=channel.takeDhtDispatch()));QVERIFY(sent->ticket==*ticket&&sent->written);
    auto signedPacket=i2p::encode1(remote.destination()->wire,remote.signingPrivateKey(),body);QVERIFY(signedPacket);auto incoming=cp::encodeDhtMuxDatagram({17,10002,10000,*signedPacket},cp::GzipMode::Deflate);QVERIFY(incoming);inbound(router,11,*incoming,fast);std::optional<Channel::DhtDatagram> got;QTRY_VERIFY(got.has_value()||(got=channel.takeDhtDatagram()));QVERIFY(got->sender&&got->sender->wire==remote.destination()->wire&&got->payload==body);QVERIFY(!channel.takeReceivedDatagram());
    auto raw=cp::encodeDhtMuxDatagram({18,10002,10001,body},cp::GzipMode::Deflate);QVERIFY(raw);inbound(router,12,*raw,fast);got.reset();QTRY_VERIFY(got.has_value()||(got=channel.takeDhtDatagram()));QVERIFY(!got->sender&&got->payload==body);QVERIFY(!channel.takeReceivedDatagram());
    auto normal=remote.makeDatagram2(signer("ed25519").destination()->hash,body);QVERIFY(normal);auto normalMux=cp::encodeMuxDatagram({19,10002,10000,*normal},cp::GzipMode::Deflate);QVERIFY(normalMux);inbound(router,13,*normalMux,fast);std::optional<Channel::ReceivedDatagram> ordinary;QTRY_VERIFY(ordinary.has_value()||(ordinary=channel.takeReceivedDatagram()));QVERIFY(std::holds_alternative<i2p::Datagram2>(ordinary->body));QVERIFY(!channel.takeDhtDatagram());
    const auto dropped=channel.receiverObservation().dropped;for(std::uint32_t n=100;n<109;++n){inbound(router,n,*raw,fast);}QTRY_VERIFY(channel.receiverObservation().dropped>=dropped+1);std::size_t queued=0;while(channel.takeDhtDatagram()){++queued;}QCOMPARE(queued,std::size_t(8));
    channel.disableDhtDatagrams();QVERIFY(channel.dhtScopeGeneration()==0);QVERIFY(channel.enableDhtDatagrams(10000));QVERIFY(channel.dhtScopeGeneration()!=epoch);QCOMPARE(channel.state(),Channel::State::SessionCreated);QVERIFY(router.failure.isEmpty());
  }
  void actualActorCorrelatesGetPeersThenRawAnnounce(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519",true);QVERIFY(channel.leaseSetSubmission());auto remote=signer("dsa"),local=signer("ed25519");d::Contact seed;seed.hash=remote.destination()->hash;seed.port=10002;seed.destination=*remote.destination();auto rid=d::makeNodeId(seed.hash,seed.port,i2p::Bytes(14,0x32));QVERIFY(rid);seed.id=*rid;auto lid=d::makeNodeId(local.destination()->hash,10000,i2p::Bytes(14,0x31));QVERIFY(lid);
    d::Actor actor(channel);QVERIFY(actor.begin({*lid,10000,{seed},3}));d::Id target{};target.fill(0x51);auto op=actor.announceSelf(target,true,3000);QVERIFY(op);QTRY_VERIFY(router.next());auto sent=cp::decodeDhtSendDatagram(router.take());QVERIFY(sent);auto mux=cp::decodeDhtMuxDatagram(sent->payload);QVERIFY(mux&&mux->protocol==17);auto signedData=i2p::decode1(mux->datagram);QVERIFY(signedData);auto get=d::decode(signedData->payload);QVERIFY(get&&get->method==d::Method::GetPeers&&get->noSeed&&get->target==target);
    QVERIFY(channel.reconfigureLeaseSession({{"i2cp.fastReceive","true"}},Now));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::ReconfigureSessionType);QCOMPARE(channel.state(),Channel::State::Reconfiguring);
    d::Message response;response.kind=d::Kind::Reply;response.id=seed.id;response.transaction=get->transaction;response.token={0,0xff,0x51};response.hasPeers=true;response.peers={{seed.hash,true}};auto bytes=d::encode(response);QVERIFY(bytes);auto reply=cp::encodeDhtMuxDatagram({18,10002,10001,*bytes},cp::GzipMode::Deflate);QVERIFY(reply);inbound(router,21,*reply,true);
    auto updated=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Updated});QVERIFY(updated&&router.send(*updated));
    QTRY_VERIFY(router.next());sent=cp::decodeDhtSendDatagram(router.take());QVERIFY(sent&&sent->skipLeaseSet);mux=cp::decodeDhtMuxDatagram(sent->payload);QVERIFY(mux&&mux->protocol==18&&mux->toPort==10003);auto announce=d::decode(mux->datagram);QVERIFY(announce&&announce->method==d::Method::AnnouncePeer&&announce->token==response.token&&announce->seed);
    d::Message ack;ack.kind=d::Kind::Reply;ack.id=seed.id;ack.transaction=announce->transaction;bytes=d::encode(ack);QVERIFY(bytes);reply=cp::encodeDhtMuxDatagram({18,10002,10001,*bytes},cp::GzipMode::Deflate);QVERIFY(reply);inbound(router,22,*reply,true);QTRY_COMPARE(actor.observation().phase,d::Phase::Complete);auto result=actor.observation();QVERIFY(result.operation==*op&&result.announced==1&&result.peers.size()==1&&result.peers[0].hash==seed.hash);actor.close();QCOMPARE(channel.state(),Channel::State::SessionCreated);
  }
  void lookupResultsOwnEightTerminalsAndClearWithSession(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519",true);auto remote=signer("dsa");std::vector<std::uint64_t> operations;
    for(unsigned n=0;n<9;++n){
      auto op=channel.lookupDestination(remote.destination()->hash,3000);QVERIFY(op);operations.push_back(*op);
      if(n){auto previous=channel.destinationLookupObservation(operations[n-1]);QVERIFY(previous&&previous->phase==Channel::LookupPhase::Resolved&&previous->destination&&previous->destination->wire==remote.destination()->wire);QCOMPARE(channel.destinationLookupObservation()->phase,Channel::LookupPhase::Pending);}
      QTRY_VERIFY(router.next());auto request=cp::decodeHostLookup(router.take());QVERIFY(request&&request->hash==remote.destination()->hash&&request->request==*op);
      auto reply=cp::encodeHostReply({7,request->request,0,*remote.destination()});QVERIFY(reply&&router.send(*reply));
      QTRY_VERIFY(channel.destinationLookupObservation(*op)&&channel.destinationLookupObservation(*op)->phase==Channel::LookupPhase::Resolved);
    }
    QVERIFY(!channel.destinationLookupObservation(operations.front()));
    for(std::size_t n=1;n<operations.size();++n){auto result=channel.destinationLookupObservation(operations[n]);QVERIFY(result&&result->destination&&result->destination->wire==remote.destination()->wire);}
    channel.close();for(const auto op:operations){QVERIFY(!channel.destinationLookupObservation(op));}QVERIFY(!channel.destinationLookupObservation());
  }
  void actorYieldsToAnotherOwnedLookupWithoutLosingCandidate(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519",true);auto remote=signer("dsa"),local=signer("ed25519");
    auto foreign=channel.lookupDestination(remote.destination()->hash,3000);QVERIFY(foreign);QTRY_VERIFY(router.next());QVERIFY(cp::decodeHostLookup(router.take()));
    d::Contact seed;seed.hash=remote.destination()->hash;seed.port=10002;auto rid=d::makeNodeId(seed.hash,seed.port,i2p::Bytes(14,0x32));QVERIFY(rid);seed.id=*rid;auto lid=d::makeNodeId(local.destination()->hash,10000,i2p::Bytes(14,0x31));QVERIFY(lid);
    d::Actor actor(channel);QVERIFY(actor.begin({*lid,10000,{seed},3}));d::Id target{};target.fill(0x51);auto operation=actor.lookupPeers(target,false,3000);QVERIFY(operation);QCOMPARE(actor.observation().phase,d::Phase::Running);
    QVERIFY(channel.cancelDestinationLookup(*foreign));QTRY_VERIFY(router.next());auto request=cp::decodeHostLookup(router.take());QVERIFY(request&&request->hash==seed.hash&&request->request!=*foreign);
    auto old=channel.destinationLookupObservation(*foreign);QVERIFY(old&&old->phase==Channel::LookupPhase::Cancelled);
    auto reply=cp::encodeHostReply({7,request->request,0,*remote.destination()});QVERIFY(reply&&router.send(*reply));QTRY_VERIFY(router.next());auto packet=cp::decodeDhtSendDatagram(router.take());QVERIFY(packet&&packet->recipient.wire==remote.destination()->wire);QVERIFY(actor.cancel(*operation));
  }
  void unusedOrRejectedActorCannotDisableAnotherDhtOwner(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519",true);
    QVERIFY(channel.enableDhtDatagrams(10000));const auto scope=channel.dhtScopeGeneration();QVERIFY(scope);
    {
      d::Actor actor(channel);QVERIFY(!actor.begin({}));actor.close();
      QCOMPARE(channel.dhtScopeGeneration(),scope);
    }
    QCOMPARE(channel.dhtScopeGeneration(),scope);
    QVERIFY(!channel.disableDhtDatagrams(std::uint64_t{0}));QCOMPARE(channel.dhtScopeGeneration(),scope);
    QVERIFY(channel.disableDhtDatagrams(scope));QCOMPARE(channel.dhtScopeGeneration(),std::uint64_t{0});
  }
  void staleActorCannotDisableReplacementDhtScope(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519",true);
    auto remote=signer("dsa"),local=signer("ed25519");d::Contact seed;seed.hash=remote.destination()->hash;seed.port=10002;seed.destination=*remote.destination();
    auto rid=d::makeNodeId(seed.hash,seed.port,i2p::Bytes(14,0x32));QVERIFY(rid);seed.id=*rid;
    auto lid=d::makeNodeId(local.destination()->hash,10000,i2p::Bytes(14,0x31));QVERIFY(lid);
    d::Actor actor(channel);QVERIFY(actor.begin({*lid,10000,{seed},3}));const auto oldScope=channel.dhtScopeGeneration();QVERIFY(oldScope);
    channel.disableDhtDatagrams();QVERIFY(channel.enableDhtDatagrams(10000));const auto replacement=channel.dhtScopeGeneration();QVERIFY(replacement&&replacement!=oldScope);
    QVERIFY(!channel.disableDhtDatagrams(oldScope));QCOMPARE(channel.dhtScopeGeneration(),replacement);
    actor.close();QCOMPARE(channel.dhtScopeGeneration(),replacement);
    QVERIFY(channel.disableDhtDatagrams(replacement));QCOMPARE(channel.dhtScopeGeneration(),std::uint64_t{0});
  }
  void inboundModeCannotUseASourcelessChannel(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519",true);
    auto remote=signer("dsa"),local=signer("ed25519");d::Contact seed;seed.hash=remote.destination()->hash;seed.port=10002;seed.destination=*remote.destination();
    auto rid=d::makeNodeId(seed.hash,seed.port,i2p::Bytes(14,0x32));QVERIFY(rid);seed.id=*rid;auto lid=d::makeNodeId(local.destination()->hash,10000,i2p::Bytes(14,0x31));QVERIFY(lid);
    d::Actor actor(channel);QVERIFY(actor.begin({*lid,10000,{seed},3}));const auto scope=channel.dhtScopeGeneration();QVERIFY(scope);
    auto observed=channel.dhtScopeObservation(scope);QVERIFY(!observed.sourceBound&&!observed.granted);QVERIFY(!channel.requestDhtEffects(scope));
    QVERIFY(!channel.submitDhtReply(scope,seed.destination->wire,i2p::Bytes{0x51},10003,std::chrono::steady_clock::now()+std::chrono::seconds(1)));
    QVERIFY(!actor.enableInbound());QVERIFY(!actor.resumeAuthorizedInbound());QVERIFY(!actor.inboundEnabled());QCOMPARE(channel.dhtScopeGeneration(),scope);
    d::Id target{};target.fill(0x51);auto operation=actor.lookupPeers(target,false,3000);QVERIFY(operation);QTRY_VERIFY(router.next());QVERIFY(cp::decodeDhtSendDatagram(router.take()));QVERIFY(actor.cancel(*operation));
  }
  void queuedInputKeepsItsActualReceiveDeadline(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519",false);QVERIFY(channel.enableDhtDatagrams(10000));const auto scope=channel.dhtScopeGeneration();
    auto raw=cp::encodeDhtMuxDatagram({18,10002,10001,{0x51}},cp::GzipMode::Deflate);QVERIFY(raw);inbound(router,901,*raw,false);
    QTRY_COMPARE(channel.dhtScopeObservation(scope).queued,std::size_t(1));
    QTRY_COMPARE_WITH_TIMEOUT(channel.dhtScopeObservation(scope).queued,std::size_t(0),5000);
    QVERIFY(!channel.takeDhtDatagram());QCOMPARE(channel.dhtScopeGeneration(),scope);QCOMPARE(channel.state(),Channel::State::SessionCreated);
  }
  void signedDhtStillRejectsInvalidSecureQueryPorts(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519",true);QVERIFY(channel.enableDhtDatagrams(10000));const auto scope=channel.dhtScopeGeneration();auto remote=signer("dsa");
    const i2p::Bytes body{0x51};auto signedBody=i2p::encode1(remote.destination()->wire,remote.signingPrivateKey(),body);QVERIFY(signedBody);
    auto dropped=channel.receiverObservation().dropped;
    for(quint16 port:std::array<quint16,2>{0,65535}){auto mux=cp::encodeDhtMuxDatagram({17,port,10000,*signedBody},cp::GzipMode::Deflate);QVERIFY(mux);inbound(router,1000+port,*mux,true);QTRY_VERIFY(channel.receiverObservation().dropped>dropped);QVERIFY(channel.receiverObservation().lastDrop==Channel::ReceiveDrop::Malformed);dropped=channel.receiverObservation().dropped;QVERIFY(!channel.takeDhtDatagram());}
    QCOMPARE(channel.dhtScopeGeneration(),scope);QCOMPARE(channel.state(),Channel::State::SessionCreated);
  }
  void cancelledQueuedFramesNeverReachTheRouter(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel,"ed25519",true);QVERIFY(channel.leaseSetSubmission());QVERIFY(channel.enableDhtDatagrams(10000));auto remote=signer("dsa");std::vector<std::uint64_t> tickets;
    for(unsigned n=0;n<8;++n){auto op=channel.submitDhtDatagram(remote.destination()->wire,{},false,10003);QVERIFY(op);tickets.push_back(*op);}QVERIFY(!channel.submitDhtDatagram(remote.destination()->wire,{},false,10003));
    for(std::size_t n=1;n<tickets.size();++n){QVERIFY(channel.cancelDhtDatagram(tickets[n]));}QTRY_VERIFY(router.next());auto first=cp::decodeDhtSendDatagram(router.take());QVERIFY(first);std::size_t written=0,cancelled=0;while(auto r=channel.takeDhtDispatch()){if(r->written){++written;}else{++cancelled;}}QCOMPARE(written,std::size_t(1));QCOMPARE(cancelled,std::size_t(7));
    // A real following reconfiguration supplies a FIFO stream barrier.
    QVERIFY(channel.reconfigureLeaseSession({},Now));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::ReconfigureSessionType);QVERIFY(!router.next());channel.disableDhtDatagrams();
  }
};
QTEST_GUILESS_MAIN(DhtChannelTests)
#include "i2p_dht_channel_tests.moc"
