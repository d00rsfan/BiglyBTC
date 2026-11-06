#include "session_bridge.hpp"
#include "library_window.hpp"
#include "file_operation_dialog.hpp"
#include "torrent_creation_dialog.hpp"
#include "owned_engine_fixture.hpp"
#include "full32_tag_fixture.hpp"
#include "persistent_session.hpp"
#include "profile.hpp"
#include "file_operations.hpp"
#include "network_policy.hpp"
#include "biglybtc/saved_download_history.hpp"
#include "biglybtc/saved_download_tag_edits.hpp"
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <sys/stat.h>

using namespace biglybtc;
namespace {
QString interruptedDirectory;
void interruptKeepData(storage::FileOperationStage stage){if(stage==storage::FileOperationStage::JournalSynced){throw std::runtime_error("simulated Keep Data pre-effect crash");}}
void interruptUnsealedReplacement(storage::FileOperationStage stage){if(stage==storage::FileOperationStage::JournalSynced){test::require(QDir().mkdir(interruptedDirectory),"Create owned unsealed crash fixture");test::write(interruptedDirectory+"/unknown","preserved");throw std::runtime_error("simulated unsealed replacement interruption");}}

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
 const auto selected=target(rows,hash);ui::FileOperationRequest out{session,id,selected.generation,selected,operation,std::move(destination)};out.recyclePayload=false;return out;
}

std::string savedMember(const core::TorrentHash& hash){
 const auto value=core::editSavedDownloadTags("d1:3d1:1d1:n3:oneeee",core::SavedTagAddMember{1,hash,1});test::require(value&&value->value.tags.size()==1&&value->value.tags[0].members.size()==1&&value->value.tags[0].members[0].rawId,"actual saved-member codec");return *value->value.tags[0].members[0].rawId;
}
void removalTagFixture(test::OwnedEngineFixture& f,bool unicode=false){
 libtorrent::entry value(libtorrent::entry::dictionary_type{});const auto member=savedMember(*f.hash);
 const auto add=[&](int id,const char* name,std::optional<std::string> group={}){auto& tag=value["3"][std::to_string(id)];tag["n"]=name;tag["o"]=libtorrent::entry::list_type{libtorrent::entry(member)};if(group){tag["c"]["gr"]=*group;}};
 if(unicode){add(11,"Тег10","Группа2");add(12,"тег2","группа2");add(13,"Σ10","Группа10");add(14,"σ2","группа10");add(15,"İ2");add(16,"i10");add(17,"Ω",std::string{});add(18,"😀2","emoji");add(19,"😀10","EMOJI");}
 else{add(1,"Disk10","Group2");value["3"]["1"]["c"]["vis"]=0;add(2,"Disk2","group2");add(3,"A1");add(4,"First",std::string{});add(5,"B1","Group10");add(6,"A1");}
 value["1"]["1"]["n"]="foreign category";value["1"]["1"]["o"]=libtorrent::entry::list_type{libtorrent::entry(member)};
 auto other=core::TorrentHash::parse(std::string(20,'z'));test::require(other.has_value(),"other saved hash");value["3"]["7"]["n"]="tag.unrelated.localization";value["3"]["7"]["o"]=libtorrent::entry::list_type{libtorrent::entry(savedMember(*other))};value["opaque"]=std::string("\0\xffT",3);
 test::write(f.profile+"/tag.config",QByteArray::fromStdString(test::encoded(value)));
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

 void invalidPendingRecoveryCannotPublishOrRetainWritableProfile_data(){QTest::addColumn<bool>("normal");QTest::newRow("owned-loopback-open")<<false;QTest::newRow("ordinary-open-no-admission")<<true;}
 void invalidPendingRecoveryCannotPublishOrRetainWritableProfile(){
  QFETCH(bool,normal);test::OwnedEngineFixture f(false);const auto source=f.payload+'/'+f.files[0].relative;const auto bytes=test::read(source);const auto occupied=f.root.path()+QStringLiteral("/occupied-refusal.bin");test::write(occupied,"prior");QString uncertain;
  {auto lease=storage::ProfileLease::acquire(f.native());QVERIFY(lease);storage::FileOperationRequest value{*f.hash,storage::FileOperationKind::ReplaceFile,{}};value.fileIndex=0;value.fileDestination=occupied.toStdString();auto plan=storage::prepareFileOperation(*lease,value);QVERIFY(plan);uncertain=QString::fromStdString(plan->preview().replacement->path.parent_path().string());const auto accepted=storage::confirmFileOperation(*plan,plan->preview().approvalScopeDigest,true);QVERIFY(accepted);interruptedDirectory=uncertain;QVERIFY(!storage::applyFileOperation(*lease,std::move(*plan),*accepted,interruptUnsealedReplacement));}
  edit(f,{{"downloaded","i17e"}}); // Valid saved library, changed exact accepted preimage: review must refuse.
  const auto inventoryBytes=test::read(f.profile+"/downloads.config"),stateBytes=test::read(f.stateFile),journalBytes=test::read(f.profile+"/BiglyBTC.file-operation");
  {auto lease=storage::ProfileLease::acquire(f.native());QVERIFY(lease);QVERIFY(!storage::recoverFileOperation(*lease));QVERIFY(!storage::prepareInterruptedReplacementReview(*lease));}
  app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),rows(&bridge,&app::SessionBridge::snapshotReady),native(&bridge,&app::SessionBridge::nativeSessionReady),messages(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(normal?bridge.openNormal(f.native()):bridge.open(f.native(),f.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!messages.empty(),10000);QCOMPARE(sessions.count(),0);QCOMPARE(rows.count(),0);QCOMPARE(native.count(),0);QCOMPARE(test::read(source),bytes);QCOMPARE(test::read(occupied),QByteArray("prior"));QCOMPARE(test::read(uncertain+"/unknown"),QByteArray("preserved"));QCOMPARE(test::read(f.profile+"/downloads.config"),inventoryBytes);QCOMPARE(test::read(f.stateFile),stateBytes);QCOMPARE(test::read(f.profile+"/BiglyBTC.file-operation"),journalBytes);
  {auto released=storage::ProfileLease::acquire(f.native());QVERIFY(released);} // Failure left no partial Host owning the lease.
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());
 }
 void unsealedReplacementReopensReadOnlyRequiresFreshConsentAndKeepsUnknownDirectory_data(){QTest::addColumn<bool>("normal");QTest::newRow("owned-loopback-open")<<false;QTest::newRow("ordinary-open-no-admission")<<true;}
 void unsealedReplacementReopensReadOnlyRequiresFreshConsentAndKeepsUnknownDirectory(){
  QFETCH(bool,normal);test::OwnedEngineFixture f(false);edit(f,{{"state","i75e"}});const auto source=f.payload+'/'+f.files[0].relative;const auto bytes=test::read(source);const auto target=f.root.path()+QStringLiteral("/occupied.bin");test::write(target,"prior");QString uncertain;
  {
    auto lease=storage::ProfileLease::acquire(f.native());QVERIFY(lease);storage::FileOperationRequest value{*f.hash,storage::FileOperationKind::ReplaceFile,{}};value.fileIndex=0;value.fileDestination=target.toStdString();auto plan=storage::prepareFileOperation(*lease,value);QVERIFY(plan);uncertain=QString::fromStdString(plan->preview().replacement->path.parent_path().string());const auto accepted=storage::confirmFileOperation(*plan,plan->preview().approvalScopeDigest,true);QVERIFY(accepted);
    // The observer only simulates the exact unsealed mkdir crash interval; all
    // actual source/target/control/document bytes remain the original preimages.
    interruptedDirectory=uncertain;const auto failed=storage::applyFileOperation(*lease,std::move(*plan),*accepted,interruptUnsealedReplacement);QVERIFY(!failed);QVERIFY(!storage::recoverFileOperation(*lease));const auto review=storage::prepareInterruptedReplacementReview(*lease);QVERIFY2(review.has_value(),review?"":review.error().message.c_str());
  }
  const auto beforeInventory=test::read(f.profile+"/downloads.config"),beforeState=test::read(f.stateFile);app::SessionBridge bridge;
  QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),native(&bridge,&app::SessionBridge::nativeSessionReady),plans(&bridge,&app::SessionBridge::fileOperationPrepared),results(&bridge,&app::SessionBridge::fileOperationFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(normal?bridge.openNormal(f.native()):bridge.open(f.native(),f.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(rows),10000);QCOMPARE(native.count(),0);QCOMPARE(test::read(f.stateFile),beforeState);QCOMPARE(test::read(f.profile+"/downloads.config"),beforeInventory);
  auto value=request(rows,sessions[0][0].toULongLong(),241,*f.hash,ui::FileOperation::ReviewReplacement);QVERIFY(bridge.prepareFileOperation(value));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);QVERIFY2(prepared(plans)->failure.isEmpty(),qPrintable(prepared(plans)->failure));const auto fresh=prepared(plans)->retainedPreimage;QVERIFY(!fresh.isEmpty()&&prepared(plans)->unsealedPreimageDirectory==uncertain&&!fresh.startsWith(uncertain+'/'));
  QVERIFY(bridge.executeFileOperation(copied(prepared(plans),false)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results),10000);QVERIFY(!finished(results)->completed);QCOMPARE(test::read(target),QByteArray("prior"));QCOMPARE(test::read(source),bytes);
  value=request(rows,sessions[0][0].toULongLong(),242,*f.hash,ui::FileOperation::ReviewReplacement);plans.clear();results.clear();QVERIFY(bridge.prepareFileOperation(value));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);QVERIFY(prepared(plans)->failure.isEmpty());QVERIFY(bridge.executeFileOperation(copied(prepared(plans),true)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results),10000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));QCOMPARE(finished(results)->retainedPreimage,fresh);QCOMPARE(test::read(fresh),QByteArray("prior"));QCOMPARE(test::read(target),bytes);QCOMPARE(test::read(uncertain+"/unknown"),QByteArray("preserved"));QVERIFY(!QFileInfo::exists(source));QCOMPARE(native.count(),0);
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());QVERIFY(storage::ProfileLease::acquire(f.native()));
 }

 void actualPausedAdmissionMovesOneFileAndRechecksWithoutChangingIntent() {
  test::OwnedEngineFixture f(true);edit(f,{{"state","i70e"},{"maxdl","i8192e"}});QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
  const auto destination=f.root.path()+QStringLiteral("/per-file");QVERIFY(QDir().mkpath(destination));const auto moved=destination+QStringLiteral("/chosen.bin");
  app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),traffic(&bridge,&app::SessionBridge::progressReady),plans(&bridge,&app::SessionBridge::fileOperationPrepared),results(&bridge,&app::SessionBridge::fileOperationFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.open(f.native(),f.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(rows),30000);QVERIFY(bridge.downloadAction({target(rows,*f.hash)},core::DownloadAction::Start));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);QTRY_VERIFY_WITH_TIMEOUT(completed(traffic,*f.hash)==test::OwnedEngineFixture::PayloadBytes,30000);
  QVERIFY(bridge.downloadAction({target(rows,*f.hash)},core::DownloadAction::Pause));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&item(rows,*f.hash)&&item(rows,*f.hash)->state==QStringLiteral("Paused"),10000);
  const auto original=f.payload+'/'+f.files[0].relative;const auto bytes=test::read(original);const auto sibling=f.payload+'/'+f.files.back().relative;const auto siblingBytes=test::read(sibling);const auto saved=inventory(f).downloads[0];
  auto value=request(rows,sessions[0][0].toULongLong(),180,*f.hash,ui::FileOperation::MoveFile,moved);value.originalIndex=0;
  QVERIFY(bridge.prepareFileOperation(value));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);QVERIFY2(prepared(plans)->failure.isEmpty(),qPrintable(prepared(plans)->failure));QCOMPARE(prepared(plans)->paths.size(),std::size_t(1));
  QVERIFY(bridge.executeFileOperation(copied(prepared(plans))));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results),15000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));QVERIFY2(finished(results)->failure.isEmpty(),qPrintable(finished(results)->failure));
  QCOMPARE(test::read(moved),bytes);QVERIFY(!QFileInfo::exists(original));QCOMPARE(test::read(sibling),siblingBytes);sameInode(moved,f.files[0].before);
  const auto current=inventory(f);QCOMPARE(current.downloads[0].saveDirectory,saved.saveDirectory);QCOMPARE(current.downloads[0].saveFile,saved.saveFile);QCOMPARE(current.downloads[0].forceStart,saved.forceStart);QCOMPARE(current.paused.size(),std::size_t(1));QCOMPARE(current.paused[0].hash,*f.hash);QTRY_VERIFY_WITH_TIMEOUT(item(rows,*f.hash)&&item(rows,*f.hash)->state==QStringLiteral("Paused"),10000);
  const auto state=parse(test::read(f.stateFile));const auto* links=state.find("attributes")->find("filelinks2");QVERIFY(links);QVERIFY(std::ranges::any_of(links->children,[&](const core::Node& n){return n.type==core::Node::Type::Bytes&&n.bytes.ends_with(moved.toStdString());}));QCOMPARE(state.find("resume")->find("data")->find("valid")->integer,std::int64_t(0));
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());QVERIFY(storage::ProfileLease::acquire(f.native()));
 }
 void explicitReplacementWorkerRequiresReviewedConsentAndRetainsPriorFile() {
  test::OwnedEngineFixture f(false);edit(f,{{"state","i70e"}});const auto source=f.payload+'/'+f.files[0].relative;const auto bytes=test::read(source);const auto target=f.root.path()+QStringLiteral("/occupied.bin");test::write(target,"prior destination");
  app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),plans(&bridge,&app::SessionBridge::fileOperationPrepared),results(&bridge,&app::SessionBridge::fileOperationFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.open(f.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(rows),10000);auto value=request(rows,sessions[0][0].toULongLong(),211,*f.hash,ui::FileOperation::ReplaceFile,target);value.originalIndex=0;
  QVERIFY(bridge.prepareFileOperation(value));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);QVERIFY2(prepared(plans)->failure.isEmpty(),qPrintable(prepared(plans)->failure));const auto retained=prepared(plans)->retainedPreimage;QVERIFY(!retained.isEmpty()&&prepared(plans)->priorDestinationSha256.size()==64);QVERIFY(!QFileInfo::exists(retained));
  QVERIFY(bridge.executeFileOperation(copied(prepared(plans),false)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results),10000);QVERIFY(!finished(results)->completed);QCOMPARE(test::read(source),bytes);QCOMPARE(test::read(target),QByteArray("prior destination"));
  value=request(rows,sessions[0][0].toULongLong(),212,*f.hash,ui::FileOperation::ReplaceFile,target);value.originalIndex=0;plans.clear();results.clear();QVERIFY(bridge.prepareFileOperation(value));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);QVERIFY(prepared(plans)->failure.isEmpty());QVERIFY(bridge.executeFileOperation(copied(prepared(plans),true)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results),10000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));QCOMPARE(finished(results)->retainedPreimage,retained);QCOMPARE(test::read(target),bytes);QCOMPARE(test::read(retained),QByteArray("prior destination"));QVERIFY(!QFileInfo::exists(source));
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());QVERIFY(storage::ProfileLease::acquire(f.native()));
 }
 void originalRecycleDefaultUsesOnlyGeneratedTrashAndRequiresConfirmation() {
  test::OwnedEngineFixture f(false);edit(f,{{"state","i70e"}});const auto data=f.root.path()+QStringLiteral("/xdg-data");QVERIFY(QDir().mkpath(data));
  struct Environment {QByteArray previous=qgetenv("XDG_DATA_HOME");bool present=qEnvironmentVariableIsSet("XDG_DATA_HOME");~Environment(){if(present){qputenv("XDG_DATA_HOME",previous);}else{qunsetenv("XDG_DATA_HOME");}}} environment;
  QVERIFY(qputenv("XDG_DATA_HOME",data.toUtf8()));const auto source=f.payload+'/'+f.files[0].relative;const auto bytes=test::read(source);
  app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),plans(&bridge,&app::SessionBridge::fileOperationPrepared),results(&bridge,&app::SessionBridge::fileOperationFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.open(f.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(rows),10000);
  auto value=request(rows,sessions[0][0].toULongLong(),190,*f.hash,ui::FileOperation::RemoveDeleteData);value.deleteTorrentFile=false;value.recyclePayload.reset();
  QVERIFY(bridge.prepareFileOperation(value));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);QVERIFY2(prepared(plans)->failure.isEmpty(),qPrintable(prepared(plans)->failure));QVERIFY(prepared(plans)->recyclePayload);QVERIFY(!QFileInfo::exists(data+QStringLiteral("/Trash")));QCOMPARE(prepared(plans)->paths.size(),std::size_t(1));const auto trashData=prepared(plans)->paths[0].destination;QVERIFY(trashData.startsWith(data+QStringLiteral("/Trash/files/")));
  QVERIFY(bridge.executeFileOperation(copied(prepared(plans),false)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results),10000);QVERIFY(!finished(results)->completed);QCOMPARE(test::read(source),bytes);QVERIFY(!QFileInfo::exists(data+QStringLiteral("/Trash")));
  value=request(rows,sessions[0][0].toULongLong(),191,*f.hash,ui::FileOperation::RemoveDeleteData);value.deleteTorrentFile=false;value.recyclePayload.reset();plans.clear();results.clear();QVERIFY(bridge.prepareFileOperation(value));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);QVERIFY(prepared(plans)->failure.isEmpty());QVERIFY(bridge.executeFileOperation(copied(prepared(plans),true)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results),10000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));QCOMPARE(test::read(trashData),bytes);QVERIFY(!QFileInfo::exists(source));
  const auto info=data+QStringLiteral("/Trash/info/")+QFileInfo(trashData).fileName()+QStringLiteral(".trashinfo");QVERIFY(test::read(info).startsWith("[Trash Info]\nPath="));QVERIFY(inventory(f).downloads.empty());
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());QVERIFY(storage::ProfileLease::acquire(f.native()));
 }

 void interruptedKeepDataStartupIsReadOnlyAndNeverReplayed_data(){QTest::addColumn<bool>("normal");QTest::newRow("preview")<<false;QTest::newRow("normal")<<true;}
 void interruptedKeepDataStartupIsReadOnlyAndNeverReplayed(){
  QFETCH(bool,normal);test::OwnedEngineFixture f(false);edit(f,{{"state","i75e"}});
  {auto lease=storage::ProfileLease::acquire(f.native());QVERIFY(lease);auto plan=storage::prepareFileOperation(*lease,{*f.hash,storage::FileOperationKind::RemoveKeepData,{}});QVERIFY(plan);auto confirmed=storage::confirmFileOperation(*plan,plan->preview().approvalScopeDigest,false);QVERIFY(confirmed);QVERIFY(!storage::applyFileOperation(*lease,std::move(*plan),*confirmed,interruptKeepData));QVERIFY(storage::keepDataRemovalNeedsReview(*lease));}
  const auto before=test::read(f.profile+"/downloads.config"),state=test::read(f.stateFile),journal=test::read(f.profile+"/BiglyBTC.file-operation"),payload=test::read(f.payload+'/'+f.files[0].relative);
  app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),native(&bridge,&app::SessionBridge::nativeSessionReady),plans(&bridge,&app::SessionBridge::fileOperationPrepared),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(normal?bridge.openNormal(f.native()):bridge.open(f.native(),f.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(rows),10000);QCOMPARE(native.count(),0);QVERIFY(item(rows,*f.hash));
  QVERIFY(bridge.prepareFileOperation(request(rows,sessions[0][0].toULongLong(),901,*f.hash,ui::FileOperation::RemoveKeepData)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);QVERIFY(!prepared(plans)->failure.isEmpty());
  QCOMPARE(test::read(f.profile+"/downloads.config"),before);QCOMPARE(test::read(f.stateFile),state);QCOMPARE(test::read(f.profile+"/BiglyBTC.file-operation"),journal);QCOMPARE(test::read(f.payload+'/'+f.files[0].relative),payload);
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());QVERIFY(storage::ProfileLease::acquire(f.native()));QCOMPARE(test::read(f.profile+"/BiglyBTC.file-operation"),journal);
 }

 void actualWindowKeepDataUsesExistingReviewAndCancelWithoutOptimisticRemoval(){
  test::OwnedEngineFixture f(false);edit(f,{{"state","i70e"}});const auto payload=test::read(f.payload+'/'+f.files[0].relative),before=test::read(f.profile+"/downloads.config");
  app::SessionBridge bridge;ui::LibraryWindow window;window.setOfflinePreview(false);window.show();
  connect(&bridge,&app::SessionBridge::snapshotReady,&window,&ui::LibraryWindow::setPresentation);connect(&bridge,&app::SessionBridge::columnLayoutSessionReady,&window,&ui::LibraryWindow::beginColumnLayoutSession);connect(&bridge,&app::SessionBridge::busyChanged,&window,&ui::LibraryWindow::setBusy);
  connect(&bridge,&app::SessionBridge::fileOperationPrepared,&window,&ui::LibraryWindow::setFileOperationPlan);connect(&bridge,&app::SessionBridge::fileOperationFinished,&window,&ui::LibraryWindow::setFileOperationResult);
  connect(&window,&ui::LibraryWindow::fileOperationPlanRequested,&bridge,[&](ui::FileOperationRequest r){if(!bridge.prepareFileOperation(r)){window.rejectFileOperationRequest(r);}});
  connect(&window,&ui::LibraryWindow::fileOperationExecuteRequested,&bridge,[&](ui::FileOperationSubmission r){if(!bridge.executeFileOperation(r)){window.rejectFileOperationSubmission(r);}});
  connect(&window,&ui::LibraryWindow::fileOperationCancelRequested,&bridge,&app::SessionBridge::cancelFileOperation);
  QSignalSpy results(&bridge,&app::SessionBridge::fileOperationFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.open(f.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&window.model().rowCount()==1,10000);window.table().selectRow(0);window.table().setCurrentIndex(window.model().index(0,0));
  auto* remove=window.findChild<QAction*>("classic-remove");QVERIFY(remove);QTRY_VERIFY(remove->isEnabled());QCOMPARE(remove->shortcut(),QKeySequence(QStringLiteral("Delete")));remove->trigger();
  QPointer<ui::FileOperationDialog> dialog=window.findChild<ui::FileOperationDialog*>();QVERIFY(dialog);QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&dialog&&dialog->plan(),10000);QVERIFY(!dialog->plan()->deleteTorrentFile);
  dialog->findChild<QPushButton*>("file-operation-cancel")->click();QTRY_VERIFY(dialog.isNull());QCOMPARE(results.count(),0);QCOMPARE(window.model().rowCount(),1);QCOMPARE(test::read(f.profile+"/downloads.config"),before);QCOMPARE(test::read(f.payload+'/'+f.files[0].relative),payload);
  remove->trigger();dialog=window.findChild<ui::FileOperationDialog*>();QVERIFY(dialog);QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&dialog&&dialog->plan(),10000);dialog->findChild<QPushButton*>("file-operation-execute")->click();
  QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results)&&window.model().rowCount()==0,10000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));QCOMPARE(test::read(f.payload+'/'+f.files[0].relative),payload);QVERIFY(!QFileInfo::exists(f.stateFile));
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());
 }
 void selectedPrivateOrMixedRouteCannotBorrowPublicRemoval_data(){QTest::addColumn<QByteArray>("networks");QTest::newRow("pure")<<QByteArray("l3:I2Pe");QTest::newRow("mixed")<<QByteArray("l6:Public3:I2Pe");}
 void selectedPrivateOrMixedRouteCannotBorrowPublicRemoval(){
  QFETCH(QByteArray,networks);test::OwnedEngineFixture f(false);edit(f,{{"state","i70e"}});auto state=parse(test::read(f.stateFile));auto attrs=core::Document::parse(encoded(*state.find("attributes")),storage::compatibleConfigLimits());QVERIFY(attrs);QVERIFY(attrs->replaceValue("networks",networks.toStdString()));QVERIFY(state.replaceValue("attributes",attrs->serialize()));test::write(f.stateFile,QByteArray::fromStdString(state.serialize()));
  const auto before=test::read(f.stateFile),inventoryBytes=test::read(f.profile+"/downloads.config"),payload=test::read(f.payload+'/'+f.files[0].relative);app::SessionBridge bridge;
  QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),plans(&bridge,&app::SessionBridge::fileOperationPrepared),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.open(f.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&latest(rows),10000);QVERIFY(bridge.prepareFileOperation(request(rows,sessions[0][0].toULongLong(),950,*f.hash,ui::FileOperation::RemoveKeepData)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);QVERIFY(!prepared(plans)->failure.isEmpty());
  QCOMPARE(test::read(f.stateFile),before);QCOMPARE(test::read(f.profile+"/downloads.config"),inventoryBytes);QCOMPARE(test::read(f.payload+'/'+f.files[0].relative),payload);QVERIFY(!QFileInfo::exists(f.profile+"/BiglyBTC.file-operation"));QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);
 }

 void actualNormalWindowTransferRemovalMovesOnlyExistingHistory_data(){QTest::addColumn<int>("historyMode");QTest::newRow("enabled-actual-active-to-dead")<<0;QTest::newRow("unicode-common-literal-names-and-groups")<<3;QTest::newRow("disabled-no-event-write")<<1;QTest::newRow("corrupt-auxiliary-warning-removal-stays-saved")<<2;}
 void actualNormalWindowTransferRemovalMovesOnlyExistingHistory(){
  QFETCH(int,historyMode);test::OwnedEngineFixture f(false,true);edit(f,{{"state","i70e"},{"maxdl","i8192e"}});QTRY_VERIFY_WITH_TIMEOUT(f.ready(),30000);
  QTcpServer tracker;QVERIFY(tracker.listen(QHostAddress::LocalHost,0));int announces=0;bool malformed=false;
  connect(&tracker,&QTcpServer::newConnection,&tracker,[&]{while(tracker.hasPendingConnections()){
    auto* socket=tracker.nextPendingConnection();auto pending=std::make_shared<QByteArray>();connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    connect(socket,&QTcpSocket::readyRead,socket,[&,socket,pending]{*pending+=socket->readAll();if(pending->size()>16384){malformed=true;socket->abort();return;}if(!pending->contains("\r\n\r\n")){return;}if(!pending->startsWith("GET /announce?")||!pending->contains("info_hash=")){malformed=true;socket->abort();return;}++announces;QByteArray peer("\x7f\0\0\x01",4);peer.append(static_cast<char>(f.port()>>8));peer.append(static_cast<char>(f.port()));const QByteArray body="d8:intervali3600e5:peers6:"+peer+'e';socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();pending->clear();});
  }});
  auto state=parse(test::read(f.stateFile));auto attrs=core::Document::parse(encoded(*state.find("attributes")),storage::compatibleConfigLimits());QVERIFY(attrs);QVERIFY(attrs->replaceValue("stats.download.added.time","i42000e"));QVERIFY(attrs->replaceValue("parameters","d25:stats.download.added.timei51000ee"));QVERIFY(state.replaceValue("attributes",attrs->serialize()));const auto url=QStringLiteral("http://127.0.0.1:%1/announce").arg(tracker.serverPort()).toUtf8();QVERIFY(state.replaceValue("announce",std::to_string(url.size())+':'+url.toStdString()));test::write(f.stateFile,QByteArray::fromStdString(state.serialize()));
  libtorrent::entry config(libtorrent::entry::dictionary_type{});config["Bind IP"]="127.0.0.1";config["Enforce Bind IP"]=1;
  for(const auto* field:{"TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Server Enable UDP","IPV6 Enable Support","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable"}){config[field]=0;}
  config["Peer Source Selection Default.Tracker"]=1;config["Tracker Client Enable TCP"]=1;config["Download History Enabled"]=historyMode==1?0:1;config["opaque.history.remove"]=std::string("\0\xffR",3);test::write(f.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(config)));
  const auto cfg=parse(test::read(f.profile+"/biglybt.config"));const auto policy=engine::readNetworkPolicy(&cfg,{.mode=engine::NetworkMode::Normal});QVERIFY(policy);QVERIFY(policy->bindAddresses==std::vector<std::string>{"127.0.0.1"}&&policy->outgoingTcp&&policy->httpTrackers&&!policy->incomingTcp&&!policy->incomingUtp&&!policy->outgoingUtp&&!policy->udpTrackers&&!policy->dht&&!policy->pex&&!policy->lsd&&!policy->upnp&&!policy->natpmp);
  const auto hash=f.hash->bytes();const std::string rawHash(reinterpret_cast<const char*>(hash.data()),hash.size());core::DownloadHistoryNewRecord old;old.uid=core::originalDownloadHistoryUid(rawHash,42000);old.hash=rawHash;old.name="Existing genuine active fixture";old.saveLocation=(f.payload+'/'+f.files[0].relative).toStdString();old.size=test::OwnedEngineFixture::PayloadBytes;old.addTime=51000;old.completeTime=123;old.tags={"historic-tag"};const auto history=core::editSavedDownloadHistory("d6:opaque3:rawe",core::DownloadHistoryAppend{old});QVERIFY(history);
  auto active=core::Document::parse(history->value.source->originalBytes());QVERIFY(active);const auto& row=active->find("records")->children.front();auto record=core::Document::parse(active->originalBytes().substr(row.begin,row.end-row.begin));QVERIFY(record);QVERIFY(record->replaceValue("opaque.removal","d4:keep2:"+std::string("\0\xff",2)+"e"));QVERIFY(active->replaceValue("records","l"+record->serialize()+"e"));const auto activeBefore=QByteArray::fromStdString(active->serialize());test::write(f.profile+"/dlhistorya.config",historyMode==2?QByteArray("not a history dictionary"):activeBefore);test::write(f.profile+"/dlhistoryd.config","d6:opaque4:deade");
  removalTagFixture(f,historyMode==3);const auto tagsBefore=test::read(f.profile+"/tag.config");
  app::SessionBridge bridge;ui::LibraryWindow window;window.setOfflinePreview(false);window.show();
  connect(&bridge,&app::SessionBridge::snapshotReady,&window,&ui::LibraryWindow::setPresentation);connect(&bridge,&app::SessionBridge::columnLayoutSessionReady,&window,&ui::LibraryWindow::beginColumnLayoutSession);connect(&bridge,&app::SessionBridge::busyChanged,&window,&ui::LibraryWindow::setBusy);
  connect(&bridge,&app::SessionBridge::fileOperationPrepared,&window,&ui::LibraryWindow::setFileOperationPlan);connect(&bridge,&app::SessionBridge::fileOperationFinished,&window,&ui::LibraryWindow::setFileOperationResult);
  connect(&window,&ui::LibraryWindow::fileOperationPlanRequested,&bridge,[&](ui::FileOperationRequest r){if(!bridge.prepareFileOperation(r)){window.rejectFileOperationRequest(r);}});connect(&window,&ui::LibraryWindow::fileOperationExecuteRequested,&bridge,[&](ui::FileOperationSubmission r){if(!bridge.executeFileOperation(r)){window.rejectFileOperationSubmission(r);}});connect(&window,&ui::LibraryWindow::fileOperationCancelRequested,&bridge,&app::SessionBridge::cancelFileOperation);
  QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),traffic(&bridge,&app::SessionBridge::progressReady),results(&bridge,&app::SessionBridge::fileOperationFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.openNormal(f.native()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(rows)&&item(rows,*f.hash),30000);QVERIFY(bridge.downloadAction({target(rows,*f.hash)},core::DownloadAction::Start));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&completed(traffic,*f.hash)>0,30000);QVERIFY(announces>0&&!malformed);
  window.table().selectRow(0);window.table().setCurrentIndex(window.model().index(0,0));auto* remove=window.findChild<QAction*>("classic-remove");QVERIFY(remove);QTRY_VERIFY(remove->isEnabled());remove->trigger();QPointer<ui::FileOperationDialog> dialog=window.findChild<ui::FileOperationDialog*>();QVERIFY(dialog);QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&dialog&&dialog->plan(),10000);QVERIFY(!dialog->plan()->deleteTorrentFile);const auto before=QDateTime::currentMSecsSinceEpoch();dialog->findChild<QPushButton*>("file-operation-execute")->click();
  QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results)&&window.model().rowCount()==0,15000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));QVERIFY(!QFileInfo::exists(f.stateFile));QVERIFY(inventory(f).downloads.empty());for(const auto& file:f.files){sameInode(f.payload+'/'+file.relative,file.before);}const auto retiredPayload=test::read(f.payload+'/'+f.files[0].relative);
  if(historyMode==0||historyMode==3){QVERIFY2(finished(results)->failure.isEmpty(),qPrintable(finished(results)->failure));const auto a=core::readSavedDownloadHistory(test::read(f.profile+"/dlhistorya.config").toStdString()),d=core::readSavedDownloadHistory(test::read(f.profile+"/dlhistoryd.config").toStdString());QVERIFY(a&&d);QVERIFY(a->records.empty());QCOMPARE(d->records.size(),std::size_t{1});const auto& removed=d->records.front();QCOMPARE(removed.uid,old.uid);QVERIFY(removed.hash==old.hash);QCOMPARE(removed.addTime,old.addTime);const std::vector<std::string> expectedTags=historyMode==3?std::vector<std::string>{"Ω","😀2","😀10","тег2","Тег10","σ2","Σ10","İ2","i10"}:std::vector<std::string>{"First","Disk2","Disk10","B1","A1","A1"};QVERIFY(removed.tags==expectedTags);QVERIFY(removed.removeTime>=before&&removed.removeTime<=QDateTime::currentMSecsSinceEpoch());const auto* opaque=d->source->find("records")->children[0].find("opaque.removal");QVERIFY(opaque);QCOMPARE(d->source->originalBytes().substr(opaque->begin,opaque->end-opaque->begin),"d4:keep2:"+std::string("\0\xff",2)+"e");QCOMPARE(d->source->find("opaque")->bytes,std::string_view("dead"));}
  else if(historyMode==1){QCOMPARE(test::read(f.profile+"/dlhistorya.config"),activeBefore);QCOMPARE(test::read(f.profile+"/dlhistoryd.config"),QByteArray("d6:opaque4:deade"));}
  else{QVERIFY(!finished(results)->failure.isEmpty());QCOMPARE(test::read(f.profile+"/dlhistorya.config"),QByteArray("not a history dictionary"));QCOMPARE(test::read(f.profile+"/dlhistoryd.config"),QByteArray("d6:opaque4:deade"));}
  QCOMPARE(test::read(f.profile+"/tag.config"),tagsBefore);
  const auto deadBefore=test::read(f.profile+"/dlhistoryd.config");QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());QVERIFY(bridge.openNormal(f.native()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(rows)&&latest(rows)->items.empty(),15000);QCOMPARE(test::read(f.payload+'/'+f.files[0].relative),retiredPayload);QCOMPARE(test::read(f.profile+"/dlhistoryd.config"),deadBefore);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),2,15000);QVERIFY(storage::ProfileLease::acquire(f.native()));
 }

 void actualRemovalTagCapturePreservesPreviousTagsWhenUnavailable_data(){
  QTest::addColumn<int>("scenario");QTest::newRow("disabled-startup-mode-not-current-toggle")<<0;QTest::newRow("empty-list-preserves-old")<<1;QTest::newRow("localized-name-unavailable")<<2;QTest::newRow("unicode-reference-properties-unavailable")<<3;QTest::newRow("case-equal-order-ambiguous")<<4;QTest::newRow("saving-candidate-read-only")<<5;QTest::newRow("externally-stale-actual-store")<<6;QTest::newRow("malformed-whole-namespace")<<7;QTest::newRow("more-than-history-bound-never-truncates")<<8;QTest::newRow("original-invalid-property-skips-member")<<9;QTest::newRow("aliased-canonical-group-lookup-unavailable")<<10;QTest::newRow("malformed-utf8-name-preserved")<<11;QTest::newRow("group-reference-properties-unavailable")<<12;QTest::newRow("unicode-case-equal-order-ambiguous")<<13;
 }
 void actualRemovalTagCapturePreservesPreviousTagsWhenUnavailable(){
  QFETCH(int,scenario);test::OwnedEngineFixture f(false);edit(f,{{"state","i70e"}});auto state=parse(test::read(f.stateFile));auto attrs=core::Document::parse(encoded(*state.find("attributes")),storage::compatibleConfigLimits());QVERIFY(attrs);QVERIFY(attrs->replaceValue("stats.download.added.time","i42000e"));QVERIFY(attrs->replaceValue("parameters","d25:stats.download.added.timei51000ee"));QVERIFY(state.replaceValue("attributes",attrs->serialize()));test::write(f.stateFile,QByteArray::fromStdString(state.serialize()));
  libtorrent::entry settings(libtorrent::entry::dictionary_type{});settings["Bind IP"]="127.0.0.1";settings["Enforce Bind IP"]=1;for(const auto* key:{"TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Server Enable UDP","Peer Source Selection Default.Tracker","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable"}){settings[key]=0;}if(scenario==0){settings["tagmanager.enable"]=0;}test::write(f.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(settings)));
  if(scenario!=1){libtorrent::entry tags(libtorrent::entry::dictionary_type{});const auto member=savedMember(*f.hash);const auto add=[&](std::string id,std::string name){tags["3"][id]["n"]=std::move(name);tags["3"][id]["o"]=libtorrent::entry::list_type{libtorrent::entry(member)};};add("1",scenario==2?"tag.completed":scenario==3?std::string("\xea\x9f\x8b"):scenario==11?std::string("\xc0\xaf"):scenario==13?"ТЕГ":"A");if(scenario==4){add("2","a");}if(scenario==12){tags["3"]["1"]["c"]["gr"]=std::string("\xea\x9f\x8b");}if(scenario==13){add("2","тег");}if(scenario==7){tags["3"]["2"]["n"]="unrelated malformed";tags["3"]["2"]["o"]=1;}if(scenario==8){for(int n=2;n<=257;++n){add(std::to_string(n),"Tag"+std::to_string(n));}}if(scenario==9){tags["3"]["1"]["p"]=libtorrent::entry::list_type{libtorrent::entry(1)};}if(scenario==10){tags["3"].dict().erase("1");add("01","A");tags["3"]["01"]["c"]["gr"]="not-original-canonical-group";}test::write(f.profile+"/tag.config",QByteArray::fromStdString(test::encoded(tags)));if(scenario==5){test::write(f.profile+"/tag.config.saving","preserved uncertainty");}}
  const auto hash=f.hash->bytes();const std::string raw(reinterpret_cast<const char*>(hash.data()),hash.size());core::DownloadHistoryNewRecord old;old.uid=core::originalDownloadHistoryUid(raw,42000);old.hash=raw;old.name="Old active";old.saveLocation=(f.payload+'/'+f.files[0].relative).toStdString();old.size=test::OwnedEngineFixture::PayloadBytes;old.addTime=51000;old.tags={"prior-tag"};const auto active=core::editSavedDownloadHistory("de",core::DownloadHistoryAppend{old});QVERIFY(active);test::write(f.profile+"/dlhistorya.config",QByteArray::fromStdString(active->value.source->originalBytes()));
  app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),plans(&bridge,&app::SessionBridge::fileOperationPrepared),results(&bridge,&app::SessionBridge::fileOperationFinished),closed(&bridge,&app::SessionBridge::sessionClosed),tagReads(&bridge,&app::SessionBridge::manualTagCommandFinished);
  QVERIFY(bridge.openNormal(f.native()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&item(rows,*f.hash),15000);
  if(scenario==0){settings["tagmanager.enable"]=1;test::write(f.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(settings)));}
  if(scenario==6){ui::ManualTagCommand read;read.session=sessions[0][0].toULongLong();read.request=91001;read.kind=ui::ManualTagCommandKind::Reload;QVERIFY(bridge.manualTagCommand(read));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!tagReads.empty(),10000);auto changed=parse(test::read(f.profile+"/tag.config"));QVERIFY(changed.replaceValue("opaque.external","i1e"));test::write(f.profile+"/tag.config",QByteArray::fromStdString(changed.serialize()));}
  const auto tagBefore=QFileInfo::exists(f.profile+"/tag.config")?test::read(f.profile+"/tag.config"):QByteArray{};
  QVERIFY(bridge.prepareFileOperation(request(rows,sessions[0][0].toULongLong(),92001,*f.hash,ui::FileOperation::RemoveKeepData)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);QVERIFY2(prepared(plans)->failure.isEmpty(),qPrintable(prepared(plans)->failure));QVERIFY(bridge.executeFileOperation(copied(prepared(plans))));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results),10000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));
  const auto dead=core::readSavedDownloadHistory(test::read(f.profile+"/dlhistoryd.config").toStdString());QVERIFY(dead);QCOMPARE(dead->records.size(),std::size_t{1});QVERIFY(dead->records[0].tags==old.tags);QVERIFY(!QFileInfo::exists(f.stateFile));QVERIFY(inventory(f).downloads.empty());if(scenario>=2){QVERIFY(!finished(results)->failure.isEmpty());}else{QVERIFY2(finished(results)->failure.isEmpty(),qPrintable(finished(results)->failure));}
  if(scenario!=1){QCOMPARE(test::read(f.profile+"/tag.config"),tagBefore);}else{QVERIFY(!QFileInfo::exists(f.profile+"/tag.config"));}if(scenario==5){QCOMPARE(test::read(f.profile+"/tag.config.saving"),QByteArray("preserved uncertainty"));}
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());QVERIFY(storage::ProfileLease::acquire(f.native()));
 }
 void actualFull32RemovalUsesPreparedSelectedOriginalTaggable_data(){
  QTest::addColumn<int>("scenario");QTest::newRow("pure-v2-selected20-full32-v")<<0;QTest::newRow("hybrid-default-v1-is-not-v2-prefix")<<1;QTest::newRow("hybrid-selected-v2-keeps-v1-history-h")<<2;QTest::newRow("full32-unavailable-tag-retains-prior-after-move")<<3;QTest::newRow("selected20-alias-cannot-borrow-history-identity")<<4;
 }
 void actualFull32RemovalUsesPreparedSelectedOriginalTaggable(){
  QFETCH(int,scenario);const auto& proof=test::Full32TagFixtures[scenario<3?scenario:0];test::OwnedEngineFixture f(false);
  const auto decode=[](std::string_view hex){return QByteArray::fromHex(QByteArray(hex.data(),static_cast<int>(hex.size())));};const auto full=decode(proof.fullV2),selected=decode(proof.selected),v1=decode(proof.fullV1);
  const auto runtime=core::TorrentHash::parse(std::string_view(full.constData(),static_cast<std::size_t>(full.size()))),original=core::TorrentHash::parse(std::string_view(selected.constData(),static_cast<std::size_t>(selected.size())));QVERIFY(runtime&&original&&runtime->bytes().size()==32&&original->bytes().size()==20);
  auto state=parse(QByteArray(proof.metainfo.data(),static_cast<int>(proof.metainfo.size())));const auto prior=parse(test::read(f.stateFile));auto attrs=core::Document::parse(encoded(*prior.find("attributes")),storage::compatibleConfigLimits());QVERIFY(attrs);QVERIFY(attrs->replaceValue("stats.download.added.time","i42000e"));QVERIFY(attrs->replaceValue("parameters","d25:stats.download.added.timei51000ee"));QVERIFY(state.replaceValue("attributes",attrs->serialize()));
  QVERIFY(QFile::remove(f.stateFile));f.hash=*runtime;f.stateFile=f.profile+"/active/"+QString::fromStdString(runtime->hex())+".dat";test::write(f.stateFile,QByteArray::fromStdString(state.serialize()));
  QByteArray payload(4097,'\0');for(qsizetype n=0;n<payload.size();++n){payload[n]=static_cast<char>(n*17+3);}test::write(f.payload+'/'+f.files[0].relative,payload);f.files[0].expected=payload;
  auto inventoryDoc=parse(test::read(f.profile+"/downloads.config"));auto row=parse(QByteArray::fromStdString(encoded(inventoryDoc.find("downloads")->children[0])));QVERIFY(row.replaceValue("torrent_hash",std::to_string(full.size())+":"+full.toStdString()));QVERIFY(row.replaceValue("torrent",std::to_string(f.stateFile.toUtf8().size())+":"+f.stateFile.toUtf8().toStdString()));QVERIFY(row.replaceValue("state","i70e"));std::string rows="l"+row.serialize();
  if(scenario==4){auto alias=row;const auto aliasPath=f.profile+"/active/"+QString::fromStdString(original->hex())+".dat";test::write(aliasPath,QByteArray::fromStdString(state.serialize()));QVERIFY(alias.replaceValue("torrent_hash",std::to_string(selected.size())+":"+selected.toStdString()));QVERIFY(alias.replaceValue("torrent",std::to_string(aliasPath.toUtf8().size())+":"+aliasPath.toUtf8().toStdString()));rows+=alias.serialize();}rows+='e';QVERIFY(inventoryDoc.replaceValue("downloads",rows));test::write(f.profile+"/downloads.config",QByteArray::fromStdString(inventoryDoc.serialize()));
  libtorrent::entry settings(libtorrent::entry::dictionary_type{});settings["Bind IP"]="127.0.0.1";settings["Enforce Bind IP"]=1;for(const auto* key:{"TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Server Enable UDP","Peer Source Selection Default.Tracker","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable"}){settings[key]=0;}test::write(f.profile+"/biglybt.config",QByteArray::fromStdString(test::encoded(settings)));
  libtorrent::entry tags(libtorrent::entry::dictionary_type{});tags["3"]["1"]["n"]=scenario==3?std::string("\xc0\xaf"):std::string("Original selected tag");tags["3"]["1"]["o"]=libtorrent::entry::list_type{libtorrent::entry(savedMember(*original))};tags["3"]["2"]["n"]="Wrong full32 runtime ID";tags["3"]["2"]["o"]=libtorrent::entry::list_type{libtorrent::entry(savedMember(*runtime))};const auto tagBytes=QByteArray::fromStdString(test::encoded(tags));test::write(f.profile+"/tag.config",tagBytes);
  core::DownloadHistoryNewRecord old;old.uid=core::originalDownloadHistoryUid(selected.toStdString(),42000);old.hash=(v1.isEmpty()?selected:v1).toStdString();old.hashV2=full.toStdString();old.name="Old full32 active";old.saveLocation=(f.payload+'/'+f.files[0].relative).toStdString();old.size=4097;old.addTime=51000;old.tags={"prior-tag"};const auto active=core::editSavedDownloadHistory("de",core::DownloadHistoryAppend{old});QVERIFY(active);const auto activeBytes=QByteArray::fromStdString(active->value.source->originalBytes());test::write(f.profile+"/dlhistorya.config",activeBytes);
  app::SessionBridge bridge;QSignalSpy views(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),plans(&bridge,&app::SessionBridge::fileOperationPrepared),results(&bridge,&app::SessionBridge::fileOperationFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.openNormal(f.native()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&item(views,*runtime),15000);QVERIFY(bridge.prepareFileOperation(request(views,sessions[0][0].toULongLong(),93001,*runtime,ui::FileOperation::RemoveKeepData)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&prepared(plans),10000);QVERIFY2(prepared(plans)->failure.isEmpty(),qPrintable(prepared(plans)->failure));QVERIFY(bridge.executeFileOperation(copied(prepared(plans))));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results),10000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));
  QVERIFY(!QFileInfo::exists(f.stateFile));QCOMPARE(test::read(f.payload+'/'+f.files[0].relative),payload);QCOMPARE(test::read(f.profile+"/tag.config"),tagBytes);
  if(scenario==4){QCOMPARE(test::read(f.profile+"/dlhistorya.config"),activeBytes);QVERIFY(!QFileInfo::exists(f.profile+"/dlhistoryd.config"));QVERIFY(!finished(results)->failure.isEmpty());QCOMPARE(inventory(f).downloads.size(),std::size_t(1));}
  else{const auto dead=core::readSavedDownloadHistory(test::read(f.profile+"/dlhistoryd.config").toStdString());QVERIFY(dead&&dead->records.size()==1);const auto& moved=dead->records[0];QCOMPARE(moved.uid,old.uid);QVERIFY(moved.hash==old.hash&&moved.hashV2==old.hashV2);QVERIFY(moved.tags==(scenario==3?old.tags:std::vector<std::string>{"Original selected tag"}));QCOMPARE(!finished(results)->failure.isEmpty(),scenario==3);QVERIFY(inventory(f).downloads.empty());}
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());QVERIFY(storage::ProfileLease::acquire(f.native()));
 }

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
  QVERIFY(dialog.findChild<QPushButton*>("file-operation-cancel")->isDefault());QVERIFY(!dialog.findChild<QPushButton*>("file-operation-execute")->autoDefault());
  QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&dialog.plan()&&prepared(plans),10000);QVERIFY(prepared(plans)->failure.isEmpty());QVERIFY(dialog.plan()!=prepared(plans));
  QCOMPARE(prepared(plans)->presentFiles,quint64(1));QCOMPARE(prepared(plans)->missingFiles,quint64(0));QCOMPARE(prepared(plans)->totalBytes,quint64(test::OwnedEngineFixture::PayloadBytes));
  QCOMPARE(test::read(f.profile+"/downloads.config"),beforeInventory);QCOMPARE(test::read(f.stateFile),beforeState);
  sameInode(f.profile+"/downloads.config",inventoryStamp);sameInode(f.stateFile,stateStamp);
  QTRY_VERIFY_WITH_TIMEOUT(completed(traffic,*f.hash)>beforeProgress,15000);QVERIFY(item(rows,*f.hash));QVERIFY(item(rows,*f.hash)->state!=QStringLiteral("Stopped"));
  dialog.accept();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished(results),15000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));QVERIFY(!dialog.isVisible());
  QVERIFY(inventory(f).downloads.empty());for(const auto& file:f.files){sameInode(f.payload+'/'+file.relative,file.before);}QVERIFY(!QFileInfo::exists(f.stateFile));
  const auto retiredBytes=test::read(f.payload+'/'+f.files[0].relative);
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());
  QVERIFY(bridge.open(f.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&latest(rows)&&latest(rows)->items.empty(),10000);QCOMPARE(test::read(f.payload+'/'+f.files[0].relative),retiredBytes);
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),2,10000);auto lease=storage::ProfileLease::acquire(f.native());QVERIFY(lease);
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
  const auto plan=prepared(plans);QVERIFY2(plan->failure.isEmpty(),qPrintable(plan->failure));QVERIFY(!plan->deleteTorrentFile);QVERIFY(plan->archivePath.isEmpty());
  auto explicitArchive=request(rows,session,402,*f.hash,ui::FileOperation::RemoveKeepData);explicitArchive.deleteTorrentFile=true;
  QVERIFY(bridge.prepareFileOperation(explicitArchive));QTRY_COMPARE_WITH_TIMEOUT(plans.count(),2,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(prepared(plans)->deleteTorrentFile);
  QVERIFY(bridge.executeFileOperation({prepared(plans),false,false}));QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(!finished(results)->completed);QCOMPARE(test::read(archive),metadata);QVERIFY(!inventory(f).downloads.empty());
  QVERIFY(bridge.prepareFileOperation(request(rows,session,403,*f.hash,ui::FileOperation::RemoveKeepData)));QTRY_COMPARE_WITH_TIMEOUT(plans.count(),3,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(!prepared(plans)->deleteTorrentFile);
  const auto previousResults=results.count();QVERIFY(bridge.executeFileOperation(copied(prepared(plans))));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&results.count()>previousResults,10000);QVERIFY2(finished(results)->completed,qPrintable(finished(results)->failure));
  QCOMPARE(test::read(f.payload+'/'+f.files[0].relative),payload);QVERIFY(QFileInfo::exists(archive));QCOMPARE(test::read(archive),metadata);
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
