#include "file_operation_sequence.hpp"
#include "file_operation_dialog.hpp"
#include "library_window.hpp"
#include "session_bridge.hpp"
#include "profile.hpp"
#include <biglybtc/bencode.hpp>
#include <biglybtc/library.hpp>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <filesystem>
#include <stdexcept>
#include <sys/stat.h>
using namespace biglybtc;
namespace {
QString hash(char value){return QString(40,QChar::fromLatin1(value));}
ui::Presentation rows(quint64 revision,QStringList hashes) {
 auto value=std::make_shared<ui::LibraryPresentation>();value->revision=revision;
 for(const auto& h:hashes){ui::LibraryItem row;row.key=h;row.hash=h;row.name=QStringLiteral("<owned>")+h.left(1);row.state=QStringLiteral("Stopped");row.bytes=4096;row.completed=0;value->items.push_back(std::move(row));}return value;
}
ui::DownloadTargets targets(quint64 revision,QStringList hashes){ui::DownloadTargets out;for(const auto& h:hashes){out.push_back({h,revision});}return out;}
// Synthetic UI admission fixtures only. Real storage/Host effects are exercised below.
ui::FileOperationPlanPresentation plan(const ui::FileOperationRequest& request) {
 auto out=std::make_shared<ui::FileOperationPlanPacket>();out->request=request;out->approvalScopeDigest=QString(64,QChar('b'));out->sourceRoot=QStringLiteral("/owned/source");out->targetRoot=request.destinationParent;
 out->paths={{QStringLiteral("/owned/source/data"),request.destinationParent.isEmpty()?QString{}:request.destinationParent+QStringLiteral("/data"),4096,true}};out->presentFiles=1;out->totalBytes=4096;return out;
}
ui::FileOperationResultPresentation result(const ui::FileOperationRequest& request,bool completed,QString failure={}) {
 return std::make_shared<const ui::FileOperationResultPacket>(ui::FileOperationResultPacket{request,completed,false,std::move(failure)});
}
void write(const QString& path,const QByteArray& bytes) {
 if(!QDir().mkpath(QFileInfo(path).absolutePath())){throw std::runtime_error("owned fixture parent");}QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)||file.write(bytes)!=bytes.size()){throw std::runtime_error("owned fixture write");}
}
QByteArray read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly)){throw std::runtime_error("owned fixture read");}return f.readAll();}
std::string encoded(const libtorrent::entry& value){std::string out;libtorrent::bencode(std::back_inserter(out),value);return out;}
// No LT session, peer or tracker is created. Bridge opens this actual generated
// original-format profile with port0 and performs its normal held-lease IO.
struct Profile {
 QTemporaryDir root{QStringLiteral("/tmp/biglybtc-file-sequence-XXXXXX")};QString profile,destination;
 QStringList hashes;std::array<QString,2> source,state;std::array<QByteArray,2> bytes;std::array<struct stat,2> stamps{};
 Profile() {
  if(!root.isValid()){throw std::runtime_error("owned temporary root");}profile=root.path()+QStringLiteral("/profile");destination=root.path()+QStringLiteral("/moved");
  if(!QDir().mkpath(destination)){throw std::runtime_error("owned move parent");}
  libtorrent::entry inventory(libtorrent::entry::dictionary_type{});libtorrent::entry::list_type downloads;
  for(std::size_t n=0;n<2;++n){
   const auto name=QStringLiteral("%1.bin").arg(static_cast<qulonglong>(n));bytes[n]=QByteArray(40003,static_cast<char>('a'+n));const auto payload=root.path()+QStringLiteral("/payload%1").arg(static_cast<qulonglong>(n));source[n]=payload+'/'+name;write(source[n],bytes[n]);
   if(::lstat(source[n].toStdString().c_str(),&stamps[n])!=0){throw std::runtime_error("owned inode");}
   libtorrent::entry info(libtorrent::entry::dictionary_type{});info["name"]=name.toStdString();info["length"]=bytes[n].size();info["piece length"]=16384;
   QByteArray pieces;for(qsizetype offset=0;offset<bytes[n].size();offset+=16384){pieces+=QCryptographicHash::hash(bytes[n].mid(offset,16384),QCryptographicHash::Sha1);}info["pieces"]=pieces.toStdString();
   const auto rawHash=QCryptographicHash::hash(QByteArray::fromStdString(encoded(info)),QCryptographicHash::Sha1);hashes<<QString::fromLatin1(rawHash.toHex().toUpper());
   state[n]=profile+QStringLiteral("/active/")+hashes.back()+QStringLiteral(".dat");libtorrent::entry torrent(libtorrent::entry::dictionary_type{});torrent["info"]=std::move(info);torrent["attributes"]["storetypes"]=libtorrent::entry::list_type{libtorrent::entry("L")};torrent["attributes"]["opaque.sequence"]=std::string("\0\xffQ",3);write(state[n],QByteArray::fromStdString(encoded(torrent)));
   libtorrent::entry row(libtorrent::entry::dictionary_type{});row["torrent_hash"]=rawHash.toStdString();row["torrent"]=state[n].toStdString();row["save_dir"]=payload.toStdString();row["save_file"]=name.toStdString();row["persistent"]=1;row["state"]=70;row["position"]=static_cast<int>(n+1);row["completedbytes"]=0;row["downloaded"]=123+static_cast<int>(n);row["uploaded"]=45;row["file_priorities"]=libtorrent::entry::list_type{libtorrent::entry(-1)};row["opaque.sequence"]=std::string("\0\xffQ",3);downloads.push_back(std::move(row));
  }
  inventory["downloads"]=std::move(downloads);inventory["opaque.sequence"]=std::string("\0\xffQ",3);write(profile+QStringLiteral("/downloads.config"),QByteArray::fromStdString(encoded(inventory)));write(profile+QStringLiteral("/tables.config"),QByteArray("de"));write(profile+QStringLiteral("/biglybt.config"),QByteArray("de"));
 }
};
core::Document document(const QString& path){auto doc=core::Document::parse(read(path).toStdString(),storage::compatibleConfigLimits());if(!doc){throw std::runtime_error("parse actual saved file");}return std::move(*doc);}
void selectAll(ui::LibraryWindow& window){window.table().selectAll();}
}
class FileOperationSequenceTests final:public QObject {
 Q_OBJECT
private slots:
 void initTestCase(){QApplication::setQuitOnLastWindowClosed(false);qRegisterMetaType<ui::FileOperationSequencePresentation>();}
 void freshRevisionAndSeparateDeleteConfirmationForEveryItem() {
  QWidget owner;ui::FileOperationSequence sequence(&owner);QSignalSpy reads(&sequence,&ui::FileOperationSequence::planRequested),writes(&sequence,&ui::FileOperationSequence::executeRequested),done(&sequence,&ui::FileOperationSequence::finished);
  QVERIFY(sequence.begin(7,ui::FileOperation::RemoveDeleteData,targets(10,{hash('a'),hash('b'),hash('c')}),{"A","B","C"}));QTRY_COMPARE(reads.count(),1);const auto first=reads[0][0].value<ui::FileOperationRequest>();QVERIFY(sequence.setPlan(plan(first)));
  auto* child=sequence.dialog();QVERIFY(child);auto* confirm=child->findChild<QCheckBox*>("file-operation-confirm-delete");QVERIFY(confirm);child->accept();QCOMPARE(writes.count(),0);confirm->setChecked(true);child->accept();QCOMPARE(writes.count(),1);QVERIFY(writes[0][0].value<ui::FileOperationSubmission>().deleteConfirmed);
  sequence.setBusy(true);QVERIFY(sequence.setResult(result(first,true)));sequence.setBusy(false);QCoreApplication::processEvents();QCOMPARE(reads.count(),1);QCOMPARE(done.count(),0);
  sequence.setPresentation(rows(10,{hash('b'),hash('c')}));QCoreApplication::processEvents();QCOMPARE(reads.count(),1);
  sequence.setPresentation(rows(11,{hash('b'),hash('c')}));QTRY_COMPARE(reads.count(),2);const auto second=reads[1][0].value<ui::FileOperationRequest>();QCOMPARE(second.target.hash,hash('b'));QCOMPARE(second.target.generation,quint64(11));QVERIFY(second.request>first.request);
  QVERIFY(!sequence.setResult(result(first,true)));QVERIFY(!sequence.setPlan(plan(first)));QVERIFY(sequence.setPlan(plan(second)));child=sequence.dialog();QVERIFY(child);QVERIFY(!child->findChild<QCheckBox*>("file-operation-confirm-delete")->isChecked());child->accept();QCOMPARE(writes.count(),1);child->reject();QTRY_COMPARE(done.count(),1);
  const auto report=done[0][0].value<ui::FileOperationSequencePresentation>();QCOMPARE(report->items.size(),std::size_t(3));QCOMPARE(report->items[0].outcome,ui::FileOperationOutcome::Completed);QCOMPARE(report->items[1].outcome,ui::FileOperationOutcome::Cancelled);QCOMPARE(report->items[2].outcome,ui::FileOperationOutcome::NotAttempted);QVERIFY(report->cancelled);
 }
 void failureStopsWithExactCompletedPrefixAndNoRetry() {
  QWidget owner;ui::FileOperationSequence sequence(&owner);QSignalSpy reads(&sequence,&ui::FileOperationSequence::planRequested),writes(&sequence,&ui::FileOperationSequence::executeRequested),done(&sequence,&ui::FileOperationSequence::finished);
  QVERIFY(sequence.begin(1,ui::FileOperation::Relink,targets(1,{hash('a'),hash('b'),hash('c')}),{"A","B","C"}));QTRY_VERIFY(sequence.dialog());auto* child=sequence.dialog();child->findChild<QLineEdit*>("file-operation-destination")->setText("/owned/destination");child->findChild<QPushButton*>("file-operation-review")->click();QTRY_COMPARE(reads.count(),1);const auto first=reads[0][0].value<ui::FileOperationRequest>();QVERIFY(sequence.setPlan(plan(first)));child->accept();QCOMPARE(writes.count(),1);QVERIFY(sequence.setResult(result(first,true)));sequence.setPresentation(rows(2,{hash('a'),hash('b'),hash('c')}));QTRY_COMPARE(reads.count(),2);
  const auto second=reads[1][0].value<ui::FileOperationRequest>();QCOMPARE(second.destinationParent,QStringLiteral("/owned/destination"));QVERIFY(sequence.setPlan(plan(second)));sequence.dialog()->accept();QCOMPARE(writes.count(),2);QVERIFY(sequence.setResult(result(second,false,"destination changed; no retry")));QTRY_COMPARE(done.count(),1);
  const auto report=done[0][0].value<ui::FileOperationSequencePresentation>();QCOMPARE(report->items[0].outcome,ui::FileOperationOutcome::Completed);QCOMPARE(report->items[1].outcome,ui::FileOperationOutcome::Failed);QCOMPARE(report->items[1].failure,QStringLiteral("destination changed; no retry"));QCOMPARE(report->items[2].outcome,ui::FileOperationOutcome::NotAttempted);QCOMPARE(reads.count(),2);
 }
 void ownerCloseWaitsForActualResultAndDoesNotUndoCompletedEffect() {
  QWidget owner;ui::FileOperationSequence sequence(&owner);QSignalSpy reads(&sequence,&ui::FileOperationSequence::planRequested),cancels(&sequence,&ui::FileOperationSequence::cancelRequested),done(&sequence,&ui::FileOperationSequence::finished);
  QVERIFY(sequence.begin(4,ui::FileOperation::RemoveKeepData,targets(1,{hash('a'),hash('b')}),{"A","B"}));QTRY_COMPARE(reads.count(),1);const auto request=reads[0][0].value<ui::FileOperationRequest>();QVERIFY(sequence.setPlan(plan(request)));sequence.dialog()->accept();QVERIFY(!sequence.closeForOwner());QCOMPARE(cancels.count(),1);QVERIFY(sequence.dialog()->pending());QCOMPARE(done.count(),0);
  QVERIFY(sequence.setResult(result(request,true)));QTRY_COMPARE(done.count(),1);const auto report=done[0][0].value<ui::FileOperationSequencePresentation>();QVERIFY(report->cancelled);QCOMPARE(report->items[0].outcome,ui::FileOperationOutcome::Completed);QCOMPARE(report->items[1].outcome,ui::FileOperationOutcome::NotAttempted);QVERIFY(sequence.closeForOwner());
 }
 void profileInvalidationAndDirectDeletionFenceQueuedOrLateReplies() {
  QWidget owner;ui::FileOperationSequence sequence(&owner);QSignalSpy reads(&sequence,&ui::FileOperationSequence::planRequested),writes(&sequence,&ui::FileOperationSequence::executeRequested),done(&sequence,&ui::FileOperationSequence::finished);
  QVERIFY(sequence.begin(8,ui::FileOperation::RemoveKeepData,targets(1,{hash('a'),hash('b')}),{"A","B"}));QTRY_COMPARE(reads.count(),1);const auto request=reads[0][0].value<ui::FileOperationRequest>();sequence.invalidateSession();QVERIFY(!sequence.setPlan(plan(request)));QVERIFY(!sequence.setResult(result(request,true)));QVERIFY(!sequence.active());QCOMPARE(writes.count(),0);QCOMPARE(done.count(),0);
  QPointer<ui::FileOperationSequence> deleted=new ui::FileOperationSequence(&owner);connect(deleted,&ui::FileOperationSequence::planRequested,&owner,[&](auto){delete deleted.data();});QVERIFY(deleted->begin(9,ui::FileOperation::RemoveKeepData,targets(2,{hash('a'),hash('b')}),{"A","B"}));QTRY_VERIFY(deleted.isNull());
 }
 void actualWindowOfflineMoves_data(){QTest::addColumn<bool>("cancelSecond");QTest::newRow("all-complete")<<false;QTest::newRow("cancel-between-items")<<true;}
 void actualWindowOfflineMoves() {
  QFETCH(bool,cancelSecond);Profile fixture;app::SessionBridge bridge;ui::LibraryWindow window;
  connect(&bridge,&app::SessionBridge::snapshotReady,&window,&ui::LibraryWindow::setPresentation);connect(&bridge,&app::SessionBridge::columnLayoutSessionReady,&window,&ui::LibraryWindow::beginColumnLayoutSession);connect(&bridge,&app::SessionBridge::busyChanged,&window,&ui::LibraryWindow::setBusy);
  connect(&bridge,&app::SessionBridge::fileOperationPrepared,&window,&ui::LibraryWindow::setFileOperationPlan);connect(&bridge,&app::SessionBridge::fileOperationFinished,&window,&ui::LibraryWindow::setFileOperationResult);
  connect(&window,&ui::LibraryWindow::fileOperationPlanRequested,&bridge,[&](ui::FileOperationRequest request){if(!bridge.prepareFileOperation(request)){window.rejectFileOperationRequest(request);}});connect(&window,&ui::LibraryWindow::fileOperationExecuteRequested,&bridge,[&](ui::FileOperationSubmission submission){if(!bridge.executeFileOperation(submission)){window.rejectFileOperationSubmission(submission);}});connect(&window,&ui::LibraryWindow::fileOperationCancelRequested,&bridge,&app::SessionBridge::cancelFileOperation);
  QSignalSpy plans(&bridge,&app::SessionBridge::fileOperationPrepared),effects(&bridge,&app::SessionBridge::fileOperationFinished),done(&window,&ui::LibraryWindow::fileOperationSequenceFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.open(std::filesystem::path(fixture.profile.toStdString()),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&window.model().rowCount()==2,10000);window.show();window.setOfflinePreview(false);selectAll(window);auto* action=window.findChild<QAction*>("classic-move-data");QVERIFY(action);QTRY_VERIFY(action->isEnabled());action->trigger();auto* sequence=window.findChild<ui::FileOperationSequence*>();QVERIFY(sequence);QTRY_VERIFY(sequence->dialog());
  auto* child=sequence->dialog();child->findChild<QLineEdit*>("file-operation-destination")->setText(fixture.destination);child->findChild<QPushButton*>("file-operation-review")->click();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&child->plan(),10000);const auto first=child->plan()->request;child->accept();QTRY_COMPARE_WITH_TIMEOUT(effects.count(),1,10000);QVERIFY2(effects[0][0].value<ui::FileOperationResultPresentation>()->completed,qPrintable(effects[0][0].value<ui::FileOperationResultPresentation>()->failure));
  QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sequence->dialog()&&sequence->dialog()->plan(),10000);child=sequence->dialog();const auto second=child->plan()->request;QVERIFY(second.target.hash!=first.target.hash);QVERIFY(second.target.generation>first.target.generation);QCOMPARE(second.destinationParent,fixture.destination);
  if(cancelSecond){child->reject();}else{child->accept();}QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QTRY_VERIFY(!bridge.busy());const auto report=done[0][0].value<ui::FileOperationSequencePresentation>();QCOMPARE(effects.count(),cancelSecond?1:2);QCOMPARE(report->items[0].outcome,ui::FileOperationOutcome::Completed);QCOMPARE(report->items[1].outcome,cancelSecond?ui::FileOperationOutcome::Cancelled:ui::FileOperationOutcome::Completed);
  auto saved=document(fixture.profile+"/downloads.config");auto library=core::readLibrary(saved);QVERIFY(library);QCOMPARE(library->downloads.size(),std::size_t(2));QCOMPARE(saved.find("opaque.sequence")->bytes,std::string("\0\xffQ",3));
  for(std::size_t n=0;n<2;++n){const bool moved=!cancelSecond||fixture.hashes[static_cast<qsizetype>(n)]==first.target.hash;const auto path=moved?fixture.destination+'/'+QFileInfo(fixture.source[n]).fileName():fixture.source[n];QCOMPARE(read(path),fixture.bytes[n]);struct stat stamp{};QCOMPARE(::lstat(path.toStdString().c_str(),&stamp),0);QCOMPARE(stamp.st_dev,fixture.stamps[n].st_dev);QCOMPARE(stamp.st_ino,fixture.stamps[n].st_ino);if(moved){QVERIFY(!QFileInfo::exists(fixture.source[n]));}
   const auto parsed=core::TorrentHash::parse(QByteArray::fromHex(fixture.hashes[static_cast<qsizetype>(n)].toLatin1()).toStdString());QVERIFY(parsed);const auto found=std::ranges::find_if(library->downloads,[&](const auto& row){return row.hash&&*row.hash==*parsed;});QVERIFY(found!=library->downloads.end());QCOMPARE(found->startup,core::StartupState::Stopped);QVERIFY(!found->forceStart);QCOMPARE(found->downloaded,std::optional<std::int64_t>(123+static_cast<int>(n)));QCOMPARE(found->saveDirectory,QFileInfo(path).absolutePath().toStdString());const auto state=document(fixture.state[n]);QCOMPARE(state.find("attributes")->find("opaque.sequence")->bytes,std::string("\0\xffQ",3));
  }
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,10000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());auto lease=storage::ProfileLease::acquire(std::filesystem::path(fixture.profile.toStdString()));QVERIFY(lease);
 }
 void windowProfileChangeAndDeletionRejectOldSequenceRoutes() {
  ui::LibraryWindow window;window.setOfflinePreview(false);window.beginColumnLayoutSession(1);window.setPresentation(rows(1,{hash('a'),hash('b')}));window.show();selectAll(window);QSignalSpy reads(&window,&ui::LibraryWindow::fileOperationPlanRequested),writes(&window,&ui::LibraryWindow::fileOperationExecuteRequested),done(&window,&ui::LibraryWindow::fileOperationSequenceFinished);
  auto* action=window.findChild<QAction*>("classic-remove");QVERIFY(action);QTRY_VERIFY(action->isEnabled());action->trigger();QTRY_COMPARE(reads.count(),1);const auto old=reads[0][0].value<ui::FileOperationRequest>();window.beginColumnLayoutSession(2);window.setPresentation(rows(2,{hash('c'),hash('d')}));window.setFileOperationPlan(plan(old));QCoreApplication::processEvents();QCOMPARE(writes.count(),0);QCOMPARE(done.count(),0);QVERIFY(!window.findChild<ui::FileOperationSequence*>()->active());
  QPointer<ui::LibraryWindow> deleted=new ui::LibraryWindow;deleted->setOfflinePreview(false);deleted->beginColumnLayoutSession(3);deleted->setPresentation(rows(3,{hash('a'),hash('b')}));deleted->show();selectAll(*deleted);connect(deleted,&ui::LibraryWindow::fileOperationPlanRequested,this,[&](auto){delete deleted.data();});action=deleted->findChild<QAction*>("classic-remove");QVERIFY(action);QTRY_VERIFY(action->isEnabled());action->trigger();QTRY_VERIFY(deleted.isNull());
 }
};
QTEST_MAIN(FileOperationSequenceTests)
#include "file_operation_sequence_tests.moc"
