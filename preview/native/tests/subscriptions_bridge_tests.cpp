// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "library_window.hpp"
#include "subscriptions_page.hpp"
#include "subscription_values.hpp"
#include "subscription_rss_http_fixture.hpp"
#include <QAction>
#include <QDateTime>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QtTest>
using namespace biglybtc;
namespace {
namespace f=subscription_http_fixture;namespace w=rss_fixture;namespace s=search;
QByteArray bytes(const QString& path){QFile file(path);w::require(file.open(QIODevice::ReadOnly),"Private subscription read failed");return file.readAll();}
void prepare(f::Profile& area,bool due=false,bool existing=false){
  auto config=storage::ConfigFile::load(*area.lease,"biglybt.config");w::require(bool(config),"Private configuration missing");
  for(const auto* key:{"Download History Enabled","TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable","IPV6 Enable Support"}){w::require(bool(config->document().replaceValue(key,"i0e")),"Private disabled setting failed");}
  w::require(bool(config->document().replaceValue("Bind IP","9:127.0.0.1"))&&bool(config->document().replaceValue("Enforce Bind IP","i1e"))&&bool(config->save(*area.lease)),"Private bind/settings failed");
  w::require(bool(storage::ConfigFile::createMissing(*area.lease,"downloads.config","d9:downloadslee"))&&bool(storage::ConfigFile::createMissing(*area.lease,"tables.config","de")),"Private empty library failed");
  auto row=core::Document::parse(area.metadataRow.toStdString());w::require(bool(row),"Actual metadata missing");const auto encoded=core::encodeValue(*row->find("history"));w::require(bool(encoded),"Actual history missing");auto history=core::Document::parse(*encoded);w::require(bool(history),"Actual history malformed");w::require(bool(history->replaceValue("last_scan",f::integer(due?0:QDateTime::currentMSecsSinceEpoch()).toStdString()))&&bool(history->replaceValue("num_unread",existing?"i1e":"i0e")),"Private schedule setup failed");w::require(bool(row->replaceValue("history",history->serialize())),"Private metadata update failed");area.metadataRow=QByteArray::fromStdString(row->serialize());area.configuration=f::dictionary({{"subs","l"+area.metadataRow+"e"}});w::write(area.path+"/subscriptions.config",area.configuration);
  if(existing){const auto json=QJsonDocument(QJsonObject{{"n","Stored"},{"tf","1"},{"ts","0"},{"future","retained"}}).toJson(QJsonDocument::Compact);w::write(area.resultsPath,f::dictionary({{"results","l"+f::dictionary({{"key",w::b("stored-key")},{"read",f::integer(0)},{"result_json",w::b(json)},{"tz",f::integer(1)}})+"e"}}));}area.lease.reset();
}
void wire(app::SessionBridge& bridge,ui::LibraryWindow& window,bool preview){
  using B=app::SessionBridge;using W=ui::LibraryWindow;window.setOfflinePreview(preview);
  QObject::connect(&bridge,&B::snapshotReady,&window,&W::setPresentation);QObject::connect(&bridge,&B::columnLayoutSessionReady,&window,&W::beginColumnLayoutSession);QObject::connect(&bridge,&B::busyChanged,&window,&W::setBusy);
  QObject::connect(&bridge,&B::subscriptionCatalogReady,&window,&W::setSubscriptionCatalog);QObject::connect(&bridge,&B::subscriptionReady,&window,&W::setSubscriptionResult);QObject::connect(&bridge,&B::subscriptionHistoryChanged,&window,&W::subscriptionHistoryChanged);
  QObject::connect(&window,&W::subscriptionCatalogRequested,&bridge,[owner=QPointer<B>(&bridge),view=QPointer<W>(&window)](ui::SubscriptionCatalogKey key){if(owner&&view&&!owner->subscriptionCatalog(key)&&view){view->rejectSubscriptionCatalog(key);}});
  QObject::connect(&window,&W::subscriptionCommandRequested,&bridge,[owner=QPointer<B>(&bridge),view=QPointer<W>(&window)](ui::SubscriptionCommand command){if(owner&&view&&!owner->subscriptionCommand(command)&&view){view->rejectSubscriptionCommand(command);}});
  QObject::connect(&window,&W::subscriptionCancelRequested,&bridge,&B::cancelSubscription);
}
app::SessionBridge::SubscriptionFetchFactory factory(f::Profile& area,std::atomic<int>& made){return [&area,&made](const storage::ProfileLease& lease,const std::filesystem::path& path,quint64 session,const ui::SubscriptionKey& key,std::stop_token stop){++made;auto opened=s::RssHttpFetch::openSubscriptionFixture(lease,path,session,{static_cast<std::size_t>(key.sourceIndex),key.sid.toStdString(),key.publicKey.toStdString(),key.singleton?std::optional{key.singletonKey.toStdString()}:std::nullopt,key.version},area.endpoint(),stop);return opened?std::move(*opened):std::unique_ptr<s::RssHttpFetch>{};};}
ui::SubscriptionPresentation last(const QSignalSpy& spy){return qvariant_cast<ui::SubscriptionPresentation>(spy.last()[0]);}
bool terminal(const QSignalSpy& spy,ui::SubscriptionAction action,ui::SubscriptionState state){return !spy.empty()&&last(spy)->key.action==action&&last(spy)->state==state;}
}
class SubscriptionsBridgeTests final:public QObject {
 Q_OBJECT
private slots:
 void actualPreviewHistoryAndForgedCheckHaveZeroProviders(){
   w::Server server;f::Profile area(server.url());prepare(area,false,true);const auto beforeResults=bytes(area.resultsPath),body=bytes(area.bodyPath);std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},factory(area,made));ui::LibraryWindow window;wire(bridge,window,true);
   QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),packets(&bridge,&app::SessionBridge::subscriptionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
   QVERIFY(bridge.open(area.path.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==1,15000);window.findChild<QAction*>("classic-subscriptions")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&terminal(packets,ui::SubscriptionAction::ReadResults,ui::SubscriptionState::Ready),10000);
   QCOMPARE(made.load(),0);QCOMPARE(server.connections,0);auto* page=window.subscriptionsPage();QVERIFY(page);QCOMPARE(page->findChild<QTableWidget*>("subscription-results")->rowCount(),1);QVERIFY(!page->findChild<QPushButton*>("subscriptions-check")->isEnabled());
   const auto catalog=qvariant_cast<ui::SubscriptionCatalogPresentation>(catalogs.last()[0]);ui::SubscriptionCommand forged{{catalog->key.session,catalog->key.page,catalog->key.request+100,catalog->generation,catalog->sources[0].key,ui::SubscriptionAction::Check},{},std::make_shared<std::stop_source>()};QVERIFY(bridge.subscriptionCommand(forged));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&terminal(packets,ui::SubscriptionAction::Check,ui::SubscriptionState::Rejected),10000);QCOMPARE(made.load(),0);QCOMPARE(bytes(area.resultsPath),beforeResults);QCOMPARE(bytes(area.bodyPath),body);
   QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);auto lock=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(lock);
 }
 void genuineNormalWorkerCancelThenCheckCommitReadDeleteAndReopen(){
   w::Server server;server.holdReply=true;f::Profile area(server.url());prepare(area);std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},factory(area,made));ui::LibraryWindow window;wire(bridge,window,false);
   QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),packets(&bridge,&app::SessionBridge::subscriptionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
   QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==1,15000);QCOMPARE(made.load(),0);window.findChild<QAction*>("classic-subscriptions")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&terminal(packets,ui::SubscriptionAction::ReadResults,ui::SubscriptionState::Ready),10000);QCOMPARE(made.load(),0);
   auto* page=window.subscriptionsPage();auto* table=page->findChild<QTableWidget*>("subscription-results");page->findChild<QPushButton*>("subscriptions-check")->click();QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(),qsizetype{1},15000);page->findChild<QPushButton*>("subscriptions-cancel")->click();QTRY_VERIFY_WITH_TIMEOUT(terminal(packets,ui::SubscriptionAction::Check,ui::SubscriptionState::Cancelled),10000);QCOMPARE(table->rowCount(),0);
   server.release();QVERIFY(page->findChild<QPushButton*>("subscriptions-check")->isEnabled());page->findChild<QPushButton*>("subscriptions-check")->click();const auto diagnostic=[&]{const auto value=packets.empty()?ui::SubscriptionPresentation{}:last(packets);return QString("factories=%1 GETs=%2 packets=%3 busy=%4 state=%5 message=%6").arg(made.load()).arg(server.requests.size()).arg(packets.count()).arg(bridge.busy()).arg(value?static_cast<int>(value->state):-1).arg(value?value->message.left(128):QString{});};
   QTRY_VERIFY2_WITH_TIMEOUT(terminal(packets,ui::SubscriptionAction::Check,ui::SubscriptionState::Ready),qPrintable(diagnostic()),15000);QVERIFY(last(packets)->committed);QCOMPARE(made.load(),2);QCOMPARE(table->rowCount(),1);QCOMPARE(table->item(0,1)->text(),QString("Owned"));QCOMPARE(table->item(0,0)->text(),QString("No"));QCOMPARE(bytes(area.bodyPath),area.body);QVERIFY(!QFile::exists(area.path+"/.subscriptions-native-write"));
   table->selectRow(0);page->findChild<QPushButton*>("subscriptions-read")->click();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&terminal(packets,ui::SubscriptionAction::MarkRead,ui::SubscriptionState::Ready),10000);QVERIFY(last(packets)->committed);QCOMPARE(table->item(0,0)->text(),QString("Yes"));
   table->selectRow(0);page->findChild<QPushButton*>("subscriptions-delete")->click();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&terminal(packets,ui::SubscriptionAction::Delete,ui::SubscriptionState::Ready),10000);QCOMPARE(table->rowCount(),0);QCOMPARE(made.load(),2);QVERIFY(server.failure.isEmpty());const auto retained=s::parseSubscriptionResults(bytes(area.resultsPath).toStdString());QVERIFY(retained&&retained->rows.size()==1&&retained->rows[0].deleted);
   QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);window.setOfflinePreview(true);QVERIFY(bridge.open(area.path.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==2,15000);const auto catalogsBeforeReopen=catalogs.count(),packetsBeforeReopen=packets.count();window.findChild<QAction*>("classic-subscriptions")->trigger();
   QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&catalogs.count()>catalogsBeforeReopen&&packets.count()>packetsBeforeReopen&&terminal(packets,ui::SubscriptionAction::ReadResults,ui::SubscriptionState::Ready),10000);const auto reopened=qvariant_cast<ui::SubscriptionCatalogPresentation>(catalogs.last()[0]);QCOMPARE(reopened->key.session,window.subscriptionsPage()->session());QCOMPARE(last(packets)->key.session,reopened->key.session);QCOMPARE(window.subscriptionsPage()->findChild<QTableWidget*>("subscription-results")->rowCount(),0);QCOMPARE(made.load(),2);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),2,15000);auto lock=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(lock);
 }
 void actualDueCheckUsesIdleWorkerWithoutOpeningPageAndCloseJoins(){
   w::Server server;server.holdReply=true;f::Profile area(server.url());prepare(area,true);std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},factory(area,made));QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),history(&bridge,&app::SessionBridge::subscriptionHistoryChanged),closed(&bridge,&app::SessionBridge::sessionClosed);
   QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==1,15000);QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(),qsizetype{1},15000);QCOMPARE(made.load(),1);server.release();QTRY_COMPARE_WITH_TIMEOUT(history.count(),1,15000);auto rows=s::parseSubscriptionResults(bytes(area.resultsPath).toStdString());QVERIFY(rows&&rows->rows.size()==1&&rows->rows[0].read);
   QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);auto lock=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(lock);QVERIFY(server.failure.isEmpty());
 }
 void closeDuringRealBodyWaitCancelsBeforeLeaseRelease(){
   w::Server server;server.holdBody=true;f::Profile area(server.url());prepare(area,true);std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},factory(area,made));QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
   QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==1,15000);QTRY_COMPARE_WITH_TIMEOUT(server.bodyPrefixBytes,1,15000);QCOMPARE(made.load(),1);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);auto lock=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(lock);QCOMPARE(bytes(area.resultsPath),f::dictionary({{"results","le"}}));QVERIFY(!QFile::exists(area.path+"/.subscriptions-native-write"));
 }
};
QTEST_MAIN(SubscriptionsBridgeTests)
#include "subscriptions_bridge_tests.moc"
