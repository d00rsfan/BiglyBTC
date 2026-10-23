#include "session_bridge.hpp"
#include "owned_azureus_service.hpp"
#include "live_speed_mapper_binding.hpp"
#include "owned_engine_fixture.hpp"
#include "profile.hpp"
#include <QApplication>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>
#include <atomic>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
std::int64_t nowMillis(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
ui::LiveSpeedManagerPresentation live(const QSignalSpy& spy){return spy.empty()?ui::LiveSpeedManagerPresentation{}:qvariant_cast<ui::LiveSpeedManagerPresentation>(spy.last().at(0));}
ui::NativeSessionPresentation native(const QSignalSpy& spy){return spy.empty()?ui::NativeSessionPresentation{}:qvariant_cast<ui::NativeSessionPresentation>(spy.last().at(0));}
bool successful(const app::OwnedAzureusService& owner,std::uint64_t request){
  const auto value=owner.observation();return value.lastResult&&value.lastResult->request==request&&value.lastResult->terminal&&!value.lastResult->error&&!value.lastResult->transportFailure;
}
auto actor(const char* address="127.0.0.2"){
  app::AzureusServiceOptions options;options.enabled=true;options.servePings=true;options.bindAddress=QHostAddress(QString::fromLatin1(address));
  auto result=app::OwnedAzureusService::create(options);test::require(bool(result),"Owned RTT actor failed");return std::move(*result);
}
struct Factory {
  std::atomic<app::OwnedAzureusService*> borrowed=nullptr;
  std::atomic<unsigned> calls=0;
  std::atomic<bool> held=false,worker=false;
  app::SessionBridge::AzureusServiceFactory function(){return [this](const storage::ProfileLease& lease){
    ++calls;held=lease.valid();worker=QThread::currentThread()!=qApp->thread();auto result=actor("127.0.0.1");borrowed=result.get();return result;
  };}
};
}
class LiveSpeedManagerBridgeTests final:public QObject {
  Q_OBJECT
private slots:
  void initTestCase(){
    QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);
    auto owner=engine::PersistentSession::create({1,2,3});QVERIFY2(owner,"Actual source-qualified owner is required; no skip");
    QVERIFY((*owner)->close(std::chrono::steady_clock::now()+5s).sessionJoined);
  }
  void actualCounterWindowsAndOwnedRttReachTheChannelWithoutOpeningStatistics_data(){
    QTest::addColumn<bool>("multifile");QTest::newRow("single")<<false;QTest::newRow("multi")<<true;
  }
  void actualCounterWindowsAndOwnedRttReachTheChannelWithoutOpeningStatistics(){
    QFETCH(bool,multifile);test::OwnedEngineFixture fixture(multifile);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    auto peer=actor();Factory factory;app::SessionBridge bridge(nullptr,{},factory.function());
    QSignalSpy maps(&bridge,&app::SessionBridge::liveSpeedManagerReady),owners(&bridge,&app::SessionBridge::nativeSessionReady);
    QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!rows.empty()&&!sessions.empty()&&factory.borrowed.load(),30000);
    QTRY_VERIFY_WITH_TIMEOUT(live(maps)&&live(maps)->snapshot&&live(maps)->snapshot->speedOnlyAccepted>0&&native(owners)&&native(owners)->sample,10000);
    const auto waiting=live(maps);QVERIFY(waiting->epoch);QVERIFY(waiting->snapshot->history.empty());
    QCOMPARE(waiting->snapshot->accepted,std::uint64_t{0});QCOMPARE(factory.calls.load(),1U);QVERIFY(factory.held.load()&&factory.worker.load());
    QCOMPARE(native(owners)->sample->counters,core::EngineCounters{}); // Actual idle session response.
    auto* local=factory.borrowed.load();QVERIFY(local);
    const auto allow=peer->allowPeer(local->endpoint());QVERIFY(allow);QTRY_VERIFY_WITH_TIMEOUT(successful(*peer,*allow),3000);
    // Wait for the real millisecond clock to leave any prior source event. No
    // +1 timestamp, fabricated zero sample, UI activation, sampler or probe is used.
    QTRY_VERIFY_WITH_TIMEOUT(!live(maps)->snapshot->lastAttempt||nowMillis()>live(maps)->snapshot->lastAttempt->observedMillis,3000);
    const auto ping=local->ping(peer->endpoint());QVERIFY(ping);QTRY_VERIFY_WITH_TIMEOUT(successful(*local,*ping),3000);
    const auto actual=local->observation();QVERIFY(actual.snapshot&&actual.snapshot->pingObservation());
    const auto receipt=*actual.snapshot->pingObservation();QVERIFY(receipt.rttNanoseconds());
    QTRY_VERIFY_WITH_TIMEOUT(live(maps)&&live(maps)->snapshot&&live(maps)->snapshot->accepted==1,5000);
    const auto first=live(maps);QVERIFY(first->current);QCOMPARE(first->session,sessions.last().at(0).toULongLong());
    const auto& initialPoint=first->snapshot->history.front();
    QCOMPARE(initialPoint.input.observedMillis,receipt.observedSteadyNanoseconds()/1000000);
    QCOMPARE(initialPoint.input.rttMillis,std::optional{*receipt.rttNanoseconds()/1000000});
    QCOMPARE(initialPoint.input.uploadBytesPerSecond,std::optional<std::int64_t>{0});
    QCOMPARE(initialPoint.input.downloadBytesPerSecond,std::optional<std::int64_t>{0});
    QVERIFY(!initialPoint.connectedToPrevious);
    ui::LibraryModel model;QVERIFY(model.apply(qvariant_cast<ui::Presentation>(rows.last().at(0))));
    const auto target=model.targetAt(0);QVERIFY(target);QVERIFY(bridge.continueDownload(*target));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&native(owners)&&native(owners)->sample&&native(owners)->sample->counters.payloadReceived>0,30000);
    // One real rate-only delivery after a completed bucket demonstrates that
    // the positive payload/protocol delta reached the three-second window.
    QTRY_VERIFY_WITH_TIMEOUT(live(maps)&&live(maps)->snapshot&&live(maps)->snapshot->lastAttempt
        &&live(maps)->snapshot->lastAttempt->downloadBytesPerSecond
        &&*live(maps)->snapshot->lastAttempt->downloadBytesPerSecond>0,10000);
    QTRY_VERIFY_WITH_TIMEOUT(nowMillis()>live(maps)->snapshot->lastAttempt->observedMillis,3000);
    const auto secondPing=local->ping(peer->endpoint());QVERIFY(secondPing);QTRY_VERIFY_WITH_TIMEOUT(successful(*local,*secondPing),3000);
    QTRY_VERIFY_WITH_TIMEOUT(live(maps)&&live(maps)->snapshot&&live(maps)->snapshot->accepted==2,5000);
    const auto second=live(maps);QVERIFY(second->current);QCOMPARE(second->epoch,first->epoch);
    QVERIFY(second->snapshot->history.back().input.downloadBytesPerSecond&&*second->snapshot->history.back().input.downloadBytesPerSecond>0);
    QVERIFY(*second->snapshot->history.back().input.downloadBytesPerSecond<=native(owners)->sample->counters.received);
    QCOMPARE(first->snapshot->accepted,std::uint64_t{1});QCOMPARE(first->snapshot->history.front().input.downloadBytesPerSecond,std::optional<std::int64_t>{0});
    const auto retainedSequence=native(owners)->sequence;
    QTRY_VERIFY_WITH_TIMEOUT(native(owners)&&native(owners)->sequence>retainedSequence+1,10000);
    QCOMPARE(live(maps)->snapshot->accepted,std::uint64_t{2}); // Never reconsume last RTT on idle polls.
    QVERIFY(bridge.closeSession());factory.borrowed=nullptr;QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);
    QTRY_VERIFY_WITH_TIMEOUT(live(maps)&&!live(maps)->current,3000);
    QCOMPARE(live(maps)->snapshot->accepted,std::uint64_t{2});QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());
    QVERIFY(fixture.accountingIntact());
    auto released=storage::ProfileLease::acquire(fixture.native());QVERIFY(released);
  }
  void offlineProfileHasNoFabricatedMapperAndNoServiceActivation(){
    test::OwnedEngineFixture fixture(false);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);app::SessionBridge bridge;
    QSignalSpy maps(&bridge,&app::SessionBridge::liveSpeedManagerReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&live(maps),10000);
    QCOMPARE(live(maps)->epoch,quint64{0});QVERIFY(!live(maps)->snapshot);QVERIFY(!live(maps)->current);QVERIFY(fixture.intact(false));
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);QVERIFY(fixture.intact(false));
  }
  void realLatestReceiptLossIsAGapAndCannotBeConsumedTwice(){
    // Synthetic engine primitives exercise only binding admission here; the
    // successful engine/sample end-to-end claim belongs to the preceding rows.
    app::LiveSpeedMapperBinding binding(7);const auto start=nowMillis()-4000;
    engine::ApplicationPeerObservation input{{1,7,1},1,engine::AccountingState::Idle,core::EngineStatistics{},0,{}};
    binding.observeEngine(input,start);input.sequence=2;input.observed->elapsedNanoseconds=1000000000;input.ownerElapsedNanoseconds=1000000000;
    binding.observeEngine(input,start+1000);
    auto client=actor("127.0.0.1"),peer=actor();const auto allow=peer->allowPeer(client->endpoint());QVERIFY(allow);
    QTRY_VERIFY_WITH_TIMEOUT(successful(*peer,*allow),3000);
    for(unsigned i=0;i<3;++i){const auto ping=client->ping(peer->endpoint());QVERIFY(ping);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*ping),3000);}
    const auto source=client->observation();QVERIFY(source.snapshot&&source.snapshot->pingObservation());
    QCOMPARE(source.snapshot->pingObservation()->sequence(),std::uint64_t{3});
    binding.observeAzureus(source,nowMillis());const auto before=binding.observation();QVERIFY(before.snapshot);
    QCOMPARE(before.failure,app::LiveSpeedFailure::ReceiptLoss);QVERIFY(before.snapshot->gaps>0);
    QCOMPARE(before.snapshot->accepted,std::uint64_t{0});QVERIFY(before.snapshot->history.empty());
    for(unsigned i=0;i<100;++i){binding.observeAzureus(source,nowMillis());}
    const auto after=binding.observation();QCOMPARE(after.revision,before.revision);QVERIFY(after.snapshot==before.snapshot);
  }
};
QTEST_MAIN(LiveSpeedManagerBridgeTests)
#include "live_speed_manager_bridge_tests.moc"
