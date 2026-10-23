#include "owned_dht_accounting.hpp"
#include "session_bridge.hpp"
#include "overall_statistics_service.hpp"
#include "persistent_session.hpp"
#include "minute_history_writer.hpp"
#include "owned_engine_fixture.hpp"
#include "biglybtc/saved_history_range.hpp"
#include "biglybtc/saved_statistics.hpp"

#include <QApplication>
#include <QDirIterator>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QThread>
#include <QUdpSocket>
#include <QtTest>
#include <atomic>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
using Field=core::SavedStatistic;
constexpr std::int64_t InitialSent=29,InitialReceived=23;
std::int64_t field(const core::OverallValues& values,Field key){return values[static_cast<std::size_t>(key)];}
QByteArray initialDocument(){
  libtorrent::entry root(libtorrent::entry::dictionary_type{});auto& all=root["all"];
  for(const auto key:core::SavedStatisticKeys){all[std::string(key)]=std::int64_t{0};}
  all["downloaded"]=999;all["uptime"]=40;all["dht_up"]=InitialSent;all["dht_down"]=InitialReceived;
  all["opaque.all"]=std::string("\0\xffkeep",6);root["opaque.root"]="retained";
  return QByteArray::fromStdString(test::encoded(root));
}
core::SavedStatistics saved(const QString& profile){
  const auto bytes=test::read(profile+QStringLiteral("/BiglyBT.statistics"));
  auto parsed=core::readSavedStatistics(std::string_view(bytes.constData(),static_cast<std::size_t>(bytes.size())));
  test::require(bool(parsed)&&parsed->hasAll,"Saved Overall map did not parse");return std::move(*parsed);
}
std::int64_t value(const core::SavedStatistics& stats,Field key){
  const auto& cell=stats.get(key);test::require(bool(cell.value)&&!cell.invalidType,"Missing saved field");return *cell.value;
}
bool opaque(const core::SavedStatistics& stats){
  const auto* all=stats.source->find("all");const auto* nested=all?all->find("opaque.all"):nullptr;
  const auto* root=stats.source->find("opaque.root");
  return nested&&nested->bytes==std::string("\0\xffkeep",6)&&root&&root->bytes=="retained";
}
bool successful(const app::OwnedAzureusService& actor,std::uint64_t id){
  const auto observed=actor.observation();return observed.lastResult&&observed.lastResult->request==id
      &&observed.lastResult->terminal&&!observed.lastResult->error&&!observed.lastResult->transportFailure;
}
auto actor(const char* address){
  app::AzureusServiceOptions options;options.enabled=true;options.servePings=true;
  options.bindAddress=QHostAddress(QString::fromLatin1(address));auto created=app::OwnedAzureusService::create(options);
  test::require(bool(created),"Cannot create owned loopback byte actor");return std::move(*created);
}
std::array<std::int64_t,2> bytes(const app::AzureusServiceObservation& observed){
  test::require(bool(observed.snapshot),"Missing actual actor snapshot");const auto& counts=observed.snapshot->transport().counters();
  test::require(bool(counts.sentPayloadBytes)&&bool(counts.receivedPayloadBytes),"Actual actor byte observation unknown");
  return {static_cast<std::int64_t>(*counts.sentPayloadBytes),static_cast<std::int64_t>(*counts.receivedPayloadBytes)};
}
ui::OverallPresentation overall(const QSignalSpy& spy){return spy.empty()?ui::OverallPresentation{}:qvariant_cast<ui::OverallPresentation>(spy.last().at(0));}
ui::NativeSessionPresentation native(const QSignalSpy& spy){return spy.empty()?ui::NativeSessionPresentation{}:qvariant_cast<ui::NativeSessionPresentation>(spy.last().at(0));}
ui::AzureusStatisticsPresentation azureus(const QSignalSpy& spy){return spy.empty()?ui::AzureusStatisticsPresentation{}:qvariant_cast<ui::AzureusStatisticsPresentation>(spy.last().at(0));}
struct Factory {
  std::atomic<app::OwnedAzureusService*> borrowed=nullptr;
  std::atomic<bool> held=false,worker=false;
  app::SessionBridge::AzureusServiceFactory function(){return [this](const storage::ProfileLease& lease){
    held=lease.valid();worker=QThread::currentThread()!=qApp->thread();auto owner=actor("127.0.0.1");borrowed=owner.get();return owner;
  };}
};
std::array<std::int64_t,2> recordedBytes(const QString& profile){
  std::array<std::int64_t,2> result{};std::size_t records=0;
  // Scan only this generated fixture's output. The production reader/writer
  // still uses exact dated dirfd paths; no ordinary user profile is inspected.
  QDirIterator files(profile+QStringLiteral("/stats"),QStringList{QStringLiteral("*.dat")},QDir::Files,QDirIterator::Subdirectories);
  while(files.hasNext()){
    const auto raw=test::read(files.next());
    auto parsed=core::readSavedHistoryRecords(std::string_view(raw.constData(),static_cast<std::size_t>(raw.size())));
    test::require(bool(parsed),"Actual appended minute records did not parse");
    for(const auto& record:*parsed){result[0]+=record.counters[4];result[1]+=record.counters[5];++records;}
  }
  test::require(records>0,"No observed numeric minute/End record was written");return result;
}
}

// Real loopback actors supply every positive DHT byte input. Explicit synthetic
// peer primitives in the partial-order method test only the reducer/IO contract;
// actual source-bound peer/Host completion is covered by the single/multi rows.
class OwnedDhtAccountingTests final:public QObject {
  Q_OBJECT
private slots:
  void initTestCase(){
    QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);
    auto owner=engine::PersistentSession::create({1,2,3});QVERIFY2(owner,"Source-bound owner required; no skip");
    QVERIFY((*owner)->close(std::chrono::steady_clock::now()+5s).sessionJoined);
  }
  void disabledAndConfiguredUnavailableHaveDifferentProviderCells(){
    app::OwnedDhtAccounting disabled(false),missing(true);
    QCOMPARE(disabled.overallInput().resolution,core::OverallDhtResolution::Empty);QVERIFY(disabled.overallInput().transports.empty());
    QCOMPARE(disabled.historyCounters(),(std::optional{std::array<std::int64_t,2>{0,0}}));
    QCOMPARE(missing.overallInput().resolution,core::OverallDhtResolution::Ready);QCOMPARE(missing.overallInput().transports.size(),std::size_t{1});
    QVERIFY(!missing.overallInput().transports[0]);QVERIFY(!missing.historyCounters());
    missing.observe({});QCOMPARE(missing.error(),app::OwnedDhtAccountingError::Missing);QVERIFY(!missing.historyCounters(true));
  }
  void actualActorIdentityRegressionGapAndJoinedFinalAdmission(){
    auto local=actor("127.0.0.1"),peer=actor("127.0.0.2");app::OwnedDhtAccounting projection(true);
    const auto first=local->observation();projection.observe(first);QCOMPARE(projection.historyCounters(),(std::optional{bytes(first)}));
    const auto allow=peer->allowPeer(local->endpoint());QVERIFY(allow);QTRY_VERIFY_WITH_TIMEOUT(successful(*peer,*allow),3000);
    const auto ping=local->ping(peer->endpoint());QVERIFY(ping);QTRY_VERIFY_WITH_TIMEOUT(successful(*local,*ping),3000);
    const auto second=local->observation();const auto pair=bytes(second);QVERIFY(pair[0]>0&&pair[1]>0);
    projection.observe(second);QCOMPARE(projection.historyCounters(),(std::optional{pair}));
    auto retained=second;retained.current=false;projection.observe(retained);QVERIFY(!projection.historyCounters());
    QVERIFY(!projection.overallInput().transports[0]);projection.observe(second);QCOMPARE(projection.historyCounters(),(std::optional{pair}));
    // Callback-metadata negatives use an actual immutable snapshot, not forged
    // counter cells. Merely claiming terminal/joined does not seal a Bound actor.
    retained.joined=true;retained.state=app::AzureusServiceState::Closed;projection.observe(retained,true);QVERIFY(!projection.historyCounters(true));
    projection.observe(second);QCOMPARE(projection.state(),app::OwnedDhtAccountingState::Current);
    app::OwnedDhtAccounting backwards(true);backwards.observe(second);backwards.observe(first);
    QCOMPARE(backwards.error(),app::OwnedDhtAccountingError::Regression);backwards.observe(second);QVERIFY(!backwards.historyCounters());
    app::OwnedDhtAccounting replaced(true);replaced.observe(second);replaced.observe(peer->observation());
    QCOMPARE(replaced.error(),app::OwnedDhtAccountingError::Generation);replaced.observe(second);QVERIFY(!replaced.historyCounters());
    QVERIFY(local->close());const auto ended=local->observation();QVERIFY(ended.joined);QVERIFY(!ended.current);
    QCOMPARE(ended.state,app::AzureusServiceState::Closed);QCOMPARE(ended.snapshot->transport().state(),app::azureus::TransportStatisticsState::Closed);
    projection.observe(ended);QVERIFY(!projection.historyCounters());projection.observe(ended,true);
    QCOMPARE(projection.state(),app::OwnedDhtAccountingState::Final);QVERIFY(!projection.historyCounters());
    QCOMPARE(projection.historyCounters(true),(std::optional{bytes(ended)}));QVERIFY(projection.overallInput(true).transports[0]);
    projection.observe(second);QCOMPARE(projection.historyCounters(true),(std::optional{bytes(ended)}));
  }
  void unknownActualReceivePreservesDhtPrefixAndClosesHistoryWithoutInventedEnd(){
    QTemporaryDir root(QStringLiteral("/tmp/biglybtc-dht-accounting-policy-XXXXXX"));QVERIFY(root.isValid());
    test::write(root.path()+QStringLiteral("/BiglyBT.statistics"),initialDocument());
    auto held=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(held);
    constexpr app::OverallOwnerIdentity id{1,7,1};constexpr std::int64_t start=1704153600000LL;
    auto service=app::OverallStatisticsService::open(*held,id,start,start);QVERIFY(service);
    auto local=actor("127.0.0.1"),peer=actor("127.0.0.2");app::OwnedDhtAccounting projection(true);projection.observe(local->observation());
    // Synthetic peer primitives isolate the source-defined partial update order.
    QVERIFY((*service)->capture({id,1,{11,13,17,19},app::OverallSampleQuality::Observed}));
    QVERIFY((*service)->tick(start+1000,projection.overallInput(),true));QVERIFY((*service)->flush());
    core::MinuteHistoryRecorder recorder;const core::MinuteHistoryIdentity minuteId{1,7,1};
    const core::MinuteHistoryInput initial{std::array<std::int64_t,4>{19,17,13,11},projection.historyCounters()};
    const core::HistoryCounters baseline{19,17,13,11,InitialSent,InitialReceived};
    QVERIFY(storage::writeMinuteHistory(*held,recorder,{core::MinuteHistoryEvent::Start,minuteId,start,initial,baseline}));
    const auto allow=peer->allowPeer(local->endpoint());QVERIFY(allow);QTRY_VERIFY_WITH_TIMEOUT(successful(*peer,*allow),3000);
    const auto ping=local->ping(peer->endpoint());QVERIFY(ping);QTRY_VERIFY_WITH_TIMEOUT(successful(*local,*ping),3000);
    projection.observe(local->observation());const auto pair=*projection.historyCounters();QVERIFY(pair[0]>0&&pair[1]>0);
    QVERIFY((*service)->capture({id,2,{111,113,117,119},app::OverallSampleQuality::Observed}));
    QVERIFY((*service)->tick(start+61000,projection.overallInput(),true));QVERIFY((*service)->flush());
    const core::MinuteHistoryInput next{std::array<std::int64_t,4>{119,117,113,111},projection.historyCounters()};
    QVERIFY(storage::writeMinuteHistory(*held,recorder,{core::MinuteHistoryEvent::Tick,minuteId,start+60000,next,{}}));
    QCOMPARE(recordedBytes(root.path()),pair);
    const auto minuteFile=root.path()+QStringLiteral("/stats/2024/01/02.dat");const auto previousBytes=test::read(minuteFile);
    // A real oversized dequeued/discarded datagram makes the cumulative RX axis
    // unknown. It is not a crafted valid stats object or a packet-to-byte guess.
    QUdpSocket foreign;foreign.setProxy(QNetworkProxy::NoProxy);QVERIFY(foreign.bind(QHostAddress(QStringLiteral("127.0.0.3")),quint16{0}));
    const auto endpoint=local->endpoint();const QByteArray datagram(1401,'x');
    QCOMPARE(foreign.writeDatagram(datagram,endpoint.address,endpoint.port),qint64{datagram.size()});
    QTRY_VERIFY_WITH_TIMEOUT(local->observation().snapshot&&!local->observation().snapshot->transport().counters().receivedPayloadBytes,3000);
    projection.observe(local->observation());QCOMPARE(projection.error(),app::OwnedDhtAccountingError::UnknownBytes);QVERIFY(!projection.historyCounters());
    QVERIFY((*service)->capture({id,3,{211,213,217,219},app::OverallSampleQuality::ClosedFinal}));
    const auto partial=(*service)->tick(start+62000,projection.overallInput(true),true);
    QVERIFY(!partial);QCOMPARE(partial.error(),app::OverallServiceError::Domain);QVERIFY(!(*service)->finalCountersApplied());
    QVERIFY((*service)->saveCurrent()); // Explicit-close policy, not retrying an unknown provider.
    const auto persisted=saved(root.path());QCOMPARE(value(persisted,Field::Downloaded),std::int64_t{999+211+213});
    QCOMPARE(value(persisted,Field::DhtUploaded),InitialSent+pair[0]);QCOMPARE(value(persisted,Field::DhtDownloaded),InitialReceived+pair[1]);QVERIFY(opaque(persisted));
    const core::MinuteHistoryInput unknown{std::array<std::int64_t,4>{219,217,213,211},projection.historyCounters(true)};
    storage::MinuteHistoryWriteReceipt receipt;
    QVERIFY(storage::writeMinuteHistory(*held,recorder,{core::MinuteHistoryEvent::End,minuteId,start+62000,unknown,{}},{},&receipt));
    QVERIFY(!recorder.state().active);QCOMPARE(receipt.appendedActions,std::size_t{0});QCOMPARE(test::read(minuteFile),previousBytes);
    QCOMPARE(recordedBytes(root.path()),pair);QVERIFY(local->close());projection.observe(local->observation(),true);QVERIFY(!projection.historyCounters(true));
  }
  void actualActorBytesReachMarksMinuteEndAndJoinedProfile_data(){
    QTest::addColumn<bool>("multifile");QTest::newRow("single")<<false;QTest::newRow("multifile")<<true;
  }
  void actualActorBytesReachMarksMinuteEndAndJoinedProfile(){
    QFETCH(bool,multifile);test::OwnedEngineFixture fixture(multifile);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    test::write(fixture.profile+QStringLiteral("/BiglyBT.statistics"),initialDocument());
    auto peer=actor("127.0.0.2");Factory factory;app::SessionBridge bridge(nullptr,{},factory.function());
    QSignalSpy owners(&bridge,&app::SessionBridge::nativeSessionReady),rows(&bridge,&app::SessionBridge::snapshotReady);
    QSignalSpy packets(&bridge,&app::SessionBridge::overallReady),dht(&bridge,&app::SessionBridge::azureusStatisticsReady);
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),history(&bridge,&app::SessionBridge::historyChanged),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!rows.empty()&&!sessions.empty()&&factory.borrowed.load()&&overall(packets)&&overall(packets)->available&&overall(packets)->writable&&!history.empty(),30000);
    QVERIFY(factory.held.load()&&factory.worker.load());QVERIFY(native(owners)&&native(owners)->sample);
    QCOMPARE(native(owners)->sample->counters,core::EngineCounters{}); // Actual idle source, not a GUI zero.
    const auto session=sessions.last().at(0).toULongLong();auto* local=factory.borrowed.load();QVERIFY(local);
    const auto origin=bytes(local->observation());QCOMPARE(origin,(std::array<std::int64_t,2>{0,0}));
    const auto allow=peer->allowPeer(local->endpoint());QVERIFY(allow);QTRY_VERIFY_WITH_TIMEOUT(successful(*peer,*allow),3000);
    const auto ping=local->ping(peer->endpoint());QVERIFY(ping);QTRY_VERIFY_WITH_TIMEOUT(successful(*local,*ping),3000);
    const auto first=bytes(local->observation());QVERIFY(first[0]>0&&first[1]>0);
    QVERIFY(bridge.markStatistics(session,false));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&overall(packets)&&field(overall(packets)->values,Field::MarkTime)>0,10000);
    const auto marked=saved(fixture.profile);QCOMPARE(value(marked,Field::DhtUploaded),InitialSent+first[0]);QCOMPARE(value(marked,Field::DhtDownloaded),InitialReceived+first[1]);QVERIFY(opaque(marked));
    ui::LibraryModel model;QVERIFY(model.apply(qvariant_cast<ui::Presentation>(rows.last().at(0))));const auto target=model.targetAt(0);QVERIFY(target);
    QVERIFY(bridge.continueDownload(*target));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&native(owners)&&native(owners)->sample&&native(owners)->sample->counters.payloadReceived>0,30000);
    const auto secondPing=local->ping(peer->endpoint());QVERIFY(secondPing);QTRY_VERIFY_WITH_TIMEOUT(successful(*local,*secondPing),3000);
    const auto second=bytes(local->observation());QVERIFY(second[0]>first[0]&&second[1]>first[1]);
    QVERIFY(bridge.markStatistics(session,true));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&overall(packets)&&field(overall(packets)->values,Field::MarkTime)==0,10000);
    const auto cleared=saved(fixture.profile);QCOMPARE(value(cleared,Field::DhtUploaded),InitialSent+second[0]);QCOMPARE(value(cleared,Field::DhtDownloaded),InitialReceived+second[1]);
    QVERIFY(bridge.closeSession());factory.borrowed=nullptr;QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);
    QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());QVERIFY(overall(packets)&&overall(packets)->closed&&overall(packets)->finalCounters);
    const auto final=azureus(dht);QVERIFY(final&&final->snapshot&&!final->current);
    QCOMPARE(final->snapshot->transport().state(),app::azureus::TransportStatisticsState::Closed);
    const auto& counters=final->snapshot->transport().counters();QVERIFY(counters.sentPayloadBytes&&counters.receivedPayloadBytes);
    const std::array<std::int64_t,2> finalBytes{static_cast<std::int64_t>(*counters.sentPayloadBytes),static_cast<std::int64_t>(*counters.receivedPayloadBytes)};
    const auto persisted=saved(fixture.profile);QCOMPARE(value(persisted,Field::DhtUploaded),InitialSent+finalBytes[0]);QCOMPARE(value(persisted,Field::DhtDownloaded),InitialReceived+finalBytes[1]);QVERIFY(opaque(persisted));
    QCOMPARE(recordedBytes(fixture.profile),finalBytes);QVERIFY(fixture.accountingIntact());
    auto released=storage::ProfileLease::acquire(fixture.native());QVERIFY(released);
    const auto reopened=storage::readOverallStatisticsView(*released);QVERIFY(reopened&&reopened->snapshot);
    QCOMPARE(field(reopened->snapshot->values,Field::DhtUploaded),InitialSent+finalBytes[0]);
    QCOMPARE(field(reopened->snapshot->values,Field::DhtDownloaded),InitialReceived+finalBytes[1]);
  }
  void configuredMissingActorCannotBecomeDisabledZeroOrCreateHistory(){
    test::OwnedEngineFixture fixture(false);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    test::write(fixture.profile+QStringLiteral("/BiglyBT.statistics"),initialDocument());
    std::atomic<bool> held=false;app::SessionBridge bridge(nullptr,{},[&](const storage::ProfileLease& lease){held=lease.valid();return std::unique_ptr<app::OwnedAzureusService>{};});
    QSignalSpy packets(&bridge,&app::SessionBridge::overallReady),owners(&bridge,&app::SessionBridge::nativeSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&overall(packets)&&overall(packets)->available&&native(owners)&&native(owners)->sample,30000);
    QVERIFY(held.load());QVERIFY(!overall(packets)->writable);QVERIFY(overall(packets)->failure.contains(QStringLiteral("DHT")));
    QCOMPARE(field(overall(packets)->values,Field::DhtUploaded),InitialSent);QCOMPARE(field(overall(packets)->values,Field::DhtDownloaded),InitialReceived);
    QVERIFY(!QFileInfo::exists(fixture.profile+QStringLiteral("/stats")));
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());
    const auto persisted=saved(fixture.profile);QCOMPARE(value(persisted,Field::DhtUploaded),InitialSent);QCOMPARE(value(persisted,Field::DhtDownloaded),InitialReceived);QVERIFY(opaque(persisted));
    QVERIFY(!overall(packets)->finalCounters);QVERIFY(!QFileInfo::exists(fixture.profile+QStringLiteral("/stats")));QVERIFY(fixture.accountingIntact(false));
  }
};
QTEST_MAIN(OwnedDhtAccountingTests)
#include "owned_dht_accounting_tests.moc"
