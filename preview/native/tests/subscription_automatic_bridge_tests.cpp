// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_automatic_host_fixture.hpp"
#include "torrent_intake.hpp"
#include "subscription_values.hpp"
#include <ranges>
#include <QCryptographicHash>
#include <QtTest>
#include <sys/stat.h>
#include <QPointer>
using namespace biglybtc;namespace a=subscription_automatic_host_fixture;
namespace {
// A real terminal refusal is an immediate assertion failure, not a reason to
// wait the whole positive budget again for every data row. Earlier attempts
// are excluded explicitly when a test checks genuine cancel/recovery.
bool automaticTerminal(const QSignalSpy& notices,qsizetype begin=0){
  for(qsizetype n=begin;n<notices.size();++n){const auto message=notices.at(n).at(0).toString();
    if(message.contains("added to the download queue")||message.contains("Automatic subscription torrent saved")||message.contains("Automatic result linked")||message.contains("Automatic subscription addition refused")||message.contains("Automatic subscription addition needs review")||message.contains("Automatic addition cancelled")||message.contains("Automatic addition ended")){return true;}}
  return false;
}
}

class SubscriptionAutomaticBridgeTests final:public QObject {
 Q_OBJECT
private slots:
 void changedUnreadEventUsesActualPolicyAndRealAdmission_data(){
  QTest::addColumn<int>("mode");
  QTest::newRow("effective-queued-real-handle")<<0;QTest::newRow("global-default-stopped")<<1;
  QTest::newRow("manager-autostart-disabled")<<2;QTest::newRow("payload-below-minimum")<<3;
  QTest::newRow("stopped-with-global-general-pause")<<4;QTest::newRow("queued-ignores-global-auto-pause")<<5;
  QTest::newRow("hash-single-file")<<6;QTest::newRow("hash-multifile-inner-root")<<7;QTest::newRow("hash-single-existing-container")<<8;QTest::newRow("exact-preexisting-named-file-preserved")<<9;
 }
 void changedUnreadEventUsesActualPolicyAndRealAdmission(){
  QFETCH(int,mode);a::w::Server server;server.body=a::metainfo(mode==7);a::f::Profile area(server.url(),nullptr,true);
  a::Policy policy;policy.defaultStopped=mode==1||mode==4;policy.start=mode!=2;policy.minMiB=mode==3?1:0;policy.autoPause=mode==4||mode==5;policy.hashNames=mode>=6&&mode<=8;
  const auto paths=a::prepare(area,server.url("/owned.torrent"),policy);const auto signedBody=a::bytes(area.bodyPath);
  const auto info=core::Document::parse(server.body.toStdString());QVERIFY(info);const auto rawInfo=core::encodeValue(*info->find("info"));QVERIFY(rawInfo);const auto fullHash=QCryptographicHash::hash(QByteArray::fromStdString(*rawInfo),QCryptographicHash::Sha1);const auto suffix="_"+QString::fromLatin1(fullHash.toHex().toUpper().left(8));
  if(mode==8){QVERIFY(QDir().mkpath(paths.payload+"/Owned"+suffix));}
  struct stat preexisting{};if(mode==9){a::w::write(paths.torrents+"/Owned.torrent",server.body);QVERIFY(::lstat((paths.torrents+"/Owned.torrent").toLocal8Bit().constData(),&preexisting)==0);}
  std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},{},{},{},a::factory(area,made));
  QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),packets(&bridge,&app::SessionBridge::subscriptionReady),notices(&bridge,&app::SessionBridge::messageReady),snapshots(&bridge,&app::SessionBridge::snapshotReady),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.size()==1,15000);QCOMPARE(made.load(),0);
  QVERIFY(bridge.subscriptionCatalog({sessions.front().front().toULongLong(),1,1}));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!catalogs.empty(),10000);QCOMPARE(made.load(),0);
  QVERIFY(bridge.subscriptionCommand(a::command(catalogs)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!packets.empty(),10000);
  const bool queued=policy.start&&!policy.defaultStopped&&!policy.minMiB;
  QTRY_VERIFY2_WITH_TIMEOUT(automaticTerminal(notices),qPrintable(a::diagnostic(notices,made.load())),20000);QVERIFY2(a::notice(notices,queued?QString("added to the download queue"):QString("saved stopped")),qPrintable(a::diagnostic(notices,made.load())));
  QCOMPARE(made.load(),1);QCOMPARE(server.requests.size(),qsizetype{1});QVERIFY(server.requests.front().startsWith("GET /owned.torrent HTTP/1.1\r\n"));
  const auto inventory=a::library(area);QCOMPARE(inventory.downloads.size(),std::size_t{1});const auto& row=inventory.downloads.front();QVERIFY(row.hash&&row.persistent&&row.saveFile&&!row.forceStart);QCOMPARE(row.originalState,std::int64_t{queued?75:70});QCOMPARE(inventory.paused.size(),std::size_t{mode==4?1U:0U});
  if(mode==4){QVERIFY(!inventory.paused.front().forceStart);QCOMPARE(inventory.paused.front().owner,std::int64_t{1});}
  const auto expectedDirectory=(mode==7||mode==8)?paths.payload+"/Owned"+suffix:paths.payload;QCOMPARE(QString::fromStdString(row.saveDirectory),expectedDirectory);QCOMPARE(QString::fromStdString(*row.saveFile),(mode==6)?"Owned"+suffix:QString("Owned"));
  QCOMPARE(a::bytes(paths.torrents+"/Owned"+(policy.hashNames?suffix:QString{})+".torrent"),server.body);QCOMPARE(a::bytes(area.bodyPath),signedBody);a::noPending(area);if(mode==9){struct stat after{};QVERIFY(::lstat((paths.torrents+"/Owned.torrent").toLocal8Bit().constData(),&after)==0);QCOMPARE(after.st_dev,preexisting.st_dev);QCOMPARE(after.st_ino,preexisting.st_ino);QCOMPARE(after.st_mtim.tv_sec,preexisting.st_mtim.tv_sec);QCOMPARE(after.st_mtim.tv_nsec,preexisting.st_mtim.tv_nsec);}
  auto result=search::parseSubscriptionResults(a::bytes(area.resultsPath).toStdString());QVERIFY(result&&result->rows.size()==1&&result->rows.front().read);
  QTRY_VERIFY_WITH_TIMEOUT(a::oneSnapshot(snapshots),10000);const auto snapshot=qvariant_cast<ui::Presentation>(snapshots.last()[0]);QVERIFY(snapshot&&snapshot->items.size()==1);
  if(queued){const auto mask=snapshot->items.front().actionMask;QVERIFY((mask&(1U<<static_cast<unsigned>(core::DownloadAction::Stop)))!=0);QVERIFY((mask&(1U<<static_cast<unsigned>(core::DownloadAction::Start)))==0);QVERIFY(snapshot->items.front().state!="Error");}
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));QVERIFY(server.failure.isEmpty());
 }
 void committedRealFeedScanFlowsThroughAutomaticMetadataAndActualQueue(){
  a::w::Server feed,meta;meta.body=a::metainfo();feed.body=QByteArray("<rss><channel><title>Owned feed</title><item><title>Scan Owned</title><link>")+QByteArray::fromStdString(meta.url("/owned.torrent"))+"</link></item></channel></rss>";
  a::f::Profile area(feed.url(),nullptr,true);const auto paths=a::prepare(area,meta.url("/owned.torrent"));const auto signedBody=a::bytes(area.bodyPath);std::atomic<int> feeds{0},metadata{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},{},{},a::feedFactory(area,feeds),a::factory(area,metadata));
  QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),notices(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed),snapshots(&bridge,&app::SessionBridge::snapshotReady);
  QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.size()==1,15000);QCOMPARE(feeds.load(),0);QCOMPARE(metadata.load(),0);QVERIFY(bridge.subscriptionCatalog({sessions.front().front().toULongLong(),1,1}));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!catalogs.empty(),10000);QVERIFY(bridge.subscriptionCommand(a::command(catalogs,ui::SubscriptionAction::Check)));
  QTRY_VERIFY2_WITH_TIMEOUT(automaticTerminal(notices),qPrintable(a::diagnostic(notices,metadata.load())),25000);QVERIFY2(a::notice(notices,"added to the download queue"),qPrintable(a::diagnostic(notices,metadata.load())));QCOMPARE(feeds.load(),1);QCOMPARE(metadata.load(),1);QCOMPARE(feed.requests.size(),qsizetype{1});QCOMPARE(meta.requests.size(),qsizetype{1});QCOMPARE(a::bytes(paths.torrents+"/Owned.torrent"),meta.body);QCOMPARE(a::bytes(area.bodyPath),signedBody);
  const auto values=search::parseSubscriptionResults(a::bytes(area.resultsPath).toStdString());QVERIFY(values&&values->rows.size()==2);const auto key=QCryptographicHash::hash("-7:Scan Owned",QCryptographicHash::Sha1).left(10).toStdString();const auto selected=std::ranges::find(values->rows,key,&search::SubscriptionResultValue::key);QVERIFY(selected!=values->rows.end()&&selected->read&&!selected->deleted);const auto inventory=a::library(area);QCOMPARE(inventory.downloads.size(),std::size_t{1});a::noPending(area);
  QTRY_VERIFY_WITH_TIMEOUT(a::oneSnapshot(snapshots),10000);const auto live=qvariant_cast<ui::Presentation>(snapshots.last()[0]);QVERIFY((live->items.front().actionMask&(1U<<static_cast<unsigned>(core::DownloadAction::Start)))==0);QVERIFY(live->items.front().state!="Error");QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));
 }

 void globalCancelJoinsWithoutIdleRetryThenChangedUnreadRecovers(){
  a::w::Server server;server.body=a::metainfo();server.holdBody=true;a::f::Profile area(server.url(),nullptr,true);const auto paths=a::prepare(area,server.url("/owned.torrent"));const auto inventory=a::bytes(area.path+"/downloads.config");std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},{},{},{},a::factory(area,made));
  QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),packets(&bridge,&app::SessionBridge::subscriptionReady),notices(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.size()==1,15000);QVERIFY(bridge.subscriptionCatalog({sessions.front().front().toULongLong(),1,1}));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!catalogs.empty(),10000);QVERIFY(bridge.subscriptionCommand(a::command(catalogs)));QTRY_COMPARE_WITH_TIMEOUT(server.bodyPrefixBytes,1,10000);
  // A no-op unread event cannot produce another hint while this owner is live.
  QVERIFY(bridge.subscriptionCommand(a::command(catalogs,ui::SubscriptionAction::MarkUnread,2)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);bridge.cancel();QTRY_VERIFY_WITH_TIMEOUT(a::notice(notices,"result remains unread"),10000);QCOMPARE(made.load(),1);QCOMPARE(a::bytes(area.path+"/downloads.config"),inventory);QVERIFY(QDir(paths.torrents).entryList(QDir::Files).empty());a::noPending(area);
  QTest::qWait(2300);QCOMPARE(made.load(),1);QCOMPARE(server.requests.size(),qsizetype{1});
  server.release();server.holdBody=false;const auto recoveryNotices=notices.size();QVERIFY(bridge.subscriptionCommand(a::command(catalogs,ui::SubscriptionAction::MarkRead,3)));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);QVERIFY(bridge.subscriptionCommand(a::command(catalogs,ui::SubscriptionAction::MarkUnread,4)));QTRY_VERIFY2_WITH_TIMEOUT(automaticTerminal(notices,recoveryNotices),qPrintable(a::diagnostic(notices,made.load())),20000);QVERIFY2(a::notice(notices,"added to the download queue"),qPrintable(a::diagnostic(notices,made.load())));QCOMPARE(made.load(),2);a::noPending(area);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));
 }
 void existingAutomaticTargetKeepsActualLocationAndNeverRestarts(){
  a::w::Server server;server.body=a::metainfo();a::f::Profile area(server.url(),nullptr,true);const auto paths=a::prepare(area,server.url("/owned.torrent"));
  const auto retained=area.root.filePath("retained-location");QVERIFY(QDir().mkpath(retained));const auto input=area.root.filePath("genuine-prior.torrent");a::w::write(input,server.body);
  {auto lease=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(lease);QVERIFY(storage::importTorrent(*lease,input.toStdString(),retained.toStdString(),true));}
  const auto before=a::library(area);QCOMPARE(before.downloads.size(),std::size_t{1});const auto hash=before.downloads.front().hash;QVERIFY(hash);const auto inventory=a::bytes(area.path+"/downloads.config"),archive=a::bytes(QString::fromStdString(before.downloads.front().torrentFile));
  std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},{},{},{},a::factory(area,made));QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),notices(&bridge,&app::SessionBridge::messageReady),snapshots(&bridge,&app::SessionBridge::snapshotReady),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.size()==1,15000);QVERIFY(bridge.subscriptionCatalog({sessions.front().front().toULongLong(),1,1}));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!catalogs.empty(),10000);QVERIFY(bridge.subscriptionCommand(a::command(catalogs)));QTRY_VERIFY2_WITH_TIMEOUT(automaticTerminal(notices),qPrintable(a::diagnostic(notices,made.load())),20000);QVERIFY2(a::notice(notices,"not restarted"),qPrintable(a::diagnostic(notices,made.load())));
  QCOMPARE(made.load(),1);QCOMPARE(a::bytes(area.path+"/downloads.config"),inventory);QCOMPARE(a::bytes(QString::fromStdString(before.downloads.front().torrentFile)),archive);QCOMPARE(a::bytes(paths.torrents+"/Owned.torrent"),archive);const auto after=a::library(area);QCOMPARE(after.downloads.size(),std::size_t{1});QCOMPARE(QString::fromStdString(after.downloads.front().saveDirectory),retained);QCOMPARE(after.downloads.front().originalState,std::int64_t{70});QVERIFY(after.paused.empty());QVERIFY(!QFile::exists(area.path+"/dlhistorya.config"));
  QTRY_VERIFY_WITH_TIMEOUT(a::oneSnapshot(snapshots),10000);const auto snapshot=qvariant_cast<ui::Presentation>(snapshots.last()[0]);QVERIFY(snapshot&&snapshot->items.size()==1);QVERIFY((snapshot->items.front().actionMask&(1U<<static_cast<unsigned>(core::DownloadAction::Start)))!=0);QVERIFY((snapshot->items.front().actionMask&(1U<<static_cast<unsigned>(core::DownloadAction::Stop)))==0);a::noPending(area);
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));
 }

 void automaticActivitySignalMayDeleteBridgeWithoutStaleHistory(){
  a::w::Server server;server.body=a::metainfo();server.holdBody=true;a::f::Profile area(server.url(),nullptr,true);a::prepare(area,server.url("/owned.torrent"));std::atomic<int> made{0};QPointer<app::SessionBridge> bridge(new app::SessionBridge(nullptr,{},{},{},{},{},{},{},{},a::factory(area,made)));
  QSignalSpy sessions(bridge.data(),&app::SessionBridge::columnLayoutSessionReady),catalogs(bridge.data(),&app::SessionBridge::subscriptionCatalogReady),histories(bridge.data(),&app::SessionBridge::subscriptionHistoryChanged);
  QVERIFY(bridge->openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(bridge&&!bridge->busy()&&sessions.size()==1,15000);QVERIFY(bridge->subscriptionCatalog({sessions.front().front().toULongLong(),1,1}));QTRY_VERIFY_WITH_TIMEOUT(bridge&&!bridge->busy()&&!catalogs.empty(),10000);
  qsizetype before=-1;QObject::connect(bridge.data(),&app::SessionBridge::messageReady,[&](const QString& message){if(message.contains("Fetching an unread automatic")){before=histories.size();delete bridge.data();}});
  QVERIFY(bridge->subscriptionCommand(a::command(catalogs)));QTRY_VERIFY_WITH_TIMEOUT(!bridge,15000);QVERIFY(before>=0);QCOMPARE(histories.size(),before);QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));a::noPending(area);
 }

 void namedArtifactConflictAndWrongConcreteModeHaveNoIntake_data(){QTest::addColumn<int>("mode");QTest::newRow("foreign-named-bytes")<<0;QTest::newRow("manual-owner-is-not-auto")<<1;QTest::newRow("detached-auto-receiver")<<2;}
 void namedArtifactConflictAndWrongConcreteModeHaveNoIntake(){
  QFETCH(int,mode);a::w::Server server;server.body=a::metainfo();a::f::Profile area(server.url(),nullptr,true);const auto paths=a::prepare(area,server.url("/owned.torrent"));const auto inventory=a::bytes(area.path+"/downloads.config");if(mode==0){a::w::write(paths.torrents+"/Owned.torrent","foreign-owned-by-user");}std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},{},{},{},a::factory(area,made,mode));
  QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),notices(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.size()==1,15000);QVERIFY(bridge.subscriptionCatalog({sessions.front().front().toULongLong(),1,1}));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!catalogs.empty(),10000);QVERIFY(bridge.subscriptionCommand(a::command(catalogs)));QTRY_VERIFY2_WITH_TIMEOUT(automaticTerminal(notices),qPrintable(a::diagnostic(notices,made.load())),15000);QVERIFY2(a::notice(notices,"result remains unread")||a::notice(notices,"result stays unread"),qPrintable(a::diagnostic(notices,made.load())));
  QCOMPARE(made.load(),1);QCOMPARE(server.connections,mode==0?1:0);QCOMPARE(a::bytes(area.path+"/downloads.config"),inventory);if(mode==0){QCOMPARE(a::bytes(paths.torrents+"/Owned.torrent"),QByteArray("foreign-owned-by-user"));}a::noPending(area);QTest::qWait(1300);QCOMPARE(made.load(),1);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));
 }
};
QTEST_MAIN(SubscriptionAutomaticBridgeTests)
#include "subscription_automatic_bridge_tests.moc"
