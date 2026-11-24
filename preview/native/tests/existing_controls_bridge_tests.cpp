// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "library_window.hpp"
#include "recheck_dialog.hpp"
#include "torrent_state.hpp"
#include "library_view.hpp"
#include "existing_controls_fixture.hpp"
#include <QAction>
#include <QLabel>
#include <QDir>
#include <QDialog>
#include <QTableWidget>
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
 void inspectOriginalJavaPluginInventory() {
  f::Profile area(true, false, false);
  const auto path = area.path + "/biglybt.config";
  {
   auto lease = storage::ProfileLease::acquire(area.path.toStdString());
   QVERIFY(lease);
   auto settings = storage::ConfigFile::load(*lease, "biglybt.config");
   QVERIFY(settings && settings->document().replaceValue("PluginInfo.azlocprov.enabled", "i0e") &&
       settings->document().replaceValue("PluginInfo.aznettor.enabled", "5:wrong") &&
       settings->document().replaceValue("PluginInfo.SpeedScheduler.enabled", "i1e") &&
       settings->save(*lease));
  }
  const auto plugin = [&](const QString& folder, const QByteArray& id) {
   const auto directory = area.path + "/plugins/" + folder;
   QVERIFY(QDir().mkpath(directory));
   f::write(directory + "/plugin.properties",
       "plugin.id=" + id + "\nplugin.name=Generated original\nplugin.class=generated.Main\n");
  };
  plugin("RenamedLocationProvider", "azlocprov");
  plugin("Rating1", "azrating");
  plugin("Rating2", "azrating");
  const auto descriptor = area.path + "/plugins/RenamedLocationProvider/plugin.properties";
  const auto descriptorBefore = f::read(descriptor);
  const auto configBefore = f::read(path);
  app::SessionBridge bridge;
  ui::LibraryWindow window;
  wire(bridge, window);
  QObject::connect(&window, &ui::LibraryWindow::javaPluginInventoryRequested,
      &bridge, &app::SessionBridge::loadJavaPluginInventory);
  QObject::connect(&bridge, &app::SessionBridge::javaPluginInventoryReady,
      &window, &ui::LibraryWindow::setJavaPluginInventory);
  QSignalSpy inventory(&bridge, &app::SessionBridge::javaPluginInventoryReady);
  QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
  auto* action = window.findChild<QAction*>("classic-java-plugin-inventory");
  QVERIFY(action && !action->isEnabled());
  window.show();
  QVERIFY(bridge.open(area.path.toStdString(), 0));
  QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && action->isEnabled(), 10000);
  action->trigger();
  QTRY_COMPARE_WITH_TIMEOUT(inventory.size(), 1, 10000);
  const auto packet = qvariant_cast<ui::JavaPluginInventory>(inventory.first().at(0));
  QVERIFY(packet.session && packet.failure.isEmpty() && packet.entries.size() == 7);
  const auto row = [&](const QString& id) {
   return std::ranges::find(packet.entries, id, &ui::JavaPlugin::runtimeId);
  };
  const auto location = row("azlocprov");
  QVERIFY(location != packet.entries.end() && location->directory && *location->directory == "RenamedLocationProvider");
  QVERIFY(location->startup && !*location->startup);
  const auto missing = row("SpeedScheduler");
  QVERIFY(missing != packet.entries.end() && !missing->directory && missing->installationFailure.isEmpty());
  QVERIFY(missing->startup && *missing->startup);
  const auto ambiguous = row("azrating");
  QVERIFY(ambiguous != packet.entries.end() && !ambiguous->directory && !ambiguous->installationFailure.isEmpty());
  const auto invalid = row("aznettor");
  QVERIFY(invalid != packet.entries.end() && !invalid->startup && !invalid->settingsFailure.isEmpty());
  auto* table = window.findChild<QTableWidget*>("java-plugin-inventory-table");
  QVERIFY(table);
  QCOMPARE(table->rowCount(), 7);
  QCOMPARE(table->item(0, 1)->text(), QStringLiteral("RenamedLocationProvider"));
  window.setJavaPluginInventory({packet.session + 1, {}, QStringLiteral("Stale request")});
  QCOMPARE(table->rowCount(), 7);
  QCOMPARE(f::read(path), configBefore);
  QCOMPARE(f::read(descriptor), descriptorBefore);
  QVERIFY(!QFileInfo::exists(area.path + "/plugins/RenamedLocationProvider/biglybtc"));
  window.findChild<QDialog*>("java-plugin-inventory")->close();
  QVERIFY(bridge.closeSession());
  QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
  qInfo("PASS desktop reports installed, missing and ambiguous Java plugins with saved startup state");
 }

 void explicitRecheckPublishesCheckingToTagRules_data() {
  QTest::addColumn<int>("metricsMode");
  QTest::newRow("current-membership") << 0;
  QTest::newRow("original-named-age-and-added-rank") << 1;
  QTest::newRow("complete-state-tag-count") << 2;
 }
 void explicitRecheckPublishesCheckingToTagRules() {
  QFETCH(int, metricsMode);
  f::Profile area(true, false, false);
  const auto payload = f::read(area.payload);
  {
   auto lease = storage::ProfileLease::acquire(area.path.toStdString());
   QVERIFY(lease);
   auto settings = storage::ConfigFile::load(*lease, "biglybt.config");
   QVERIFY(settings);
   QVERIFY(settings->document().replaceValue("locale", "5:en_GB"));
   QVERIFY(settings->document().replaceValue("tagmanager.enable", "i1e"));
   QVERIFY(settings->save(*lease));
  }
  std::string rule = metricsMode ? "hasTagAge(\"Checking\")==0&&tagPosition(\"Checking\",2)==-1" : "hasTag(\"Checking\")";
  if (metricsMode == 2) { rule += "&&countTag(\"Checking\")==1"; }
  const auto definition = "d1:3d1:1d1:cd13:pp.constraintl" + f::b(rule)
      + f::b("am=0;") + "ee1:n" + f::b("Checking consumer") + "1:oleeee";
  const auto tagPath = area.path + "/tag.config";
  f::write(tagPath, QByteArray::fromStdString(definition));
  app::SessionBridge bridge;
  ui::LibraryWindow window;
  wire(bridge, window);
  window.show();
  QSignalSpy packets(&bridge, &app::SessionBridge::recheckReady);
  QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
  bool assigned = false, removed = false;
  connect(&bridge, &app::SessionBridge::manualTagStatisticsReady, this,
      [&](const ui::ManualTagStatisticsPresentation& packet) {
        if (!packet->membership || packet->membership->rows().size() != 1) { return; }
        const auto& tag = packet->membership->rows().front();
        if (!tag.membershipKnown) { return; }
        if (tag.resolvedHashes.size() == 1 && tag.resolvedHashes.front() == area.hash) { assigned = true; }
        if (assigned && tag.resolvedCount == 0) { removed = true; }
      });
  QVERIFY(bridge.openNormal(area.path.toStdString()));
  QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && window.model().rowCount() == 1, 15000);
  window.incompleteTable().selectRow(0);
  auto* action = window.findChild<QAction*>("classic-force-recheck");
  QVERIFY(action);
  QTRY_VERIFY(action->isEnabled());
  action->trigger();
  QTRY_VERIFY_WITH_TIMEOUT(latest(packets) && latest(packets)->terminal && removed, 20000);
  QVERIFY(assigned);
  QCOMPARE(latest(packets)->stage, ui::RecheckStage::Finished);
  QVERIFY(bridge.closeSession());
  QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
  const auto tags = core::readSavedDownloadTags(f::read(tagPath).toStdString());
  QVERIFY(tags && tags->tags.size() == 1 && tags->tags.front().uniqueHashes.empty());
  QCOMPARE(f::read(area.payload), payload);
 }
 void ordinaryStoppedRecheckSavesActualMapWithoutRepair_data(){QTest::addColumn<bool>("corrupt");QTest::addColumn<bool>("missing");QTest::addColumn<bool>("paused");QTest::newRow("stopped-corrupt-piece")<<true<<false<<false;QTest::newRow("stopped-missing-payload")<<false<<true<<false;QTest::newRow("paused-preserves-force-resume-intent")<<false<<false<<true;}
 void ordinaryStoppedRecheckSavesActualMapWithoutRepair(){
  QFETCH(bool,corrupt);QFETCH(bool,missing);QFETCH(bool,paused);f::Profile area(corrupt,missing,paused);const auto before=missing?QByteArray{}:f::read(area.payload);app::SessionBridge bridge;ui::LibraryWindow window;wire(bridge,window);window.resize(1100,780);window.show();QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),packets(&bridge,&app::SessionBridge::recheckReady),closed(&bridge,&app::SessionBridge::sessionClosed);QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.size()==1&&window.model().rowCount()==1,15000);window.incompleteTable().selectRow(0);auto* action=window.findChild<QAction*>("classic-force-recheck");QVERIFY(action);QTRY_VERIFY(action->isEnabled());action->trigger();QTRY_VERIFY_WITH_TIMEOUT(latest(packets)&&latest(packets)->terminal,30000);const auto result=latest(packets);QVERIFY2(result->stage==ui::RecheckStage::Finished,qPrintable(result->message));QVERIFY(result->pieceMapSaved&&!result->saveUncertain&&!result->resumed);QCOMPARE(result->goodPieces,missing?quint64{0}:corrupt?quint64{3}:quint64{4});QCOMPARE(result->badPieces,corrupt?quint64{1}:quint64{0});QCOMPARE(result->missingPieces,missing?quint64{4}:quint64{0});QCOMPARE(result->unknownPieces,quint64{0});if(corrupt){const auto capture=qEnvironmentVariable("BIGLYBTC_RECHECK_CAPTURE_PATH");if(!capture.isEmpty()){auto* dialog=window.findChild<ui::RecheckDialog*>();QVERIFY(dialog&&dialog->isVisible());QVERIFY(dialog->grab().save(capture,"PNG"));}}if(missing){QVERIFY(!QFileInfo::exists(area.payload));}else{QCOMPARE(f::read(area.payload),before);}QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);
  auto lease=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(lease);auto library=storage::readLibraryView(*lease);QVERIFY(library&&library->items.size()==1);QCOMPARE(library->items[0].saved.originalState,std::int64_t{70});QVERIFY(!library->items[0].saved.forceStart);QCOMPARE(library->pauses.size(),paused?std::size_t{1}:std::size_t{0});if(paused){QVERIFY(library->pauses[0].forceStart);}auto store=storage::TorrentStateStore::open(*lease);QVERIFY(store);auto state=store->load(*lease,area.hash);QVERIFY(state);const auto* resume=state->document().find("resume");QVERIFY(resume);const auto* data=resume->find("data");QVERIFY(data);const auto* bits=data->find("resume data");QVERIFY(bits&&bits->type==core::Node::Type::Bytes);QCOMPARE(bits->bytes.size(),std::size_t{4});for(std::size_t i=0;i<4;++i){QCOMPARE(static_cast<unsigned char>(bits->bytes[i]),static_cast<unsigned char>(missing||(corrupt&&i==0)?0:1));}QVERIFY(state->document().find("unknown.future")->bytes=="raw");const auto* attributes=state->document().find("attributes");QVERIFY(attributes&&attributes->find("unknown.future")&&attributes->find("unknown.future")->bytes=="raw");
 }
};
QTEST_MAIN(ExistingControlsBridgeTests)
#include "existing_controls_bridge_tests.moc"
