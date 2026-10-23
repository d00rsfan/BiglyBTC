#include "session_bridge.hpp"
#include "country_provider.hpp"
#include "country_statistics_view.hpp"
#include <QBuffer>
#include <QFileInfo>
#include <QPixmap>
#include <QTableView>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include "profile.hpp"
#include "owned_engine_fixture.hpp"
#include <QApplication>
#include <QPointer>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>
#include <algorithm>
#include <atomic>
#include <sys/stat.h>

namespace {
using namespace biglybtc;
ui::CountryStatisticsPresentation latest(const QSignalSpy& spy) {
  return spy.empty()?ui::CountryStatisticsPresentation{}:qvariant_cast<ui::CountryStatisticsPresentation>(spy.last().at(0));
}
ui::NativeSessionPresentation native(const QSignalSpy& spy) {
  return spy.empty()?ui::NativeSessionPresentation{}:qvariant_cast<ui::NativeSessionPresentation>(spy.last().at(0));
}
ui::Target target(const QSignalSpy& spy) {
  const auto data=qvariant_cast<ui::Presentation>(spy.last().at(0));test::require(data&&data->items.size()==1,"Expected generated target");
  return {.hash=data->items.front().hash,.generation=data->revision};
}
bool positiveLocal(const ui::CountryStatisticsPresentation& packet) {
  if(!packet||!packet->current||!packet->snapshot){return false;}
  const auto& data=*packet->snapshot;
  for(std::size_t i=0;i<data.rowCount;++i){const auto& row=data.rows[i];
    if(row.kind==core::CountryRowKind::Local&&row.observedDownloadRate&&*row.observedDownloadRate>0&&row.rateObservedPeers){return true;}}
  return false;
}
bool retiredPrefix(const ui::CountryStatisticsPresentation& packet) {
  if(!packet||packet->current||!packet->snapshot){return false;}
  for(std::size_t i=0;i<packet->snapshot->rowCount;++i){const auto& row=packet->snapshot->rows[i];
    if(row.kind==core::CountryRowKind::Local&&row.retiredPeers&&row.observedDownloadBytes&&*row.observedDownloadBytes>0){return true;}}
  return false;
}
bool rendered(const ui::CountryStatisticsView& view,const ui::CountryStatisticsPresentation& packet,bool retired) {
  if(view.presentation()!=packet||!view.isVisible()||view.isMinimized()||!packet||!packet->snapshot){return false;}
  const auto* table=view.findChild<QTableView*>(QStringLiteral("country-table"));if(!table||!table->model()){return false;}
  const auto column=retired?5:2;
  for(std::size_t i=0;i<packet->snapshot->rowCount;++i){const auto& row=packet->snapshot->rows[i];if(row.kind!=core::CountryRowKind::Local){continue;}
    const auto number=retired?row.observedDownloadBytes:row.observedDownloadRate;if(!number||*number<=0){continue;}
    for(int j=0;j<table->model()->rowCount();++j){if(table->model()->index(j,0).data().toString()==QStringLiteral("Local")
        &&table->model()->index(j,column).data(Qt::UserRole).isValid()
        &&table->model()->index(j,column).data(Qt::UserRole).toLongLong()==*number){return true;}}
  }return false;
}
bool sawPositive(const QSignalSpy& spy) {
  return std::any_of(spy.begin(),spy.end(),[](const auto& row){return positiveLocal(qvariant_cast<ui::CountryStatisticsPresentation>(row.at(0)));});
}
class CaptureDirectory final {
public:
  explicit CaptureDirectory(QString path) : path_(std::move(path)) {
    const QFileInfo info(path_);
    if (!info.isAbsolute() || !info.isDir() || info.isSymLink() || info.canonicalFilePath() != path_) {
      throw std::runtime_error("Country capture directory must be canonical and private");
    }
    const auto encoded = QFile::encodeName(path_);
    descriptor_ = ::open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (!valid()) {
      if (descriptor_ >= 0) { static_cast<void>(::close(descriptor_)); descriptor_ = -1; }
      throw std::runtime_error("Country capture directory must be owned0700");
    }
  }
  ~CaptureDirectory() { if (descriptor_ >= 0) { static_cast<void>(::close(descriptor_)); } }
  CaptureDirectory(const CaptureDirectory&) = delete;
  CaptureDirectory& operator=(const CaptureDirectory&) = delete;
  bool save(ui::CountryStatisticsView& view, const ui::CountryStatisticsPresentation& expected,
      const char* basename) const {
    if (view.presentation() != expected) { return false; }
    QByteArray bytes; QBuffer stream(&bytes);
    if (!stream.open(QIODevice::WriteOnly) || !view.grab().save(&stream, "PNG") || !valid()
        || view.presentation() != expected || !view.isVisible() || view.isMinimized()) { return false; }
    const int output = ::openat(descriptor_, basename, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (output < 0) { return false; }
    bool success = true; qsizetype offset = 0;
    while (offset < bytes.size()) {
      const auto written = ::write(output, bytes.constData() + offset, static_cast<std::size_t>(bytes.size() - offset));
      if (written < 0 && errno == EINTR) { continue; }
      if (written <= 0) { success = false; break; }
      offset += written;
    }
    if (success && ::fsync(output) != 0) { success = false; }
    if (::close(output) != 0) { success = false; }
    if (success && ::fsync(descriptor_) != 0) { success = false; }
    return success;
  }
private:
  bool valid() const {
    struct stat held{}, named{}; const auto encoded = QFile::encodeName(path_);
    return descriptor_ >= 0 && ::fstat(descriptor_, &held) == 0 && ::lstat(encoded.constData(), &named) == 0
        && S_ISDIR(held.st_mode) && S_ISDIR(named.st_mode) && held.st_uid == ::geteuid()
        && (held.st_mode & 0777) == 0700 && held.st_dev == named.st_dev && held.st_ino == named.st_ino;
  }
  QString path_;
  int descriptor_ = -1;
};
struct stat stamp(const QString& path){struct stat value{};test::require(::stat(path.toStdString().c_str(),&value)==0,"Cannot stamp owned settings");return value;}
bool sameStamp(const struct stat& before,const QString& path){const auto after=stamp(path);return before.st_dev==after.st_dev&&before.st_ino==after.st_ino
    &&before.st_mode==after.st_mode&&before.st_nlink==after.st_nlink&&before.st_size==after.st_size
    &&before.st_mtim.tv_sec==after.st_mtim.tv_sec&&before.st_mtim.tv_nsec==after.st_mtim.tv_nsec
    &&before.st_ctim.tv_sec==after.st_ctim.tv_sec&&before.st_ctim.tv_nsec==after.st_ctim.tv_nsec;}
}
class CountryStatisticsBridgeTests final:public QObject {
  Q_OBJECT
private slots:
  void offlineRequestIsLazyHasNoInventedOwnerAndFactoryIsWorkerAffine(){
    test::OwnedEngineFixture fixture(false);std::atomic<unsigned> calls=0;std::atomic<bool> held=false,worker=false;
    app::SessionBridge bridge(nullptr,[&](const storage::ProfileLease& lease)->std::unique_ptr<storage::CountryProvider>{
      ++calls;held=lease.valid();worker=QThread::currentThread()!=qApp->thread();return {};
    });
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),packets(&bridge,&app::SessionBridge::countryStatisticsReady);
    QSignalSpy owners(&bridge,&app::SessionBridge::nativeSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
    QCOMPARE(calls.load(),0U);QCOMPARE(packets.count(),0);QCOMPARE(owners.count(),0);
    const auto session=sessions.last().at(0).toULongLong();QVERIFY(!bridge.countryStatistics(0));QVERIFY(!bridge.countryStatistics(session+1));
    bool busyHeld=false,reentryRejected=false;
    connect(&bridge,&app::SessionBridge::countryStatisticsReady,&bridge,[&](auto){busyHeld=bridge.busy();reentryRejected=!bridge.countryStatistics(session);});
    QVERIFY(bridge.countryStatistics(session));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(packets),10000);
    QCOMPARE(calls.load(),1U);QVERIFY(held.load()&&worker.load());QVERIFY(busyHeld&&reentryRejected);
    QCOMPARE(latest(packets)->epoch,quint64{0});QVERIFY(!latest(packets)->snapshot);QVERIFY(!latest(packets)->current);
    QCOMPARE(owners.count(),0);const auto previous=latest(packets)->sequence;
    QVERIFY(bridge.countryStatistics(session));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(packets)->sequence>previous,10000);QCOMPARE(calls.load(),1U);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(!bridge.countryStatistics(session));
    QVERIFY(fixture.intact(false));
  }
  void actualSingleAndMultiUseLocalObservedPayloadRates_data(){
    QTest::addColumn<bool>("multifile");QTest::newRow("single")<<false;QTest::newRow("multi")<<true;
  }
  void actualSingleAndMultiUseLocalObservedPayloadRates(){
    QFETCH(bool,multifile);test::OwnedEngineFixture fixture(multifile);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    // Keep this tiny loopback transfer active across several real peer-rate and
    // one-second GUI publication intervals, rather than racing completion.
    const auto inventoryPath=fixture.profile+QStringLiteral("/downloads.config");
    auto inventory=test::read(inventoryPath);QCOMPARE(inventory.count("5:maxdli65536e"),1);
    inventory.replace("5:maxdli65536e","5:maxdli8192e");test::write(inventoryPath,inventory);
    const auto settings=fixture.profile+QStringLiteral("/biglybt.config");
    const auto raw=QByteArray("d6:opaque3:")+QByteArray("\0\xffQ",3)+QByteArray("e");test::write(settings,raw);const auto before=stamp(settings);
    std::atomic<unsigned> factoryCalls=0;app::SessionBridge::CountryProviderFactory factory;
    if(!multifile){factory=[&](const storage::ProfileLease&)->std::unique_ptr<storage::CountryProvider>{++factoryCalls;throw std::runtime_error("Owned optional provider fault");};}
    app::SessionBridge bridge(nullptr,std::move(factory));QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady);
    QSignalSpy owners(&bridge,&app::SessionBridge::nativeSessionReady),packets(&bridge,&app::SessionBridge::countryStatisticsReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),fixture.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!rows.empty()&&!sessions.empty(),10000);
    QTRY_VERIFY_WITH_TIMEOUT(native(owners)&&native(owners)->state==ui::NativeSessionState::Idle&&native(owners)->sample,10000);
    QCOMPARE(packets.count(),0);const auto session=sessions.last().at(0).toULongLong();
    const auto capturePath=qEnvironmentVariable("BIGLYBTC_COUNTRY_CAPTURE_DIRECTORY");
    std::unique_ptr<CaptureDirectory> captures;std::unique_ptr<ui::CountryStatisticsView> view;
    bool activeCaptured=false,retiredCaptured=false;QString captureFailure;
    if(!capturePath.isEmpty()){
      captures=std::make_unique<CaptureDirectory>(capturePath);view=std::make_unique<ui::CountryStatisticsView>();
      QVERIFY(view->beginSession(session));view->resize(1700,650);view->show();
      connect(&bridge,&app::SessionBridge::countryStatisticsReady,view.get(),&ui::CountryStatisticsView::setPresentation);
      connect(&bridge,&app::SessionBridge::countryStatisticsReady,view.get(),[&](auto packet){
        if(!captureFailure.isEmpty()){return;}const bool retired=retiredPrefix(packet);
        if((retired&&retiredCaptured)||(!retired&&activeCaptured)||(!retired&&!positiveLocal(packet))||!rendered(*view,packet,retired)){return;}
        const char* name=retired?(multifile?"actual-country-retired-multi.png":"actual-country-retired-single.png")
            :(multifile?"actual-country-active-multi.png":"actual-country-active-single.png");
        const bool saved=captures->save(*view,packet,name);if(!saved){captureFailure=QStringLiteral("Cannot capture actual Country frame");}
        else if(retired){retiredCaptured=true;}else{activeCaptured=true;}
      });
    }
    QVERIFY(bridge.countryStatistics(session));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(packets)&&latest(packets)->snapshot,10000);
    const auto idle=latest(packets);QVERIFY(idle->current);QVERIFY(!idle->snapshot->providerAvailable);QCOMPARE(idle->snapshot->rowCount,std::size_t{0});
    QVERIFY(bridge.continueDownload(target(rows)));QTRY_VERIFY_WITH_TIMEOUT(sawPositive(packets),30000);
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&rows.count()>=2,30000);
    quint64 previous=0;bool checked=false;
    for(const auto& row:packets){const auto packet=qvariant_cast<ui::CountryStatisticsPresentation>(row.at(0));
      QCOMPARE(packet->session,session);QVERIFY(packet->sequence>previous);previous=packet->sequence;
      if(!packet->snapshot){continue;}QVERIFY(core::validCountryStatistics(*packet->snapshot));QVERIFY(!packet->snapshot->providerAvailable);
      QCOMPARE(packet->snapshot->identity.profileSession,static_cast<std::uint64_t>(session));QCOMPARE(packet->snapshot->identity.engineEpoch,static_cast<std::uint64_t>(packet->epoch));
      for(std::size_t i=0;i<packet->snapshot->rowCount;++i){const auto& group=packet->snapshot->rows[i];
        QVERIFY(group.kind==core::CountryRowKind::Local||group.kind==core::CountryRowKind::Unavailable);}
      checked|=positiveLocal(packet);
    }
    QVERIFY(checked);QCOMPARE(factoryCalls.load(),multifile?0U:1U);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);
    QVERIFY(closed.first().at(1).toBool()&&closed.first().at(2).toBool());QTRY_VERIFY(latest(packets)&&!latest(packets)->current);
    QVERIFY(latest(packets)->snapshot);QVERIFY(latest(packets)->snapshot->tailUncertain);QVERIFY(!bridge.countryStatistics(session));
    bool retainedObservedBytes=false;
    for(std::size_t i=0;i<latest(packets)->snapshot->rowCount;++i){const auto& group=latest(packets)->snapshot->rows[i];
      if(group.kind==core::CountryRowKind::Local&&group.retiredPeers&&group.observedDownloadBytes&&*group.observedDownloadBytes>0){
        retainedObservedBytes=true;QCOMPARE(group.retiredPeers,group.recordedPeers);QVERIFY(!group.observedDownloadRate);QVERIFY(!group.observedUploadRate);
      }
    }
    QVERIFY(retainedObservedBytes); // Actual retired connection prefixes, not rebuilt expressions.
    if(captures){QVERIFY2(captureFailure.isEmpty(),qPrintable(captureFailure));QVERIFY(activeCaptured);QVERIFY(retiredCaptured);}
    QCOMPARE(test::read(settings),raw);QVERIFY(sameStamp(before,settings));QVERIFY(fixture.accountingIntact());QVERIFY(idle->current);
  }
  void cancelKeepsActualPrefixAndDoesNotInventFinalCountryTotals(){
    test::OwnedEngineFixture fixture(false,true);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady);
    QSignalSpy packets(&bridge,&app::SessionBridge::countryStatisticsReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),fixture.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!rows.empty()&&!sessions.empty(),10000);
    const auto session=sessions.last().at(0).toULongLong();QVERIFY(bridge.countryStatistics(session));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(packets),10000);
    bool cancelled=false;connect(&bridge,&app::SessionBridge::countryStatisticsReady,&bridge,[&](auto packet){if(!cancelled&&bridge.busy()&&positiveLocal(packet)){cancelled=true;bridge.cancel();}});
    QVERIFY(bridge.continueDownload(target(rows)));QTRY_VERIFY_WITH_TIMEOUT(cancelled&&!bridge.busy(),30000);
    QVERIFY(fixture.accountingIntact(false));QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);
    QVERIFY(closed.first().at(1).toBool()&&closed.first().at(2).toBool());QTRY_VERIFY(latest(packets)&&!latest(packets)->current);
    QVERIFY(latest(packets)->snapshot&&latest(packets)->snapshot->tailUncertain);
  }
  void reopenRequiresNewActivationAndRejectsOldProfile(){
    test::OwnedEngineFixture first(false),second(false);QTRY_VERIFY_WITH_TIMEOUT(second.ready(),30000);
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QSignalSpy packets(&bridge,&app::SessionBridge::countryStatisticsReady),owners(&bridge,&app::SessionBridge::nativeSessionReady);
    QVERIFY(bridge.open(first.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==1,10000);
    const auto old=sessions.last().at(0).toULongLong();QVERIFY(bridge.countryStatistics(old));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(packets),10000);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);const auto count=packets.count();
    QVERIFY(bridge.open(second.native(),second.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==2,10000);
    const auto next=sessions.last().at(0).toULongLong();QVERIFY(next>old);
    QTRY_VERIFY_WITH_TIMEOUT(native(owners)&&native(owners)->session==next&&native(owners)->state==ui::NativeSessionState::Idle&&native(owners)->sample,10000);
    QCOMPARE(packets.count(),count);QVERIFY(!bridge.countryStatistics(old));QVERIFY(bridge.countryStatistics(next));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(packets)->session==next&&latest(packets)->snapshot,10000);
    QCOMPARE(latest(packets)->snapshot->identity.profileSession,static_cast<std::uint64_t>(next));
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),2,15000);
  }
  void directCountryReceiverMayDeleteBridgeAndReleaseLease(){
    test::OwnedEngineFixture fixture(false);QObject cleanup;auto* bridge=new app::SessionBridge(&cleanup);QPointer<app::SessionBridge> alive(bridge);
    QSignalSpy sessions(bridge,&app::SessionBridge::columnLayoutSessionReady);
    QVERIFY(bridge->open(fixture.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge->busy()&&!sessions.empty(),10000);
    const auto session=sessions.first().at(0).toULongLong();int trailingIdle=0;
    connect(bridge,&app::SessionBridge::busyChanged,&cleanup,[&](bool busy){if(!busy){++trailingIdle;}});
    connect(bridge,&app::SessionBridge::countryStatisticsReady,&cleanup,[&](auto packet){QCOMPARE(packet->session,session);QVERIFY(bridge->busy());delete bridge;});
    QVERIFY(bridge->countryStatistics(session));QTRY_VERIFY(alive.isNull());QCOMPARE(trailingIdle,0);
    auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
  }
};
QTEST_MAIN(CountryStatisticsBridgeTests)
#include "country_statistics_bridge_tests.moc"
