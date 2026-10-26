#include "scheduler_host_fixture.hpp"
#include "imported_scheduler_fixture.hpp"
#include "owned_engine_fixture.hpp"
#include "library_commands.hpp"
#include "library_view.hpp"
#include "ownership_file.hpp"
#include "persistent_session.hpp"
#include <QApplication>
#include <array>
#include <atomic>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace biglybtc;
namespace sh=test::scheduler_host;
using test::imported_scheduler::prepare;
namespace {
ui::Presentation lastRows(const QSignalSpy& spy){return spy.empty()?ui::Presentation{}:qvariant_cast<ui::Presentation>(spy.last()[0]);}
std::optional<ui::LibraryItem> row(const QSignalSpy& spy,const core::TorrentHash& hash){auto p=lastRows(spy);if(!p){return {};}
  const auto found=std::ranges::find(p->items,QString::fromStdString(hash.hex()),&ui::LibraryItem::hash);
  return found==p->items.end()?std::nullopt:std::optional{*found};
}
// Test-only opt-in retains the real generated tree at its original absolute
// paths, so the fenced Java readback needs no path rewriting. Even a failed
// assertion leaves the generated fixture available to the root runner's finally.
QString capture(test::OwnedEngineFixture& f,bool force,bool multi){
  const auto directory=qEnvironmentVariable("BIGLYBTC_IMPORTED_SCHEDULER_CAPTURE_DIRECTORY");if(directory.isEmpty()){return {};}
  const QFileInfo info(directory);sh::require(info.isDir()&&!info.isSymLink()&&info.isAbsolute()
      &&info.canonicalFilePath()==info.absoluteFilePath()&&directory.startsWith("/tmp/"),"Owned existing capture directory");
  const auto path=directory+QStringLiteral("/case-%1-%2.json").arg(multi?"multi":"single",force?"force":"ordinary");
  sh::require(!QFile::exists(path),"Fresh capture receipt");f.root.setAutoRemove(false);
  QJsonObject object;object["root"]=f.root.path();object["profile"]=f.profile;object["hash"]=QString::fromStdString(f.hash->hex());
  object["forced"]=force;object["multifile"]=multi;object["joined"]=false;
  sh::write(path,QJsonDocument(object).toJson());return path;
}
}
class SchedulerImportedIntegrationTests final:public QObject{
  Q_OBJECT
private slots:
  void initTestCase(){
    QApplication::setQuitOnLastWindowClosed(false);
    auto owner=engine::PersistentSession::create({1701,1702,1703});QVERIFY2(owner,"Approved actual runtime required; no skip");
    QVERIFY((*owner)->close(std::chrono::steady_clock::now()+std::chrono::seconds{5}).sessionJoined);
  }
  void importedTransactionHasSeparateIdentityAndOriginalFirstOnlyConsumption(){
    test::OwnedEngineFixture f(false);prepare(f,false);sh::ApprovedModule module;auto held=storage::ProfileLease::acquire(f.native());QVERIFY(held);
    auto service=module.factory()(*held);QVERIFY(service);
    const auto bytes=f.hash->bytes();const auto text=storage::scheduler::originalOwnershipHash({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()});QVERIFY(text);
    sh::write(sh::schedulerFile(f.profile,"PausedTorrents.conf"),QByteArray::fromStdString(*text+"\n"+*text+"\nopaque survivor\n"));QVERIFY(service->reload(*held));
    storage::ImportedSchedulerTarget target;target.hash=*f.hash;target.libraryRevision=11;target.paused=true;target.complete=false;
    auto plan=service->evaluateImported(*held,{0,1},std::span(&target,1));QVERIFY(plan);QCOMPARE(plan->actions.size(),std::size_t{1});
    QCOMPARE(plan->actions[0].kind,storage::scheduler::ActionKind::Resume);
    auto stale=target;++stale.libraryRevision;QVERIFY(!service->beginImportedAction(*held,plan->actions[0],stale));
    QVERIFY(!QFile::exists(sh::schedulerFile(f.profile,"BiglyBTC.scheduler-intent")));
    QVERIFY(service->beginImportedAction(*held,plan->actions[0],target));
    QVERIFY(sh::read(sh::schedulerFile(f.profile,"BiglyBTC.scheduler-intent")).startsWith("BiglyBTC Scheduler imported intent 1\n"));
    // Actual saved transaction; caller-supplied success is a domain fixture.
    // The real owner effect is exercised by the next Host methods.
    const auto publication=storage::applyLibraryCommand(*held,*f.hash,core::DownloadAction::Resume);QVERIFY(publication);
    QVERIFY(service->finishImportedAction(*held,plan->actions[0],storage::SchedulerActionResult::Succeeded,&*publication));
    QCOMPARE(sh::read(sh::schedulerFile(f.profile,"PausedTorrents.conf")),QByteArray::fromStdString(*text+"\nopaque survivor\n"));
    storage::SchedulerTorrent native;native.token=91;native.hash=*f.hash;native.state=storage::scheduler::TorrentState::Stopped;
    const auto nativePlan=service->evaluate({0,1},std::span(&native,1),7,9);QVERIFY(nativePlan);QVERIFY(nativePlan->actions.empty());
    // Native manual revoke remains permanent in memory even if the file CAS
    // refuses cleanup; a later raw reload must not reimport that permission.
    sh::write(sh::schedulerFile(f.profile,"PausedTorrents.conf"),QByteArray::fromStdString(*text+"\nchanged\n"));
    QVERIFY(!service->revokeForManual(*held,*f.hash));QVERIFY(service->reload(*held));
    target.paused=false;target.libraryRevision=12;
    plan=service->evaluateImported(*held,{0,1},std::span(&target,1));QVERIFY(plan);QVERIFY(plan->actions.empty());
  }
  void multipleImportedCommandsAdvanceOnlyTheirApprovedInventoryScope(){
    test::OwnedEngineFixture first(false),second(true);prepare(first,false);prepare(second,false);sh::ApprovedModule module;
    auto a=sh::document(test::read(first.profile+"/downloads.config")),b=sh::document(test::read(second.profile+"/downloads.config"));
    const auto raw=[](const core::Document& doc,const core::Node& node){return doc.originalBytes().substr(node.begin,node.end-node.begin);};
    const auto downloads="l"+raw(a,a.find("downloads")->children[0])+raw(b,b.find("downloads")->children[0])+"e";
    const auto pauses="l"+raw(a,a.find("pause_data")->children[0])+raw(b,b.find("pause_data")->children[0])+"e";
    QVERIFY(a.replaceValue("downloads",downloads));QVERIFY(a.replaceValue("pause_data",pauses));
    test::write(first.profile+"/downloads.config",QByteArray::fromStdString(a.serialize()));
    const auto format=[](const core::TorrentHash& hash){const auto v=hash.bytes();return storage::scheduler::originalOwnershipHash({reinterpret_cast<const std::uint8_t*>(v.data()),v.size()});};
    const auto x=format(*first.hash),y=format(*second.hash);QVERIFY(x&&y);
    sh::write(sh::schedulerFile(first.profile,"PausedTorrents.conf"),QByteArray::fromStdString(*x+"\n"+*y+"\nopaque survivor\n"));
    auto held=storage::ProfileLease::acquire(first.native());QVERIFY(held);auto service=module.factory()(*held);QVERIFY(service);
    std::array<storage::ImportedSchedulerTarget,2> targets;
    for(auto& target:targets){target.libraryRevision=20;target.paused=true;target.complete=false;}
    targets[0].hash=*first.hash;targets[1].hash=*second.hash;
    const auto plan=service->evaluateImported(*held,{0,1},targets);QVERIFY(plan);QCOMPARE(plan->actions.size(),std::size_t{2});
    // Domain fixture: real saved transactions and module, with caller-supplied
    // engine acknowledgements. The next methods test actual native admissions.
    for(std::size_t i=0;i<targets.size();++i){
      QVERIFY(service->beginImportedAction(*held,plan->actions[i],targets[i]));
      const auto publication=storage::applyLibraryCommand(*held,targets[i].hash,core::DownloadAction::Resume);QVERIFY(publication);
      QVERIFY(service->finishImportedAction(*held,plan->actions[i],storage::SchedulerActionResult::Succeeded,&*publication));
    }
    QCOMPARE(sh::read(sh::schedulerFile(first.profile,"PausedTorrents.conf")),QByteArray("opaque survivor\n"));
    const auto after=storage::readLibraryView(*held);QVERIFY(after);QVERIFY(after->pauses.empty());QCOMPARE(after->items.size(),std::size_t{2});
  }
  void originalStaleHashRestartsSavedStoppedTargetWithoutNativeAuthority(){
    test::OwnedEngineFixture f(false);prepare(f,false);sh::ApprovedModule module;
    auto held=storage::ProfileLease::acquire(f.native());QVERIFY(held);
    // Java's wePausedTorrent is hash-only: a later non-paused stopped target
    // follows restart, even though no same-session native pause was acknowledged.
    QVERIFY(storage::applyLibraryCommand(*held,*f.hash,core::DownloadAction::Stop));
    auto service=module.factory()(*held);QVERIFY(service);
    storage::ImportedSchedulerTarget target;target.hash=*f.hash;target.libraryRevision=25;target.paused=false;
    const auto plan=service->evaluateImported(*held,{0,1},std::span(&target,1));QVERIFY(plan);QCOMPARE(plan->actions.size(),std::size_t{1});
    QCOMPARE(plan->actions[0].kind,storage::scheduler::ActionKind::Restart);
    QVERIFY(service->beginImportedAction(*held,plan->actions[0],target));
    const auto publication=storage::applyLibraryCommand(*held,*f.hash,core::DownloadAction::Start);QVERIFY(publication);
    QVERIFY(service->finishImportedAction(*held,plan->actions[0],storage::SchedulerActionResult::Succeeded,&*publication));
    QCOMPARE(sh::read(sh::schedulerFile(f.profile,"PausedTorrents.conf")),QByteArray("opaque survivor\r\n"));
    storage::SchedulerTorrent native;native.hash=*f.hash;native.token=92;native.state=storage::scheduler::TorrentState::Stopped;
    const auto ordinary=service->evaluate({0,1},std::span(&native,1),7,9);QVERIFY(ordinary);QVERIFY(ordinary->actions.empty());
  }
  void incorrectPublicationRetainsJournalAcrossReopen(){
    test::OwnedEngineFixture f(false);prepare(f,false);sh::ApprovedModule module;
    auto held=storage::ProfileLease::acquire(f.native());QVERIFY(held);auto service=module.factory()(*held);QVERIFY(service);
    storage::ImportedSchedulerTarget target;target.hash=*f.hash;target.libraryRevision=31;target.paused=true;target.complete=false;
    const auto plan=service->evaluateImported(*held,{0,1},std::span(&target,1));QVERIFY(plan);QCOMPARE(plan->actions.size(),std::size_t{1});
    QVERIFY(service->beginImportedAction(*held,plan->actions[0],target));
    const auto publication=storage::applyLibraryCommand(*held,*f.hash,core::DownloadAction::Resume);QVERIFY(publication);
    auto incorrect=*publication;incorrect.after.forceStart=!incorrect.after.forceStart;
    QVERIFY(!service->finishImportedAction(*held,plan->actions[0],storage::SchedulerActionResult::Succeeded,&incorrect));
    QVERIFY(QFile::exists(sh::schedulerFile(f.profile,"BiglyBTC.scheduler-intent")));
    QVERIFY(sh::read(sh::schedulerFile(f.profile,"PausedTorrents.conf")).contains("opaque survivor"));
    service.reset();service=module.factory()(*held);QVERIFY(service->snapshot().interruptedAction);
    target.paused=false;QVERIFY(!service->evaluateImported(*held,{0,1},std::span(&target,1)));
    QVERIFY(service->revokeForManual(*held,*f.hash));QVERIFY(!QFile::exists(sh::schedulerFile(f.profile,"BiglyBTC.scheduler-intent")));
  }
  void savedOnlyInheritedPauseRunsOriginalPermissionWithoutFakeAdmission_data(){
    QTest::addColumn<bool>("multifile");QTest::addColumn<bool>("forced");
    QTest::newRow("single")<<false<<false;QTest::newRow("multi-forced")<<true<<true;
  }
  void savedOnlyInheritedPauseRunsOriginalPermissionWithoutFakeAdmission(){
    QFETCH(bool,multifile);QFETCH(bool,forced);test::OwnedEngineFixture f(multifile);prepare(f,false,forced);sh::ApprovedModule module;
    const auto retained=capture(f,forced,multifile);app::SessionBridge bridge(nullptr,{},{},module.factory());
    QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(f.native()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&row(rows,*f.hash),30000);
    QTRY_COMPARE_WITH_TIMEOUT(sh::read(sh::schedulerFile(f.profile,"PausedTorrents.conf")),QByteArray("opaque survivor\r\n"),15000);
    QTRY_VERIFY_WITH_TIMEOUT(row(rows,*f.hash)&&row(rows,*f.hash)->state!=QStringLiteral("Paused"),15000);
    QVERIFY(!QFile::exists(sh::schedulerFile(f.profile,"BiglyBTC.scheduler-intent")));
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(closed.first()[1].toBool());QVERIFY(closed.first()[2].toBool());
    auto held=storage::ProfileLease::acquire(f.native());QVERIFY(held);const auto library=storage::readLibraryView(*held);QVERIFY(library);QVERIFY(library->pauses.empty());
    QCOMPARE(library->items[0].saved.forceStart,forced);QVERIFY(library->items[0].saved.startup!=core::StartupState::Stopped);
    const auto config=sh::document(test::read(f.profile+"/biglybt.config"));QVERIFY(config.find("opaque.imported.fixture")&&config.find("opaque.imported.fixture")->bytes=="preserved");
    if(!retained.isEmpty()){auto object=QJsonDocument::fromJson(sh::read(retained)).object();object["joined"]=true;sh::write(retained,QJsonDocument(object).toJson());}
  }
  void explicitJournalReviewKeepsActualOwnedSessionAndSavedPauseWithoutReplay(){
    test::OwnedEngineFixture f(false);prepare(f,false);sh::ApprovedModule module;
    {
      auto held=storage::ProfileLease::acquire(f.native());QVERIFY(held);auto service=module.factory()(*held);QVERIFY(service);
      storage::ImportedSchedulerTarget target;target.hash=*f.hash;target.libraryRevision=37;target.paused=true;target.complete=false;
      const auto plan=service->evaluateImported(*held,{0,1},std::span(&target,1));QVERIFY(plan);QCOMPARE(plan->actions.size(),std::size_t{1});
      QVERIFY(service->beginImportedAction(*held,plan->actions[0],target));
      // Real journal produced before its effect; no success/engine receipt is
      // fabricated. The next owner must treat the durable outcome as unknown.
    }
    const auto before=sh::read(f.profile+"/downloads.config");QTRY_VERIFY_WITH_TIMEOUT(f.ready(),15000);
    app::SessionBridge bridge(nullptr,{},{},module.factory());
    QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),
        observations(&bridge,&app::SessionBridge::nativeSessionReady),reads(&bridge,&app::SessionBridge::schedulerReady),
        writes(&bridge,&app::SessionBridge::schedulerSaved),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(f.native(),f.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&row(rows,*f.hash),15000);
    QTRY_VERIFY_WITH_TIMEOUT(!observations.empty(),15000);QCOMPARE(row(rows,*f.hash)->state,QStringLiteral("Paused"));
    const auto session=sessions.first()[0].toULongLong();QVERIFY(bridge.loadScheduler({session,601}));
    QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());const auto packet=sh::last(reads);
    QVERIFY(packet&&packet->snapshot&&packet->snapshot->interrupted);QVERIFY(packet->snapshot->interrupted->imported);
    auto review=sh::submission(packet,602);review.settleInterruptedHash=QString::fromStdString(f.hash->hex());
    review.settleInterruptedIdentity=packet->snapshot->interrupted->identity;
    QVERIFY(bridge.saveScheduler(review));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    const auto result=sh::saved(writes);QVERIFY2(result&&result->saved,qPrintable(result?result->failure:QStringLiteral("No result")));
    QVERIFY(result->settlement&&result->settlement->settled);QVERIFY(!result->snapshot->snapshot->interruptedAction);
    QCOMPARE(sh::read(f.profile+"/downloads.config"),before);QCOMPARE(row(rows,*f.hash)->state,QStringLiteral("Paused"));
    QCOMPARE(sh::read(sh::schedulerFile(f.profile,"PausedTorrents.conf")),QByteArray("opaque survivor\r\n"));
    QVERIFY(!QFile::exists(sh::schedulerFile(f.profile,"BiglyBTC.scheduler-intent")));
    // Explicit normal re-evaluation must not re-import the reviewed permission.
    auto next=sh::submission(result->snapshot,603);QVERIFY(bridge.saveScheduler(next));
    QTRY_COMPARE_WITH_TIMEOUT(writes.size(),2,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(sh::saved(writes)->saved);
    QCOMPARE(sh::read(f.profile+"/downloads.config"),before);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(closed.first()[1].toBool());QVERIFY(closed.first()[2].toBool());
    auto held=storage::ProfileLease::acquire(f.native());QVERIFY(held);const auto saved=storage::readLibraryView(*held);QVERIFY(saved);
    QCOMPARE(saved->pauses.size(),std::size_t{1});QCOMPARE(saved->pauses[0].hash,*f.hash);
  }
  void manualCommandBeforeLazySchedulerLoadCannotBeReimported_data(){
    QTest::addColumn<int>("action");
    QTest::newRow("stop-before-load")<<static_cast<int>(core::DownloadAction::Stop);
    QTest::newRow("pause-before-load")<<static_cast<int>(core::DownloadAction::Pause);
  }
  void manualCommandBeforeLazySchedulerLoadCannotBeReimported(){
    QFETCH(int,action);test::OwnedEngineFixture f(false);prepare(f,false);sh::ApprovedModule module;
    if(action==static_cast<int>(core::DownloadAction::Pause)){
      // Start unpaused so Host genuinely admits a new manual Pause. Keep the
      // imported permission bytes: only the saved library state is resumed.
      auto held=storage::ProfileLease::acquire(f.native());QVERIFY(held);
      QVERIFY(storage::applyLibraryCommand(*held,*f.hash,core::DownloadAction::Resume));
      const auto saved=storage::readLibraryView(*held);QVERIFY(saved);QVERIFY(saved->pauses.empty());
    }
    QTRY_VERIFY_WITH_TIMEOUT(f.ready(),15000);
    std::atomic<unsigned> loads{0};auto factory=module.factory();
    app::SessionBridge bridge(nullptr,{},{},[&](const storage::ProfileLease& held){
      loads.fetch_add(1);return factory(held);
    });
    QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),
        reads(&bridge,&app::SessionBridge::schedulerReady),writes(&bridge,&app::SessionBridge::schedulerSaved),closed(&bridge,&app::SessionBridge::sessionClosed);
    bool attempted=false,accepted=false;unsigned loadsAtManual=~0U;
    // Direct initial delivery queues the real user command before any later
    // idle event can load Scheduler. The legacy owned open does not tick it.
    connect(&bridge,&app::SessionBridge::snapshotReady,&bridge,[&](ui::Presentation frame){
      if(attempted||!frame){return;}attempted=true;loadsAtManual=loads.load();
      accepted=bridge.downloadAction({{QString::fromStdString(f.hash->hex()),frame->revision}},static_cast<core::DownloadAction>(action));
    });
    const auto permission=sh::read(sh::schedulerFile(f.profile,"PausedTorrents.conf"));
    QVERIFY(bridge.open(f.native(),f.port()));
    QTRY_VERIFY_WITH_TIMEOUT(attempted&&accepted&&!bridge.busy()&&rows.size()>=2&&!sessions.empty(),30000);
    QCOMPARE(loadsAtManual,0U);
    // There was no service to clean this file. Persistent permission is still
    // present, so source matching alone would wrongly Resume/Restart the row.
    QCOMPARE(sh::read(sh::schedulerFile(f.profile,"PausedTorrents.conf")),permission);
    const auto manualInventory=test::read(f.profile+"/downloads.config");
    const auto session=sessions.first()[0].toULongLong();
    QVERIFY(bridge.loadScheduler({session,1}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    QCOMPARE(loads.load(),1U);QVERIFY(sh::last(reads)->snapshot);
    auto poll=sh::submission(sh::last(reads),2);poll.preferences.enabled=false;
    // A confirmed save resets the existing poll deadline and synchronously
    // evaluates the imported branch before its result reaches this test.
    QVERIFY(bridge.saveScheduler(poll));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    QVERIFY2(sh::saved(writes)->saved,qPrintable(sh::saved(writes)->failure));
    QCOMPARE(test::read(f.profile+"/downloads.config"),manualInventory);
    QCOMPARE(sh::read(sh::schedulerFile(f.profile,"PausedTorrents.conf")),permission);
    QVERIFY(!QFile::exists(sh::schedulerFile(f.profile,"BiglyBTC.scheduler-intent")));
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);
    QVERIFY(closed.first()[1].toBool());QVERIFY(closed.first()[2].toBool());
    auto held=storage::ProfileLease::acquire(f.native());QVERIFY(held);
    const auto library=storage::readLibraryView(*held);QVERIFY(library);QCOMPARE(library->items[0].saved.startup,core::StartupState::Stopped);
    QCOMPARE(library->pauses.empty(),action==static_cast<int>(core::DownloadAction::Stop));
  }
  void interruptedImportedIntentRequiresExplicitMatchingManualSettlement(){
    test::OwnedEngineFixture f(false);prepare(f,true);sh::ApprovedModule module;
    sh::write(sh::schedulerFile(f.profile,"BiglyBTC.scheduler-intent"),QByteArray::fromStdString(
        "BiglyBTC Scheduler imported intent 1\n"+f.hash->hex()+"\n11\n0\n1\n"));
    const auto inventory=test::read(f.profile+"/downloads.config");app::SessionBridge bridge(nullptr,{},{},module.factory());
    QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),reads(&bridge,&app::SessionBridge::schedulerReady),writes(&bridge,&app::SessionBridge::schedulerSaved),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(f.native()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&row(rows,*f.hash),30000);
    QCOMPARE(test::read(f.profile+"/downloads.config"),inventory);QVERIFY(QFile::exists(sh::schedulerFile(f.profile,"BiglyBTC.scheduler-intent")));
    const auto frame=lastRows(rows);QVERIFY(frame);QVERIFY(bridge.downloadAction({{QString::fromStdString(f.hash->hex()),frame->revision}},core::DownloadAction::Stop));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&row(rows,*f.hash)&&row(rows,*f.hash)->state==QStringLiteral("Stopped"),15000);
    QCOMPARE(sh::read(sh::schedulerFile(f.profile,"PausedTorrents.conf")),QByteArray("opaque survivor\r\n"));QVERIFY(!QFile::exists(sh::schedulerFile(f.profile,"BiglyBTC.scheduler-intent")));
    const auto session=sessions.first()[0].toULongLong();QVERIFY(bridge.loadScheduler({session,1}));QTRY_COMPARE(reads.size(),1);QTRY_VERIFY(!bridge.busy());
    auto disable=sh::submission(sh::last(reads),2);disable.preferences.enabled=false;QVERIFY(bridge.saveScheduler(disable));QTRY_COMPARE(writes.size(),1);QTRY_VERIFY(!bridge.busy());QVERIFY2(sh::saved(writes)->saved,qPrintable(sh::saved(writes)->failure));
    QCOMPARE(row(rows,*f.hash)->state,QStringLiteral("Stopped"));QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);
  }
};
QTEST_MAIN(SchedulerImportedIntegrationTests)
#include "scheduler_imported_integration_tests.moc"
