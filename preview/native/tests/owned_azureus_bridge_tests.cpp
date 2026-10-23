#include "session_bridge.hpp"
#include "owned_azureus_service.hpp"
#include "statistics_page.hpp"
#include "profile.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <sys/stat.h>

namespace {
using namespace biglybtc;
namespace udp=services::azureus_udp;
using namespace std::chrono_literals;
void require(bool result,const char* message){if(!result){throw std::runtime_error(message);}}
void write(const QString& path,const QByteArray& value){QFile file(path);require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Generated profile write failed");require(file.write(value)==value.size()&&file.flush(),"Generated profile write incomplete");file.close();require(QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::WriteOwner),"Generated file mode failed");}
QByteArray read(const QString& path){QFile file(path);require(file.open(QIODevice::ReadOnly),"Generated profile read failed");return file.readAll();}
struct Stamp {
  struct stat value{};
  explicit Stamp(const QString& path){require(::stat(QFile::encodeName(path).constData(),&value)==0,"Generated fixture stat failed");}
  bool matches(const QString& path)const{struct stat now{};return ::stat(QFile::encodeName(path).constData(),&now)==0&&value.st_dev==now.st_dev&&value.st_ino==now.st_ino&&value.st_mode==now.st_mode&&value.st_size==now.st_size&&value.st_nlink==now.st_nlink&&value.st_mtim.tv_sec==now.st_mtim.tv_sec&&value.st_mtim.tv_nsec==now.st_mtim.tv_nsec&&value.st_ctim.tv_sec==now.st_ctim.tv_sec&&value.st_ctim.tv_nsec==now.st_ctim.tv_nsec;}
};
struct Profile {
  QTemporaryDir directory{QStringLiteral("/tmp/biglybtc-azureus-bridge-XXXXXX")};
  QString path;
  const QByteArray inventory="d9:downloadslee",saved="d3:alld10:downloadedi99e8:uploadedi44e6:uptimei5eee";
  explicit Profile(bool valid=true){require(directory.isValid(),"Generated root failed");path=directory.path()+QStringLiteral("/profile");require(QDir().mkdir(path),"Generated profile mkdir failed");require(QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner),"Generated profile mode failed");write(path+"/downloads.config",valid?inventory:QByteArray("invalid"));write(path+"/tables.config","de");write(path+"/biglybt.config","de");write(path+"/BiglyBT.statistics",saved);}
  std::filesystem::path native()const{return path.toStdString();}
  bool intact()const{return read(path+"/downloads.config")==inventory&&read(path+"/tables.config")=="de"&&read(path+"/biglybt.config")=="de"&&read(path+"/BiglyBT.statistics")==saved&&!QFile::exists(path+"/stats")&&!QFile::exists(path+"/BiglyBT.statistics.bak")&&!QFile::exists(path+"/BiglyBT.statistics.saving");}
};
struct Factory {
  std::atomic<unsigned> calls=0;
  std::atomic<bool> held=false,worker=false;
  std::atomic<app::OwnedAzureusService*> local=nullptr; // Borrowed only before close/deletion starts.
  app::SessionBridge::AzureusServiceFactory function(){return [this](const storage::ProfileLease& lease){++calls;held=lease.valid();worker=QThread::currentThread()!=qApp->thread();app::AzureusServiceOptions options;options.enabled=true;options.servePings=true;auto result=app::OwnedAzureusService::create(options);if(!result){throw std::runtime_error("Explicit generated service failed to start");}local.store(result->get());return std::move(*result);};}
};
auto remote(bool serve=true){app::AzureusServiceOptions options;options.enabled=true;options.servePings=serve;auto result=app::OwnedAzureusService::create(options);require(bool(result),"Explicit remote service failed");return std::move(*result);}
ui::AzureusStatisticsPresentation latest(const QSignalSpy& spy){return spy.empty()?ui::AzureusStatisticsPresentation{}:qvariant_cast<ui::AzureusStatisticsPresentation>(spy.last().at(0));}
bool success(const app::OwnedAzureusService& owner,std::uint64_t request){const auto value=owner.observation();return value.lastResult&&value.lastResult->request==request&&value.lastResult->terminal&&!value.lastResult->error&&!value.lastResult->transportFailure;}
bool finished(const app::OwnedAzureusService& owner,std::uint64_t request){const auto value=owner.observation();return value.lastResult&&value.lastResult->request==request&&value.lastResult->terminal;}
bool realReply(const ui::AzureusStatisticsPresentation& value){if(!value||!value->snapshot||value->snapshot->transport().counters().sent<1||value->snapshot->transport().counters().received<1||value->snapshot->coordinateObservations()<1){return false;}bool sent=false,reply=false;for(const auto& event:value->snapshot->events()){sent|=event.kind==udp::OperationEventKind::Sent;reply|=event.kind==udp::OperationEventKind::Reply;}return sent&&reply;}
}
class OwnedAzureusBridgeTests final:public QObject {
 Q_OBJECT
private slots:
 void initTestCase(){QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);}
 void defaultPolicyRemainsDisabledWhenStatisticsIsOpenedOffline(){
   Profile profile;const Stamp before(profile.path+"/BiglyBT.statistics");app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),packets(&bridge,&app::SessionBridge::azureusStatisticsReady),owners(&bridge,&app::SessionBridge::nativeSessionReady),operations(&bridge,&app::SessionBridge::engineStatisticsReady),rows(&bridge,&app::SessionBridge::snapshotReady),closed(&bridge,&app::SessionBridge::sessionClosed);
   ui::StatisticsPage page;page.resize(1280,900);page.show();QCoreApplication::processEvents();QCOMPARE(packets.count(),0);
   QVERIFY(bridge.open(profile.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(packets),10000);const auto session=sessions.last().at(0).toULongLong();QVERIFY(session);QCOMPARE(latest(packets)->session,session);QCOMPARE(latest(packets)->state,ui::AzureusStatisticsState::Disabled);QVERIFY(!latest(packets)->snapshot);QVERIFY(!latest(packets)->current);
   QVERIFY(bridge.statistics(session,1));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);QCoreApplication::processEvents();QCOMPARE(owners.count(),0);QCOMPARE(operations.count(),0);QCOMPARE(rows.count(),1);QCOMPARE(packets.count(),1);QVERIFY(profile.intact());QVERIFY(before.matches(profile.path+"/BiglyBT.statistics"));
   QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());auto released=storage::ProfileLease::acquire(profile.native());QVERIFY(released);
 }
 void invalidProfileNeverInvokesTheOptionalFactory(){
   Profile profile(false);Factory factory;app::SessionBridge bridge(nullptr,{},factory.function());QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),packets(&bridge,&app::SessionBridge::azureusStatisticsReady),messages(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed);
   QVERIFY(bridge.open(profile.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!messages.empty(),10000);QCOMPARE(factory.calls.load(),0U);QVERIFY(!factory.local.load());QCOMPARE(sessions.count(),0);QCOMPARE(packets.count(),0);
   QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);auto released=storage::ProfileLease::acquire(profile.native());QVERIFY(released);
 }
 void explicitFactoryFailureIsUnavailableAndNeverRetriesOnStatisticsReads_data(){QTest::addColumn<bool>("throwFailure");QTest::newRow("empty-owner")<<false;QTest::newRow("throwing-owner")<<true;}
 void explicitFactoryFailureIsUnavailableAndNeverRetriesOnStatisticsReads(){
   QFETCH(bool,throwFailure);Profile profile;std::atomic<unsigned> calls=0;std::atomic<bool> held=false,worker=false;
   app::SessionBridge bridge(nullptr,{},[&](const storage::ProfileLease& lease)->std::unique_ptr<app::OwnedAzureusService>{++calls;held=lease.valid();worker=QThread::currentThread()!=qApp->thread();if(throwFailure){throw std::runtime_error("Generated optional service creation failure");}return {};});
   QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),packets(&bridge,&app::SessionBridge::azureusStatisticsReady),owners(&bridge,&app::SessionBridge::nativeSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
   QVERIFY(bridge.open(profile.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(packets),10000);QCOMPARE(calls.load(),1U);QVERIFY(held.load()&&worker.load());QCOMPARE(latest(packets)->state,ui::AzureusStatisticsState::Unavailable);QVERIFY(!latest(packets)->snapshot);QVERIFY(!latest(packets)->current);
   ui::StatisticsPage page;page.show();const auto session=sessions.last().at(0).toULongLong();QVERIFY(bridge.statistics(session,1));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);QVERIFY(bridge.refresh());QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);QCOMPARE(calls.load(),1U);QCOMPARE(owners.count(),0);QVERIFY(profile.intact());
   QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);auto released=storage::ProfileLease::acquire(profile.native());QVERIFY(released);
 }
 void actualLoopbackServicePublishesWithoutAnyTorrentEngineAndClosesBeforeLeaseRelease(){
   Profile profile;const Stamp before(profile.path+"/downloads.config");auto peer=remote();Factory factory;app::SessionBridge bridge(nullptr,{},factory.function());
   QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),packets(&bridge,&app::SessionBridge::azureusStatisticsReady),owners(&bridge,&app::SessionBridge::nativeSessionReady),operations(&bridge,&app::SessionBridge::engineStatisticsReady),closed(&bridge,&app::SessionBridge::sessionClosed);
   bool wrongSession=false,closedBeforeAck=false;quint64 currentSession=0;connect(&bridge,&app::SessionBridge::columnLayoutSessionReady,&bridge,[&](quint64 value){currentSession=value;});
   connect(&bridge,&app::SessionBridge::azureusStatisticsReady,&bridge,[&](auto value){wrongSession|=!value||value->session!=currentSession;if(value&&value->snapshot&&value->snapshot->transport().state()==udp::TransportStatisticsState::Closed){closedBeforeAck=closed.empty();}});
   QVERIFY(bridge.open(profile.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(packets)&&factory.local.load(),10000);auto* local=factory.local.load();QVERIFY(local);QVERIFY(factory.held.load()&&factory.worker.load());QCOMPARE(factory.calls.load(),1U);
   const auto initial=latest(packets);QCOMPARE(initial->state,ui::AzureusStatisticsState::Observed);QVERIFY(initial->current&&initial->snapshot);QCOMPARE(initial->snapshot->transport().family(),udp::TransportStatisticsFamily::IPv4);QVERIFY(initial->snapshot->transport().generation());QCOMPARE(initial->snapshot->transport().counters().sent,std::uint64_t{0});
   const auto allowRemote=peer->allowPeer(local->endpoint()),allowLocal=local->allowPeer(peer->endpoint());QVERIFY(allowRemote&&allowLocal);QTRY_VERIFY_WITH_TIMEOUT(success(*peer,*allowRemote)&&success(*local,*allowLocal),3000);
   const auto request=local->ping(peer->endpoint());QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(success(*local,*request),3000);QTRY_VERIFY_WITH_TIMEOUT(realReply(latest(packets)),5000);const auto observed=latest(packets);
   QCOMPARE(observed->session,currentSession);QVERIFY(observed->snapshot->lastRttNanoseconds()&&*observed->snapshot->lastRttNanoseconds()>0);QVERIFY(observed->snapshot->remotePosition());QCOMPARE(initial->snapshot->transport().counters().sent,std::uint64_t{0});QCOMPARE(owners.count(),0);QCOMPARE(operations.count(),0);QVERIFY(!bridge.busy());QVERIFY(profile.intact());
   auto held=storage::ProfileLease::acquire(profile.native());QVERIFY(!held);QCOMPARE(held.error().code,storage::ErrorCode::Busy);
   QVERIFY(bridge.closeSession());factory.local.store(nullptr);QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);QTRY_VERIFY_WITH_TIMEOUT(latest(packets)&&latest(packets)->snapshot&&latest(packets)->snapshot->transport().state()==udp::TransportStatisticsState::Closed,3000);
   QVERIFY(closedBeforeAck);QVERIFY(!latest(packets)->current);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());QVERIFY(!wrongSession);auto released=storage::ProfileLease::acquire(profile.native());QVERIFY(released);QVERIFY(before.matches(profile.path+"/downloads.config"));QVERIFY(profile.intact());QCOMPARE(observed->snapshot->transport().state(),udp::TransportStatisticsState::Bound);
 }
 void reopenUsesFreshGenerationAndDropsAllClosedProfilePublications(){
   Profile first,second;auto peer=remote();Factory factory;app::SessionBridge bridge(nullptr,{},factory.function());QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),packets(&bridge,&app::SessionBridge::azureusStatisticsReady),closed(&bridge,&app::SessionBridge::sessionClosed);quint64 admitted=0;bool wrong=false;
   connect(&bridge,&app::SessionBridge::columnLayoutSessionReady,&bridge,[&](quint64 value){admitted=value;});connect(&bridge,&app::SessionBridge::azureusStatisticsReady,&bridge,[&](auto value){wrong|=!value||value->session!=admitted;});
   QVERIFY(bridge.open(first.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(packets)&&factory.local.load(),10000);const auto firstSession=admitted;const auto generation=latest(packets)->snapshot->transport().generation();const auto oldEndpoint=factory.local.load()->endpoint();const auto allow=peer->allowPeer(oldEndpoint);QVERIFY(allow);QTRY_VERIFY_WITH_TIMEOUT(success(*peer,*allow),3000);
   QVERIFY(bridge.closeSession());factory.local.store(nullptr);QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);admitted=0;const auto packetCount=packets.count();const auto late=peer->ping(oldEndpoint,30ms);QVERIFY(late);QTRY_VERIFY_WITH_TIMEOUT(finished(*peer,*late),3000);QCOMPARE(peer->observation().lastResult->transportFailure,std::optional{udp::Failure::Timeout});QCoreApplication::processEvents();QCOMPARE(packets.count(),packetCount);
   {auto released=storage::ProfileLease::acquire(first.native());QVERIFY(released);}QVERIFY(bridge.open(second.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==2&&latest(packets)->session==admitted,10000);QVERIFY(admitted>firstSession);QCOMPARE(factory.calls.load(),2U);QVERIFY(latest(packets)->snapshot->transport().generation()>generation);QVERIFY(latest(packets)->current);QVERIFY(!wrong);
   QVERIFY(bridge.closeSession());factory.local.store(nullptr);QTRY_COMPARE_WITH_TIMEOUT(closed.count(),2,10000);QVERIFY(first.intact()&&second.intact());
 }
 void destructionWithActualPendingWorkJoinsAndSuppressesQueuedGuiCallbacks(){
   Profile profile;auto peer=remote(false);Factory factory;QPointer<app::SessionBridge> bridge=new app::SessionBridge(nullptr,{},factory.function());QSignalSpy packets(bridge,&app::SessionBridge::azureusStatisticsReady),sessions(bridge,&app::SessionBridge::columnLayoutSessionReady);
   QVERIFY(bridge->open(profile.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge->busy()&&latest(packets)&&factory.local.load(),10000);auto* local=factory.local.load();const auto endpoint=local->endpoint();const auto allow=peer->allowPeer(endpoint);QVERIFY(allow);QTRY_VERIFY_WITH_TIMEOUT(success(*peer,*allow),3000);const auto pending=local->ping(peer->endpoint(),2s);QVERIFY(pending);QTRY_VERIFY_WITH_TIMEOUT(local->observation().snapshot->transport().pending()==1,3000);
   const auto retained=local->observation().snapshot;delete bridge.data();factory.local.store(nullptr);QVERIFY(!bridge);auto released=storage::ProfileLease::acquire(profile.native());QVERIFY(released);const auto count=packets.count();QCoreApplication::processEvents();QCOMPARE(packets.count(),count);QCOMPARE(retained->transport().pending(),std::size_t{1});
   const auto after=peer->ping(endpoint,30ms);QVERIFY(after);QTRY_VERIFY_WITH_TIMEOUT(finished(*peer,*after),3000);QCOMPARE(peer->observation().lastResult->transportFailure,std::optional{udp::Failure::Timeout});QVERIFY(profile.intact());
 }
 void directActualObservationReceiverMayDeleteBridgeAndReleaseTheLease(){
   Profile profile;Factory factory;QPointer<app::SessionBridge> bridge=new app::SessionBridge(nullptr,{},factory.function());bool observed=false;connect(bridge,&app::SessionBridge::azureusStatisticsReady,bridge,[&](auto value){if(value&&value->state==ui::AzureusStatisticsState::Observed&&value->snapshot){observed=true;delete bridge.data();factory.local.store(nullptr);}});
   QVERIFY(bridge->open(profile.native(),0));QTRY_VERIFY_WITH_TIMEOUT(bridge.isNull(),10000);QVERIFY(observed);QCOMPARE(factory.calls.load(),1U);auto released=storage::ProfileLease::acquire(profile.native());QVERIFY(released);QCoreApplication::processEvents();QVERIFY(profile.intact());
 }
};
QTEST_MAIN(OwnedAzureusBridgeTests)
#include "owned_azureus_bridge_tests.moc"
