#include "scheduler_host_fixture.hpp"
#include "owned_engine_fixture.hpp"
#include "persistent_session.hpp"
#include "library_commands.hpp"
#include "ownership_file.hpp"
#include <QApplication>
#include <QtTest>
#include <algorithm>
#include <chrono>
#include <sys/stat.h>

using namespace biglybtc;
namespace sh=test::scheduler_host;
namespace {
libtorrent::entry configuration(){
  libtorrent::entry cfg(libtorrent::entry::dictionary_type{});
  cfg["Bind IP"]="127.0.0.1";cfg["TCP.Listen.Port.Enable"]=0;cfg["peercontrol.udp.public.enable"]=0;
  cfg["Peer Source Selection Default.Tracker"]=0;cfg["Peer Source Selection Default.DHT"]=0;
  cfg["Peer Source Selection Default.PeerExchange"]=0;cfg["Plugin.DHT.dht.enabled"]=0;
  cfg["Max Upload Speed KBs"]=7;cfg["Max Download Speed KBs"]=9;
  cfg["Plugin.SpeedScheduler.speedscheduler.enabled"]=0;cfg["Plugin.SpeedScheduler.thread.sleep.time"]=1000;
  cfg["opaque.scheduler.fixture"]=std::string("\0\xffS",3);return cfg;
}
void prepare(test::OwnedEngineFixture& f){
  sh::original(f.profile);auto cfg=configuration();test::write(f.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(cfg)));
  auto doc=sh::document(test::read(f.profile+"/biglybt.config"));auto policy=engine::readNetworkPolicy(&doc,{.mode=engine::NetworkMode::Normal});
  test::require(policy&&policy->bindAddresses==std::vector<std::string>{"127.0.0.1"}&&!policy->incomingTcp&&!policy->incomingUtp&&!policy->outgoingUtp
      &&!policy->httpTrackers&&!policy->udpTrackers&&!policy->dht&&!policy->pex&&!policy->lsd&&!policy->upnp&&!policy->natpmp,"Owned scheduler fixture must not enable discovery/public providers");
}
ui::Presentation lastRows(const QSignalSpy& spy){return spy.empty()?ui::Presentation{}:qvariant_cast<ui::Presentation>(spy.last()[0]);}
std::optional<ui::LibraryItem> row(const QSignalSpy& spy,const core::TorrentHash& hash){auto p=lastRows(spy);if(!p){return {};}
  auto it=std::ranges::find(p->items,QString::fromStdString(hash.hex()),&ui::LibraryItem::hash);return it==p->items.end()?std::nullopt:std::optional{*it};}
bool actualRates(const QSignalSpy& spy,std::int64_t up,std::int64_t down){
  if(spy.empty()){return false;}auto p=qvariant_cast<ui::NativeSessionPresentation>(spy.last()[0]);return p&&p->rateLimits.upload.state==core::RateLimitState::Limited&&p->rateLimits.download.state==core::RateLimitState::Limited
      &&p->rateLimits.upload.bytesPerSecond==up*1024&&p->rateLimits.download.bytesPerSecond==down*1024;
}
void opaque(const test::OwnedEngineFixture& f){const auto cfg=sh::document(test::read(f.profile+"/biglybt.config"));const auto* value=cfg.find("opaque.scheduler.fixture");test::require(value&&value->type==core::Node::Type::Bytes&&value->bytes==std::string("\0\xffS",3),"Scheduler changed opaque config bytes");}
}
// Explicit source-bound cohort ONLY; no skips or fake owner packets. Normal
// profile entry is constrained to numeric loopback/outgoing TCP, with no peers
// supplied, tracker, DHT, discovery or public networking.
class SchedulerHostEngineTests final:public QObject{
  Q_OBJECT
private slots:
  void initTestCase(){
    QApplication::setQuitOnLastWindowClosed(false);auto owner=engine::PersistentSession::create({701,702,703});
    QVERIFY2(owner.has_value(),"Scheduler actual-owner target requires approved bound runtime; no skip");
    const auto closed=(*owner)->close(std::chrono::steady_clock::now()+std::chrono::seconds{5});QVERIFY(closed.sessionJoined);
  }
  void scheduledRatesAndPauseUseFreshConfigThenManualStopWins_data(){QTest::addColumn<bool>("multifile");QTest::newRow("single")<<false;QTest::newRow("multi")<<true;}
  void scheduledRatesAndPauseUseFreshConfigThenManualStopWins(){
    QFETCH(bool,multifile);test::OwnedEngineFixture f(multifile);prepare(f);sh::ApprovedModule module;
    std::vector<QByteArray> payload;for(const auto& file:f.files){payload.push_back(test::read(f.payload+'/'+file.relative));}
    app::SessionBridge bridge(nullptr,{},{},module.factory());QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),rows(&bridge,&app::SessionBridge::snapshotReady),native(&bridge,&app::SessionBridge::nativeSessionReady),reads(&bridge,&app::SessionBridge::schedulerReady),writes(&bridge,&app::SessionBridge::schedulerSaved),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(f.native()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&row(rows,*f.hash),30000);const auto session=sessions.first()[0].toULongLong();
    QTRY_VERIFY_WITH_TIMEOUT(actualRates(native,7,9),10000);
    QVERIFY(bridge.loadScheduler({session,1}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    auto enable=sh::submission(sh::last(reads),2);enable.preferences.enabled=true;QVERIFY(bridge.saveScheduler(enable));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QTRY_VERIFY(!bridge.busy());QVERIFY2(sh::saved(writes)->saved,qPrintable(sh::saved(writes)->failure));
    QTRY_VERIFY_WITH_TIMEOUT(actualRates(native,17,19),10000);
    QTRY_VERIFY_WITH_TIMEOUT(row(rows,*f.hash)&&row(rows,*f.hash)->state.contains(QStringLiteral("Speed Scheduler")),15000);
    QVERIFY(QFile::exists(sh::schedulerFile(f.profile,"PausedTorrents.conf")));QVERIFY(!QFile::exists(sh::schedulerFile(f.profile,"BiglyBTC.scheduler-intent")));
    // A fresh load after the live rate/config write must itself be editable.
    QVERIFY(bridge.loadScheduler({session,3}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),2,10000);QTRY_VERIFY(!bridge.busy());
    const auto fresh=sh::last(reads);QVERIFY(fresh->revision>=sh::saved(writes)->snapshot->revision);auto edit=sh::submission(fresh,4);edit.preferences.overrideForced=true;
    QVERIFY(bridge.saveScheduler(edit));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),2,10000);QTRY_VERIFY(!bridge.busy());QVERIFY2(sh::saved(writes)->saved,qPrintable(sh::saved(writes)->failure));
    const auto frame=lastRows(rows);QVERIFY(frame);QVERIFY(bridge.downloadAction({{QString::fromStdString(f.hash->hex()),frame->revision}},core::DownloadAction::Stop));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&row(rows,*f.hash)&&row(rows,*f.hash)->state==QStringLiteral("Stopped"),15000);
    QVERIFY(bridge.loadScheduler({session,5}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),3,10000);QTRY_VERIFY(!bridge.busy());
    auto disable=sh::submission(sh::last(reads),6);disable.preferences.enabled=false;QVERIFY(bridge.saveScheduler(disable));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),3,10000);QTRY_VERIFY(!bridge.busy());QVERIFY2(sh::saved(writes)->saved,qPrintable(sh::saved(writes)->failure));
    QTRY_VERIFY_WITH_TIMEOUT(actualRates(native,7,9),10000);QCOMPARE(row(rows,*f.hash)->state,QStringLiteral("Stopped"));
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(closed.first()[1].toBool());QVERIFY(closed.first()[2].toBool());
    auto held=storage::ProfileLease::acquire(f.native());QVERIFY(held);auto library=storage::readLibraryView(*held);QVERIFY(library&&library->items.size()==1);QCOMPARE(library->items[0].saved.startup,core::StartupState::Stopped);QVERIFY(library->pauses.empty());
    for(std::size_t i=0;i<f.files.size();++i){const auto path=f.payload+'/'+f.files[i].relative;struct stat current{};QCOMPARE(::stat(path.toStdString().c_str(),&current),0);QCOMPARE(current.st_dev,f.files[i].before.st_dev);QCOMPARE(current.st_ino,f.files[i].before.st_ino);QCOMPARE(test::read(path),payload[i]);}
    QVERIFY(f.accountingIntact(false));opaque(f);
  }
  void inheritedSavedPauseNeverBecomesCurrentAuthority(){
    test::OwnedEngineFixture f(false);prepare(f);sh::ApprovedModule module;
    {auto held=storage::ProfileLease::acquire(f.native());QVERIFY(held);QVERIFY(storage::applyLibraryCommand(*held,*f.hash,core::DownloadAction::Pause));}
    auto formatted=storage::scheduler::originalOwnershipHash({reinterpret_cast<const std::uint8_t*>(f.hash->bytes().data()),f.hash->bytes().size()});QVERIFY(formatted);
    const auto legacy=QByteArray::fromStdString(*formatted+"\r\nopaque legacy line\r\n");sh::write(sh::schedulerFile(f.profile,"PausedTorrents.conf"),legacy);
    const auto inventory=test::read(f.profile+"/downloads.config");app::SessionBridge bridge(nullptr,{},{},module.factory());QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),reads(&bridge,&app::SessionBridge::schedulerReady),writes(&bridge,&app::SessionBridge::schedulerSaved),closed(&bridge,&app::SessionBridge::sessionClosed),rows(&bridge,&app::SessionBridge::snapshotReady);
    QVERIFY(bridge.openNormal(f.native()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),30000);const auto session=sessions.first()[0].toULongLong();
    QVERIFY(bridge.loadScheduler({session,1}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(sh::last(reads)->snapshot->inheritedOwnershipUnverified);
    auto enable=sh::submission(sh::last(reads),2);enable.preferences.enabled=true;QVERIFY(bridge.saveScheduler(enable));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QTRY_VERIFY(!bridge.busy());QVERIFY2(sh::saved(writes)->saved,qPrintable(sh::saved(writes)->failure));
    QCOMPARE(test::read(f.profile+"/downloads.config"),inventory);QCOMPARE(sh::read(sh::schedulerFile(f.profile,"PausedTorrents.conf")),legacy);QVERIFY(!QFile::exists(sh::schedulerFile(f.profile,"BiglyBTC.scheduler-intent")));
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(closed.first()[1].toBool());QVERIFY(closed.first()[2].toBool());QCOMPARE(test::read(f.profile+"/downloads.config"),inventory);opaque(f);
  }
};
QTEST_MAIN(SchedulerHostEngineTests)
#include "scheduler_host_engine_tests.moc"
