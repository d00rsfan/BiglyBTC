// SPDX-License-Identifier: GPL-2.0-or-later
#include "library_window.hpp"
#include "recheck_dialog.hpp"
#include "library_commands.hpp"
#include <QAction>
#include <QDir>
#include <QFile>
#include <QItemSelectionModel>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>
#include <stdexcept>
using namespace biglybtc;
namespace {
std::string b(std::string_view value){return std::to_string(value.size())+":"+std::string(value);}
QByteArray read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly)){throw std::runtime_error("Cannot read owned fixture file");}return f.readAll();}
void write(const QString& path,const QByteArray& value){QFile f(path);if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate)||f.write(value)!=value.size()||!f.flush()){throw std::runtime_error("Cannot write owned fixture file");}}
}
class ExistingControlsUiTests final:public QObject {
 Q_OBJECT
private slots:
 void cancelForcePreservesStartupPauseAndOpaqueValues_data(){QTest::addColumn<bool>("paused");QTest::newRow("forced-waiting")<<false;QTest::newRow("paused-force-restoration-intent")<<true;}
 void cancelForcePreservesStartupPauseAndOpaqueValues(){
  QFETCH(bool,paused);QTemporaryDir area;QVERIFY(area.isValid());const auto rawHash=std::string(20,'h');auto hash=core::TorrentHash::parse(rawHash);QVERIFY(hash);const auto row="d12:torrent_hash"+b(rawHash)+"7:torrent9:owned.dat8:save_dir4:/tmp10:persistenti1e5:statei"+(paused?std::string("70"):std::string("0"))+"e10:forceStarti"+(paused?std::string("0"):std::string("1"))+"e15:startStopLockedi"+(paused?std::string("0"):std::string("1"))+"e14:unknown.futurel3:rawi7eee";auto doc=core::Document::parse("d9:downloadsl"+row+"e10:pause_data"+(paused?"ld4:hash"+b(rawHash)+"5:forcei1e5:owneri1eee":std::string("le"))+"14:unknown.future3:ende");QVERIFY(doc);write(area.filePath("downloads.config"),QByteArray::fromStdString(doc->serialize()));auto lease=storage::ProfileLease::acquire(area.path().toStdString());QVERIFY(lease);const auto before=read(area.filePath("downloads.config"));auto result=storage::applyLibraryCommand(*lease,*hash,core::DownloadAction::CancelForceStart);QVERIFY(result);QVERIFY(!result->after.forceStart);QCOMPARE(result->after.originalState,paused?std::int64_t{70}:std::int64_t{0});QCOMPARE(result->pauseAfter.has_value(),paused);if(paused){QVERIFY(result->pauseAfter->forceStart);QVERIFY(!result->changed);QCOMPARE(read(area.filePath("downloads.config")),before);}auto loaded=storage::ConfigFile::load(*lease,"downloads.config");QVERIFY(loaded);const auto* rows=loaded->document().find("downloads");QVERIFY(rows&&rows->children.size()==1);const auto* opaque=rows->children[0].find("unknown.future");QVERIFY(opaque&&opaque->type==core::Node::Type::List);QVERIFY(loaded->document().find("unknown.future")->bytes=="end");const auto accepted=read(area.filePath("downloads.config"));auto again=storage::applyLibraryCommand(*lease,*hash,core::DownloadAction::CancelForceStart);QVERIFY(again&&!again->changed);QCOMPARE(read(area.filePath("downloads.config")),accepted);
 }
 void bulkForceCancellationAndRecheckKeepCurrentTargetIdentity(){
  ui::LibraryWindow window;window.setOfflinePreview(false);window.beginColumnLayoutSession(1);auto library=std::make_shared<ui::LibraryPresentation>();library->revision=1;for(int i=0;i<2;++i){ui::LibraryItem row;row.hash=QString(40,i?'B':'A');row.key=row.hash;row.name=i?"Second":"First";row.state="Waiting";row.bytes=100;row.completed=0;row.actionMask=ui::downloadActionBit(core::DownloadAction::CancelForceStart);row.canRecheck=true;library->items.push_back(row);}window.setPresentation(library);window.incompleteTable().selectAll();auto* clear=window.findChild<QAction*>("classic-cancel-force-start");auto* check=window.findChild<QAction*>("classic-force-recheck");QVERIFY(clear&&check);QTRY_VERIFY(clear->isEnabled()&&check->isEnabled());QSignalSpy commands(&window,&ui::LibraryWindow::downloadActionRequested),requests(&window,&ui::LibraryWindow::recheckRequested);clear->trigger();QCOMPARE(commands.size(),1);QCOMPARE(qvariant_cast<core::DownloadAction>(commands.last()[1]),core::DownloadAction::CancelForceStart);QCOMPARE(qvariant_cast<ui::DownloadTargets>(commands.last()[0]).size(),std::size_t{2});check->trigger();QCOMPARE(requests.size(),1);const auto request=qvariant_cast<ui::RecheckRequest>(requests.last()[0]);QVERIFY(request.receiver&&!request.receiver->stop_requested());QCOMPARE(request.targets.size(),std::size_t{2});auto* dialog=window.findChild<ui::RecheckDialog*>();QVERIFY(dialog&&dialog->isVisible());QSignalSpy resets(&window.model(),&QAbstractItemModel::modelReset);auto packet=std::make_shared<ui::RecheckPacket>();packet->key=request.key;packet->sequence=1;packet->target=request.targets[0];packet->targetIndex=1;packet->totalTargets=2;packet->stage=ui::RecheckStage::Checking;packet->scannedBytes=50;packet->totalBytes=100;packet->goodPieces=1;packet->unknownPieces=1;packet->message="Actual <b>literal</b> progress";window.setRecheckProgress(packet);QCOMPARE(resets.size(),0);QCOMPARE(dialog->findChild<QLabel*>("recheck-status")->textFormat(),Qt::PlainText);QCOMPARE(dialog->findChild<QProgressBar*>("recheck-progress")->value(),500);auto stale=std::make_shared<ui::RecheckPacket>(*packet);stale->key.request++;stale->message="stale";window.setRecheckProgress(stale);QCOMPARE(dialog->findChild<QLabel*>("recheck-status")->text(),packet->message);bool stoppedFirst=false;QObject::connect(&window,&ui::LibraryWindow::recheckCancelRequested,&window,[&](ui::RecheckKey){stoppedFirst=request.receiver->stop_requested();window.beginColumnLayoutSession(2);});dialog->reject();QVERIFY(stoppedFirst);QVERIFY(request.receiver->stop_requested());window.setRecheckProgress(packet);QVERIFY(!dialog->isVisible());QCOMPARE(resets.size(),0);
 }
 void dialogCancellationReentryCannotCloseReplacementOrTouchDestroyedWindow(){
  ui::RecheckDialog dialog;ui::RecheckRequest first{{1,1,1},{{"A",1}},std::make_shared<std::stop_source>()},second{{2,2,2},{{"B",2}},std::make_shared<std::stop_source>()};QVERIFY(dialog.begin(first));dialog.open();QObject::connect(&dialog,&ui::RecheckDialog::cancelled,&dialog,[&](ui::RecheckKey){QVERIFY(first.receiver->stop_requested());QVERIFY(dialog.begin(second));dialog.open();});dialog.reject();QVERIFY(dialog.key()&&*dialog.key()==second.key);QVERIFY(dialog.isVisible());QVERIFY(!second.receiver->stop_requested());
 }
};
QTEST_MAIN(ExistingControlsUiTests)
#include "existing_controls_ui_tests.moc"
