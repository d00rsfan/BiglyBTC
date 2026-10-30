// SPDX-License-Identifier: GPL-2.0-or-later
#include "channel.hpp"
#include <QFile>
#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QSignalSpy>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <array>
#include <chrono>
#include <optional>
namespace p=biglybtc::services::i2p;namespace cp=p::i2cp;namespace st=p::streaming;
using Channel=cp::InitialChannel;
namespace {
constexpr std::uint64_t Now=1761739200123ULL;
std::uint64_t stamp(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
p::ClientIdentity identity(const char* family){
  const auto read=[&](const char* suffix){QFile file(QString::fromUtf8(BIGLYBTC_I2P_TEST_KEYS_DIR)+'/'+QString::fromLatin1(family)+QString::fromLatin1(suffix));if(!file.open(QIODevice::ReadOnly)||file.size()<1||file.size()>1024){qFatal("Missing generated key fixture");}const auto bytes=file.readAll();return p::Bytes(reinterpret_cast<const p::Byte*>(bytes.constData()),reinterpret_cast<const p::Byte*>(bytes.constData())+bytes.size());};
  auto value=p::makeClientIdentity(read(".sender"),p::Bytes(256,0x51),read(".known-test-key"));if(!value){qFatal("Invalid generated signer");}return std::move(*value);
}
struct Router {
  QTcpServer server;QPointer<QTcpSocket> socket;QByteArray bytes;qsizetype offset=1;QString failure;
  const std::uint64_t openedAt=stamp();
  p::ClientIdentity local=identity("dsa"),remote=identity("ed25519");
  struct Peer {std::uint16_t nativePort=0;std::optional<st::Connection> connection;p::Bytes received;};
  std::array<Peer,3> peers{};std::uint32_t message=100;std::uint64_t leaseFrames=0;
  Router(){
    server.setProxy(QNetworkProxy::NoProxy);
    QObject::connect(&server,&QTcpServer::newConnection,&server,[this]{
      if(socket&&socket->state()!=QAbstractSocket::UnconnectedState){failure="Unexpected extra client";return;}
      socket=server.nextPendingConnection();bytes.clear();offset=1;
      QObject::connect(socket,&QTcpSocket::readyRead,&server,[this]{collect();});collect();
    });
  }
  ~Router(){QObject::disconnect(&server,nullptr,&server,nullptr);if(socket){QObject::disconnect(socket,nullptr,&server,nullptr);socket->abort();}}
  bool listen(){return server.listen(QHostAddress::LocalHost,0);}
  Channel::Options options()const{return {QHostAddress::LocalHost,server.serverPort(),"2.13.0",false,3000,false};}
  void collect(){if(!socket){return;}const auto n=socket->bytesAvailable();if(n<0||n>131077-bytes.size()){failure="Owned frame buffer exceeded bound";socket->abort();return;}bytes+=socket->read(n);}
  std::optional<cp::Frame> next()const{
    if(bytes.size()-offset<5){return std::nullopt;}const auto* data=reinterpret_cast<const cp::Byte*>(bytes.constData()+offset);std::uint32_t n=0;for(unsigned i=0;i<4;++i){n=(n<<8)|data[i];}if(n>cp::MaximumPayloadBytes){qFatal("Owned oversized frame");}if(bytes.size()-offset<5+static_cast<qsizetype>(n)){return std::nullopt;}auto decoded=cp::decodeFrame(cp::View(data,n+5));if(!decoded){qFatal("Owned malformed frame");}return std::move(*decoded);
  }
  cp::Frame take(){auto frame=next();if(!frame){qFatal("Missing owned frame");}offset+=5+static_cast<qsizetype>(frame->payload.size());return std::move(*frame);}
  bool send(p::View frame){return socket&&socket->write(reinterpret_cast<const char*>(frame.data()),static_cast<qint64>(frame.size()))==static_cast<qint64>(frame.size());}
  bool lease(){cp::RequestedLease lease;lease.gateway.fill(0x51);lease.tunnelId=33;lease.expiresMilliseconds=Now+600000;auto frame=cp::encodeLeaseRequest({7,{lease}});return frame&&send(*frame);}
  bool leaseUntil(std::uint64_t end){cp::RequestedLease lease;lease.gateway.fill(0x51);lease.tunnelId=33;lease.expiresMilliseconds=end;auto frame=cp::encodeLeaseRequest({7,{lease}});return frame&&send(*frame);}
  bool deliver(const st::Packet& packet,std::uint16_t port){
    auto encoded=st::encode(packet,remote);if(!encoded){failure="Remote packet encode failed";return false;}auto mux=st::encodeMux({80,port,std::move(*encoded)});if(!mux){failure="Remote mux failed";return false;}auto frame=cp::encodeMessagePayload({7,message++,std::move(*mux)});return frame&&send(*frame);
  }
  void flush(Peer& peer){
    for(unsigned turn=0;turn<16&&peer.connection;++turn){auto packet=peer.connection->next(stamp());if(!packet){return;}if(!deliver(*packet,peer.nativePort)){failure="Remote packet dispatch failed";return;}peer.connection->dispatched(*packet,stamp());}
  }
  bool pump(){
    collect();
    while(next()&&failure.isEmpty()){
      auto frame=take();
      if(frame.type==cp::CreateLeaseSet2Type){auto lease=cp::decodeCreateLeaseSet2(frame);if(!lease||lease->sessionId!=7||lease->leaseSet.destination.wire!=local.destination()->wire){failure="Wrong renewed owned key/session";return false;}++leaseFrames;continue;}
      auto sent=cp::decodeSendDatagram(frame);if(!sent||sent->sessionId!=7||sent->recipient.wire!=remote.destination()->wire||sent->nonce!=0){failure="Wrong actual manager recipient/session";return false;}
      auto mux=st::decodeMux(sent->payload);if(!mux||mux->toPort!=80){failure="Wrong manager mux";return false;}
      auto packet=st::decode(mux->packet,*local.destination());if(!packet){failure="Bad actual native signature";return false;}
      Peer* selected=nullptr;for(auto& peer:peers){if(peer.nativePort==mux->fromPort){selected=&peer;break;}}
      if(!selected){for(auto& peer:peers){if(!peer.connection){peer.nativePort=mux->fromPort;selected=&peer;break;}}}
      if(!selected){failure="Unexpected fourth mock stream";return false;}
      if(!selected->connection){
        auto parsed=st::decodeInitialSyn(mux->packet,*remote.destination());if(!parsed){failure="Bad real outbound SYN";return false;}
        const auto id=300+static_cast<std::uint32_t>(selected-peers.data());auto connection=st::Connection::accept(id,id,*remote.destination(),*local.destination(),*parsed,8,stamp());
        if(!connection){failure="Cannot accept actual outbound stream";return false;}selected->connection.emplace(std::move(*connection));
      }else{selected->connection->receive(std::move(*packet),stamp());}
      auto incoming=selected->connection->read(st::MaximumBuffered);selected->received.insert(selected->received.end(),incoming.begin(),incoming.end());flush(*selected);
    }
    return failure.isEmpty();
  }
};
}
class ManagerChannelTests final:public QObject {
  Q_OBJECT
  void registered(Router& router,Channel& channel,st::ManagerOptions policy={}){
    QVERIFY(channel.begin(router.options()));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::GetDateType);auto date=cp::encodeMessage(cp::SetDate{Now,std::string("2.13.0")});QVERIFY(date&&router.send(*date));QTRY_VERIFY(channel.routerDate());
    auto signer=identity("dsa");QVERIFY(channel.createLeaseSession(std::move(signer),{{"i2cp.fastReceive","true"},{"i2cp.messageReliability","none"}},Now));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateSessionType);auto created=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});QVERIFY(created&&router.send(*created));QTRY_VERIFY(channel.sessionId());QVERIFY(router.lease());QTRY_VERIFY(router.next());QVERIFY(cp::decodeCreateLeaseSet2(router.take()));QTRY_VERIFY(channel.leaseSetSubmission());QVERIFY(channel.enableStreamManager(policy));
  }
private slots:
  void concurrentActualFramesAndExplicitIncomingPeerRemainIndependent(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel);
    auto first=channel.connectStream(router.remote.destination()->wire,{9000,80,3000});auto second=channel.connectStream(router.remote.destination()->wire,{9001,80,3000});QVERIFY(first&&second&&*first!=*second);
    QTRY_VERIFY(router.pump()&&channel.managedStreamObservation(*first)&&channel.managedStreamObservation(*second)&&channel.managedStreamObservation(*first)->stream.phase==st::Phase::Established&&channel.managedStreamObservation(*second)->stream.phase==st::Phase::Established);
    QVERIFY(channel.writeStream(*first,p::Bytes{'A'}));QVERIFY(channel.writeStream(*second,p::Bytes{'B'}));
    QTRY_VERIFY(router.pump()&&router.peers[0].received==p::Bytes{'A'}&&router.peers[1].received==p::Bytes{'B'});
    QVERIFY(channel.cancelStream(*first));QVERIFY(channel.releaseStream(*first));QCOMPARE(channel.managedStreamObservation(*second)->stream.phase,st::Phase::Established);
    QVERIFY(channel.listenStreams({{6881,80},{*router.remote.destination()},8}));
    auto& incoming=router.peers[2];incoming.nativePort=6881;incoming.connection.emplace(500,500,*router.remote.destination(),*router.local.destination());router.flush(incoming);
    QTRY_COMPARE(channel.streamManagerObservation().pendingSyns,std::size_t{1});auto accepted=channel.acceptStream();QVERIFY(accepted&&*accepted!=*first&&*accepted!=*second);QVERIFY(channel.managedStreamObservation(*accepted)->inbound);
    QTRY_VERIFY(router.pump()&&incoming.connection->observation().phase==st::Phase::Established);
    p::Bytes handshake{19};const std::string protocol="BitTorrent protocol";handshake.insert(handshake.end(),protocol.begin(),protocol.end());handshake.resize(68,0x51);QVERIFY(incoming.connection->write(handshake));router.flush(incoming);
    QVERIFY(channel.waitStream(*accepted,st::WaitFor::Readable,68,3000));QTRY_VERIFY(channel.managedStreamObservation(*accepted)->wait&&channel.managedStreamObservation(*accepted)->wait->state==st::WaitState::Ready);auto received=channel.takeStreamBytes(*accepted,68);QVERIFY(received);QVERIFY(*received==handshake);
    const auto revision=channel.leaseSetSubmission()->revision;QVERIFY(router.lease());QTRY_VERIFY(router.pump()&&channel.leaseSetSubmission()->revision>revision);QVERIFY(router.leaseFrames>0);QCOMPARE(channel.managedStreamObservation(*second)->stream.phase,st::Phase::Established);QCOMPARE(channel.managedStreamObservation(*accepted)->stream.phase,st::Phase::Established);
    channel.close();QCOMPARE(channel.streamManagerObservation().streams,std::size_t{0});QVERIFY(!channel.writeStream(*second,p::Bytes{'X'}));QVERIFY(router.failure.isEmpty());
  }
  void currentPendingSynNotificationMayDeleteTheController(){
    Router router;QVERIFY(router.listen());QPointer<Channel> channel=new Channel(&router.server);registered(router,*channel);QVERIFY(channel->listenStreams({{6881,80},{*router.remote.destination()},8}));
    QObject::connect(channel,&Channel::streamChanged,&router.server,[&]{if(channel&&channel->streamManagerObservation().pendingSyns){delete channel.data();}});
    auto& incoming=router.peers[2];incoming.nativePort=6881;incoming.connection.emplace(500,500,*router.remote.destination(),*router.local.destination());router.flush(incoming);
    QTRY_VERIFY(channel.isNull());QTRY_COMPARE(router.socket->state(),QAbstractSocket::UnconnectedState);
  }
  void finiteApplicationWaitDoesNotCloseOtherStreams(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel);auto operation=channel.connectStream(router.remote.destination()->wire,{9000,80,3000});QVERIFY(operation);
    QTRY_VERIFY(router.pump()&&channel.managedStreamObservation(*operation)->stream.phase==st::Phase::Established);QVERIFY(channel.waitStream(*operation,st::WaitFor::Readable,1,20));
    QTRY_VERIFY(channel.managedStreamObservation(*operation)->wait&&channel.managedStreamObservation(*operation)->wait->state==st::WaitState::TimedOut);QCOMPARE(channel.managedStreamObservation(*operation)->stream.phase,st::Phase::Established);QVERIFY(channel.writeStream(*operation,p::Bytes{'A'}));QTRY_VERIFY(router.pump()&&router.peers[0].received==p::Bytes{'A'});channel.close();
  }
  void finiteHandshakesAndWaitRemainIndependentAcrossQueuedFrames(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel);
    auto first=channel.connectStream(router.remote.destination()->wire,{9000,80,1000});auto second=channel.connectStream(router.remote.destination()->wire,{9001,80,1500});QVERIFY(first&&second);
    // Both actual SYN writes share the one-frame transport. The owned router
    // reads frames but deliberately supplies no Streaming acknowledgement.
    QVERIFY(channel.waitStream(*first,st::WaitFor::Readable,1,20));
    QTRY_VERIFY_WITH_TIMEOUT(channel.managedStreamObservation(*first)&&channel.managedStreamObservation(*first)->wait&&channel.managedStreamObservation(*first)->wait->state==st::WaitState::TimedOut,5000);
    QTRY_VERIFY_WITH_TIMEOUT(channel.managedStreamObservation(*first)&&channel.managedStreamObservation(*second)&&channel.managedStreamObservation(*first)->stream.phase==st::Phase::Timeout&&channel.managedStreamObservation(*second)->stream.phase==st::Phase::Timeout,5000);
    QVERIFY(router.next());auto firstFrame=cp::decodeSendDatagram(router.take());QVERIFY(firstFrame&&firstFrame->recipient.wire==router.remote.destination()->wire);QCOMPARE(channel.state(),Channel::State::SessionCreated);QVERIFY(router.failure.isEmpty());channel.close();
  }
  void shortenedActualLeaseRetiresAuthorityAndRenewalCannotRestoreOldStreams(){
    Router router;QVERIFY(router.listen());Channel channel;registered(router,channel);auto operation=channel.connectStream(router.remote.destination()->wire,{9000,80,3000});QVERIFY(operation);
    QTRY_VERIFY(router.pump()&&channel.managedStreamObservation(*operation)&&channel.managedStreamObservation(*operation)->stream.phase==st::Phase::Established);QVERIFY(channel.listenStreams({{6881,80},{*router.remote.destination()},8}));
    const auto revision=channel.leaseSetSubmission()->revision;QVERIFY(router.leaseUntil(Now+(stamp()-router.openedAt)+3000));QTRY_VERIFY(router.pump()&&channel.leaseSetSubmission()->revision>revision);const auto shortened=channel.leaseSetSubmission()->revision;QVERIFY(channel.streamManagerObservation().listening);QCOMPARE(channel.managedStreamObservation(*operation)->stream.phase,st::Phase::Established);
    // This is actual source-authored wall/steady time, with no test clock or
    // timeout override. Earlier LS2 expiry must replace the old long timer.
    QTRY_VERIFY_WITH_TIMEOUT(channel.managedStreamObservation(*operation)&&channel.managedStreamObservation(*operation)->stream.phase==st::Phase::LeaseExpired,10000);QVERIFY(!channel.streamManagerObservation().listening);QVERIFY(!channel.writeStream(*operation,p::Bytes{'X'}));
    QVERIFY(router.lease());QTRY_VERIFY(router.pump()&&channel.leaseSetSubmission()->revision>shortened);QCOMPARE(channel.managedStreamObservation(*operation)->stream.phase,st::Phase::LeaseExpired);QVERIFY(!channel.streamManagerObservation().listening);QVERIFY(!channel.writeStream(*operation,p::Bytes{'X'}));QVERIFY(channel.releaseStream(*operation));
    auto fresh=channel.connectStream(router.remote.destination()->wire,{9001,80,3000});QVERIFY(fresh&&*fresh!=*operation);QTRY_VERIFY(router.pump()&&channel.managedStreamObservation(*fresh)&&channel.managedStreamObservation(*fresh)->stream.phase==st::Phase::Established);QVERIFY(router.failure.isEmpty());channel.close();
  }
  void explicitSharedCapacityEmitsMoreThanFourSignedFramesAndFailedReleaseDoesNotWake(){
    Router router;QVERIFY(router.listen());Channel channel;st::ManagerOptions policy;policy.maximumStreams=64;registered(router,channel,policy);
    std::array<std::uint64_t,6> operations{};for(std::size_t i=0;i<operations.size();++i){auto op=channel.connectStream(router.remote.destination()->wire,{static_cast<std::uint16_t>(9000+i),80,10000});QVERIFY(op);operations[i]=*op;}
    std::array<bool,6> observed{};std::size_t count=0;
    const auto collect=[&]{router.collect();while(router.next()){
      auto sent=cp::decodeSendDatagram(router.take());if(!sent||sent->recipient.wire!=router.remote.destination()->wire){router.failure="Wrong shared actual recipient";return false;}
      auto mux=st::decodeMux(sent->payload);if(!mux||mux->toPort!=80||mux->fromPort<9000||mux->fromPort>=9006||!st::decodeInitialSyn(mux->packet,*router.remote.destination())){router.failure="Wrong shared actual signed SYN/ports";return false;}
      const auto index=static_cast<std::size_t>(mux->fromPort-9000);if(!observed[index]){observed[index]=true;++count;}
    }return router.failure.isEmpty()&&count==operations.size();};
    QTRY_VERIFY(collect());QCOMPARE(channel.streamManagerObservation().streams,std::size_t{6});
    QCoreApplication::sendPostedEvents(&channel,QEvent::MetaCall);QSignalSpy wakes(&channel,&Channel::streamChanged);
    QVERIFY(!channel.releaseStream(operations[2]));QCoreApplication::sendPostedEvents(&channel,QEvent::MetaCall);QCOMPARE(wakes.count(),0);
    QVERIFY(channel.cancelStream(operations[2]));QVERIFY(channel.releaseStream(operations[2]));QVERIFY(!channel.managedStreamObservation(operations[2]));
    for(std::size_t i=0;i<operations.size();++i){if(i!=2){QCOMPARE(channel.managedStreamObservation(operations[i])->stream.phase,st::Phase::Connecting);}}
    auto replacement=channel.connectStream(router.remote.destination()->wire,{9010,80,10000});QVERIFY(replacement&&*replacement!=operations[2]);QCOMPARE(channel.streamManagerObservation().streams,std::size_t{6});QVERIFY(router.failure.isEmpty());channel.close();
  }
};
QTEST_GUILESS_MAIN(ManagerChannelTests)
#include "i2cp_stream_manager_tests.moc"
