// SPDX-License-Identifier: GPL-2.0-or-later
#include "actor.hpp"
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
class InboundActorTests final:public QObject {
  Q_OBJECT
private slots:
  void ownPendingPublicationKeepsLiveBridgeAndReplacementWatched_data(){
    QTest::addColumn<bool>("group");QTest::addColumn<int>("mutation");
    QTest::newRow("group-then-in-place")<<true<<0;
    QTest::newRow("group-then-atomic")<<true<<1;
    QTest::newRow("group-then-key")<<true<<2;
    QTest::newRow("group-then-lock")<<true<<3;
    QTest::newRow("strict-single-config")<<false<<0;
  }
  void ownPendingPublicationKeepsLiveBridgeAndReplacementWatched(){
#if !defined(BIGLYBTC_TEST_LIVE_I2P_BRIDGE)
    QSKIP("The optional private owner/real Bridge target is disabled; source-only tests remain active");
#else
    QFETCH(bool,group);QFETCH(int,mutation);namespace engine=biglybtc::engine;namespace storage=biglybtc::storage;
    owned_i2p_test::Fixture fixture;auto policy=engine::readNetworkPolicy(nullptr,{engine::NetworkMode::Normal});QVERIFY(policy);
    const auto pendingHash=biglybtc::core::TorrentHash::parse(std::string(20,'q'));QVERIFY(pendingHash);
    auto intent=engine::parseMagnetIntent("magnet:?xt=urn:btih:"+pendingHash->hex()+"&net=I2P",std::filesystem::path(fixture.payloadDirectory.path().toStdString())/"metadata-target",true,*policy,engine::MagnetNetwork::I2p);QVERIFY(intent);
    auto pending=engine::queuePendingMagnet(*fixture.lease,*intent,*policy,engine::MagnetNetwork::I2p);QVERIFY(pending);
    auto source=group?provider::ProfileSource::createSession(*fixture.lease):provider::ProfileSource::open(*fixture.lease,fixture.hash);QVERIFY(source);
    std::shared_ptr<provider::ProfileSource> sibling;std::shared_ptr<provider::PendingMagnetSource> pendingSource;
    std::optional<engine::detail::ManagedStorage> preparedStorage;
    if(group){auto accepted=(*source)->prepareChild(fixture.hash);QVERIFY(accepted);sibling=*accepted;auto storage=sibling->prepareStorage();QVERIFY(storage);preparedStorage.emplace(std::move(*storage));auto acceptedPending=(*source)->preparePendingMagnet(*pending);QVERIFY(acceptedPending);pendingSource=*acceptedPending;}
    else{auto storage=(*source)->prepareStorage();QVERIFY(storage);preparedStorage.emplace(std::move(*storage));}
    const auto state=owned_i2p_test::read(fixture.state()),inventory=owned_i2p_test::read(fixture.path/"downloads.config"),key=owned_i2p_test::read(fixture.key);
    Router router;QVERIFY(router.listen());Channel channel;QVERIFY(channel.bindEffectAuthority((*source)->effectAuthority()));
    {SourceDriver driver(*source,channel);QVERIFY(channel.begin(router.options()));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::GetDateType);
      auto date=cp::encodeMessage(cp::SetDate{Now,std::string("2.13.0")});QVERIFY(date&&router.send(*date));QTRY_VERIFY(channel.routerDate());auto identity=(*source)->takeIdentity();QVERIFY(identity);
      QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;QVERIFY(driver.permit.request());QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
      {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});auto created=channel.createLeaseSession(std::move(*identity),{{"i2cp.fastReceive","true"}},Now);QVERIFY2(created,created?"":created.error().message.c_str());}
      driver.release();QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateSessionType);auto ack=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});QVERIFY(ack&&router.send(*ack));QTRY_VERIFY(channel.sessionId());
      QVERIFY(router.lease());QTRY_VERIFY(router.next());QVERIFY(cp::decodeCreateLeaseSet2(router.take()));QTRY_VERIFY(channel.leaseSetSubmission());
    } // The setup worker is joined before Bridge installs its own real permit.
    provider::Bridge bridge(*source,channel);std::future<std::expected<void,storage::Error>> work;
    const auto configPath=fixture.path/"biglybt.config";
    std::mutex savedMutex;std::condition_variable savedChanged;std::atomic_bool saved=false;bool allowPostcheck=false;
    // On any assertion return, release the worker's test barrier and stop the
    // Bridge Ready permit BEFORE a launch::async future can block destruction.
    const auto stopIo=[&]{{std::lock_guard lock(savedMutex);allowPostcheck=true;}savedChanged.notify_all();bridge.close();bridge.joinSource();if(work.valid()){work.wait();}};
    const auto close=qScopeGuard(stopIo);
    QVERIFY(bridge.begin({6881,6881},91));QTRY_COMPARE(bridge.observation().phase,provider::Bridge::Phase::Ready);
    const auto generation=bridge.observation().generation;QVERIFY(generation!=0);
    if(group){
      struct stat before{},after{};QVERIFY(::stat(configPath.c_str(),&before)==0);
      const int witness=::inotify_init1(IN_CLOEXEC|IN_NONBLOCK);QVERIFY(witness>=0);const auto closeWitness=qScopeGuard([&]{::close(witness);});
      QVERIFY(::inotify_add_watch(witness,configPath.c_str(),IN_MOVE_SELF|IN_DELETE_SELF|IN_ATTRIB)>=0);
      bool realRename=false;auto inside=provider::Bridge::Phase::Idle;
      // Await any capturing worker BEFORE this block's marker/descriptor die.
      // A consumed future is invalid, so normal completion keeps Bridge live.
      const auto groupWork=qScopeGuard([&]{if(work.valid()){stopIo();}});
      work=std::async(std::launch::async,[&]()->std::expected<void,storage::Error>{return (*source)->publishPendingMagnet(pending->hash,[&]()->std::expected<void,storage::Error>{
        auto cancelled=engine::cancelPendingMagnet(*fixture.lease,*pending);if(!cancelled){return cancelled;}
        std::array<char,4096> bytes{};const auto n=::read(witness,bytes.data(),bytes.size());
        if(n<static_cast<ssize_t>(sizeof(inotify_event))){return std::unexpected(storage::Error{storage::ErrorCode::Io,"No actual config rename watch event"});}
        for(std::size_t at=0;at<static_cast<std::size_t>(n);){
          if(static_cast<std::size_t>(n)-at<sizeof(inotify_event)){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Incomplete fixture watch event"});}
          inotify_event event{};std::memcpy(&event,bytes.data()+at,sizeof(event));
          if(event.len>static_cast<std::size_t>(n)-at-sizeof(event)){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Unbounded fixture watch event"});}
          realRename=realRename||bool(event.mask&IN_MOVE_SELF);at+=sizeof(event)+event.len;
        }
        saved.store(true,std::memory_order_release);savedChanged.notify_all();
        // Test-only bounded handoff models concurrent Host I/O and Qt watcher;
        // no Qt/SDK callback is invoked by this source-gate-owning worker.
        std::unique_lock lock(savedMutex);if(!savedChanged.wait_for(lock,std::chrono::seconds(5),[&]{return allowPostcheck;})){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Fixture watch handoff expired"});}return {};
      });});
      QTRY_VERIFY(saved.load(std::memory_order_acquire)||work.wait_for(std::chrono::seconds(0))==std::future_status::ready);
      QCoreApplication::processEvents(QEventLoop::AllEvents);
      QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);
      QCoreApplication::processEvents(QEventLoop::AllEvents);inside=bridge.observation().phase;
      {std::lock_guard lock(savedMutex);allowPostcheck=true;}savedChanged.notify_all();
      QTRY_VERIFY(work.wait_for(std::chrono::seconds(0))==std::future_status::ready);const auto published=work.get();
      QVERIFY2(published,published?"":published.error().message.c_str());QVERIFY(saved.load(std::memory_order_acquire));QVERIFY(realRename);QCOMPARE(inside,provider::Bridge::Phase::Ready);
      QVERIFY(::stat(configPath.c_str(),&after)==0);QVERIFY(before.st_ino!=after.st_ino);QCOMPARE(before.st_dev,after.st_dev);
      QTRY_VERIFY((*source)->effectAuthority()->current());QCOMPARE(bridge.observation().phase,provider::Bridge::Phase::Ready);QCOMPARE(bridge.observation().generation,generation);
      work=std::async(std::launch::async,[&]()->std::expected<void,storage::Error>{auto current=(*source)->validate();if(!current){return current;}current=sibling->validate();if(!current){return current;}return pendingSource->release();});
      QTRY_VERIFY(work.wait_for(std::chrono::seconds(0))==std::future_status::ready);const auto validated=work.get();QVERIFY2(validated,validated?"":validated.error().message.c_str());QVERIFY(sibling->prepared());
      QCOMPARE(owned_i2p_test::read(fixture.state()),state);QCOMPARE(owned_i2p_test::read(fixture.path/"downloads.config"),inventory);QCOMPARE(owned_i2p_test::read(fixture.key),key);
      auto config=biglybtc::core::Document::parse(owned_i2p_test::read(configPath));QVERIFY(config);const auto* future=config->find("future");QVERIFY(future&&future->type==biglybtc::core::Node::Type::Bytes);QCOMPARE(std::string(future->bytes),std::string("x\0y",3));
    }
    work=std::async(std::launch::async,[&]()->std::expected<void,storage::Error>{
      if(mutation<=1){
        auto changed=storage::ConfigFile::load(*fixture.lease,"biglybt.config");if(!changed){return std::unexpected(changed.error());}if(!changed->document().replaceValue("future",owned_i2p_test::b("external-change"))){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Fixture external config edit"});}
        if(mutation==0){struct stat before{},after{};if(::stat(configPath.c_str(),&before)!=0){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Fixture config stat"});}owned_i2p_test::write(configPath,changed->document().serialize());if(::stat(configPath.c_str(),&after)!=0||before.st_ino!=after.st_ino){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Fixture inplace inode changed"});}}
        else{return changed->save(*fixture.lease);}
      }else if(mutation==2){auto changed=key;changed.back()^=1;owned_i2p_test::write(fixture.key,changed);}
      else{const auto lock=fixture.path/".azlock";owned_i2p_test::write(lock,owned_i2p_test::read(lock)+"changed");}return {};
    });
    QTRY_VERIFY(work.wait_for(std::chrono::seconds(0))==std::future_status::ready);const auto mutated=work.get();QVERIFY2(mutated,mutated?"":mutated.error().message.c_str());
    QTRY_COMPARE(bridge.observation().phase,provider::Bridge::Phase::Withdrawn);QVERIFY(!(*source)->prepared());QVERIFY(!(*source)->effectAuthority()->current());
    // Critical watch and fresh worker validation may race; both must revoke,
    // and the fixed public reason is observational rather than a timing proof.
    QVERIFY(bridge.failureReason()&&bridge.failureReason()[0]!='\0');
#endif
  }
  void actualSourcePermitPreservesQueuedQueryAcrossClientStartAndWithdraws_data(){
    QTest::addColumn<bool>("cancelQueued");QTest::addColumn<quint16>("announceSourcePort");QTest::addColumn<bool>("pressure");
    QTest::newRow("actual-reply")<<false<<quint16(10020)<<false;
    QTest::newRow("cancel-queued-server-reply")<<true<<quint16(10020)<<false;
    QTest::newRow("raw-zero-source-port")<<false<<quint16(0)<<false;
    QTest::newRow("raw-max-source-port")<<false<<quint16(65535)<<false;
    QTest::newRow("eight-replies-defer-client")<<false<<quint16(10020)<<true;
  }
  void actualSourcePermitPreservesQueuedQueryAcrossClientStartAndWithdraws(){
    QFETCH(bool,cancelQueued);QFETCH(quint16,announceSourcePort);QFETCH(bool,pressure);owned_i2p_test::Fixture fixture;auto source=provider::ProfileSource::open(*fixture.lease,fixture.hash);QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY(storage);
    const auto config=owned_i2p_test::read(fixture.path/"biglybt.config"),key=owned_i2p_test::read(fixture.key),state=owned_i2p_test::read(fixture.state());
    Router router;QVERIFY(router.listen());Channel channel;QVERIFY(channel.bindEffectAuthority((*source)->effectAuthority()));SourceDriver driver(*source,channel);
    QVERIFY(channel.begin(router.options()));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::GetDateType);
    auto date=cp::encodeMessage(cp::SetDate{Now,std::string("2.13.0")});QVERIFY(date&&router.send(*date));QTRY_VERIFY(channel.routerDate());
    auto identity=(*source)->takeIdentity();QVERIFY(identity);
    QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;QVERIFY(driver.permit.request());
    QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
    {QVERIFY(driver.permit.take());auto ackPermit=qScopeGuard([&]{driver.permit.acknowledge();});
      auto created=channel.createLeaseSession(std::move(*identity),{{"i2cp.fastReceive","true"}},Now);QVERIFY2(created,created?"":created.error().message.c_str());}
    driver.release();
    QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateSessionType);auto ack=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});QVERIFY(ack&&router.send(*ack));QTRY_VERIFY(channel.sessionId());
    QVERIFY(router.lease());QTRY_VERIFY(router.next());QVERIFY(cp::decodeCreateLeaseSet2(router.take()));QTRY_VERIFY(channel.leaseSetSubmission());
    auto remote=signer("dsa");d::Contact seed;seed.hash=remote.destination()->hash;seed.port=10002;seed.destination=*remote.destination();auto rid=d::makeNodeId(seed.hash,seed.port,i2p::Bytes(14,0x32));QVERIFY(rid);seed.id=*rid;
    auto lid=d::makeNodeId((*source)->destination().hash,10000,i2p::Bytes(14,0x31));QVERIFY(lid);d::Actor actor(channel);driver.actor=&actor;QVERIFY(actor.begin({*lid,10000,{seed},3,true}));QTRY_VERIFY(actor.inboundEnabled());const auto scope=channel.dhtScopeGeneration();
    if(pressure){
      QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;QVERIFY(driver.permit.request());
      QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);QVERIFY(driver.permit.take());QVERIFY(channel.dhtScopeObservation(scope).granted);
      d::Message response;response.kind=d::Kind::Reply;response.id=*lid;response.transaction={0x41};auto payload=d::encode(response);QVERIFY(payload);const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
      // One actual socket-enqueued frame keeps bytesToWrite nonzero within
      // this same thread turn. Consume only this test-owned frame's outcome.
      auto first=channel.submitDhtReply(scope,seed.destination->wire,*payload,10003,end);QVERIFY(first);auto written=channel.takeDhtDispatch();QVERIFY(written&&written->ticket==*first&&written->written);
      for(unsigned i=0;i<8;++i){auto reply=channel.submitDhtReply(scope,seed.destination->wire,*payload,10003,end);QVERIFY(reply);}
      auto full=channel.dhtScopeObservation(scope);QCOMPARE(full.writeSlots,std::size_t(0));QVERIFY(!full.lifetimeExhausted);
      d::Id target{};target.fill(0x51);auto operation=actor.lookupPeers(target,false,3000);QVERIFY(operation);QCOMPARE(actor.observation().phase,d::Phase::Running);QCOMPARE(channel.dhtScopeObservation(scope).writeSlots,std::size_t(0));
      driver.permit.acknowledge();driver.release();unsigned replies=0;std::optional<d::Message> query;
      for(unsigned i=0;i<10;++i){QTRY_VERIFY(router.next());auto sent=cp::decodeDhtSendDatagram(router.take());QVERIFY(sent);auto mux=cp::decodeDhtMuxDatagram(sent->payload);QVERIFY(mux);
        if(mux->protocol==18){++replies;}else{QVERIFY(mux->protocol==17&&!query);auto signedQuery=i2p::decode1(mux->datagram);QVERIFY(signedQuery);auto decoded=d::decode(signedQuery->payload);QVERIFY(decoded&&decoded->method==d::Method::GetPeers&&decoded->target==target);query=std::move(*decoded);}
      }
      QCOMPARE(replies,9U);QVERIFY(query);d::Message answer;answer.kind=d::Kind::Reply;answer.id=seed.id;answer.transaction=query->transaction;answer.hasNodes=true;payload=d::encode(answer);QVERIFY(payload);auto mux=cp::encodeDhtMuxDatagram({18,10002,10001,*payload},cp::GzipMode::Deflate);QVERIFY(mux);auto frame=cp::encodeMessagePayload({7,590,*mux});QVERIFY(frame&&router.send(*frame));QTRY_COMPARE(actor.observation().phase,d::Phase::Complete);QCOMPARE(actor.observation().replied,std::size_t(1));
      driver.actor=nullptr;actor.close();QCOMPARE(owned_i2p_test::read(fixture.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(fixture.key),key);QCOMPARE(owned_i2p_test::read(fixture.state()),state);return;
    }
    driver.hold=true;d::Message ping;ping.id=seed.id;ping.transaction={0x61,0x62};auto body=d::encode(ping);QVERIFY(body);auto signedBody=i2p::encode1(remote.destination()->wire,remote.signingPrivateKey(),*body);QVERIFY(signedBody);
    auto mux=cp::encodeDhtMuxDatagram({17,10002,10000,*signedBody},cp::GzipMode::Deflate);QVERIFY(mux);auto frame=cp::encodeMessagePayload({7,501,*mux});QVERIFY(frame&&router.send(*frame));
    QTRY_COMPARE(channel.dhtScopeObservation(scope).queued,std::size_t(1));QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);QVERIFY(!channel.dhtScopeObservation(scope).granted);QVERIFY(!router.next());
    d::Id target{};target.fill(0x51);auto operation=actor.lookupPeers(target,false,3000);QVERIFY(operation);QCOMPARE(channel.dhtScopeObservation(scope).queued,std::size_t(1));QVERIFY(!router.next());
    driver.release();if(cancelQueued){QCOMPARE(channel.dhtScopeObservation(scope).writeSlots,std::size_t(7));actor.stopInbound();}QTRY_VERIFY(router.next());auto client=cp::decodeDhtSendDatagram(router.take());QVERIFY(client);auto clientMux=cp::decodeDhtMuxDatagram(client->payload);QVERIFY(clientMux&&clientMux->protocol==17);auto clientSigned=i2p::decode1(clientMux->datagram);QVERIFY(clientSigned);auto query=d::decode(clientSigned->payload);QVERIFY(query&&query->kind==d::Kind::Query&&query->method==d::Method::GetPeers);
    if(!cancelQueued){
    QTRY_VERIFY(router.next());auto response=cp::decodeDhtSendDatagram(router.take());QVERIFY(response&&response->recipient.wire==seed.destination->wire&&response->skipLeaseSet);auto responseMux=cp::decodeDhtMuxDatagram(response->payload);QVERIFY(responseMux&&responseMux->protocol==18&&responseMux->fromPort==10000&&responseMux->toPort==10003);auto pong=d::decode(responseMux->datagram);QVERIFY(pong&&pong->kind==d::Kind::Reply&&pong->transaction==ping.transaction&&pong->id==*lid);
      d::Id serverKey{};serverKey.fill(0x37);d::Message get;get.method=d::Method::GetPeers;get.id=seed.id;get.target=serverKey;get.transaction={0x71};
      body=d::encode(get);QVERIFY(body);auto signedGet=i2p::encode1(remote.destination()->wire,remote.signingPrivateKey(),*body);QVERIFY(signedGet);mux=cp::encodeDhtMuxDatagram({17,10002,10000,*signedGet},cp::GzipMode::Deflate);QVERIFY(mux);frame=cp::encodeMessagePayload({7,504,*mux});QVERIFY(frame&&router.send(*frame));
      QTRY_VERIFY(router.next());response=cp::decodeDhtSendDatagram(router.take());QVERIFY(response);responseMux=cp::decodeDhtMuxDatagram(response->payload);QVERIFY(responseMux);auto tokenReply=d::decode(responseMux->datagram);QVERIFY(tokenReply&&tokenReply->transaction==get.transaction&&tokenReply->token.size()==8&&tokenReply->hasNodes&&tokenReply->nodes.size()<=10);
      d::Message announce;announce.method=d::Method::AnnouncePeer;announce.target=serverKey;announce.transaction={0x72};announce.token=tokenReply->token;announce.seed=true; // Raw id/fromPort are not authority.
      body=d::encode(announce);QVERIFY(body);mux=cp::encodeDhtMuxDatagram({18,announceSourcePort,10001,*body},cp::GzipMode::Deflate);QVERIFY(mux);frame=cp::encodeMessagePayload({7,505,*mux});QVERIFY(frame&&router.send(*frame));
      QTRY_VERIFY(router.next());response=cp::decodeDhtSendDatagram(router.take());QVERIFY(response&&response->recipient.wire==seed.destination->wire);responseMux=cp::decodeDhtMuxDatagram(response->payload);QVERIFY(responseMux&&responseMux->toPort==10003);auto stored=d::decode(responseMux->datagram);QVERIFY(stored&&stored->transaction==announce.transaction&&stored->kind==d::Kind::Reply);
      get.transaction={0x73};body=d::encode(get);QVERIFY(body);signedGet=i2p::encode1(remote.destination()->wire,remote.signingPrivateKey(),*body);QVERIFY(signedGet);mux=cp::encodeDhtMuxDatagram({17,10002,10000,*signedGet},cp::GzipMode::Deflate);QVERIFY(mux);frame=cp::encodeMessagePayload({7,506,*mux});QVERIFY(frame&&router.send(*frame));
      QTRY_VERIFY(router.next());response=cp::decodeDhtSendDatagram(router.take());QVERIFY(response);responseMux=cp::decodeDhtMuxDatagram(response->payload);QVERIFY(responseMux);auto readback=d::decode(responseMux->datagram);QVERIFY(readback&&readback->transaction==get.transaction&&readback->hasPeers&&readback->peers.size()==1&&readback->peers[0].hash==seed.hash&&readback->peers[0].seed);
    }
    actor.stopInbound();QVERIFY(!actor.inboundEnabled());QCOMPARE(actor.observation().phase,d::Phase::Running);QCOMPARE(channel.dhtScopeGeneration(),scope);
    d::Message answer;answer.kind=d::Kind::Reply;answer.id=seed.id;answer.transaction=query->transaction;answer.hasNodes=true;body=d::encode(answer);QVERIFY(body);mux=cp::encodeDhtMuxDatagram({18,10002,10001,*body},cp::GzipMode::Deflate);QVERIFY(mux);frame=cp::encodeMessagePayload({7,502,*mux});QVERIFY(frame&&router.send(*frame));QTRY_COMPARE(actor.observation().phase,d::Phase::Complete);
    // Actual following I2CP frame is a FIFO no-late-server-write barrier.
    QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;QVERIFY(driver.permit.request());
    QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
    {QVERIFY(driver.permit.take());auto ackPermit=qScopeGuard([&]{driver.permit.acknowledge();});
      auto changed=channel.reconfigureLeaseSession({{"i2cp.fastReceive","true"}},Now);QVERIFY2(changed,changed?"":changed.error().message.c_str());}
    driver.release();QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::ReconfigureSessionType);
    auto updated=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Updated});QVERIFY(updated&&router.send(*updated));QTRY_COMPARE(channel.state(),Channel::State::SessionCreated);
    QVERIFY(actor.enableInbound());QTRY_VERIFY(actor.inboundEnabled());driver.hold=true;mux=cp::encodeDhtMuxDatagram({17,10002,10000,*signedBody},cp::GzipMode::Deflate);QVERIFY(mux);frame=cp::encodeMessagePayload({7,503,*mux});QVERIFY(frame&&router.send(*frame));QTRY_COMPARE(channel.dhtScopeObservation(scope).queued,std::size_t(1));
    (*source)->revoke();driver.release();QTRY_VERIFY(!actor.inboundEnabled());QTRY_COMPARE(actor.observation().phase,d::Phase::Closed);QCOMPARE(channel.dhtScopeGeneration(),std::uint64_t(0));QVERIFY(!router.next());driver.actor=nullptr;
    QCOMPARE(owned_i2p_test::read(fixture.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(fixture.key),key);QCOMPARE(owned_i2p_test::read(fixture.state()),state);
  }
};
QTEST_GUILESS_MAIN(InboundActorTests)
#include "i2p_dht_inbound_actor_tests.moc"
