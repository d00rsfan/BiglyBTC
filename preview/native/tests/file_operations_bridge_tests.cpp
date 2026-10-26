#include "session_bridge.hpp"
#include "library_window.hpp"
#include "file_operation_dialog.hpp"
#include "torrent_creation_dialog.hpp"
#include "owned_engine_fixture.hpp"
#include "persistent_session.hpp"
#include "profile.hpp"
#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>
#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <sys/stat.h>

using namespace biglybtc;
namespace {
core::Document parse(const QByteArray& raw) {
 auto document=core::Document::parse(raw.toStdString(),storage::compatibleConfigLimits());test::require(document.has_value(),"Parse owned file-operation fixture");return std::move(*document);
}
std::string encoded(const core::Node& node) {
 auto value=core::encodeValue(node,storage::compatibleConfigLimits());test::require(value.has_value(),"Encode owned row");return std::move(*value);
}
void edit(test::OwnedEngineFixture& f,std::initializer_list<std::pair<std::string_view,std::string>> fields) {
 auto document=parse(test::read(f.profile+"/downloads.config"));auto row=core::Document::parse(encoded(document.find("downloads")->children.at(0)),storage::compatibleConfigLimits());test::require(row.has_value(),"Own fixture row");
 for(const auto& [key,value]:fields){test::require(row->replaceValue(key,value).has_value(),"Edit generated row");}
 test::require(document.replaceValue("downloads","l"+row->serialize()+'e').has_value(),"Replace generated row");test::write(f.profile+"/downloads.config",QByteArray::fromStdString(document.serialize()));
}
core::LibrarySnapshot inventory(const test::OwnedEngineFixture& f) {
 auto document=parse(test::read(f.profile+"/downloads.config"));auto value=core::readLibrary(document);test::require(value.has_value(),"Read owned inventory");return std::move(*value);
}
ui::Presentation latest(const QSignalSpy& spy){return spy.empty()?ui::Presentation{}:qvariant_cast<ui::Presentation>(spy.last()[0]);}
std::optional<ui::LibraryItem> item(const QSignalSpy& spy,const core::TorrentHash& hash) {
 auto value=latest(spy);if(!value){return {}; }auto found=std::ranges::find(value->items,QString::fromStdString(hash.hex()),&ui::LibraryItem::hash);return found==value->items.end()?std::nullopt:std::optional{*found};
}
ui::Target target(const QSignalSpy& spy,const core::TorrentHash& hash) {
 const auto value=latest(spy);test::require(value&&item(spy,hash).has_value(),"Current real target required");return {QString::fromStdString(hash.hex()),value->revision};
}
qint64 completed(const QSignalSpy& spy,const core::TorrentHash& hash) {
 qint64 result=0;for(const auto& arguments:spy){const auto update=qvariant_cast<ui::Telemetry>(arguments[0]);if(update.target.hash==QString::fromStdString(hash.hex())){result=std::max(result,update.completed);}}return result;
}
ui::FileOperationRequest request(const QSignalSpy& rows,quint64 session,quint64 id,const core::TorrentHash& hash,ui::FileOperation operation,QString destination={}) {
 const auto selected=target(rows,hash);return {session,id,selected.generation,selected,operation,std::move(destination)};
}
ui::FileOperationPlanPresentation prepared(const QSignalSpy& spy){return spy.empty()?ui::FileOperationPlanPresentation{}:qvariant_cast<ui::FileOperationPlanPresentation>(spy.last()[0]);}
ui::FileOperationResultPresentation finished(const QSignalSpy& spy){return spy.empty()?ui::FileOperationResultPresentation{}:qvariant_cast<ui::FileOperationResultPresentation>(spy.last()[0]);}
ui::TorrentCreationResultPresentation created(const QSignalSpy& spy){return spy.empty()?ui::TorrentCreationResultPresentation{}:qvariant_cast<ui::TorrentCreationResultPresentation>(spy.last()[0]);}
ui::FileOperationSubmission copied(ui::FileOperationPlanPresentation plan,bool confirm=false){return {std::make_shared<const ui::FileOperationPlanPacket>(*plan),confirm,plan->deleteTorrentFile};}
void sameInode(const QString& path,const struct stat& before) {
 struct stat after{};test::require(::lstat(path.toStdString().c_str(),&after)==0&&S_ISREG(after.st_mode)&&after.st_dev==before.st_dev&&after.st_ino==before.st_ino,"Owned payload inode changed unexpectedly");
}
ui::TorrentCreationRequest creation(const test::OwnedEngineFixture& f,quint64 session,quint64 id,bool add) {
 const auto source=f.root.path()+QStringLiteral("/new-source.bin");test::write(source,QByteArray(65536,'q'));
 ui::TorrentCreationOptions options;options.source=source;options.output=f.root.path()+QStringLiteral("/created.torrent");options.privateTorrent=true;options.comment=QStringLiteral("owned original-compatible fixture");options.trackerTiers={{QStringLiteral("http://127.0.0.1:9/announce")}};options.importAfterCreation=add;
 return {session,id,1,std::move(options)};
}
}

// Actual generated profiles and real Bridge worker/held-lease boundaries.
// Engine cases require approved source runtime: no skips/unbound substitution.
class FileOperationsBridgeTests final:public QObject {
 Q_OBJECT
private slots:
 void initTestCase() {
  QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);
  auto owner=engine::PersistentSession::create({801,802,803});QVERIFY2(owner.has_value(),"File-operation real-owner cohort requires approved source runtime; no skip");
  const auto close=(*owner)->close(std::chrono::steady_clock::now()+std::chrono::seconds{5});QVERIFY(close.sessionJoined);
 }
 void liveReadOnlyPreviewAndDefensiveDialogCopyThenRemoveKeep() {
  test::OwnedEngineFixture f(false,true);edit(f,{{"state","i70e"},{"maxdl","i8192e"}});QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
  app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),traffic(&bridge,&app::SessionBridge::progressReady),plans(&bridge,&app::SessionBridge::fileOperationPrepared),results(&bridge,&app::SessionBridge::fileOperationFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.open(f.native(),f.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(rows),30000);
  QVERIFY(bridge.downloadAction({target(rows,*f.hash)},core::DownloadAction::Start));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);QTRY_VERIFY_WITH_TIMEOUT(completed(traffic,*f.hash)>0,30000);
  const auto beforeInventory=test::read(f.profile+"/downloads.config"),beforeState=test::read(f.stateFile);const auto beforeProgress=completed(traffic,*f.hash);
  struct stat inventoryStamp{},stateStamp{};QCOMPARE(::lstat((f.profile+"/downloads.config").toStdString().c_str(),&inventoryStamp),0);QCOMPARE(::lstat(f.stateFile.toStdString().c_str(),&stateStamp),0);
  ui::FileOperationDialog dialog;connect(&bridge,&app::SessionBridge::busyChanged,&dialog,&ui::FileOperationDialog::setBusy);connect(&bridge,&app::SessionBridge::fileOperationPrepared,&dialog,&ui::FileOperationDialog::setPlan);connect(&bridge,&app::SessionBridge::fileOperationFinished,&dialog,&ui::FileOperationDialog::setResult);
  connect(&dialog,&ui::FileOperationDialog::planRequested,&bridge,[&](ui::FileOperationRequest value){QVERIFY(bridge.prepareFileOperation(value));});connect(&dialog,&ui::FileOperationDialog::executeRequested,&bridge,[&](ui::FileOperationSubmission value){QVERIFY(bridge.executeFileOperation(std::move(value)));});
  const auto selected=target(rows,*f.hash);QVERIFY(dialog.beginOperation(sessions[0][0].toULongLong(),selected.generation,selected,ui::FileOperation::RemoveKeepData));dialog.show();
  QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&dialog.plan()&&prepared(plans),10000);QVERIFY(prepared(plans)->failure.isEmpty());QVERIFY(dialog.plan()!=prepared(plans));
  QCOMPARE(prepared(plans)->presentFiles,quint64(1));QCOMPARE(prepared(plans)->missingFiles,quint64(0));QCOMPARE(prepared(plans)->totalBytes,quint64(test::OwnedEngineFixture::PayloadBytes));
  QCOMPARE(test::read(f.profile+"/downloads.config"),beforeInventory);QCOMPARE(test::read(f.stateFile),beforeState);
  sameInode(f.profile+"/downloads.config",inventoryStamp);sameInode(f.stateFile,stateStamp);
  QTRY_VERIFY_WITH_TIMEOUT(completed(traffic,*f.hash)>beforeProgress,15000);QVERIFY(item(rows,*f.hash));QVERIFY(item(rows,*f.hash)->state!=QStringLiteral("Stopped"));
  dialog.accept();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results),15000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));QVERIFY(!dialog.isVisible());
  QVERIFY(inventory(f).downloads.empty());for(const auto& file:f.files){sameInode(f.payload+'/'+file.relative,file.before);}QVERIFY(!QFileInfo::exists(f.stateFile));
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());auto lease=storage::ProfileLease::acquire(f.native());QVERIFY(lease);
 }
 void pausedLiveMovePreservesCountersPauseAndRequiredRecheck() {
  test::OwnedEngineFixture f(true,true);edit(f,{{"state","i70e"},{"maxdl","i8192e"},{"downloaded","i1000e"},{"uploaded","i500e"}});QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
  const auto destination=f.root.path()+QStringLiteral("/relocated");QVERIFY(QDir().mkpath(destination));
  app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),traffic(&bridge,&app::SessionBridge::progressReady),plans(&bridge,&app::SessionBridge::fileOperationPrepared),results(&bridge,&app::SessionBridge::fileOperationFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.open(f.native(),f.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(rows),30000);QVERIFY(bridge.downloadAction({target(rows,*f.hash)},core::DownloadAction::Start));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);QTRY_VERIFY_WITH_TIMEOUT(completed(traffic,*f.hash)>0,30000);
  QVERIFY(bridge.downloadAction({target(rows,*f.hash)},core::DownloadAction::Pause));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&item(rows,*f.hash)&&item(rows,*f.hash)->state==QStringLiteral("Paused"),10000);
  const auto value=request(rows,sessions[0][0].toULongLong(),50,*f.hash,ui::FileOperation::Relocate,destination);QVERIFY(bridge.prepareFileOperation(value));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);QVERIFY2(prepared(plans)->failure.isEmpty(),qPrintable(prepared(plans)->failure));
  QVERIFY(bridge.executeFileOperation(copied(prepared(plans))));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results),15000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));QVERIFY2(finished(results)->failure.isEmpty(),qPrintable(finished(results)->failure));
  QTRY_VERIFY_WITH_TIMEOUT(item(rows,*f.hash)&&item(rows,*f.hash)->state==QStringLiteral("Paused"),10000);
  auto saved=inventory(f);QCOMPARE(saved.downloads.size(),std::size_t(1));QCOMPARE(saved.downloads[0].saveDirectory,destination.toStdString());QVERIFY(saved.downloads[0].downloaded&&*saved.downloads[0].downloaded>1000);QVERIFY(saved.downloads[0].uploaded&&*saved.downloads[0].uploaded>=500);
  QCOMPARE(saved.paused.size(),std::size_t(1));QCOMPARE(saved.paused[0].hash,*f.hash);
  const auto state=parse(test::read(f.stateFile));const auto* resume=state.find("resume");QVERIFY(resume);const auto* data=resume->find("data");QVERIFY(data);const auto* valid=data->find("valid");QVERIFY(valid);QCOMPARE(valid->type,core::Node::Type::Integer);QCOMPARE(valid->integer,std::int64_t(0));
  const auto* pieces=data->find("resume data");QVERIFY(pieces);QCOMPARE(pieces->type,core::Node::Type::Bytes);QCOMPARE(pieces->bytes.size(),std::size_t(test::OwnedEngineFixture::PayloadBytes/16384));QVERIFY(std::ranges::all_of(pieces->bytes,[](char value){return value==2;}));
  for(const auto& file:f.files){QVERIFY(!QFileInfo::exists(f.payload+'/'+file.relative));sameInode(destination+'/'+file.relative,file.before);}
  QVERIFY(QDir(destination).entryList(QDir::Files|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());auto lease=storage::ProfileLease::acquire(f.native());QVERIFY(lease);
 }
 void stoppedRelinkRetainsSourceAndDeleteRequiresNewConfirmedPlan() {
  test::OwnedEngineFixture f(false);edit(f,{{"state","i70e"}});const auto destination=f.root.path()+QStringLiteral("/existing");QVERIFY(QDir().mkpath(destination));
  const auto oldPath=f.payload+'/'+f.files[0].relative,newPath=destination+'/'+f.files[0].relative;QVERIFY(QFile::copy(oldPath,newPath));const auto bytes=test::read(oldPath);
  app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),plans(&bridge,&app::SessionBridge::fileOperationPrepared),results(&bridge,&app::SessionBridge::fileOperationFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.open(f.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(rows),10000);const auto session=sessions[0][0].toULongLong();
  QVERIFY(bridge.prepareFileOperation(request(rows,session,70,*f.hash,ui::FileOperation::Relink,destination)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);QVERIFY(prepared(plans)->failure.isEmpty());QVERIFY(bridge.executeFileOperation(copied(prepared(plans))));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results),10000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));
  QCOMPARE(test::read(oldPath),bytes);QCOMPARE(test::read(newPath),bytes);QCOMPARE(inventory(f).downloads[0].saveDirectory,destination.toStdString());
  QVERIFY(bridge.prepareFileOperation(request(rows,session,71,*f.hash,ui::FileOperation::RemoveDeleteData)));QTRY_COMPARE_WITH_TIMEOUT(plans.count(),2,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(prepared(plans)->failure.isEmpty());
  const auto inventoryBefore=test::read(f.profile+"/downloads.config");QVERIFY(bridge.executeFileOperation(copied(prepared(plans),false)));QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(!finished(results)->completed);QCOMPARE(test::read(f.profile+"/downloads.config"),inventoryBefore);QCOMPARE(test::read(newPath),bytes);
  QVERIFY(bridge.prepareFileOperation(request(rows,session,72,*f.hash,ui::FileOperation::RemoveDeleteData)));QTRY_COMPARE_WITH_TIMEOUT(plans.count(),3,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(bridge.executeFileOperation(copied(prepared(plans),true)));QTRY_COMPARE_WITH_TIMEOUT(results.count(),3,10000);QTRY_VERIFY(!bridge.busy());QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));
  QVERIFY(!QFileInfo::exists(newPath));QCOMPARE(test::read(oldPath),bytes);QVERIFY(inventory(f).downloads.empty());QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);auto lease=storage::ProfileLease::acquire(f.native());QVERIFY(lease);
 }
 void archivePreferenceFromHeldProfile_data(){QTest::addColumn<int>("preference");QTest::newRow("original-default")<<-1;QTest::newRow("saved-keep")<<0;QTest::newRow("saved-nonzero")<<2;}
 void archivePreferenceFromHeldProfile(){
  QFETCH(int,preference);test::OwnedEngineFixture f(false);const auto archive=f.root.path()+QStringLiteral("/saved.torrent");test::write(archive,test::read(f.stateFile));
  const auto nativePath=archive.toUtf8().toStdString();edit(f,{{"state","i70e"},{"torrent",std::to_string(nativePath.size())+':'+nativePath}});
  if(preference>=0){auto config=parse("de");test::require(config.replaceValue("def.deletetorrent","i"+std::to_string(preference)+'e').has_value(),"archive setting");test::require(config.replaceValue("opaque.options","li7e3:rawe").has_value(),"unknown setting");test::write(f.profile+"/biglybt.config",QByteArray::fromStdString(config.serialize()));}
  const auto payload=test::read(f.payload+'/'+f.files[0].relative),metadata=test::read(archive);
  app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),plans(&bridge,&app::SessionBridge::fileOperationPrepared),results(&bridge,&app::SessionBridge::fileOperationFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.open(f.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(rows),10000);const auto session=sessions[0][0].toULongLong();
  QVERIFY(bridge.prepareFileOperation(request(rows,session,401,*f.hash,ui::FileOperation::RemoveKeepData)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);
  const auto plan=prepared(plans);QVERIFY2(plan->failure.isEmpty(),qPrintable(plan->failure));QCOMPARE(plan->deleteTorrentFile,preference!=0);QCOMPARE(plan->archivePath,preference!=0?archive:QString{});
  if(plan->deleteTorrentFile){QVERIFY(bridge.executeFileOperation({plan,false,false}));QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(!finished(results)->completed);QCOMPARE(test::read(archive),metadata);QVERIFY(!inventory(f).downloads.empty());
    QVERIFY(bridge.prepareFileOperation(request(rows,session,402,*f.hash,ui::FileOperation::RemoveKeepData)));QTRY_COMPARE_WITH_TIMEOUT(plans.count(),2,10000);QTRY_VERIFY(!bridge.busy());}
  const auto previousResults=results.count();QVERIFY(bridge.executeFileOperation(copied(prepared(plans))));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&results.count()>previousResults,10000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));
  QCOMPARE(test::read(f.payload+'/'+f.files[0].relative),payload);QCOMPARE(QFileInfo::exists(archive),preference==0);if(preference==0){QCOMPARE(test::read(archive),metadata);}
  if(preference<0){QVERIFY(!QFileInfo::exists(f.profile+"/biglybt.config"));}
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);
 }
 void changedDisplayedScopeAndStaleTargetRejectBeforeAnyMutation() {
  test::OwnedEngineFixture f(false);edit(f,{{"state","i70e"}});const auto inventoryBefore=test::read(f.profile+"/downloads.config"),stateBefore=test::read(f.stateFile),payloadBefore=test::read(f.payload+'/'+f.files[0].relative);
  app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),plans(&bridge,&app::SessionBridge::fileOperationPrepared),results(&bridge,&app::SessionBridge::fileOperationFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.open(f.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(rows),10000);const auto session=sessions[0][0].toULongLong();
  QVERIFY(bridge.prepareFileOperation(request(rows,session,80,*f.hash,ui::FileOperation::RemoveKeepData)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);
  auto tampered=std::make_shared<ui::FileOperationPlanPacket>(*prepared(plans));tampered->paths[0].source=QStringLiteral("/not-approved");QVERIFY(bridge.executeFileOperation({tampered,false}));QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(!finished(results)->completed);
  QVERIFY(bridge.prepareFileOperation(request(rows,session,81,*f.hash,ui::FileOperation::RemoveKeepData)));QTRY_COMPARE_WITH_TIMEOUT(plans.count(),2,10000);QTRY_VERIFY(!bridge.busy());const auto stale=prepared(plans);const auto revision=latest(rows)->revision;
  QVERIFY(bridge.refresh());QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(rows)->revision>revision,10000);QVERIFY(bridge.executeFileOperation(copied(stale)));QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(!finished(results)->completed);
  QCOMPARE(test::read(f.profile+"/downloads.config"),inventoryBefore);QCOMPARE(test::read(f.stateFile),stateBefore);QCOMPARE(test::read(f.payload+'/'+f.files[0].relative),payloadBefore);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);
 }
 void actualCreatorOnlyImportsWhenExplicitAndImportIsStopped_data(){QTest::addColumn<bool>("add");QTest::newRow("create-only")<<false;QTest::newRow("explicit-stopped-import")<<true;}
 void actualCreatorOnlyImportsWhenExplicitAndImportIsStopped() {
  QFETCH(bool,add);test::OwnedEngineFixture f(false);edit(f,{{"state","i70e"}});app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),results(&bridge,&app::SessionBridge::torrentCreated),progress(&bridge,&app::SessionBridge::torrentCreationProgress),traffic(&bridge,&app::SessionBridge::progressReady),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.open(f.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(rows),10000);auto value=creation(f,sessions[0][0].toULongLong(),90,add);const auto source=test::read(value.options.source);
  auto wrong=value;wrong.session++;QVERIFY(!bridge.createTorrent(wrong));QVERIFY(!QFileInfo::exists(value.options.output));QVERIFY(bridge.createTorrent(value));wrong=value;wrong.request++;bridge.cancelTorrentCreation(wrong);
  QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&created(results),15000);const auto result=created(results);QVERIFY2(result->completed,qPrintable(result->failure));QVERIFY(result->outputPublished);QVERIFY(!result->cancelled);QVERIFY(result->failure.isEmpty());QCOMPARE(test::read(value.options.source),source);QVERIFY(!progress.empty());QCOMPARE(traffic.count(),0);
  const auto raw=test::read(value.options.output);libtorrent::error_code error;libtorrent::torrent_info torrent(libtorrent::span<const char>(raw.constData(),static_cast<std::size_t>(raw.size())),error,libtorrent::from_span);QVERIFY(!error);
  const auto hash=core::TorrentHash::parse(torrent.info_hashes().v1.to_string());QVERIFY(hash);QCOMPARE(result->hash,QString::fromStdString(hash->hex()));
  for(const auto& args:progress){const auto packet=qvariant_cast<ui::TorrentCreationProgressPresentation>(args[0]);QVERIFY(packet&&packet->session==value.session&&packet->request==value.request&&packet->revision==value.revision&&packet->sequence>0&&packet->completedBytes<=packet->totalBytes);}
  const auto saved=inventory(f);QCOMPARE(saved.downloads.size(),std::size_t(add?2:1));if(add){const auto found=std::ranges::find_if(saved.downloads,[&](const auto& row){return row.hash&&*row.hash==*hash;});QVERIFY(found!=saved.downloads.end());QCOMPARE(found->startup,core::StartupState::Stopped);QVERIFY(!found->forceStart);}
  const auto published=test::read(value.options.output);value.request++;QVERIFY(bridge.createTorrent(value));QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(!created(results)->completed);QCOMPARE(test::read(value.options.output),published);QCOMPARE(inventory(f).downloads.size(),saved.downloads.size());
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);value.request++;QVERIFY(!bridge.createTorrent(value));auto lease=storage::ProfileLease::acquire(f.native());QVERIFY(lease);
 }
 void actualWindowResultCanDeleteWindowWhileBridgeCompletes() {
  test::OwnedEngineFixture f(false);edit(f,{{"state","i70e"}});app::SessionBridge bridge;QPointer<ui::LibraryWindow> window=new ui::LibraryWindow;
  struct Cleanup {QPointer<ui::LibraryWindow> window;~Cleanup(){delete window.data();}} cleanup{window};
  connect(&bridge,&app::SessionBridge::snapshotReady,window,&ui::LibraryWindow::setPresentation);connect(&bridge,&app::SessionBridge::columnLayoutSessionReady,window,&ui::LibraryWindow::beginColumnLayoutSession);connect(&bridge,&app::SessionBridge::busyChanged,window,&ui::LibraryWindow::setBusy);
  connect(&bridge,&app::SessionBridge::torrentCreationProgress,window,&ui::LibraryWindow::setTorrentCreationProgress);connect(&bridge,&app::SessionBridge::torrentCreated,window,&ui::LibraryWindow::setTorrentCreationResult);
  connect(window,&ui::LibraryWindow::torrentCreateRequested,&bridge,[&](ui::TorrentCreationRequest value){if(!bridge.createTorrent(value)&&window){window->rejectTorrentCreationRequest(value);}});
  QSignalSpy results(&bridge,&app::SessionBridge::torrentCreated),closed(&bridge,&app::SessionBridge::sessionClosed);QVERIFY(bridge.open(f.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);window->show();
  auto* action=window->findChild<QAction*>("classic-create-torrent");QVERIFY(action);QVERIFY(action->isEnabled());action->trigger();auto* dialog=window->findChild<ui::TorrentCreationDialog*>();QVERIFY(dialog);
  const auto source=f.root.path()+QStringLiteral("/window-source.bin");test::write(source,QByteArray(32768,'w'));dialog->findChild<QLineEdit*>("torrent-creation-source")->setText(source);dialog->findChild<QLineEdit*>("torrent-creation-output")->setText(f.root.path()+QStringLiteral("/window.torrent"));
  dialog->findChild<QPlainTextEdit*>("torrent-creation-trackers")->setPlainText(QStringLiteral("http://127.0.0.1:9/announce"));
  connect(dialog,&QDialog::finished,&bridge,[&](int){delete window.data();});dialog->accept();QTRY_VERIFY_WITH_TIMEOUT(window.isNull()&&!bridge.busy()&&created(results),15000);QVERIFY2(created(results)->completed,qPrintable(created(results)->failure));
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());auto lease=storage::ProfileLease::acquire(f.native());QVERIFY(lease);
 }
};
QTEST_MAIN(FileOperationsBridgeTests)
#include "file_operations_bridge_tests.moc"
