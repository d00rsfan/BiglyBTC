// SPDX-License-Identifier: GPL-2.0-or-later
#include "actor.hpp"
#ifdef BIGLYBTC_I2P_TEST_SUBSCRIPTION_FIXTURE
#include "generate_identity.hpp"
#include "owned_i2p_profile_fixture.hpp"
#include "owner_runtime_guard.hpp"
#include "profile_owner.hpp"
#include "profile_backup.hpp"
#include "saved_subscription_seed.hpp"
#include "stream.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QtEndian>
#include <future>
#include <set>
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
  Router* publication=nullptr;
  std::optional<i2p::Destination> registeredDestination;
  Router(){QObject::connect(&server,&QTcpServer::newConnection,&server,[this]{
    auto* client=peer?publication:this;
    if (!client||client->peer) { failure="Unexpected extra client";return; }
    client->peer=server.nextPendingConnection();
    QObject::connect(client->peer,&QTcpSocket::readyRead,&client->server,[client]{client->collect();});
    client->collect();
  });}
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
      const discovery::Contact& contact, std::string_view key, std::string_view payload,bool requireSeed=true,bool serve=false)
      : router_(router), remote_(remote), local_(std::move(local)), contact_(contact),
        key_(key), payload_(payload), requireSeed_(requireSeed),serving_(serve) { clock_.start(); }

  bool pump() {
    if (!failure.isEmpty()) { return true; }
    try {
      while (router_.next()) { receive(router_.take()); }
      if (!serving_&&!connection_ && resolvedAt_ && elapsed()-*resolvedAt_>=500) {
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
      return announcing_ && (serving_?served_==payload_.size():blocks_==expectedBlocks());
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
    require(message && message->method==discovery::Method::AnnouncePeer && (!requireSeed_ || message->seed)&&
        (!serving_||!message->seed)
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
      if (serving_) {
        QByteArray available("\0\0\0\2\5",5);
        available.append(static_cast<char>(expectedBlocks()==2?0xc0:0x80));
        available.append("\0\0\0\1\1",5);
        write(available);
      } else { write(QByteArray("\0\0\0\1\2",5)); }
    }
    while (data_.size()>=4) {
      const auto size=readInt(data_.constData());
      require(size<=32768,"Unbounded BitTorrent message length");
      if (data_.size()<4+static_cast<qsizetype>(size)) { break; }
      const auto packet=data_.mid(4,size);
      data_.remove(0,4+static_cast<qsizetype>(size));
      if (serving_) {
        if (packet.size()==13&&packet.front()=='\6') {
          const auto index=readInt(packet.constData()+1);
          const auto begin=readInt(packet.constData()+5);
          const auto count=readInt(packet.constData()+9);
          const auto offset=static_cast<std::size_t>(index)*PieceBytes+begin;
          require(index<expectedBlocks()&&begin<PieceBytes&&count&&count<=16384&&offset<=payload_.size()&&
              count<=payload_.size()-offset,"Unexpected I2P subscription block requested");
          QByteArray block;
          appendInt(block,9+count);
          block.append('\7');
          appendInt(block,index);
          appendInt(block,begin);
          block.append(payload_.data()+offset,count);
          write(block);
          served_+=count;
        }
        continue;
      }
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
  bool requireSeed_;
  bool serving_;
  QElapsedTimer clock_;
  std::optional<stream::Connection> connection_;
  std::optional<std::uint64_t> resolvedAt_;
  QByteArray data_;
  std::array<bool,2> seen_{};
  std::uint32_t messageId_=30;
  unsigned blocks_=0;
  std::size_t served_=0;
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
#ifdef BIGLYBTC_I2P_TEST_SUBSCRIPTION_FIXTURE
  static biglybtc::engine::HeldSubscriptionSeedResolver seedResolver(owned_i2p_test::Fixture& profile,bool publication=false) {
    namespace search=biglybtc::search;
    namespace engine=biglybtc::engine;
    return [&profile,publication](const auto& access,std::string_view id,engine::SubscriptionSeedRead read)
        ->std::optional<engine::SubscriptionSeedSource> {
      const auto mode=read==engine::SubscriptionSeedRead::Prepare?search::SubscriptionSeedFile::Prepare:search::SubscriptionSeedFile::Verify;
      auto staged=search::resolveVersionedSubscriptionSeed(*profile.lease,access,id,search::SubscriptionSeedNetwork::I2P,1000,mode);
      if (!staged || !*staged) { return {}; }
      auto& checked=**staged;
      engine::SubscriptionSeedSource result;
      if (publication&&checked.metainfo.presence) {
        const auto& presence=*checked.metainfo.presence;
        auto wire=search::encodeSubscriptionPresence(presence,-315);
        if (wire) {
          result.presence=engine::SubscriptionSeedPublication{presence.dhtKey,std::move(*wire),
              static_cast<std::int32_t>(presence.version),presence.applicationKey};
          for (auto& association:checked.associations) {
            result.associations.push_back({association.dhtKey,{association.value.begin(),association.value.end()},
                static_cast<std::int32_t>(presence.version),std::move(association.applicationKey)});
          }
        }
        if (checked.ddbTransferAllowed) {
          auto response=search::subscriptionSeedDdbResponse(checked.metainfo);
          if (response) {
            result.ddbMetainfo=engine::SubscriptionSeedMetainfo{checked.metainfo.lookupKey,std::move(*response)};
          }
        }
      }
      if (checked.upgrade) {
        auto& probe=*checked.upgrade;
        result.upgrade=engine::SubscriptionSeedUpgradeProbe{
            std::move(probe.publicKey),std::move(probe.associations),probe.minimumVersion};
      }
      result.metainfo=std::move(checked.metainfo.metainfo);
      result.directory=std::move(checked.directory);
      result.directoryHandle=std::move(checked.directoryHandle);
      return result;
    };
  }
  void replaceSubscriptionRows(const biglybtc::storage::ProfileLease& lease,std::string rows) {
    namespace storage=biglybtc::storage;
    auto saved=std::async(std::launch::async,[&lease,rows=std::move(rows)]()->std::expected<void,storage::Error> {
      auto file=storage::ConfigFile::load(lease,"subscriptions.config");
      if (!file) { return std::unexpected(file.error()); }
      if (!file->document().replaceValue("subs",rows)) {
        return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Cannot change the test subscription registry"});
      }
      return file->save(lease);
    });
    QTRY_VERIFY(saved.wait_for(std::chrono::seconds(0))==std::future_status::ready);
    const auto result=saved.get();
    QVERIFY2(result,result?"":result.error().message.c_str());
  }
  std::optional<d::Contact> seedContact(const i2p::ClientIdentity& remote) {
    d::Contact contact;
    contact.hash=remote.destination()->hash;
    contact.destination=*remote.destination();
    contact.port=10002;
    const auto id=d::makeNodeId(contact.hash,contact.port,i2p::Bytes(14,0x32));
    if (!id) { return {}; }
    contact.id=*id;
    return contact;
  }
  void registerSeedOwner(Router& router,i2p::provider::OwnedProfile& owner,const d::Contact& contact,
      const d::Contact* mixed=nullptr) {
    QVERIFY(router.listen());
    const auto mixedRoutes=mixed?std::optional(d::Actor::Options{{},0,{*mixed}}):std::nullopt;
    QVERIFY(owner.configureDiscovery({{},10000,{contact}},mixedRoutes));
    QVERIFY(owner.begin({router.options(),{{"i2cp.fastReceive","true"}},{6881,6881}},{},{}));
    QTRY_VERIFY(router.next());
    QCOMPARE(router.take().type,cp::GetDateType);
    const auto now=static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch());
    const auto date=cp::encodeMessage(cp::SetDate{now,std::string("2.13.0")});
    QVERIFY(date && router.send(*date));
    QTRY_VERIFY(router.next());
    const auto create=cp::decodeSessionMessage(router.take());
    QVERIFY(create&&std::holds_alternative<cp::CreateSession>(*create));
    router.registeredDestination=std::get<cp::CreateSession>(*create).config.destination;
    const auto created=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});
    QVERIFY(created && router.send(*created));
    QVERIFY(router.lease(now+600000));
    QTRY_VERIFY(router.next());
    QVERIFY(cp::decodeCreateLeaseSet2(router.take()));
  }
  void replyToSeed(Router& router,std::uint32_t id,const d::Message& response,std::uint16_t queryPort=10000) {
    const auto reply=d::encode(response);
    QVERIFY(reply);
    const auto packet=cp::encodeDhtMuxDatagram({18,10002,static_cast<std::uint16_t>(queryPort+1),*reply},cp::GzipMode::Deflate);
    QVERIFY(packet);
    inbound(router,id,*packet,true);
  }
  std::optional<d::Message> signedSeedQuery(const cp::Frame& frame,const d::Contact& contact,bool requireNoSeed=true) {
    const auto sent=cp::decodeDhtSendDatagram(frame);
    if (!sent || sent->recipient.wire!=contact.destination->wire) { return {}; }
    const auto mux=cp::decodeDhtMuxDatagram(sent->payload);
    if (!mux || mux->protocol!=17) { return {}; }
    const auto signedQuery=i2p::decode1(mux->datagram);
    if (!signedQuery) { return {}; }
    const auto query=d::decode(signedQuery->payload);
    if (!query || query->method!=d::Method::GetPeers || (requireNoSeed && !query->noSeed)) { return {}; }
    return *query;
  }
#endif
private slots:
#ifdef BIGLYBTC_I2P_TEST_SUBSCRIPTION_FIXTURE
  void actualPrivateSubscriptionPresenceUsesCurrentRegistry() {
    namespace original=owned_i2p_test;
    namespace provider=i2p::provider;
    namespace search=biglybtc::search;
    original::Fixture profile;
    if (!provider::privateOwnerRuntimePin()) { QSKIP("Approved private I2P SDK required"); }
    const auto fixture=original::core::Document::parse(original::read(BIGLYBTC_I2P_TEST_SUBSCRIPTION_FIXTURE));
    QVERIFY(fixture);
    const auto* source=fixture->find("i2p");
    QVERIFY(source&&source->find("hash")&&source->find("file")&&source->find("payload")&&source->find("config"));
    const auto hash=original::core::TorrentHash::parse(source->find("hash")->bytes);
    QVERIFY(hash);
    const auto name=source->find("file")->bytes;
    const auto catalog=profile.path/"subscriptions.config";
    const auto saved=source->find("config")->bytes;
    original::fs::create_directory(profile.path/"subs");
    original::write(profile.path/"subs"/name.substr(0,name.rfind('.')),source->find("payload")->bytes);
    original::write(profile.path/"downloads.config","d9:downloadslee");
    original::write(catalog,saved);
    const auto config=original::core::Document::parse(std::string(saved));
    QVERIFY(config&&config->find("subs")&&config->find("subs")->children.size()==1);
    const auto& row=config->find("subs")->children.front();
    const auto originalSeed=search::makeSubscriptionSeed(row,std::string(source->find("payload")->bytes),1000);
    QVERIFY(originalSeed&&originalSeed->presence&&originalSeed->anonymous&&row.find("public_key"));
    d::Id versionKey{};
    std::ranges::copy(originalSeed->presence->dhtKey,versionKey.begin());
    d::Id handler{},lookupKey{};
    const auto handlerDigest=QCryptographicHash::hash(
        "org.gudy.azureus2.pluginsimpl.local.ddb.DDBaseTTTorrent",QCryptographicHash::Sha1);
    std::copy_n(handlerDigest.begin(),handler.size(),handler.begin());
    std::ranges::copy(originalSeed->lookupKey,lookupKey.begin());
    const auto metainfo=search::subscriptionSeedDdbResponse(*originalSeed);
    QVERIFY(metainfo);

    auto owner=provider::OwnedProfile::prepareSubscription(*profile.lease,*hash,seedResolver(profile,true));
    QVERIFY2(owner,owner?"":owner.error().message.c_str());
    auto remote=signer("dsa");
    const auto contact=seedContact(remote);
    QVERIFY(contact);
    Router router;
    registerSeedOwner(router,**owner,*contact);
    QTRY_VERIFY_WITH_TIMEOUT(router.next(),10000);
    QVERIFY(signedSeedQuery(router.take(),*contact));

    i2p::Bytes token;
    const auto readMetainfo=[&](d::Id transferHandler,d::Id transferKey,std::uint8_t sequence,bool serve,
        std::int32_t offset=0,std::int32_t length=0) {
      d::Message query;
      query.id=contact->id;
      query.transaction={sequence,1,2,3};
      query.method=d::Method::AzureusData;
      query.wakeRequested=true;
      const auto time=QDateTime::currentMSecsSinceEpoch();
      d::AzureusReadPacket read{d::AzureusReadPacketType::Request,transferHandler,transferKey,time,
          -1337-sequence,offset,length,0,{}};
      query.azureus=d::AzureusMessage{191,0,read};
      const auto plain=d::encode(query);
      QVERIFY(plain);
      const auto signedPacket=i2p::encode1(remote.destination()->wire,remote.signingPrivateKey(),*plain);
      QVERIFY(signedPacket);
      const auto packet=cp::encodeDhtMuxDatagram({17,10002,10000,*signedPacket},cp::GzipMode::Deflate);
      QVERIFY(packet);
      for (unsigned n=0;n<(serve?4U:1U)&&!router.next();++n) {
        inbound(router,400+sequence*4+n,*packet,true);
        if (QTest::qWaitFor([&]{return router.next().has_value();},serve?800:180)) { break; }
      }
      QCOMPARE(router.next().has_value(),serve);
      if (!serve) { return; }
      const auto out=cp::decodeDhtSendDatagram(router.take());
      QVERIFY(out&&!out->skipLeaseSet&&out->recipient.wire==remote.destination()->wire);
      const auto mux=cp::decodeDhtMuxDatagram(out->payload);
      QVERIFY(mux&&mux->protocol==17&&mux->fromPort==10000&&mux->toPort==10002);
      const auto actual=i2p::decode1(mux->datagram);
      QVERIFY(actual&&router.registeredDestination&&actual->sender.wire==router.registeredDestination->wire);
      const auto reply=d::decode(actual->payload);
      QVERIFY(reply&&reply->method==d::Method::AzureusData&&reply->kind==d::Kind::Query&&reply->azureus);
      const auto* data=std::get_if<d::AzureusReadPacket>(&reply->azureus->payload);
      QVERIFY(data&&data->type==d::AzureusReadPacketType::Reply&&data->connection==read.connection&&data->handler==handler
          &&data->key==lookupKey&&data->total==static_cast<std::int32_t>(metainfo->size())&&data->start==offset);
      const auto expected=metainfo->substr(static_cast<std::size_t>(offset),length?static_cast<std::size_t>(length):std::string::npos);
      QCOMPARE(QByteArray(reinterpret_cast<const char*>(data->fragment.data()),data->fragment.size()),
          QByteArray::fromStdString(expected));
    };
    const auto request=[&](d::Id key,std::uint8_t number,bool hit) {
      d::Message query;
      query.id=contact->id;
      query.transaction={number,0x7f,0x4a,0x23};
      query.method=d::Method::AzureusFindValue;
      query.azureus=d::AzureusMessage{191,0,d::AzureusFindValue{key,12,0,QDateTime::currentMSecsSinceEpoch()}};
      const auto bytes=d::encode(query);
      QVERIFY(bytes);
      const auto signedQuery=i2p::encode1(remote.destination()->wire,remote.signingPrivateKey(),*bytes);
      QVERIFY(signedQuery);
      const auto packet=cp::encodeDhtMuxDatagram({17,10002,10000,*signedQuery},cp::GzipMode::Deflate);
      QVERIFY(packet);
      // A datagram can arrive during profile-watch invalidation. Retransmit
      // the same transaction after the source has had a chance to revalidate.
      for (unsigned attempt=0;attempt<4&&!router.next();++attempt) {
        inbound(router,100+number*4+attempt,*packet,true);
        if (QTest::qWaitFor([&]{return router.next().has_value();},1000)) { break; }
      }
      const auto details=QStringLiteral("FIND_VALUE request %1 %2").arg(number).arg(hit?"expected a value":"expected a miss");
      QVERIFY2(router.next(),qPrintable(details));
      const auto out=cp::decodeDhtSendDatagram(router.take());
      QVERIFY(out&&out->skipLeaseSet&&out->recipient.wire==remote.destination()->wire);
      const auto mux=cp::decodeDhtMuxDatagram(out->payload);
      QVERIFY(mux&&mux->protocol==18&&mux->fromPort==10000&&mux->toPort==10003);
      const auto reply=d::decode(mux->datagram);
      QVERIFY(reply&&reply->azureus&&reply->transaction==query.transaction);
      QVERIFY(reply->token.size()==8);
      token=reply->token;
      const auto* values=std::get_if<d::AzureusValueHit>(&reply->azureus->payload);
      if (!hit) {
        QVERIFY(!values&&std::holds_alternative<std::vector<d::AzureusContact>>(reply->azureus->payload));
        return;
      }
      QVERIFY2(values&&values->values.size()==1,qPrintable(details));
      const auto& returned=values->values.front();
      QVERIFY(!returned.origin&&returned.flags==16);
      const auto signedRecord=search::verifySubscriptionPresence(
          {reinterpret_cast<const char*>(returned.bytes.data()),returned.bytes.size()},row.find("public_key")->bytes,4);
      QVERIFY(signedRecord&&signedRecord->publicationHash==originalSeed->publicationHash&&signedRecord->dhtKey==originalSeed->presence->dhtKey);
    };
    request(versionKey,1,true);
    readMetainfo(handler,lookupKey,21,true);
    readMetainfo(handler,lookupKey,22,true,3,9);
    auto otherHandler=handler;
    otherHandler.front()^=1;
    readMetainfo(otherHandler,lookupKey,23,false);
    auto otherKey=lookupKey;
    otherKey.back()^=1;
    readMetainfo(handler,otherKey,24,false);
    auto unknown=versionKey;
    unknown.front()^=1;
    request(unknown,2,false);
    const auto encoded=original::core::encodeValue(row);
    QVERIFY(encoded);
    auto locallyPrivate=original::core::Document::parse(*encoded);
    QVERIFY(locallyPrivate&&locallyPrivate->replaceValue("is_public","i0e"));
    replaceSubscriptionRows(*profile.lease,"l"+locallyPrivate->serialize()+'e');
    request(versionKey,3,false);
    readMetainfo(handler,lookupKey,25,false);
    QCOMPARE((*owner)->phase(),provider::OwnedProfile::Phase::Active);
    replaceSubscriptionRows(*profile.lease,"l"+*encoded+'e');
    request(versionKey,4,true);
    readMetainfo(handler,lookupKey,26,true);
    replaceSubscriptionRows(*profile.lease,"le");
    request(versionKey,5,false);

    const auto encodedVersion=search::encodeSubscriptionPresence(*originalSeed->presence,-315);
    QVERIFY(encodedVersion&&token.size()==8);
    const auto wall=QDateTime::currentMSecsSinceEpoch();
    d::AzureusValue value;
    value.bytes.assign(encodedVersion->begin(),encodedVersion->end());
    value.created=wall;
    value.version=4;
    value.flags=16;
    d::Message store;
    store.id=contact->id;
    store.method=d::Method::AzureusStore;
    store.azureus=d::AzureusMessage{191,0,d::AzureusStore{{{versionKey,{value}}},wall}};
    const auto write=[&](std::uint8_t sequence,i2p::Bytes selected) {
      store.token=std::move(selected);
      store.transaction={sequence,0x7f,0x4a,0x23};
      const auto encoded=d::encode(store);
      QVERIFY(encoded);
      const auto signedStore=i2p::encode1(remote.destination()->wire,remote.signingPrivateKey(),*encoded);
      QVERIFY(signedStore);
      const auto packet=cp::encodeDhtMuxDatagram({17,10002,10000,*signedStore},cp::GzipMode::Deflate);
      QVERIFY(packet);
      inbound(router,200+sequence,*packet,true);
    };
    auto invalidToken=token;
    invalidToken.front()^=1;
    write(7,invalidToken);
    QTest::qWait(250);
    QVERIFY(!router.next());
    request(versionKey,6,false);
    write(8,token);
    QTRY_VERIFY(router.next());
    const auto stored=cp::decodeDhtSendDatagram(router.take());
    QVERIFY(stored&&stored->skipLeaseSet);
    const auto mux=cp::decodeDhtMuxDatagram(stored->payload);
    QVERIFY(mux&&mux->protocol==18);
    const auto ack=d::decode(mux->datagram);
    QVERIFY(ack&&ack->azureus&&ack->transaction==store.transaction&&ack->method==d::Method::AzureusStore);
    QVERIFY(std::get<d::AzureusStoreAck>(ack->azureus->payload).diversification==i2p::Bytes{1});
    request(versionKey,9,true);
    readMetainfo(handler,lookupKey,27,false);
    QCOMPARE((*owner)->phase(),provider::OwnedProfile::Phase::Active);
    (*owner)->close();
    (*owner)->join();
  }

  void actualPrivateSubscriptionChecksUpgradeFromSavedAssociation_data() {
    QTest::addColumn<bool>("verified");
    QTest::addColumn<bool>("transfer");
    QTest::addColumn<bool>("corrupted");
    QTest::addColumn<int>("originMode");
    QTest::newRow("verified-definition")<<true<<false<<false<<0;
    QTest::newRow("verified-payload")<<true<<true<<false<<0;
    QTest::newRow("torrent-verified-invalid-signature")<<true<<true<<true<<0;
    QTest::newRow("behind-replica")<<true<<false<<false<<1;
    QTest::newRow("mismatched-origin-falls-back-to-reporter")<<true<<false<<false<<2;
    QTest::newRow("replayed-older-definition")<<false<<false<<false<<1;
  }
  void actualPrivateSubscriptionChecksUpgradeFromSavedAssociation() {
    QFETCH(bool,verified);
    QFETCH(bool,transfer);
    QFETCH(bool,corrupted);
    QFETCH(int,originMode);
    namespace original=owned_i2p_test;
    namespace provider=i2p::provider;
    namespace search=biglybtc::search;
    original::Fixture profile;
    if (!provider::privateOwnerRuntimePin()) { QSKIP("Approved private I2P SDK required"); }
    const auto fixture=original::core::Document::parse(original::read(BIGLYBTC_I2P_TEST_UPGRADE_FIXTURE));
    QVERIFY(fixture&&fixture->find("v4")&&fixture->find("v5"));
    const auto& before=*fixture->find("v4");
    const auto& after=*fixture->find("v5");
    const auto& row=*before.find("row");
    const auto payload=std::string(before.find("definition")->bytes);
    const auto originalSeed=search::makeSubscriptionSeed(row,payload,1000);
    auto newPayload=std::string(after.find("definition")->bytes);
    auto newerSeed=search::makeSubscriptionSeed(*after.find("row"),newPayload,1000);
    QVERIFY(originalSeed&&originalSeed->presence&&newerSeed&&newerSeed->presence);
    if (corrupted) {
      newPayload.back()='x';
      auto metainfo=original::core::Document::parse(newerSeed->metainfo);
      QVERIFY(metainfo&&metainfo->find("info"));
      const auto infoBytes=original::core::encodeValue(*metainfo->find("info"));
      QVERIFY(infoBytes);
      auto info=original::core::Document::parse(*infoBytes);
      const auto replacement=QCryptographicHash::hash(QByteArray::fromStdString(newPayload),QCryptographicHash::Sha1);
      QVERIFY(info&&info->replaceValue("pieces","20:"+replacement.toStdString())&&metainfo->replaceValue("info",info->serialize()));
      newerSeed->metainfo=metainfo->serialize();
    }
    const auto hash=original::core::TorrentHash::parse({originalSeed->publicationHash.data(),originalSeed->publicationHash.size()});
    QVERIFY(hash);
    auto associated=original::core::Document::parse(*original::core::encodeValue(row));
    const auto sourceHistory=std::string(reinterpret_cast<const char*>(profile.hash.bytes().data()),profile.hash.bytes().size());
    QVERIFY(associated&&sourceHistory.size()==20&&associated->eraseValue("private_key").has_value()
        &&associated->replaceValue("is_public","i1e")&&associated->replaceValue("rand","i12e")&&
        associated->replaceValue("assoc","ld1:h20:"+sourceHistory+"1:wi1eee"));
    const auto saved="d4:subsl"+associated->serialize()+"ee";
    const auto& name=originalSeed->fileName;
    original::fs::create_directory(profile.path/"subs");
    original::write(profile.path/"subs"/name.substr(0,name.rfind('.')),payload);
    original::write(profile.path/"subscriptions.config",saved);
    original::write(profile.path/"downloads.config","d9:downloadslee");

    auto owner=provider::OwnedProfile::prepareSubscription(*profile.lease,*hash,seedResolver(profile,true));
    QVERIFY2(owner,owner?"":owner.error().message.c_str());
    auto remote=signer("dsa");
    const auto contact=seedContact(remote);
    QVERIFY(contact);
    std::optional<i2p::ClientIdentity> originIdentity;
    std::optional<d::Contact> origin;
    if (originMode) {
      auto identity=i2p::generateClientIdentity();
      QVERIFY(identity);
      originIdentity.emplace(std::move(*identity));
      origin=seedContact(*originIdentity);
      QVERIFY(origin&&origin->hash!=contact->hash);
    }
    Router publisher,router;
    router.publication=&publisher;
    registerSeedOwner(router,**owner,*contact);
    QTRY_VERIFY_WITH_TIMEOUT(router.next(),10000);
    const auto announcement=signedSeedQuery(router.take(),*contact);
    QVERIFY(announcement);
    d::Message response;
    response.kind=d::Kind::Reply;
    response.id=contact->id;
    response.transaction=announcement->transaction;
    response.token={0,0xff,0x51};
    response.hasPeers=true;
    response.peers={{remote.destination()->hash,true}};
    replyToSeed(router,510,response);
    QTRY_VERIFY(router.next());
    const auto announcePacket=cp::decodeDhtSendDatagram(router.take());
    QVERIFY(announcePacket);
    const auto raw=cp::decodeDhtMuxDatagram(announcePacket->payload);
    QVERIFY(raw&&raw->protocol==18);
    const auto announced=d::decode(raw->datagram);
    QVERIFY(announced&&announced->method==d::Method::AnnouncePeer);
    d::Message ack;
    ack.kind=d::Kind::Reply;ack.id=contact->id;ack.transaction=announced->transaction;
    replyToSeed(router,511,ack);

    const auto takeFind=[&]()->std::optional<d::Message> {
      const auto packet=cp::decodeDhtSendDatagram(router.take());
      if (!packet||packet->recipient.wire!=contact->destination->wire) { return {}; }
      const auto mux=cp::decodeDhtMuxDatagram(packet->payload);
      if (!mux||mux->protocol!=17) { return {}; }
      const auto signedPacket=i2p::decode1(mux->datagram);
      if (!signedPacket) { return {}; }
      auto query=d::decode(signedPacket->payload);
      if (!query||!query->azureus||query->method!=d::Method::AzureusFindValue) { return {}; }
      return std::move(*query);
    };
    QTRY_VERIFY_WITH_TIMEOUT(router.next(),8000);
    const auto lookup=takeFind();
    const auto associationKey=search::subscriptionAssociationDhtKey(profile.hash);
    QVERIFY(lookup&&associationKey);
    d::Id expectedAssociationKey{};
    std::ranges::copy(*associationKey,expectedAssociationKey.begin());
    QCOMPARE(std::get<d::AzureusFindValue>(lookup->azureus->payload).key,expectedAssociationKey);
    const auto& presence=*originalSeed->presence;
    const auto prefix=std::string_view("subscription:publish:").size();
    const auto sid=QByteArray::fromHex(QByteArray::fromStdString(presence.applicationKey).mid(prefix,20));
    search::SubscriptionAssociationHint hint;
    QCOMPARE(sid.size(),static_cast<qsizetype>(hint.shortId.size()));
    std::copy_n(sid.cbegin(),hint.shortId.size(),hint.shortId.begin());
    hint.version=presence.version+1;
    const auto hintWire=search::encodeSubscriptionAssociationHint(hint);
    QVERIFY(hintWire);
    const auto replyValue=[&](const d::Message& request,std::string_view bytes,std::uint32_t packet) {
      d::AzureusValue value;
      value.bytes.assign(bytes.begin(),bytes.end());
      if (origin&&packet==513) { value.origin=d::AzureusContact{*origin,4}; }
      d::Message hit;
      hit.kind=d::Kind::Reply;hit.id=contact->id;hit.transaction=request.transaction;
      hit.method=d::Method::AzureusFindValue;
      hit.azureus=d::AzureusMessage{191,0,d::AzureusValueHit{{std::move(value)}}};
      replyToSeed(router,packet,hit);
    };
    replyValue(*lookup,{hintWire->data(),hintWire->size()},512);
    QTRY_VERIFY_WITH_TIMEOUT(router.next(),8000);
    const auto versionLookup=takeFind();
    QVERIFY(versionLookup);
    const auto newer=presence.applicationKey.substr(0,prefix+21)+std::to_string(hint.version);
    const auto newerKey=QCryptographicHash::hash(QByteArray::fromStdString(newer),QCryptographicHash::Sha1);
    d::Id expectedVersionKey{};
    std::copy_n(newerKey.cbegin(),expectedVersionKey.size(),expectedVersionKey.begin());
    QCOMPARE(std::get<d::AzureusFindValue>(versionLookup->azureus->payload).key,expectedVersionKey);
    const auto version=search::encodeSubscriptionPresence(verified?*newerSeed->presence:presence,-315);
    QVERIFY(version);
    replyValue(*versionLookup,*version,513);
    std::optional<search::I2pSubscriptionUpgrade> acquired;
    std::optional<std::expected<std::string,biglybtc::storage::Error>> downloaded;
    if (verified) {
      const auto* responder=&remote;
      const auto* endpoint=&*contact;
      if (origin) {
        QTRY_VERIFY_WITH_TIMEOUT(router.next(),8000);
        const auto lookupDestination=cp::decodeHostLookup(router.take());
        QVERIFY(lookupDestination&&lookupDestination->hash==origin->hash);
        const auto resolved=cp::encodeHostReply({7,lookupDestination->request,0,
            originMode==1?*originIdentity->destination():*remote.destination()});
        QVERIFY(resolved&&router.send(*resolved));
        if (originMode==1) { responder=&*originIdentity;endpoint=&*origin; }
      }
      QTRY_VERIFY_WITH_TIMEOUT(router.next(),8000);
      const auto packet=cp::decodeDhtSendDatagram(router.take());
      QVERIFY(packet&&packet->recipient.wire==endpoint->destination->wire);
      const auto mux=cp::decodeDhtMuxDatagram(packet->payload);
      QVERIFY(mux&&mux->protocol==17);
      const auto signedQuery=i2p::decode1(mux->datagram);
      QVERIFY(signedQuery);
      auto query=d::decode(signedQuery->payload);
      QVERIFY(query&&query->azureus&&query->method==d::Method::AzureusData);
      const auto body=search::subscriptionSeedDdbResponse(*newerSeed);
      QVERIFY(body);
      auto* data=std::get_if<d::AzureusReadPacket>(&query->azureus->payload);
      QVERIFY(data&&std::equal(data->key.begin(),data->key.end(),newerSeed->lookupKey.begin(),[](auto a,char b) {
        return a==static_cast<unsigned char>(b);
      }));
      data->type=d::AzureusReadPacketType::Reply;
      data->length=data->total=static_cast<std::int32_t>(body->size());
      data->fragment.assign(body->begin(),body->end());
      const auto replyData=[&](const i2p::ClientIdentity& identity,const d::Contact& from,std::uint32_t id) {
        query->id=from.id;
        query->transaction={0xda,0xab,0x15};
        const auto plain=d::encode(*query);
        QVERIFY(plain);
        const auto signedData=i2p::encode1(identity.destination()->wire,identity.signingPrivateKey(),*plain);
        QVERIFY(signedData);
        const auto datagram=cp::encodeDhtMuxDatagram({17,from.port,10000,*signedData},cp::GzipMode::Deflate);
        QVERIFY(datagram);
        inbound(router,id,*datagram,true);
      };
      if (originMode==1) {
        replyData(remote,*contact,520);
        QTest::qWait(100);
        QVERIFY(!(*owner)->takeSubscriptionUpgrade());
      }
      replyData(*responder,*endpoint,514);
      const auto ready=[&]{if (!acquired) { acquired=(*owner)->takeSubscriptionUpgrade(); }return acquired.has_value();};
      QTRY_VERIFY_WITH_TIMEOUT(ready(),8000);
      QCOMPARE(acquired->previous,originalSeed->publicationHash);
      QCOMPARE(acquired->presence.publicationHash,newerSeed->publicationHash);
      QCOMPARE(acquired->metainfo,newerSeed->metainfo);
      if (transfer) {
        QVERIFY(!(*owner)->downloadSubscriptionUpgrade(acquired->previous,acquired->presence.version+1));
        const auto started=(*owner)->downloadSubscriptionUpgrade(acquired->previous,acquired->presence.version);
        QVERIFY2(started,started?"":started.error().message.c_str());
        QTRY_VERIFY_WITH_TIMEOUT(router.next(),8000);
        const auto get=signedSeedQuery(router.take(),*contact,false);
        QVERIFY(get&&!get->noSeed&&std::ranges::equal(get->target,newerSeed->publicationHash,
            [](auto x,char y){return x==static_cast<unsigned char>(y);}));
        response.transaction=get->transaction;
        replyToSeed(router,515,response);
        const auto local=router.registeredDestination;
        QVERIFY(local);
        SubscriptionLeecher seeder(router,remote,*local,*contact,
            {newerSeed->publicationHash.data(),newerSeed->publicationHash.size()},newPayload,false,true);
        QTRY_VERIFY_WITH_TIMEOUT(seeder.pump(),20000);
        QVERIFY2(seeder.failure.isEmpty(),qPrintable(seeder.failure));
        const auto complete=[&]{
          seeder.pump();
          if (!downloaded) { downloaded=(*owner)->takeSubscriptionUpgradePayload(); }
          return downloaded.has_value();
        };
        QTRY_VERIFY_WITH_TIMEOUT(complete(),10000);
        if (corrupted) {
          QVERIFY(!*downloaded);
          QCOMPARE(downloaded->error().code,biglybtc::storage::ErrorCode::InvalidFormat);
        } else {
          QVERIFY2(*downloaded,*downloaded?"":downloaded->error().message.c_str());
          QCOMPARE(**downloaded,newPayload);
        }
        QVERIFY(!(*owner)->takeSubscriptionUpgradePayload());
      }
    } else {
      QTest::qWait(500);
      QVERIFY(!router.next()&&!(*owner)->takeSubscriptionUpgrade());
    }
    QCOMPARE((*owner)->phase(),provider::OwnedProfile::Phase::Active);
    QCOMPARE(original::read(profile.path/"subscriptions.config"),saved);
    QCOMPARE(original::read(profile.path/"downloads.config"),std::string("d9:downloadslee"));
    if (transfer&&!corrupted) {
      QVERIFY(search::installI2pSubscriptionUpgrade(*profile.lease,*acquired,std::move(**downloaded)).has_value());
      const auto confirmed=search::readSavedSubscriptionSeed(*profile.lease,
          {newerSeed->publicationHash.data(),newerSeed->publicationHash.size()},search::SubscriptionSeedKey::Publication,
          search::SubscriptionSeedNetwork::I2P,1000);
      QVERIFY(confirmed&&*confirmed);
      QCOMPARE((**confirmed).payload,newPayload);
      QCOMPARE(original::read(profile.path/"downloads.config"),std::string("d9:downloadslee"));
    }
    (*owner)->close();
    (*owner)->join();
  }

  void actualSignedPrivateSeed_data() {
    QTest::addColumn<bool>("withdraw");
    QTest::addColumn<bool>("transfer");
    QTest::addColumn<bool>("idle");
    QTest::addColumn<int>("publication");
    QTest::newRow("announce-dormant-signed-version") << false << false << false << 0;
    QTest::newRow("withdraw-before-announce") << true << false << false << 0;
    QTest::newRow("stream-checked-signed-pieces") << false << true << false << 0;
    QTest::newRow("idle-reactivation") << false << true << true << 0;
    QTest::newRow("replicate-signed-version") << false << false << false << 1;
    QTest::newRow("withdraw-before-store") << false << false << false << 2;
    QTest::newRow("relay-signed-version") << false << false << false << 3;
    QTest::newRow("withdraw-before-relay") << false << false << false << 4;
    QTest::newRow("relay-subscription-association") << false << false << false << 5;
    QTest::newRow("suppress-repeated-association") << false << false << false << 6;
  }
  void actualSignedPrivateSeed() {
    QFETCH(bool,withdraw);
    QFETCH(bool,transfer);
    QFETCH(bool,idle);
    QFETCH(int,publication);
    if (idle && !qEnvironmentVariableIsSet("BIGLYBTC_I2P_IDLE_SOAK")) {
      QSKIP("The real-clock idle test needs BIGLYBTC_I2P_IDLE_SOAK=1 and QTEST_FUNCTION_TIMEOUT=480000");
    }
    namespace original=owned_i2p_test;
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
    auto registry=original::core::Document::parse(std::string(source->find("config")->bytes));
    QVERIFY(registry);
    if (publication>=5) {
      const auto* rows=registry->find("subs");
      QVERIFY(rows&&rows->children.size()==1);
      auto associated=original::core::Document::parse(original::core::encodeValue(rows->children.front()).value());
      QVERIFY(associated&&associated->replaceValue("is_public","i1e")&&associated->replaceValue("rand","i-27e"));
      const auto bytes=profile.hash.bytes();
      const std::string raw(reinterpret_cast<const char*>(bytes.data()),bytes.size());
      QVERIFY(associated->replaceValue("assoc","ld1:h20:"+raw+"1:wi1eee"));
      QVERIFY(registry->replaceValue("subs","l"+associated->serialize()+"e"));
    }
    original::write(profile.path/"subscriptions.config",registry->serialize());
    const std::string inventory="d9:downloadslee";
    original::write(profile.path/"downloads.config",inventory);
    auto owner=provider::OwnedProfile::prepareSubscription(*profile.lease,*hash,seedResolver(profile,publication!=0));
    QVERIFY2(owner,owner?"":owner.error().message.c_str());
    Router publisher,router;
    if (publication) { router.publication=&publisher; }
    auto remote=signer("dsa");
    const auto contact=seedContact(remote);
    QVERIFY(contact);
    const auto mixedRemote=i2p::generateClientIdentity();
    QVERIFY(mixedRemote);
    const auto mixedContact=seedContact(*mixedRemote);
    QVERIFY(mixedContact);
    registerSeedOwner(router,**owner,*contact,publication>=3?&*mixedContact:nullptr);
    QVERIFY(router.registeredDestination&&router.registeredDestination->hash!=mixedContact->hash);
    QTRY_VERIFY_WITH_TIMEOUT(router.next(),10000);
    const auto query=signedSeedQuery(router.take(),*contact);
    QVERIFY(query);
    QCOMPARE(std::string(reinterpret_cast<const char*>(query->target.data()),query->target.size()),key);
    QVERIFY(!(*owner)->subscriptionSeedLoaded(*hash));
    if (withdraw) { QVERIFY(original::fs::remove(profile.path/"subscriptions.config")); }
    d::Message response;
    response.kind=d::Kind::Reply;
    response.id=contact->id;
    response.transaction=query->transaction;
    response.token={0,0xff,0x51};
    response.hasNodes=withdraw;
    if (!withdraw) {
      response.hasPeers=true;
      response.peers={{remote.destination()->hash,!transfer}};
    }
    replyToSeed(router,21,response);
    if (idle) {
      QElapsedTimer clock;
      for (int index=0; index<2; ++index) {
        QTRY_VERIFY_WITH_TIMEOUT(router.next(),10000);
        const auto frame=router.take();
        if (frame.type==cp::HostLookupType) {
          QVERIFY(!clock.isValid());
          const auto request=cp::decodeHostLookup(frame);
          QVERIFY(request && request->hash==remote.destination()->hash);
          const auto unavailable=cp::encodeHostReply({7,request->request,1,std::nullopt});
          QVERIFY(unavailable && router.send(*unavailable));
          QVERIFY((*owner)->subscriptionSeedLoaded(*hash));
          clock.start();
        } else {
          const auto packet=cp::decodeDhtSendDatagram(frame);
          QVERIFY(packet && packet->recipient.wire==contact->destination->wire);
          const auto mux=cp::decodeDhtMuxDatagram(packet->payload);
          QVERIFY(mux && mux->protocol==18);
          const auto announced=d::decode(mux->datagram);
          QVERIFY(announced && announced->method==d::Method::AnnouncePeer && announced->seed && announced->target==query->target);
          d::Message ack;
          ack.kind=d::Kind::Reply;
          ack.id=contact->id;
          ack.transaction=announced->transaction;
          replyToSeed(router,22,ack);
        }
      }
      QVERIFY(clock.isValid());
      QTest::qWait(2000);
      QVERIFY((*owner)->subscriptionSeedLoaded(*hash));
      QTRY_VERIFY_WITH_TIMEOUT(!(*owner)->subscriptionSeedLoaded(*hash),320000);
      QVERIFY(clock.elapsed()>=295000);
      QCOMPARE((*owner)->phase(),provider::OwnedProfile::Phase::Active);
      QTRY_VERIFY_WITH_TIMEOUT(router.next(),20000);
      const auto again=signedSeedQuery(router.take(),*contact);
      QVERIFY(again && again->target==query->target && again->transaction!=query->transaction);
      response.transaction=again->transaction;
      replyToSeed(router,23,response);
    }
    if (withdraw) {
      QTest::qWait(1000);
      QVERIFY(!router.next());
      QVERIFY(!(*owner)->subscriptionSeedLoaded(*hash));
    } else if (transfer) {
      auto local=signer("ed25519");
      SubscriptionLeecher leecher(router,remote,*local.destination(),*contact,key,body);
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
      if (publication) {
        d::Message ack;
        ack.kind=d::Kind::Reply;
        ack.id=contact->id;
        ack.transaction=announced->transaction;
        replyToSeed(router,22,ack);
        QTRY_VERIFY_WITH_TIMEOUT(publisher.next(),5000);
        QCOMPARE(publisher.take().type,cp::GetDateType);
        const auto now=static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch());
        const auto date=cp::encodeMessage(cp::SetDate{now,std::string("2.13.0")});
        QVERIFY(date&&publisher.send(*date));
        QTRY_VERIFY(publisher.next());
        const auto creation=cp::decodeSessionMessage(publisher.take());
        QVERIFY(creation&&std::holds_alternative<cp::CreateSession>(*creation));
        const auto& ephemeral=std::get<cp::CreateSession>(*creation).config;
        QVERIFY(cp::verifySessionConfigSignature(ephemeral));
        QVERIFY(router.registeredDestination&&ephemeral.destination.hash!=router.registeredDestination->hash);
        const auto created=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});
        QVERIFY(created&&publisher.send(*created));
        QVERIFY(publisher.lease(now+600000));
        QTRY_VERIFY(publisher.next());
        QVERIFY(cp::decodeCreateLeaseSet2(publisher.take()));
        std::uint16_t publisherPort=0;
        const auto takeSigned=[&](const d::Contact& endpoint)->std::optional<d::Message> {
          const auto frame=cp::decodeDhtSendDatagram(publisher.take());
          if (!frame||frame->recipient.wire!=endpoint.destination->wire||frame->skipLeaseSet) { return {}; }
          const auto mux=cp::decodeDhtMuxDatagram(frame->payload);
          if (!mux||mux->protocol!=17||mux->toPort!=10002||(publisherPort&&mux->fromPort!=publisherPort)) { return {}; }
          const auto signedData=i2p::decode1(mux->datagram);
          if (!signedData||signedData->sender.wire!=ephemeral.destination.wire) { return {}; }
          publisherPort=mux->fromPort;
          const auto decoded=d::decode(signedData->payload);
          return decoded?std::optional(*decoded):std::nullopt;
        };
        if (publication>=5) {
          // Leave the primary session's association upgrade lookup pending
          // while the transient session publishes both of its own records.
          QTRY_VERIFY_WITH_TIMEOUT(router.next(),5000);
          const auto packet=cp::decodeDhtSendDatagram(router.take());
          QVERIFY(packet&&packet->recipient.wire==contact->destination->wire);
          const auto mux=cp::decodeDhtMuxDatagram(packet->payload);
          QVERIFY(mux&&mux->protocol==17&&mux->fromPort==10000);
          const auto signedQuery=i2p::decode1(mux->datagram);
          QVERIFY(signedQuery);
          const auto probe=d::decode(signedQuery->payload);
          QVERIFY(probe&&probe->method==d::Method::AzureusFindValue);
        }
        QTRY_VERIFY_WITH_TIMEOUT(publisher.next(),5000);
        const auto find=takeSigned(*contact);
        QVERIFY(find&&find->azureus&&find->method==d::Method::AzureusFindValue);
        const auto* requested=std::get_if<d::AzureusFindValue>(&find->azureus->payload);
        QVERIFY(requested&&requested->maximum==12&&requested->flags==0&&requested->key!=query->target);
        if (publication==2) { replaceSubscriptionRows(*profile.lease,"le"); }
        ack.transaction=find->transaction;
        ack.method=d::Method::AzureusFindValue;
        ack.token={1,9,0xff,2,5,7,8,3};
        ack.azureus=d::AzureusMessage{71,0,std::vector<d::AzureusContact>{}};
        replyToSeed(publisher,23,ack,publisherPort);
        if (publication==2) {
          QTest::qWait(1200);
          QVERIFY(!publisher.next());
        } else {
          QTRY_VERIFY_WITH_TIMEOUT(publisher.next(),5000);
          const auto store=takeSigned(*contact);
          QVERIFY(store&&store->method==d::Method::AzureusStore&&store->azureus&&store->token==ack.token);
          const auto* records=std::get_if<d::AzureusStore>(&store->azureus->payload);
          QVERIFY(records&&records->entries.size()==1);
          const auto& entry=records->entries.front();
          QVERIFY(entry.key==requested->key&&entry.values.size()==1);
          const auto& value=entry.values.front();
          QVERIFY(!value.origin&&value.flags==16);
          const auto saved=original::core::Document::parse(std::string(source->find("config")->bytes));
          QVERIFY(saved&&saved->find("subs")&&saved->find("subs")->children.size()==1);
          const auto* publicKey=saved->find("subs")->children.front().find("public_key");
          QVERIFY(publicKey);
          const auto signedVersion=biglybtc::search::verifySubscriptionPresence(
              {reinterpret_cast<const char*>(value.bytes.data()),value.bytes.size()},publicKey->bytes,4);
          QVERIFY(signedVersion&&std::string_view(signedVersion->publicationHash.data(),20)==key);
          QVERIFY(std::ranges::equal(entry.key,signedVersion->dhtKey,
              [](i2p::Byte left,char right){return left==static_cast<i2p::Byte>(right);}));
          ack.transaction=store->transaction;
          ack.method=d::Method::AzureusStore;
          ack.token.clear();
          ack.azureus=d::AzureusMessage{71,0,d::AzureusStoreAck{{1}}};
          replyToSeed(publisher,24,ack,publisherPort);
          const auto relayToMixed=[&](const d::Id& dhtKey,const i2p::Bytes& wire,
              std::string_view applicationKey,bool withdrawMapping,std::uint32_t replyId) {
            publisherPort=0;
            QTRY_VERIFY_WITH_TIMEOUT(publisher.next(),5000);
            const auto mixedFind=takeSigned(*mixedContact);
            QVERIFY(mixedFind&&mixedFind->method==d::Method::AzureusFindValue&&mixedFind->azureus);
            const auto* requestedMixed=std::get_if<d::AzureusFindValue>(&mixedFind->azureus->payload);
            QVERIFY(requestedMixed&&requestedMixed->key==dhtKey);
            ack.id=mixedContact->id;
            ack.method=d::Method::AzureusFindValue;
            ack.transaction=mixedFind->transaction;
            ack.token={3,8,7,5,2,0xff,9,1};
            ack.azureus=d::AzureusMessage{72,0,std::vector<d::AzureusContact>{}};
            replyToSeed(publisher,replyId,ack,publisherPort);
            QTRY_VERIFY_WITH_TIMEOUT(publisher.next(),5000);
            const auto mixedStore=takeSigned(*mixedContact);
            QVERIFY(mixedStore&&mixedStore->method==d::Method::AzureusStore&&mixedStore->azureus&&mixedStore->token==ack.token);
            const auto* mixedRecords=std::get_if<d::AzureusStore>(&mixedStore->azureus->payload);
            QVERIFY(mixedRecords&&mixedRecords->entries.size()==1);
            const auto& mixedEntry=mixedRecords->entries.front();
            QVERIFY(mixedEntry.key==dhtKey&&mixedEntry.values.size()==1);
            const auto& published=mixedEntry.values.front();
            QVERIFY(!published.origin&&published.flags==16&&published.bytes==wire);
            QTest::qWait(100);
            QVERIFY(!publisher.next());
            if (withdrawMapping) { replaceSubscriptionRows(*profile.lease,"le"); }
            ack.method=d::Method::AzureusStore;
            ack.transaction=mixedStore->transaction;
            ack.token.clear();
            ack.azureus=d::AzureusMessage{72,0,d::AzureusStoreAck{{1}}};
            replyToSeed(publisher,replyId+1,ack,publisherPort);
            if (withdrawMapping) {
              QTest::qWait(1200);
              QVERIFY(!publisher.next());
            } else {
              QTRY_VERIFY_WITH_TIMEOUT(publisher.next(),5000);
              auto request=takeSigned(*mixedContact);
              QVERIFY(request&&request->method==d::Method::AzureusData&&request->azureus);
              auto* data=std::get_if<d::AzureusReadPacket>(&request->azureus->payload);
              QVERIFY(data&&data->type==d::AzureusReadPacketType::Request&&data->handler==d::AzureusBridgeTransferHandler);
              const auto mapping=d::decodeAzureusBridgeRequest(data->bridgeRequest);
              QVERIFY(mapping&&mapping->value==wire);
              QCOMPARE(std::string(mapping->key.begin(),mapping->key.end()),std::string(applicationKey));
              data->type=d::AzureusReadPacketType::Reply;
              data->fragment={'d','1',':','a','i','1','e','e'};
              data->length=data->total=static_cast<std::int32_t>(data->fragment.size());
              request->id=mixedContact->id;
              request->transaction={0xda,0x23,0x77};
              const auto plain=d::encode(*request);
              QVERIFY(plain);
              const auto signedData=i2p::encode1(mixedRemote->destination()->wire,mixedRemote->signingPrivateKey(),*plain);
              QVERIFY(signedData);
              const auto datagram=cp::encodeDhtMuxDatagram({17,mixedContact->port,publisherPort,*signedData},cp::GzipMode::Deflate);
              QVERIFY(datagram);
              inbound(publisher,replyId+2,*datagram,true);
            }
          };
          if (publication>=3) {
            relayToMixed(entry.key,value.bytes,signedVersion->applicationKey,publication==4,25);
            if (QTest::currentTestFailed()) { return; }
          }
          if (publication>=5) {
            const auto expectedKey=biglybtc::search::subscriptionAssociationKey(profile.hash);
            QVERIFY(expectedKey);
            d::Id associationKey;
            std::ranges::copy(expectedKey->dht,associationKey.begin());
            const auto sid=QCryptographicHash::hash(
                QByteArrayView(publicKey->bytes.data(),static_cast<qsizetype>(publicKey->bytes.size())),
                QCryptographicHash::Sha1).first(10);
            biglybtc::search::SubscriptionAssociationHint hint{{},signedVersion->version,0xe5};
            std::ranges::copy(sid,hint.shortId.begin());
            const auto encoded=biglybtc::search::encodeSubscriptionAssociationHint(hint);
            QVERIFY(encoded);
            const i2p::Bytes expected(encoded->begin(),encoded->end());

            publisherPort=0;
            QTRY_VERIFY_WITH_TIMEOUT(publisher.next(),5000);
            const auto check=takeSigned(*contact);
            QVERIFY(check&&check->method==d::Method::AzureusFindValue&&check->azureus);
            const auto* request=std::get_if<d::AzureusFindValue>(&check->azureus->payload);
            QVERIFY(request&&request->key==associationKey&&request->maximum==30);
            d::AzureusValueHit existing;
            if (publication==6) {
              for (unsigned index=1;index<=10;++index) {
                auto earlier=expected;
                earlier[2]=static_cast<i2p::Byte>(index);
                existing.values.push_back({1,static_cast<std::int64_t>(now),std::move(earlier),{},16,0});
              }
            }
            ack.id=contact->id;
            ack.method=d::Method::AzureusFindValue;
            ack.transaction=check->transaction;
            ack.token={8,7,6,5,4,3,2,1};
            ack.azureus=d::AzureusMessage{71,0,std::move(existing)};
            replyToSeed(publisher,31,ack,publisherPort);
            if (publication==6) {
              QTest::qWait(1000);
              QVERIFY(!publisher.next());
            } else {
              QTRY_VERIFY_WITH_TIMEOUT(publisher.next(),5000);
              const auto writeLookup=takeSigned(*contact);
              QVERIFY(writeLookup&&writeLookup->method==d::Method::AzureusFindValue&&writeLookup->azureus);
              const auto* writeKey=std::get_if<d::AzureusFindValue>(&writeLookup->azureus->payload);
              QVERIFY(writeKey&&writeKey->key==associationKey&&writeKey->maximum==12);
              ack.transaction=writeLookup->transaction;
              ack.azureus=d::AzureusMessage{71,0,std::vector<d::AzureusContact>{}};
              replyToSeed(publisher,32,ack,publisherPort);
              QTRY_VERIFY_WITH_TIMEOUT(publisher.next(),5000);
              const auto write=takeSigned(*contact);
              QVERIFY(write&&write->method==d::Method::AzureusStore&&write->azureus&&write->token==ack.token);
              const auto* association=std::get_if<d::AzureusStore>(&write->azureus->payload);
              QVERIFY(association&&association->entries.size()==1);
              const auto& records=association->entries.front();
              QVERIFY(records.key==associationKey&&records.values.size()==1&&records.values.front().bytes==expected&&
                  !records.values.front().origin&&records.values.front().flags==16);
              ack.method=d::Method::AzureusStore;
              ack.transaction=write->transaction;
              ack.token.clear();
              ack.azureus=d::AzureusMessage{71,0,d::AzureusStoreAck{{1}}};
              replyToSeed(publisher,33,ack,publisherPort);
              relayToMixed(associationKey,expected,expectedKey->application,false,40);
              if (QTest::currentTestFailed()) { return; }
            }
          }
        }
        QVERIFY(!router.next());
      }
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
  void actualPrivateProfileTransferInBackup_data() {
    QTest::addColumn<bool>("receive");
    QTest::addColumn<bool>("pause");
    QTest::newRow("seed-counters")<<false<<false;
    QTest::newRow("live-received-pieces")<<true<<false;
    QTest::newRow("paused-received-pieces")<<true<<true;
  }
  void actualPrivateProfileTransferInBackup() {
    QFETCH(bool,receive);
    QFETCH(bool,pause);
    namespace source = owned_i2p_test;
    namespace storage = biglybtc::storage;
    if (!i2p::provider::privateOwnerRuntimePin()) { QSKIP("Approved private I2P SDK required"); }
    source::Fixture fixture;
    const auto file = source::fs::path(fixture.payloadDirectory.path().toStdString()) / "owned.bin";
    source::write(file,receive?std::string{}:fixture.payload);
    const auto state = source::dict({{"info", fixture.info}, {"attributes", source::dict({
        {"networks", "l3:I2Pe"}, {"peersources", "l3:DHTe"}, {"resumecomplete", receive?"i1e":"i2e"}, {"storetypes", "l1:Le"}
    })}, {"resume", source::dict({{"data", source::dict({
        {"resume data", source::b(std::string(1, receive?char(0):char(1)))}, {"valid", "i1e"}, {"blocks", "de"}
    })}})}});
    source::write(fixture.state(), state);
    source::write(fixture.path / "fixture.torrent", source::dict({{"info", fixture.info}}));
    const auto key = source::sha1(fixture.info);
    const auto saveDirectory = source::fs::path(fixture.payloadDirectory.path().toStdString());
    const auto originalBytes=receive?std::size_t{0}:fixture.payload.size();
    const auto saved = source::dict({{"downloads","l"+source::dict({
        {"torrent_hash", source::b(key)}, {"torrent", source::b((fixture.path/"fixture.torrent").string())},
        {"save_dir", source::b(saveDirectory.string())}, {"save_file", "9:owned.bin"}, {"state", "i0e"}, {"forceStart","i1e"},
        {"downloaded","i"+std::to_string(originalBytes)+"e"},{"uploaded","i17e"},
        {"completedbytes","i"+std::to_string(originalBytes)+"e"}
    })+"e"}});
    source::write(fixture.path / "downloads.config", saved);
    auto owner = i2p::provider::OwnedProfile::prepare(*fixture.lease, fixture.hash);
    QVERIFY2(owner,owner?"":owner.error().message.c_str());
    Router router;
    auto remote = signer("dsa");
    const auto contact = seedContact(remote);
    QVERIFY(contact);
    registerSeedOwner(router, **owner, *contact);
    QTRY_VERIFY_WITH_TIMEOUT(router.next(),12000);
    const auto query = signedSeedQuery(router.take(), *contact, false);
    QVERIFY(query);
    QCOMPARE(std::string(reinterpret_cast<const char*>(query->target.data()),query->target.size()),key);
    d::Message answer;
    answer.kind=d::Kind::Reply;
    answer.id=contact->id;
    answer.transaction=query->transaction;
    answer.token={0,0xff,0x51};
    answer.hasPeers=true;
    answer.peers={{remote.destination()->hash,receive}};
    replyToSeed(router,31,answer);
    QVERIFY(router.registeredDestination);
    SubscriptionLeecher leecher(router,remote,*router.registeredDestination,*contact,key,fixture.payload,false,receive);
    QTRY_VERIFY2_WITH_TIMEOUT(leecher.pump(),qPrintable(leecher.state()),45000);
    QVERIFY2(leecher.failure.isEmpty(),qPrintable(leecher.failure));
    std::optional<i2p::provider::OwnedProfile::Observation> sample;
    const auto findSample=[&] {
      const auto rows = (*owner)->observations();
      if (rows.size()!=1||!rows.front().backupBaseline||(receive&&!rows.front().torrent.seeding)) { return false; }
      sample=rows.front();
      return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(findSample(),5000);
    QCOMPARE(sample->torrent.phase,i2p::provider::OwnedProfile::Phase::Active);
    QCOMPARE(sample->backupBaseline->hash,std::optional(fixture.hash));
    QCOMPARE(sample->backupBaseline->uploaded,std::optional<std::int64_t>(17));
    const auto total=static_cast<std::int64_t>(fixture.payload.size());
    QCOMPARE(sample->cumulativeSent,std::optional<std::int64_t>(17+(receive?0:total)));
    if (receive) { QCOMPARE(sample->cumulativeReceived,std::optional(total)); }
    const auto beforeBackup=source::read(fixture.state());
    const auto target=source::fs::path(fixture.payloadDirectory.path().toStdString())/"copy";
    source::fs::create_directory(target);
    auto statistics=std::vector{storage::ProfileBackupTransferStatistics{*sample->backupBaseline,
        sample->cumulativeReceived,sample->cumulativeSent}};
    if (pause) {
      const auto paused=(*owner)->pauseForProfileBackup();
      QVERIFY(paused&&*paused);
    }
    auto capture=(*owner)->beginProfileBackupCapture();
    if (receive) {
      QVERIFY(capture);
      QTRY_VERIFY_WITH_TIMEOUT((*owner)->settleProfileBackupCapture(*capture,false),5000);
      auto progress=(*owner)->readProfileBackupCapture(*capture,{});
      QVERIFY(progress.size()==1&&progress.front().resume);
      QCOMPARE(progress.front().admitted.hash,std::optional(fixture.hash));
      QCOMPARE(progress.front().resume->completedBytes,total);
      QCOMPARE(progress.front().downloaded,std::optional(total));
      statistics.front()=std::move(progress.front());
    } else { QVERIFY(!capture); }
    const auto backed = storage::backupSavedProfile(*fixture.lease,target,"2026-11-27",{},statistics);
    QVERIFY2(backed,backed?"":backed.error().message.c_str());
    const auto inventory=source::core::Document::parse(source::read(backed->directory/"downloads.config"));
    QVERIFY(inventory);
    const auto result=source::core::readLibrary(*inventory);
    QVERIFY(result && result->downloads.size()==1);
    QCOMPARE(result->downloads.front().uploaded,sample->cumulativeSent);
    QCOMPARE(result->downloads.front().completedBytes,std::optional(total));
    if (receive) {
      const auto copied=backed->directory/source::fs::relative(fixture.state(),fixture.path);
      const auto savedResume=source::core::Document::parse(source::read(copied));
      QVERIFY(savedResume&&savedResume->find("resume"));
      const auto* resumeData=savedResume->find("resume")->find("data");
      QVERIFY(resumeData&&resumeData->find("resume data"));
      QCOMPARE(resumeData->find("resume data")->bytes,std::string_view("\x01",1));
      QCOMPARE(source::read(file),fixture.payload);
    }
    QCOMPARE(source::read(fixture.path/"downloads.config"),saved);
    QCOMPARE(source::read(fixture.state()),beforeBackup);
    if (pause) { (*owner)->resumeAfterProfileBackup(); }
    (*owner)->close();
    (*owner)->join();
  }
  void actualDormantSubscriptionCatalog_data() {
    QTest::addColumn<bool>("rotate");
    QTest::newRow("six-dormant") << false;
    QTest::newRow("next-window-after-discovery") << true;
  }
  void actualDormantSubscriptionCatalog() {
    QFETCH(bool,rotate);
    namespace original=owned_i2p_test;
    namespace provider=i2p::provider;
    if (!provider::privateOwnerRuntimePin()) { QSKIP("Approved private I2P SDK required"); }
    original::Fixture profile;
    const auto path=original::fs::path(BIGLYBTC_I2P_TEST_SUBSCRIPTION_FIXTURE).parent_path()/"subscription-i2p-catalog-reference.dat";
    const auto fixture=original::core::Document::parse(original::read(path));
    QVERIFY(fixture && fixture->find("rows") && fixture->find("config"));
    const auto& rows=fixture->find("rows")->children;
    constexpr auto capacity=biglybtc::engine::PrivateSubscriptionCandidateLimit;
    QCOMPARE(rows.size(),capacity+1);
    const auto desired=rotate ? rows.size() : std::size_t{6};
    const auto initial=std::min(desired,capacity);
    original::fs::create_directory(profile.path/"subs");
    original::write(profile.path/"subscriptions.config",fixture->find("config")->bytes);
    const std::string inventory="d9:downloadslee";
    original::write(profile.path/"downloads.config",inventory);
    std::vector<original::core::TorrentHash> hashes;
    std::set<std::string> pending;
    for (const auto& row : std::span(rows).first(desired)) {
      QVERIFY(row.find("file") && row.find("payload") && row.find("hash"));
      const auto file=row.find("file")->bytes;
      const auto key=row.find("hash")->bytes;
      const auto hash=original::core::TorrentHash::parse(key);
      QVERIFY(hash);
      if (hashes.size()<initial) { QVERIFY(pending.emplace(key).second); }
      original::write(profile.path/"subs"/file.substr(0,file.rfind('.')),row.find("payload")->bytes);
      hashes.push_back(*hash);
    }
    const auto resolve=seedResolver(profile);
    auto owner=provider::OwnedProfile::prepareSubscription(*profile.lease,hashes.front(),resolve);
    QVERIFY2(owner,owner?"":owner.error().message.c_str());
    for (const auto& hash : std::span(hashes).first(initial).subspan(1)) {
      auto prepared=(*owner)->prepareSubscriptionSeed(hash,resolve);
      QVERIFY2(prepared,prepared?"":prepared.error().message.c_str());
      const auto added=(*owner)->addSubscriptionSeed(std::move(*prepared));
      QVERIFY2(added,added?"":added.error().message.c_str());
    }
    std::unique_ptr<provider::OwnedProfile::PreparedSubscription> replacement;
    if (rotate) {
      // Prepare before running the Qt owner. The desktop prepares on its I/O worker.
      auto probe=(*owner)->prepareSubscriptionSeed(hashes.back(),resolve);
      QVERIFY2(probe,probe?"":probe.error().message.c_str());
      const auto limited=(*owner)->addSubscriptionSeed(std::move(*probe));
      QVERIFY(!limited && limited.error().code==biglybtc::storage::ErrorCode::Busy);
      auto prepared=(*owner)->prepareSubscriptionSeed(hashes.back(),resolve);
      QVERIFY2(prepared,prepared?"":prepared.error().message.c_str());
      replacement=std::move(*prepared);
    }
    auto remote=signer("dsa");
    const auto contact=seedContact(remote);
    QVERIFY(contact);
    Router router;
    registerSeedOwner(router,**owner,*contact);
    bool rotated=false;
    for (std::uint32_t message=21; !pending.empty(); message+=2) {
      QTRY_VERIFY_WITH_TIMEOUT(router.next(),8000);
      const auto sent=cp::decodeDhtSendDatagram(router.take());
      QVERIFY(sent && sent->recipient.wire==contact->destination->wire);
      const auto mux=cp::decodeDhtMuxDatagram(sent->payload);
      QVERIFY(mux && mux->protocol==17);
      const auto signedQuery=i2p::decode1(mux->datagram);
      QVERIFY(signedQuery);
      const auto query=d::decode(signedQuery->payload);
      QVERIFY(query && query->method==d::Method::GetPeers && query->noSeed);
      const std::string key(reinterpret_cast<const char*>(query->target.data()),query->target.size());
      QCOMPARE(pending.erase(key),std::size_t{1});
      d::Message response;
      response.kind=d::Kind::Reply;
      response.id=contact->id;
      response.transaction=query->transaction;
      response.token={0x45};
      response.hasPeers=true;
      response.peers={{remote.destination()->hash,true}};
      replyToSeed(router,message,response);
      QTRY_VERIFY(router.next());
      const auto announcedFrame=cp::decodeDhtSendDatagram(router.take());
      QVERIFY(announcedFrame);
      const auto announceMux=cp::decodeDhtMuxDatagram(announcedFrame->payload);
      QVERIFY(announceMux && announceMux->protocol==18);
      const auto announced=d::decode(announceMux->datagram);
      QVERIFY(announced && announced->method==d::Method::AnnouncePeer && announced->seed && announced->target==query->target);
      d::Message ack;
      ack.kind=d::Kind::Reply;
      ack.id=contact->id;
      ack.transaction=announced->transaction;
      replyToSeed(router,message+1,ack);
      if (rotate && pending.empty() && !rotated) {
        QTest::qWait(1000);
        const auto now=std::chrono::steady_clock::now();
        QCOMPARE((*owner)->retainSubscriptionSeeds(hashes,now).size(),capacity);
        QVERIFY(!(*owner)->subscriptionSeedSlotAvailable(now));
        QCOMPARE(biglybtc::engine::nextSubscriptionSeedCandidate(hashes,hashes[capacity-1]),capacity);
        QCOMPARE(biglybtc::engine::nextSubscriptionSeedCandidate(hashes,hashes.back()),std::size_t{0});
        const auto later=now+std::chrono::minutes(6);
        QVERIFY((*owner)->subscriptionSeedSlotAvailable(later));
        const auto window=(*owner)->retainSubscriptionSeeds(hashes,later);
        QCOMPARE(window.size(),capacity-1);
        QTRY_VERIFY_WITH_TIMEOUT((*owner)->subscriptionSeedSlotAvailable(now),5000);
        const auto added=(*owner)->addSubscriptionSeed(std::move(replacement));
        QVERIFY2(added,added?"":added.error().message.c_str());
        const auto data=hashes.back().bytes();
        QVERIFY(pending.emplace(reinterpret_cast<const char*>(data.data()),data.size()).second);
        rotated=true;
      }
    }
    QTest::qWait(1000);
    QCOMPARE(rotated,rotate);
    QCOMPARE((*owner)->retainSubscriptionSeeds(hashes).size(),initial);
    for (const auto& hash : hashes) { QVERIFY(!(*owner)->subscriptionSeedLoaded(hash)); }
    QCOMPARE((*owner)->phase(),provider::OwnedProfile::Phase::Active);
    (*owner)->close();
    (*owner)->join();
    QCOMPARE(original::read(profile.path/"downloads.config"),inventory);
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
  void actualActorLooksUpAzureusValues() {
    Router router;
    QVERIFY(router.listen());
    Channel channel;
    registered(router,channel,"ed25519",true);
    const auto remote=signer("dsa"),local=signer("ed25519");
    d::Contact seed;
    seed.hash=remote.destination()->hash;
    seed.port=10002;
    seed.destination=*remote.destination();
    const auto rid=d::makeNodeId(seed.hash,seed.port,i2p::Bytes(14,0x32));
    const auto lid=d::makeNodeId(local.destination()->hash,10000,i2p::Bytes(14,0x31));
    QVERIFY(rid&&lid);
    seed.id=*rid;
    d::Actor actor(channel);
    d::Actor::Options options{*lid,10000,{seed}};
    d::Id key{};
    key.fill(0x51);
    QVERIFY(actor.begin(options));
    QVERIFY(!actor.lookupAzureusValues(key,{4,16},3000));
    QVERIFY(!router.next());
    actor.close();
    options.azureusValues=true;
    QVERIFY(actor.begin(options));
    const auto operation=actor.lookupAzureusValues(key,{4,16},3000);
    QVERIFY(operation);
    QTRY_VERIFY(router.next());
    const auto sent=cp::decodeDhtSendDatagram(router.take());
    QVERIFY(sent&&sent->recipient.wire==seed.destination->wire&&!sent->skipLeaseSet);
    const auto mux=cp::decodeDhtMuxDatagram(sent->payload);
    QVERIFY(mux&&mux->protocol==17&&mux->fromPort==10000&&mux->toPort==10002);
    const auto signedPacket=i2p::decode1(mux->datagram);
    QVERIFY(signedPacket&&signedPacket->sender.wire==local.destination()->wire);
    const auto packet=d::decode(signedPacket->payload);
    QVERIFY(packet&&packet->method==d::Method::AzureusFindValue&&packet->azureus);
    const auto* query=std::get_if<d::AzureusFindValue>(&packet->azureus->payload);
    QVERIFY(query&&query->key==key&&query->maximum==4&&query->flags==16);

    const auto deliver=[&](d::Message response,std::uint16_t from,std::uint32_t messageId) {
      const auto encoded=d::encode(response);
      QVERIFY(encoded);
      const auto datagram=cp::encodeDhtMuxDatagram({18,from,10001,*encoded},cp::GzipMode::Deflate);
      QVERIFY(datagram);
      inbound(router,messageId,*datagram,true);
    };
    d::Message reply;
    reply.kind=d::Kind::Reply;
    reply.id=seed.id;
    reply.transaction=packet->transaction;
    reply.hasPeers=true;
    reply.peers={{seed.hash,true}};
    deliver(reply,10002,41);
    QTRY_COMPARE(actor.observation().rejected,std::uint64_t(1));
    QCOMPARE(actor.observation().phase,d::Phase::Running);

    reply.hasPeers=false;
    reply.peers.clear();
    reply.method=d::Method::AzureusFindValue;
    d::AzureusValue value;
    value.bytes={0,0xff,0x51};
    value.version=751;
    value.created=Now;
    value.flags=16;
    reply.azureus=d::AzureusMessage{702,0,d::AzureusValueHit{{value}}};
    deliver(reply,10004,42);
    QTRY_COMPARE(actor.observation().rejected,std::uint64_t(2));
    deliver(reply,10002,43);
    QTRY_COMPARE(actor.observation().phase,d::Phase::Complete);
    const auto result=actor.observation();
    QVERIFY(result.operation==*operation&&result.target==key&&result.peers.empty()&&result.announced==0);
    QCOMPARE(result.azureusValues.size(),std::size_t(1));
    QCOMPARE(result.azureusReporters.size(),std::size_t(1));
    const auto& reporter=result.azureusReporters.front();
    QVERIFY(reporter.contact.destination&&reporter.contact.destination->wire==seed.destination->wire);
    QCOMPARE(reporter.contact.port,seed.port);
    QCOMPARE(reporter.contact.id,seed.id);
    QCOMPARE(reporter.valueIndexes,(std::vector<std::size_t>{0}));
    QCOMPARE(result.azureusValues.front().bytes,value.bytes);
    QVERIFY(!result.azureusValues.front().origin);
    actor.close();
    QVERIFY(actor.observation().azureusValues.empty());
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
