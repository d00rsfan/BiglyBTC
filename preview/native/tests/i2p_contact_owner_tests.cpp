// SPDX-License-Identifier: GPL-2.0-or-later
#include "actor.hpp"
#include "contact_owner.hpp"
#include <future>
#include <condition_variable>
#include <functional>
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
// One test Host worker, matching production affinity. Qt keeps processing
// while this worker acquires the SAME source gate for filesystem operations.
class HostWorker final {
public:
  HostWorker():thread_([this]{for(;;){std::function<void()> task;{std::unique_lock lock(mutex_);changed_.wait(lock,[&]{return stopping_||bool(task_);});if(stopping_&&!task_){return;}task=std::move(task_);task_={};}task();}}){}
  ~HostWorker(){{std::lock_guard lock(mutex_);stopping_=true;}changed_.notify_all();thread_.join();}
  template<class F>auto submit(F&& f){using R=std::invoke_result_t<F>;auto task=std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));auto result=task->get_future();{std::lock_guard lock(mutex_);if(task_||stopping_){qFatal("Overlapping contact owner test operation");}task_=[task]{(*task)();};}changed_.notify_one();return result;}
private:
  std::mutex mutex_;std::condition_variable changed_;std::function<void()> task_;bool stopping_=false;std::thread thread_;
};
class ContactOwnerTests final:public QObject {
  Q_OBJECT
private slots:
  void actualModesPeriodicFinalForeignKeyAndCancel_data(){QTest::addColumn<bool>("mixed");QTest::addColumn<int>("mode");for(bool mixed:{false,true}){for(int mode=0;mode<6;++mode){QTest::newRow(qPrintable(QString("%1-%2").arg(mixed?"key0":"key1").arg(mode)))<<mixed<<mode;}}}
  void actualModesPeriodicFinalForeignKeyAndCancel(){
    QFETCH(bool,mixed);QFETCH(int,mode);owned_i2p_test::Fixture fixture;HostWorker worker;
    const auto index=mixed?0U:1U;const auto helper=fixture.key.parent_path();const auto key=mixed?helper/"dest_key.dat":fixture.key;const auto cache=helper/(mixed?"dhtdata":"dhtdata1");
    auto remote=signer("dsa");d::Contact imported;imported.hash=remote.destination()->hash;imported.destination=*remote.destination();imported.port=10004;imported.id=*d::makeNodeId(imported.hash,imported.port,i2p::Bytes(14,0x32));
    d::Contact seed=imported;seed.port=10002;seed.id=*d::makeNodeId(seed.hash,seed.port,i2p::Bytes(14,0x33));
    const std::vector<d::SavedContact> old{{imported,std::numeric_limits<std::int64_t>::max(),1,1}};auto encoded=d::encodeSavedContacts(old);QVERIFY(encoded);const std::string before(reinterpret_cast<const char*>(encoded->data()),encoded->size());
    std::unique_ptr<provider::ContactOwner> owner;std::shared_ptr<provider::ProfileSource> source;std::optional<d::Actor::Options> options{d::Actor::Options{}};options->inbound=true;options->routingMaintenance=true;
    auto prepared=worker.submit([&]()->std::expected<void,biglybtc::storage::Error>{if(mixed){owned_i2p_test::write(key,owned_i2p_test::read(fixture.key));if(::chmod(key.c_str(),0600)!=0){return std::unexpected(biglybtc::storage::Error{biglybtc::storage::ErrorCode::Io,"Generated key mode"});}}std::filesystem::create_directory(cache);owned_i2p_test::write(cache/"contacts.dat",before);auto cacheOwner=provider::ContactOwner::open(*fixture.lease,index,options);if(!cacheOwner){return std::unexpected(cacheOwner.error());}owner=std::move(*cacheOwner);auto held=mixed?provider::ProfileSource::createMixedSession(*fixture.lease):provider::ProfileSource::createSession(*fixture.lease);if(!held){return std::unexpected(held.error());}source=*held;return {};});
    QTRY_VERIFY(prepared.wait_for(std::chrono::seconds(0))==std::future_status::ready);auto preparedResult=prepared.get();QVERIFY2(preparedResult,preparedResult?"":preparedResult.error().message.c_str());QVERIFY(owner&&owner->exchange());QCOMPARE(options->savedContacts.size(),std::size_t{1});QCOMPARE(options->savedContacts.front().timeAlive,std::numeric_limits<std::int64_t>::max());
    Router router;QVERIFY(router.listen());Channel channel;QVERIFY(channel.bindEffectAuthority(source->effectAuthority()));SourceDriver driver(source,channel);d::Actor actor(channel);driver.actor=&actor;
    // Stop/release all actual source workers before the final cleanup can wait
    // for a filesystem task. Captured owner/source locals outlive this guard.
    const auto cleanup=qScopeGuard([&]{driver.actor=nullptr;actor.close();channel.close();driver.permit.stop();driver.permit.join();source->revoke();auto reset=worker.submit([&]{owner.reset();});reset.wait();});
    QVERIFY(channel.begin(router.options()));QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::GetDateType);auto date=cp::encodeMessage(cp::SetDate{Now,std::string("2.13.0")});QVERIFY(date&&router.send(*date));QTRY_VERIFY(channel.routerDate());auto identity=source->takeIdentity();QVERIFY(identity);
    QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;QVERIFY(driver.permit.request());QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
    {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});QVERIFY(channel.createLeaseSession(std::move(*identity),{{"i2cp.fastReceive","true"}},Now));}driver.release();QTRY_VERIFY(router.next());QCOMPARE(router.take().type,cp::CreateSessionType);auto created=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});QVERIFY(created&&router.send(*created));QTRY_VERIFY(channel.sessionId());QVERIFY(router.lease());QTRY_VERIFY(router.next());QVERIFY(cp::decodeCreateLeaseSet2(router.take()));QTRY_VERIFY(channel.leaseSetSubmission());
    options->queryPort=10000;options->localId=*d::makeNodeId(source->destination().hash,10000,i2p::Bytes(14,0x31));options->seeds={seed};
    QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;QVERIFY(actor.begin(*options));QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);auto exchange=owner->exchange();QVERIFY(!exchange->capture(actor,channel,source));QVERIFY(!actor.exportSavedContacts());
    {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});QVERIFY(channel.resumeAuthorizedEffects());QVERIFY(actor.resumeAuthorizedInbound());}
    const auto initialized=QDateTime::currentMSecsSinceEpoch();QTRY_VERIFY(QDateTime::currentMSecsSinceEpoch()>initialized);
    QTRY_VERIFY(driver.permit.phase()==provider::SourcePermit::Phase::Idle||driver.permit.phase()==provider::SourcePermit::Phase::Ready);if(driver.permit.phase()==provider::SourcePermit::Phase::Idle){QVERIFY(driver.permit.request());}QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
    {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});QVERIFY(actor.resumeAuthorizedInbound());Channel foreign;QVERIFY(!exchange->capture(actor,foreign,source));QVERIFY(exchange->capture(actor,channel,source));}QVERIFY(!exchange->pending());driver.release();
    auto firstPoll=worker.submit([&]{owner->poll();return owned_i2p_test::read(cache/"contacts.dat");});QTRY_VERIFY(firstPoll.wait_for(std::chrono::seconds(0))==std::future_status::ready);QCOMPARE(firstPoll.get(),before);
    if(mode==0||mode==3){
      // Deterministic test of the passive delivered-tick controller only. Real
      // SourcePermit authority and actual captured rows are never synthesized.
      for(unsigned n=0;n<7;++n){auto next=exchange->nextCheck();QVERIFY(next);exchange->tick(*next);QVERIFY(!exchange->pending());}exchange->tick(*exchange->nextCheck());QVERIFY(exchange->pending());
      QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Idle);driver.hold=true;QVERIFY(driver.permit.request());QTRY_COMPARE(driver.permit.phase(),provider::SourcePermit::Phase::Ready);
      {QVERIFY(driver.permit.take());const auto ack=qScopeGuard([&]{driver.permit.acknowledge();});QVERIFY(actor.resumeAuthorizedInbound());QVERIFY(exchange->capture(actor,channel,source));}driver.release();
      if(mode==3){exchange->stop();}
      auto poll=worker.submit([&]{owner->poll();return owned_i2p_test::read(cache/"contacts.dat");});QTRY_VERIFY(poll.wait_for(std::chrono::seconds(0))==std::future_status::ready);const auto actual=poll.get();
      if(mode==3){QCOMPARE(actual,before);}else{QVERIFY(actual!=before);auto rows=d::decodeSavedContacts(i2p::View(reinterpret_cast<const i2p::Byte*>(actual.data()),actual.size()));QVERIFY(rows);auto found=std::find_if(rows->begin(),rows->end(),[&](const auto& row){return row.contact.id==imported.id;});QVERIFY(found!=rows->end());QCOMPARE(found->timeAlive,std::int64_t{0});for(const auto& row:*rows){QCOMPARE(row.serializationVersion,std::uint8_t{1});}}
    }
    if(mode==4){
      // An actual accepted own publication replaces downloads.config and
      // advances the SAME cooperative epoch after the last cache capture.
      // No synthetic revision/ID setter or fresh cache grant is used.
      auto publication=worker.submit([&]()->std::expected<void,biglybtc::storage::Error>{
        using Error=biglybtc::storage::Error;using Code=biglybtc::storage::ErrorCode;
        const auto revision=fixture.lease->sourceRevision();const auto path=fixture.path/"downloads.config";struct stat beforeName{},afterName{};
        if(::stat(path.c_str(),&beforeName)!=0){return std::unexpected(Error{Code::Io,"Generated inventory before publication"});}
        auto changed=source->publishPositions([&]()->std::expected<void,Error>{auto file=biglybtc::storage::ConfigFile::load(*fixture.lease,"downloads.config");if(!file){return std::unexpected(file.error());}return file->save(*fixture.lease);});
        if(!changed){return changed;}
        if(fixture.lease->sourceRevision()<=revision||::stat(path.c_str(),&afterName)!=0||(beforeName.st_dev==afterName.st_dev&&beforeName.st_ino==afterName.st_ino)){return std::unexpected(Error{Code::Conflict,"No actual cooperative inventory replacement/epoch change"});}
        return source->validateTransportSnapshot();
      });QTRY_VERIFY(publication.wait_for(std::chrono::seconds(0))==std::future_status::ready);const auto published=publication.get();QVERIFY2(published,published?"":published.error().message.c_str());
    }
    // This is the existing withdrawal/join order, with no cache request/take.
    driver.actor=nullptr;exchange->stop();actor.close();channel.close();driver.permit.stop();driver.permit.join();source->revoke();
    auto final=worker.submit([&]{if(mode==2){auto bytes=owned_i2p_test::read(key);bytes.back()^=1;owned_i2p_test::write(key,bytes);}if(mode==5){auto bytes=owned_i2p_test::read(fixture.path/"biglybt.config");bytes.push_back('x');owned_i2p_test::write(fixture.path/"biglybt.config",bytes);}if(mode!=3){owner->finish();}return std::pair{owned_i2p_test::read(cache/"contacts.dat"),owner->diagnostic()};});QTRY_VERIFY(final.wait_for(std::chrono::seconds(0))==std::future_status::ready);const auto result=final.get();
    if(mode==2||mode==5){QCOMPARE(result.first,before);QVERIFY(result.second);}else if(mode==3){QCOMPARE(result.first,before);}else{QVERIFY(result.first!=before);QVERIFY(!result.second);}
    QVERIFY(!exchange->capture(actor,channel,source));QVERIFY(!exchange->nextCheck());
  }
  void explicitOptInMalformedAndAmbiguousStayReadOnly(){
    owned_i2p_test::Fixture fixture;HostWorker worker;std::unique_ptr<provider::ContactOwner> owner;
    auto result=worker.submit([&]{std::optional<d::Actor::Options> options;auto absent=provider::ContactOwner::open(*fixture.lease,1,options);if(!absent||*absent){return false;}options.emplace();options->inbound=true;auto legacy=provider::ContactOwner::open(*fixture.lease,1,options);if(!legacy||*legacy){return false;}const auto cache=fixture.key.parent_path()/"dhtdata1";if(std::filesystem::exists(cache)){return false;}options->routingMaintenance=true;std::filesystem::create_directory(cache);owned_i2p_test::write(cache/"contacts.dat","malformed");auto malformed=provider::ContactOwner::open(*fixture.lease,1,options);if(!malformed||!*malformed||(*malformed)->exchange()||!(*malformed)->diagnostic()||!options->savedContacts.empty()){return false;}(*malformed)->poll();(*malformed)->finish();if(owned_i2p_test::read(cache/"contacts.dat")!="malformed"){return false;}owned_i2p_test::write(cache/"contacts.dat",std::string(4,'\0'));owned_i2p_test::write(cache/"contacts.saving","uncertain");auto ambiguous=provider::ContactOwner::open(*fixture.lease,1,options);if(!ambiguous||!*ambiguous||(*ambiguous)->exchange()||!(*ambiguous)->diagnostic()){return false;}(*ambiguous)->poll();(*ambiguous)->finish();return owned_i2p_test::read(cache/"contacts.dat")==std::string(4,'\0')&&owned_i2p_test::read(cache/"contacts.saving")=="uncertain";});QTRY_VERIFY(result.wait_for(std::chrono::seconds(0))==std::future_status::ready);QVERIFY(result.get());
  }
};
QTEST_GUILESS_MAIN(ContactOwnerTests)
#include "i2p_contact_owner_tests.moc"
