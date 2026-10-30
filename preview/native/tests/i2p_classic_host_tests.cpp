// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_profile_fixture.hpp"
#include "session_bridge.hpp"
#include "i2p_saved_settings.hpp"
#include "library_window.hpp"
#include "statistics_page.hpp"
#include "i2p_statistics_view.hpp"
#include "original_torrent_identity.hpp"
#include <QTcpServer>
#include <QHostAddress>
#include <QApplication>
#include <QLabel>
#include <QSignalSpy>
#include <QtTest>
using namespace biglybtc;
class I2pClassicHostTests final:public QObject {
  Q_OBJECT
private slots:
  void privateImporterRefusesUnsupportedModesBeforeEffectsAndSavesI2pContext_data(){
    QTest::addColumn<bool>("paused");QTest::newRow("stopped")<<false;QTest::newRow("paused")<<true;
  }
  void privateImporterRefusesUnsupportedModesBeforeEffectsAndSavesI2pContext(){
    QFETCH(bool,paused);owned_i2p_test::Fixture f;
    const auto empty=owned_i2p_test::dict({{"downloads","le"},{"future",owned_i2p_test::b("retained inventory")}});
    owned_i2p_test::write(f.path/"downloads.config",empty);QVERIFY(std::filesystem::remove_all(f.path/"active")>0);
    const auto config=owned_i2p_test::read(f.path/"biglybt.config"),key=owned_i2p_test::read(f.key),input=owned_i2p_test::read(f.path/"fixture.torrent");
    const auto destination=f.path/"absent-import-data";f.lease.reset();
    QTcpServer router;QVERIFY(router.listen(QHostAddress::LocalHost,0));QSignalSpy connections(&router,&QTcpServer::newConnection);
    app::I2pServiceOptions options;options.routerPort=router.serverPort();app::SessionBridge bridge(nullptr,{},{},{},options);
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),snapshots(&bridge,&app::SessionBridge::snapshotReady),reads(&bridge,&app::SessionBridge::torrentIntakeReady),writes(&bridge,&app::SessionBridge::torrentIntakeFinished),native(&bridge,&app::SessionBridge::nativeSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(f.path));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&!snapshots.empty(),10000);
    const auto initial=qvariant_cast<ui::Presentation>(snapshots.last()[0]);QVERIFY(initial);QVERIFY(initial->items.empty());
    const auto session=sessions.last()[0].toULongLong();QVERIFY(bridge.previewTorrentIntake({session,1,QString::fromStdString((f.path/"fixture.torrent").string())}));
    QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());const auto preview=qvariant_cast<ui::TorrentIntakePresentation>(reads.last()[0]);
    QVERIFY(preview);QVERIFY2(ui::validTorrentIntake(preview),qPrintable(preview->failure));QVERIFY(preview->i2pOnly);QCOMPARE(preview->hash,QString::fromStdString(f.hash.hex()));
    ui::TorrentIntakeSubmission accepted;accepted.session=session;accepted.request=1;accepted.revision=preview->revision;accepted.destination=QString::fromStdString(destination.string());accepted.start=false;accepted.decoderShowAll=preview->decoderShowAll;
    for(const auto& file:preview->files){accepted.files.push_back({file.padding,0});}
    int responses=0;
    for(int kind=0;kind<5;++kind){
      auto forged=accepted;
      if(kind==0){forged.start=true;forged.forceStart=true;}
      else if(kind==1){forged.verifyExisting=true;}
      else if(kind==2){forged.goal=core::ExistingVerificationGoal::SelectedFiles;}
      else{forged.allocation=kind==3?core::TorrentAllocationMode::Stopped:core::TorrentAllocationMode::Paused;}
      QVERIFY(bridge.importTorrentFiles(forged));++responses;QTRY_COMPARE_WITH_TIMEOUT(writes.size(),responses,10000);QTRY_VERIFY(!bridge.busy());
      const auto denied=qvariant_cast<ui::TorrentIntakeResultPresentation>(writes.last()[0]);QVERIFY(denied);QVERIFY(!denied->imported&&!denied->started&&!denied->verificationPending&&!denied->allocationPending);
      QVERIFY2(denied->failure.contains(QStringLiteral("private I2P importer")),qPrintable(denied->failure));
      QCOMPARE(owned_i2p_test::read(f.path/"downloads.config"),empty);QCOMPARE(owned_i2p_test::read(f.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(f.key),key);QCOMPARE(owned_i2p_test::read(f.path/"fixture.torrent"),input);
      QVERIFY(!std::filesystem::exists(f.path/"active"));QVERIFY(!std::filesystem::exists(f.path/"torrents"));QVERIFY(!std::filesystem::exists(f.path/"BiglyBTC.torrent-import"));QVERIFY(!std::filesystem::exists(destination));QCOMPARE(native.size(),0);QCOMPARE(connections.size(),0);QVERIFY(!router.hasPendingConnections());
    }
    accepted.paused=paused;QVERIFY(bridge.importTorrentFiles(accepted));++responses;QTRY_COMPARE_WITH_TIMEOUT(writes.size(),responses,10000);QTRY_VERIFY(!bridge.busy());
    const auto added=qvariant_cast<ui::TorrentIntakeResultPresentation>(writes.last()[0]);QVERIFY(added);QVERIFY2(added->imported,qPrintable(added->failure));QVERIFY(!added->started&&!added->verificationPending&&!added->allocationPending);QCOMPARE(added->hash,preview->hash);
    QVERIFY(!std::filesystem::exists(destination));QCOMPARE(native.size(),0);QCOMPARE(connections.size(),0);QVERIFY(!router.hasPendingConnections());
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QVERIFY(closed.front()[1].toBool()&&closed.front()[2].toBool());
    auto held=storage::ProfileLease::acquire(f.path);QVERIFY(held);
    QCOMPARE(owned_i2p_test::read(f.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(f.key),key);QCOMPARE(owned_i2p_test::read(f.path/"fixture.torrent"),input);QCOMPARE(owned_i2p_test::read(f.path/"torrents"/(f.hash.hex()+".torrent")),input);
    const auto inventory=core::Document::parse(owned_i2p_test::read(f.path/"downloads.config"));QVERIFY(inventory);const auto library=core::readLibrary(*inventory);QVERIFY(library);QCOMPARE(library->downloads.size(),std::size_t{1});
    QVERIFY(library->downloads[0].hash&&*library->downloads[0].hash==f.hash);QCOMPARE(library->downloads[0].startup,core::StartupState::Stopped);QVERIFY(!library->downloads[0].forceStart);QCOMPARE(library->paused.size(),paused?std::size_t{1}:std::size_t{0});
    if(paused){QVERIFY(library->paused[0].hash==f.hash&&!library->paused[0].forceStart);}
    const auto* future=inventory->find("future");QVERIFY(future);QCOMPARE(future->bytes,std::string("retained inventory"));
    const auto state=core::Document::parse(owned_i2p_test::read(f.state()));QVERIFY(state);const auto* attributes=state->find("attributes");QVERIFY(attributes);
    for(const auto& field:std::array<std::pair<const char*,const char*>,2>{{{"networks","I2P"},{"peersources","Tracker"}}}){
      const auto* values=attributes->find(field.first);QVERIFY(values);QCOMPARE(values->type,core::Node::Type::List);QCOMPARE(values->children.size(),std::size_t{1});QCOMPARE(values->children[0].bytes,std::string(field.second));
    }
    const auto raw=storage::originalTorrentInfoSubject(*state);QVERIFY(raw);QCOMPARE(std::string(*raw),f.info);QVERIFY(!std::filesystem::exists(destination));QVERIFY(!std::filesystem::exists(f.path/"BiglyBTC.torrent-import"));QCOMPARE(native.size(),0);QCOMPARE(connections.size(),0);
  }
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
