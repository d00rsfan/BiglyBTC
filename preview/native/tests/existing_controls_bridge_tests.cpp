// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "library_window.hpp"
#include "recheck_dialog.hpp"
#include "torrent_state.hpp"
#include "library_view.hpp"
#include "existing_controls_fixture.hpp"
#include <QAction>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QtTest>
using namespace biglybtc;namespace f=existing_controls_fixture;
namespace {
void wire(app::SessionBridge& bridge,ui::LibraryWindow& window){using B=app::SessionBridge;using W=ui::LibraryWindow;window.setOfflinePreview(false);QObject::connect(&bridge,&B::snapshotReady,&window,&W::setPresentation);QObject::connect(&bridge,&B::columnLayoutSessionReady,&window,&W::beginColumnLayoutSession);QObject::connect(&bridge,&B::busyChanged,&window,&W::setBusy);QObject::connect(&bridge,&B::recheckReady,&window,&W::setRecheckProgress);QObject::connect(&window,&W::recheckRequested,&bridge,[&](ui::RecheckRequest request){if(!bridge.forceRecheck(request)){window.rejectRecheck(request.key);}});QObject::connect(&window,&W::recheckCancelRequested,&bridge,&B::cancelRecheck);}
ui::RecheckPresentation latest(const QSignalSpy& packets){return packets.empty()?ui::RecheckPresentation{}:qvariant_cast<ui::RecheckPresentation>(packets.last()[0]);}
}
class ExistingControlsBridgeTests final:public QObject {
 Q_OBJECT
private slots:
 void ordinaryStoppedRecheckSavesActualMapWithoutRepair_data(){QTest::addColumn<bool>("corrupt");QTest::addColumn<bool>("missing");QTest::addColumn<bool>("paused");QTest::newRow("stopped-corrupt-piece")<<true<<false<<false;QTest::newRow("stopped-missing-payload")<<false<<true<<false;QTest::newRow("paused-preserves-force-resume-intent")<<false<<false<<true;}
 void ordinaryStoppedRecheckSavesActualMapWithoutRepair(){
  QFETCH(bool,corrupt);QFETCH(bool,missing);QFETCH(bool,paused);f::Profile area(corrupt,missing,paused);const auto before=missing?QByteArray{}:f::read(area.payload);app::SessionBridge bridge;ui::LibraryWindow window;wire(bridge,window);window.resize(1100,780);window.show();QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),packets(&bridge,&app::SessionBridge::recheckReady),closed(&bridge,&app::SessionBridge::sessionClosed);QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.size()==1&&window.model().rowCount()==1,15000);window.incompleteTable().selectRow(0);auto* action=window.findChild<QAction*>("classic-force-recheck");QVERIFY(action);QTRY_VERIFY(action->isEnabled());action->trigger();QTRY_VERIFY_WITH_TIMEOUT(latest(packets)&&latest(packets)->terminal,30000);const auto result=latest(packets);QVERIFY2(result->stage==ui::RecheckStage::Finished,qPrintable(result->message));QVERIFY(result->pieceMapSaved&&!result->saveUncertain&&!result->resumed);QCOMPARE(result->goodPieces,missing?quint64{0}:corrupt?quint64{3}:quint64{4});QCOMPARE(result->badPieces,corrupt?quint64{1}:quint64{0});QCOMPARE(result->missingPieces,missing?quint64{4}:quint64{0});QCOMPARE(result->unknownPieces,quint64{0});if(corrupt){const auto capture=qEnvironmentVariable("BIGLYBTC_RECHECK_CAPTURE_PATH");if(!capture.isEmpty()){auto* dialog=window.findChild<ui::RecheckDialog*>();QVERIFY(dialog&&dialog->isVisible());QVERIFY(dialog->grab().save(capture,"PNG"));}}if(missing){QVERIFY(!QFileInfo::exists(area.payload));}else{QCOMPARE(f::read(area.payload),before);}QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);
  auto lease=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(lease);auto library=storage::readLibraryView(*lease);QVERIFY(library&&library->items.size()==1);QCOMPARE(library->items[0].saved.originalState,std::int64_t{70});QVERIFY(!library->items[0].saved.forceStart);QCOMPARE(library->pauses.size(),paused?std::size_t{1}:std::size_t{0});if(paused){QVERIFY(library->pauses[0].forceStart);}auto store=storage::TorrentStateStore::open(*lease);QVERIFY(store);auto state=store->load(*lease,area.hash);QVERIFY(state);const auto* resume=state->document().find("resume");QVERIFY(resume);const auto* data=resume->find("data");QVERIFY(data);const auto* bits=data->find("resume data");QVERIFY(bits&&bits->type==core::Node::Type::Bytes);QCOMPARE(bits->bytes.size(),std::size_t{4});for(std::size_t i=0;i<4;++i){QCOMPARE(static_cast<unsigned char>(bits->bytes[i]),static_cast<unsigned char>(missing||(corrupt&&i==0)?0:1));}QVERIFY(state->document().find("unknown.future")->bytes=="raw");const auto* attributes=state->document().find("attributes");QVERIFY(attributes&&attributes->find("unknown.future")&&attributes->find("unknown.future")->bytes=="raw");
 }
};
QTEST_MAIN(ExistingControlsBridgeTests)
#include "existing_controls_bridge_tests.moc"
