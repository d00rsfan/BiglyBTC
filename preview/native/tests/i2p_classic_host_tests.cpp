// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_profile_fixture.hpp"
#include "session_bridge.hpp"
#include "i2p_saved_settings.hpp"
#include "library_window.hpp"
#include "statistics_page.hpp"
#include "i2p_statistics_view.hpp"
#include <QApplication>
#include <QLabel>
#include <QSignalSpy>
#include <QtTest>
using namespace biglybtc;
class I2pClassicHostTests final:public QObject {
  Q_OBJECT
private slots:
  void savedPureTunnelSettingsPreserveDefaultsOverridesAndMalformedBytes(){
    const auto document=core::Document::parse("de");QVERIFY(document);
    auto options=app::readSavedI2pOptions(*document,{});QVERIFY(options);QCOMPARE(options->sessionOptions.size(),6);
    const auto value=[&](std::string key){for(const auto& pair:options->sessionOptions){if(pair.first==key){return pair.second;}}return std::string{};};
    QCOMPARE(value("inbound.length"),std::string("3"));QCOMPARE(value("outbound.quantity"),std::string("2"));
    app::I2pServiceOptions explicitOptions;explicitOptions.sessionOptions={{"inbound.length","0"}};
    options=app::readSavedI2pOptions(*document,explicitOptions);QVERIFY(options);QCOMPARE(value("inbound.length"),std::string("0"));
    const auto raw=owned_i2p_test::dict({{"Plugin.azneti2phelper.azi2phelper.i2p.inbound.hops",owned_i2p_test::b("invalid")}});
    const auto bad=core::Document::parse(raw);QVERIFY(bad);QVERIFY(!app::readSavedI2pOptions(*bad,{}));QCOMPARE(bad->serialize(),raw);
  }
  void currentI2pMeasurementsAreIsolatedFromOtherProfileAndPublicStatistics(){
    ui::StatisticsPage page;page.beginNativeSession(11);
    auto p=std::make_shared<ui::I2pPacket>();p->session=11;p->sequence=1;p->state="Downloading — I2P";p->hash="original-hash";p->payloadDownloaded=1234;p->verifiedBytes=1000;p->trackerReplies=2;
    page.setI2p(p);auto* view=page.findChild<ui::I2pStatisticsView*>("statistics-i2p");QVERIFY(view);QCOMPARE(view->presentation()->payloadDownloaded,1234);
    auto foreign=std::make_shared<ui::I2pPacket>(*p);foreign->session=12;foreign->sequence=2;foreign->payloadDownloaded=9999;page.setI2p(foreign);QCOMPARE(view->presentation()->payloadDownloaded,1234);
    auto stale=std::make_shared<ui::I2pPacket>(*p);stale->payloadDownloaded=2;page.setI2p(stale);QCOMPARE(view->presentation()->payloadDownloaded,1234);
    page.beginNativeSession(12);QVERIFY(!view->presentation());page.setI2p(p);QVERIFY(!view->presentation());
    foreign->sequence=1;page.setI2p(foreign);QCOMPARE(view->presentation()->payloadDownloaded,9999);
    ui::StatisticsNavigation nav;nav.section=10;QVERIFY(ui::validStatisticsNavigation(nav));
  }
  void privateModeKeepsOriginalStoppedIntentAndRefusesPublicFallback(){
    owned_i2p_test::Fixture f(true);const auto config=owned_i2p_test::read(f.path/"biglybt.config"),key=owned_i2p_test::read(f.key);f.lease.reset();
    app::I2pServiceOptions options;app::SessionBridge bridge(nullptr,{},{},{},options);
    QSignalSpy snapshots(&bridge,&app::SessionBridge::snapshotReady),closed(&bridge,&app::SessionBridge::sessionClosed),observed(&bridge,&app::SessionBridge::i2pReady);
    QVERIFY(bridge.openNormal(f.path));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!snapshots.empty(),10000);
    const auto packet=qvariant_cast<ui::Presentation>(snapshots.last()[0]);QVERIFY(packet);QCOMPARE(packet->items.size(),1);QCOMPARE(packet->items.front().actionMask,0);QCOMPARE(packet->items.front().state,QString("Error"));
    QCOMPARE(owned_i2p_test::read(f.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(f.key),key);QTRY_VERIFY(!observed.empty());
    auto lease=storage::ProfileLease::acquire(f.path);QVERIFY(!lease);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QVERIFY(closed.front()[1].toBool());QVERIFY(closed.front()[2].toBool());
    lease=storage::ProfileLease::acquire(f.path);QVERIFY(lease);
  }
  void stoppedPureI2pRejectsUnadvertisedPause(){
    owned_i2p_test::Fixture f;auto inventory=owned_i2p_test::read(f.path/"downloads.config");auto pos=inventory.find("5:statei75e");QVERIFY(pos!=std::string::npos);inventory.replace(pos,11,"5:statei70e");owned_i2p_test::write(f.path/"downloads.config",inventory);f.lease.reset();
    app::SessionBridge bridge(nullptr,{},{},{},app::I2pServiceOptions{});QSignalSpy snapshots(&bridge,&app::SessionBridge::snapshotReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(f.path));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!snapshots.empty(),10000);
    auto packet=qvariant_cast<ui::Presentation>(snapshots.last()[0]);QVERIFY(packet);QCOMPARE(packet->items.front().state,QString("Stopped"));
    const ui::Target target{packet->items.front().hash,packet->revision};QVERIFY(packet->items.front().actionMask&ui::downloadActionBit(core::DownloadAction::Start));
    QVERIFY(bridge.downloadAction({target},core::DownloadAction::Pause));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);
    // Pause was not advertised for a stopped row: stale/forged UI intent cannot mutate disk.
    QCOMPARE(owned_i2p_test::read(f.path/"downloads.config"),inventory);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);
  }
};
QTEST_MAIN(I2pClassicHostTests)
#include "i2p_classic_host_tests.moc"
