// SPDX-License-Identifier: GPL-2.0-or-later
#include "actor.hpp"
#ifdef BIGLYBTC_I2P_TEST_SUBSCRIPTION_FIXTURE
#include "owned_i2p_profile_fixture.hpp"
#include "owner_runtime_guard.hpp"
#include "profile_owner.hpp"
#include "saved_subscription_seed.hpp"
#include "stream.hpp"
#include <QDateTime>
#include <QtEndian>
#endif
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
#ifdef BIGLYBTC_I2P_TEST_SUBSCRIPTION_FIXTURE
namespace stream=i2p::streaming;
namespace discovery=i2p::dht;
class SubscriptionLeecher {
public:
  SubscriptionLeecher(Router& router, const i2p::ClientIdentity& remote, i2p::Destination local,
      const discovery::Contact& contact, std::string_view key, std::string_view payload)
      : router_(router), remote_(remote), local_(std::move(local)), contact_(contact),
        key_(key), payload_(payload) { clock_.start(); }

  bool pump() {
    if (!failure.isEmpty()) { return true; }
    try {
      while (router_.next()) { receive(router_.take()); }
      if (!connection_ && resolvedAt_ && elapsed()-*resolvedAt_>=500) {
        connection_.emplace(1,1717,*remote_.destination(),local_,stream::MaximumWindow,elapsed());
      }
      if (!connection_) { return false; }
      if (!offered_ && connection_->observation().phase==stream::Phase::Established) {
        QByteArray handshake("\x13" "BitTorrent protocol",20);
        handshake += QByteArray(8,'\0') + QByteArray::fromStdString(key_) + "-BB0001-123456789012";
        write(handshake);
        offered_=true;
      }
      const auto received=connection_->read(65536);
      data_.append(reinterpret_cast<const char*>(received.data()),static_cast<qsizetype>(received.size()));
      require(data_.size()<=65536,"Unbounded subscription peer response");
      processMessages();
      for (unsigned turn=0; turn<8; ++turn) {
        const auto packet=connection_->next(elapsed());
        if (!packet) { break; }
        const auto bytes=stream::encode(*packet,remote_);
        require(bool(bytes),"Cannot sign the test peer's Streaming packet");
        const auto mux=stream::encodeMux({6881,6881,*bytes});
        require(bool(mux),"Cannot encode the test peer's Streaming mux");
        deliver(*mux);
        connection_->dispatched(*packet,elapsed());
      }
      return announcing_ && blocks_==expectedBlocks();
    } catch (const std::exception& error) {
      failure=QString::fromUtf8(error.what());
      return true;
    }
  }
  QString state() const {
    return QStringLiteral("announce=%1 peer-resolved=%2 stream-phase=%3 peer-handshake=%4 requests=%5 blocks=%6")
        .arg(announcing_).arg(resolvedAt_.has_value())
        .arg(connection_ ? static_cast<int>(connection_->observation().phase) : -1)
        .arg(handshake_).arg(requested_).arg(blocks_);
  }
  QString failure;

private:
  static void require(bool condition,const char* reason) { if (!condition) { throw std::runtime_error(reason); } }
  std::uint64_t elapsed() const { return static_cast<std::uint64_t>(clock_.elapsed()); }
  static void appendInt(QByteArray& bytes,quint32 value) {
    const auto wire=qToBigEndian(value);
    bytes.append(reinterpret_cast<const char*>(&wire),sizeof(wire));
  }
  static quint32 readInt(const char* bytes) { return qFromBigEndian<quint32>(bytes); }
  unsigned expectedBlocks() const { return payload_.size()>PieceBytes ? 2 : 1; }
  std::size_t length(unsigned index) const { return std::min<std::size_t>(8192,payload_.size()-index*PieceBytes); }
  void deliver(const i2p::Bytes& payload) {
    const auto frame=cp::encodeMessagePayload({7,messageId_++,payload});
    require(frame && router_.send(*frame),"Cannot route a test peer packet through actual I2CP");
  }
  void write(const QByteArray& bytes) {
    require(bool(connection_->write({reinterpret_cast<const i2p::Byte*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size())})),"Cannot queue the test peer's BitTorrent request");
  }
  void receive(const cp::Frame& frame) {
    if (frame.type==cp::HostLookupType) {
      const auto request=cp::decodeHostLookup(frame);
      require(request && request->hash==remote_.destination()->hash,"Owned SDK must resolve the discovered peer");
      const auto reply=cp::encodeHostReply({7,request->request,0,*remote_.destination()});
      require(reply && router_.send(*reply),"Cannot send the resolved test peer Destination");
      resolvedAt_=elapsed();
    } else if (frame.type==cp::SendMessageType || frame.type==cp::SendMessageExpiresType) {
      if (frame.type==cp::SendMessageExpiresType) {
        if (const auto dht=cp::decodeDhtSendDatagram(frame); dht) {
          require(dht->recipient.wire==remote_.destination()->wire,"Wrong I2P announcement destination");
          receiveAnnouncement(dht->payload);
          return;
        }
      }
      const auto sent=cp::decodeSendDatagram(frame);
      require(sent && sent->recipient.wire==remote_.destination()->wire,"Wrong peer transport destination");
      const auto mux=stream::decodeMux(sent->payload);
      require(mux && mux->fromPort==6881 && mux->toPort==6881,"Wrong owned Streaming ports");
      auto packet=stream::decode(mux->packet,local_);
      require(bool(packet),"Invalid Streaming packet from the owned SDK");
      if (!connection_) {
        packet=stream::decodeInitialSyn(mux->packet,*remote_.destination());
        require(bool(packet),"Owned SDK did not sign its initial peer SYN");
        auto accepted=stream::Connection::accept(1,1717,*remote_.destination(),local_,*packet,stream::MaximumWindow,elapsed());
        require(bool(accepted),"Test peer could not accept the owned SDK connection");
        connection_.emplace(std::move(*accepted));
      } else if (packet->send==1717) { connection_->receive(std::move(*packet),elapsed()); }
    } else {
      throw std::runtime_error("Unexpected I2CP frame type " + std::to_string(frame.type) + " in the subscription transfer");
    }
  }
  void receiveAnnouncement(const i2p::Bytes& payload) {
    const auto mux=cp::decodeDhtMuxDatagram(payload);
    require(mux && mux->protocol==18,"Unexpected owned DHT dispatch while serving the seed");
    const auto message=discovery::decode(mux->datagram);
    require(message && message->method==discovery::Method::AnnouncePeer && message->seed
        && std::string_view(reinterpret_cast<const char*>(message->target.data()),message->target.size())==key_,
        "Wrong signed seed key in actual I2P announce");
    discovery::Message ack;
    ack.kind=discovery::Kind::Reply;
    ack.id=contact_.id;
    ack.transaction=message->transaction;
    const auto bytes=discovery::encode(ack);
    require(bool(bytes),"Cannot encode the test router's DHT acknowledgement");
    const auto reply=cp::encodeDhtMuxDatagram({18,contact_.port,10001,*bytes},cp::GzipMode::Deflate);
    require(bool(reply),"Cannot mux the test router's DHT acknowledgement");
    deliver(*reply);
    announcing_=true;
  }
  void processMessages() {
    if (!handshake_) {
      if (data_.size()<68) { return; }
      require(data_.first(20)==QByteArray("\x13" "BitTorrent protocol",20)
          && data_.mid(28,20)==QByteArray::fromStdString(key_),"Peer-wire handshake must use the signed subscription hash");
      handshake_=true;
      data_.remove(0,68);
      write(QByteArray("\0\0\0\1\2",5));
    }
    while (data_.size()>=4) {
      const auto size=readInt(data_.constData());
      require(size<=32768,"Unbounded BitTorrent message length");
      if (data_.size()<4+static_cast<qsizetype>(size)) { break; }
      const auto packet=data_.mid(4,size);
      data_.remove(0,4+static_cast<qsizetype>(size));
      if (packet==QByteArray(1,'\0')) { continue; }
      if (packet==QByteArray(1,'\1') && !requested_) {
        QByteArray request;
        for (unsigned index=0; index<expectedBlocks(); ++index) {
          appendInt(request,13);
          request.append('\6');
          appendInt(request,index);
          appendInt(request,0);
          appendInt(request,static_cast<quint32>(length(index)));
        }
        write(request);
        requested_=true;
      }
      if (packet.size()>=9 && packet.front()=='\7') {
        const auto index=readInt(packet.constData()+1);
        require(requested_ && index<expectedBlocks() && !seen_[index] && readInt(packet.constData()+5)==0,
            "Expected one requested block from each checked piece");
        require(packet.sliced(9)==QByteArray(payload_.data()+index*PieceBytes,static_cast<qsizetype>(length(index))),
            "Owned I2P peer returned different signed subscription bytes");
        seen_[index]=true;
        ++blocks_;
      }
    }
  }

  static constexpr std::size_t PieceBytes=256*1024;
  Router& router_;
  const i2p::ClientIdentity& remote_;
  i2p::Destination local_;
  discovery::Contact contact_;
  std::string key_,payload_;
  QElapsedTimer clock_;
  std::optional<stream::Connection> connection_;
  std::optional<std::uint64_t> resolvedAt_;
  QByteArray data_;
  std::array<bool,2> seen_{};
  std::uint32_t messageId_=30;
  unsigned blocks_=0;
  bool offered_=false,handshake_=false,requested_=false,announcing_=false;
};
#endif
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
#ifdef BIGLYBTC_I2P_TEST_SUBSCRIPTION_FIXTURE
  void actualSignedPrivateSeed_data() {
    QTest::addColumn<bool>("withdraw");
    QTest::addColumn<bool>("transfer");
    QTest::newRow("announce-dormant-signed-version") << false << false;
    QTest::newRow("withdraw-before-announce") << true << false;
    QTest::newRow("stream-checked-signed-pieces") << false << true;
  }
  void actualSignedPrivateSeed() {
    QFETCH(bool,withdraw);
    QFETCH(bool,transfer);
    namespace original=owned_i2p_test;
    namespace search=biglybtc::search;
    namespace engine=biglybtc::engine;
    namespace provider=i2p::provider;
    original::Fixture profile;
    if (!provider::privateOwnerRuntimePin()) { QSKIP("Approved private I2P SDK required"); }
    const auto fixture=original::core::Document::parse(original::read(BIGLYBTC_I2P_TEST_SUBSCRIPTION_FIXTURE));
    QVERIFY(fixture);
    const auto* source=fixture->find("i2p");
    QVERIFY(source && source->find("hash") && source->find("file") && source->find("payload") && source->find("config"));
    const auto key=source->find("hash")->bytes;
    const auto hash=original::core::TorrentHash::parse(key);
    QVERIFY(hash);
    const auto file=source->find("file")->bytes;
    const auto body=source->find("payload")->bytes;
    original::fs::create_directory(profile.path/"subs");
    original::write(profile.path/"subs"/file.substr(0,file.rfind('.')),body);
    original::write(profile.path/"subscriptions.config",source->find("config")->bytes);
    const std::string inventory="d9:downloadslee";
    original::write(profile.path/"downloads.config",inventory);
    const engine::HeldSubscriptionSeedResolver resolve=[&](const auto& access,std::string_view id,engine::SubscriptionSeedRead read)
        ->std::optional<engine::SubscriptionSeedSource> {
      const auto mode=read==engine::SubscriptionSeedRead::Prepare?search::SubscriptionSeedFile::Prepare:search::SubscriptionSeedFile::Verify;
      auto staged=search::resolveVersionedSubscriptionSeed(*profile.lease,access,id,search::SubscriptionSeedNetwork::I2P,1000,mode);
      if (!staged || !*staged) { return {}; }
      return engine::SubscriptionSeedSource{std::move((**staged).metainfo.metainfo),std::move((**staged).directory),
          std::move((**staged).directoryHandle)};
    };
    auto owner=provider::OwnedProfile::prepareSubscription(*profile.lease,*hash,resolve);
    QVERIFY2(owner,owner?"":owner.error().message.c_str());
    Router router;
    QVERIFY(router.listen());
    auto remote=signer("dsa");
    d::Contact contact;
    contact.hash=remote.destination()->hash;
    contact.destination=*remote.destination();
    contact.port=10002;
    const auto rid=d::makeNodeId(contact.hash,contact.port,i2p::Bytes(14,0x32));
    QVERIFY(rid);
    contact.id=*rid;
    QVERIFY((*owner)->configureDiscovery({{},10000,{contact}}));
    QVERIFY((*owner)->begin({router.options(),{{"i2cp.fastReceive","true"}},{6881,6881}},{},{}));
    QTRY_VERIFY(router.next());
    QCOMPARE(router.take().type,cp::GetDateType);
    const auto now=static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch());
    const auto date=cp::encodeMessage(cp::SetDate{now,std::string("2.13.0")});
    QVERIFY(date && router.send(*date));
    QTRY_VERIFY(router.next());
    QCOMPARE(router.take().type,cp::CreateSessionType);
    const auto created=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});
    QVERIFY(created && router.send(*created));
    QVERIFY(router.lease(now+600000));
    QTRY_VERIFY(router.next());
    QVERIFY(cp::decodeCreateLeaseSet2(router.take()));
    QTRY_VERIFY_WITH_TIMEOUT(router.next(),10000);
    const auto sent=cp::decodeDhtSendDatagram(router.take());
    QVERIFY(sent && sent->recipient.wire==contact.destination->wire);
    const auto mux=cp::decodeDhtMuxDatagram(sent->payload);
    QVERIFY(mux && mux->protocol==17);
    const auto signedQuery=i2p::decode1(mux->datagram);
    QVERIFY(signedQuery);
    const auto query=d::decode(signedQuery->payload);
    QVERIFY(query && query->method==d::Method::GetPeers && query->noSeed);
    QCOMPARE(std::string(reinterpret_cast<const char*>(query->target.data()),query->target.size()),key);
    QVERIFY(!(*owner)->subscriptionSeedLoaded(*hash));
    if (withdraw) { QVERIFY(original::fs::remove(profile.path/"subscriptions.config")); }
    d::Message response;
    response.kind=d::Kind::Reply;
    response.id=contact.id;
    response.transaction=query->transaction;
    response.token={0,0xff,0x51};
    response.hasNodes=withdraw;
    if (!withdraw) {
      response.hasPeers=true;
      response.peers={{remote.destination()->hash,!transfer}};
    }
    const auto reply=d::encode(response);
    QVERIFY(reply);
    const auto packet=cp::encodeDhtMuxDatagram({18,10002,10001,*reply},cp::GzipMode::Deflate);
    QVERIFY(packet);
    inbound(router,21,*packet,true);
    if (withdraw) {
      QTest::qWait(1000);
      QVERIFY(!router.next());
      QVERIFY(!(*owner)->subscriptionSeedLoaded(*hash));
    } else if (transfer) {
      auto local=signer("ed25519");
      SubscriptionLeecher leecher(router,remote,*local.destination(),contact,key,body);
      QTRY_VERIFY2_WITH_TIMEOUT(leecher.pump(),qPrintable(leecher.state()),15000);
      QVERIFY2(leecher.failure.isEmpty(),qPrintable(leecher.failure));
      QVERIFY((*owner)->subscriptionSeedLoaded(*hash));
    } else {
      QTRY_VERIFY(router.next());
      const auto packet=cp::decodeDhtSendDatagram(router.take());
      QVERIFY(packet);
      const auto mux=cp::decodeDhtMuxDatagram(packet->payload);
      QVERIFY(mux && mux->protocol==18);
      const auto announced=d::decode(mux->datagram);
      QVERIFY(announced && announced->method==d::Method::AnnouncePeer && announced->seed && announced->target==query->target);
      QTest::qWait(1000);
      QVERIFY(!(*owner)->subscriptionSeedLoaded(*hash));
    }
    QCOMPARE((*owner)->phase(),provider::OwnedProfile::Phase::Active);
    QVERIFY((*owner)->observations().empty());
    (*owner)->close();
    (*owner)->join();
    QCOMPARE(original::read(profile.path/"downloads.config"),inventory);
    QCOMPARE(original::read(profile.path/"subs"/file),body);
  }
#endif
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
