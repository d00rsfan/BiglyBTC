// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "library_window.hpp"
#include "rss_sources_page.hpp"
#include "rss_http_fetch_fixture.hpp"
#include <QAction>
#include <QComboBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QtTest>
using namespace biglybtc;
namespace {
namespace f=rss_fixture;
void prepare(f::Profile& area){
  auto config=storage::ConfigFile::load(*area.lease,"biglybt.config");f::require(bool(config),"Private configuration missing");
  for(const auto* key:{"Download History Enabled","TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable","IPV6 Enable Support"}){f::require(bool(config->document().replaceValue(key,"i0e")),"Private disabled setting failed");}
  f::require(bool(config->document().replaceValue("Bind IP","9:127.0.0.1"))&&bool(config->document().replaceValue("Enforce Bind IP","i1e")),"Private bind failed");f::require(bool(config->save(*area.lease)),"Private settings save failed");
  f::require(bool(storage::ConfigFile::createMissing(*area.lease,"downloads.config","d9:downloadslee"))&&bool(storage::ConfigFile::createMissing(*area.lease,"tables.config","de")),"Private empty library failed");area.lease.reset();
}
void wire(app::SessionBridge& bridge,ui::LibraryWindow& window,bool preview){
  using B=app::SessionBridge;using W=ui::LibraryWindow;window.setOfflinePreview(preview);
  QObject::connect(&bridge,&B::snapshotReady,&window,&W::setPresentation);QObject::connect(&bridge,&B::columnLayoutSessionReady,&window,&W::beginColumnLayoutSession);QObject::connect(&bridge,&B::busyChanged,&window,&W::setBusy);
  QObject::connect(&bridge,&B::rssCatalogReady,&window,&W::setRssCatalog);QObject::connect(&bridge,&B::rssFetchReady,&window,&W::setRssFetchResult);
  QObject::connect(&window,&W::rssCatalogRequested,&bridge,[owner=QPointer<B>(&bridge),view=QPointer<W>(&window)](ui::RssCatalogRequest key){if(owner&&view&&!owner->rssCatalog(key)&&view){view->rejectRssCatalog(key);}});
  QObject::connect(&window,&W::rssFetchRequested,&bridge,[owner=QPointer<B>(&bridge),view=QPointer<W>(&window)](ui::RssFetchCommand command){if(owner&&view&&!owner->rssFetch(command)&&view){view->rejectRssFetch(command);}});
  QObject::connect(&window,&W::rssCancelRequested,&bridge,&B::cancelRssFetch);
}
app::SessionBridge::RssFetchFactory factory(std::atomic<int>& calls,const f::Profile& area){
  return [&calls,&area](const storage::ProfileLease& lease,const std::filesystem::path& path,quint64 session,const ui::RssSourceKey& key,std::stop_token stop){
    ++calls;auto opened=search::RssHttpFetch::openFixture(lease,path,session,{static_cast<std::size_t>(key.sourceIndex),key.id,key.uid.toStdString()},area.endpoint(),stop);
    return opened?std::move(*opened):std::unique_ptr<search::RssHttpFetch>{};
  };
}
ui::RssFetchPresentation lastFetch(const QSignalSpy& spy){return qvariant_cast<ui::RssFetchPresentation>(spy.last()[0]);}
QByteArray readBytes(const QString& path){QFile file(path);f::require(file.open(QIODevice::ReadOnly),"Private fixture read failed");return file.readAll();}
}
class RssSourcesBridgeTests final:public QObject {
  Q_OBJECT
private slots:
  void actualMalformedAndAmbiguousCatalogsCannotConstructAProvider_data(){QTest::addColumn<bool>("duplicate");QTest::newRow("malformed-primary")<<false;QTest::newRow("duplicate-id-and-uid")<<true;}
  void actualMalformedAndAmbiguousCatalogsCannotConstructAProvider(){
    QFETCH(bool,duplicate);f::Server server;f::Profile area(server.url());
    if(duplicate){auto doc=core::Document::parse(readBytes(area.path+"/metasearch.config").toStdString());QVERIFY(doc);const auto* rows=doc->find("engines");QVERIFY(rows&&rows->children.size()==1);const auto row=core::encodeValue(rows->children[0]);QVERIFY(row);QVERIFY(doc->replaceValue("engines","l"+*row+*row+"e"));f::write(area.path+"/metasearch.config",QByteArray::fromStdString(doc->serialize()));}
    else{f::write(area.path+"/metasearch.config","broken");}prepare(area);std::atomic<int> calls{0};app::SessionBridge bridge(nullptr,{},{},{},{},factory(calls,area));ui::LibraryWindow window;wire(bridge,window,false);
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::rssCatalogReady),fetched(&bridge,&app::SessionBridge::rssFetchReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==1,15000);window.findChild<QAction*>("classic-rss-sources")->trigger();QTRY_VERIFY(!bridge.busy()&&catalogs.count()==1);
    const auto packet=qvariant_cast<ui::RssCatalogPresentation>(catalogs.last()[0]);QCOMPARE(calls.load(),0);QVERIFY(!window.rssSourcesPage()->findChild<QPushButton*>("rss-fetch")->isEnabled());
    if(duplicate){QCOMPARE(packet->sources.size(),std::size_t{2});ui::RssFetchCommand forged{{packet->key.session,packet->key.page,packet->key.request+100,packet->generation,packet->sources[0].key},std::make_shared<std::stop_source>()};QVERIFY(bridge.rssFetch(forged));QTRY_VERIFY(!bridge.busy()&&fetched.count()==1);QCOMPARE(lastFetch(fetched)->state,ui::RssFetchState::Rejected);}
    else{QVERIFY(!packet->failure.isEmpty());QVERIFY(packet->sources.empty());}QCOMPARE(calls.load(),0);QCOMPARE(server.connections,0);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);
  }
  void actualPreviewCatalogAndForgedFetchInvokeZeroConcreteFactories(){
    f::Server server;f::Profile area(server.url());prepare(area);std::atomic<int> calls{0};app::SessionBridge bridge(nullptr,{},{},{},{},factory(calls,area));ui::LibraryWindow window;wire(bridge,window,true);
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::rssCatalogReady),fetched(&bridge,&app::SessionBridge::rssFetchReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(area.path.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==1,15000);
    window.findChild<QAction*>("classic-rss-sources")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&catalogs.count()==1,5000);
    const auto catalog=qvariant_cast<ui::RssCatalogPresentation>(catalogs.last()[0]);QCOMPARE(catalog->sources.size(),std::size_t{1});QVERIFY(!catalog->normalNetwork);QCOMPARE(calls.load(),0);
    QVERIFY(!window.rssSourcesPage()->findChild<QPushButton*>("rss-fetch")->isEnabled());
    ui::RssFetchCommand forged{{catalog->key.session,catalog->key.page,catalog->key.request+100,catalog->generation,catalog->sources[0].key},std::make_shared<std::stop_source>()};
    QVERIFY(bridge.rssFetch(forged));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&fetched.count()==1,5000);QCOMPARE(lastFetch(fetched)->state,ui::RssFetchState::Rejected);QCOMPARE(calls.load(),0);QCOMPARE(server.connections,0);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);auto lock=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(lock);
  }
  void genuineNormalWorkerManualFetchAndCancelRecoverWithoutAutoFetch(){
    f::Server server;server.holdReply=true;f::Profile area(server.url());prepare(area);std::atomic<int> calls{0};app::SessionBridge bridge(nullptr,{},{},{},{},factory(calls,area));ui::LibraryWindow window;wire(bridge,window,false);window.show();
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::rssCatalogReady),fetched(&bridge,&app::SessionBridge::rssFetchReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==1,15000);QCOMPARE(calls.load(),0);
    window.findChild<QAction*>("classic-rss-sources")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&catalogs.count()==1,5000);QCOMPARE(calls.load(),0);
    auto* page=window.rssSourcesPage();page->findChild<QPushButton*>("rss-fetch")->click();QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(),qsizetype{1},15000);QCOMPARE(calls.load(),1);
    page->findChild<QPushButton*>("rss-cancel")->click();QTRY_VERIFY_WITH_TIMEOUT(fetched.count()>=2&&lastFetch(fetched)->state==ui::RssFetchState::Cancelled,10000);QCOMPARE(page->findChild<QTableWidget*>("rss-results")->rowCount(),0);
    server.release();auto* fetchButton=page->findChild<QPushButton*>("rss-fetch");QVERIFY(fetchButton->isEnabled());
    QSignalSpy recoveryRequests(&window,&ui::LibraryWindow::rssFetchRequested);fetchButton->click();QCOMPARE(recoveryRequests.count(),1);const auto recovery=qvariant_cast<ui::RssFetchCommand>(recoveryRequests.last()[0]);QVERIFY(recovery.receiver&&!recovery.receiver->stop_requested());
    const auto diagnostic=[&]{const auto packet=fetched.isEmpty()?ui::RssFetchPresentation{}:lastFetch(fetched);return QStringLiteral("factories=%1 requests=%2 packets=%3 state=%4 busy=%5 fetch_enabled=%6 receiver_cancelled=%7 message=%8")
      .arg(calls.load()).arg(server.requests.size()).arg(fetched.count()).arg(packet?static_cast<int>(packet->state):-1).arg(bridge.busy()).arg(fetchButton->isEnabled()).arg(recovery.receiver->stop_requested()).arg(packet?packet->message.left(128):QString{});};
    QTRY_VERIFY2_WITH_TIMEOUT(fetched.count()>=4&&lastFetch(fetched)->state==ui::RssFetchState::Ready,qPrintable(diagnostic()),15000);QCOMPARE(calls.load(),2);QVERIFY(!recovery.receiver->stop_requested());
    QCOMPARE(page->findChild<QTableWidget*>("rss-results")->rowCount(),1);QCOMPARE(page->findChild<QTableWidget*>("rss-results")->item(0,0)->text(),QStringLiteral("Owned"));QVERIFY(server.failure.isEmpty());
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);auto lock=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(lock);
  }
  void pageCloseReopenAndProfileSwitchRejectActualOldProducerPackets(){
    f::Server server;server.holdReply=true;f::Profile area(server.url()),next(server.url());prepare(area);prepare(next);std::atomic<int> calls{0};app::SessionBridge bridge(nullptr,{},{},{},{},factory(calls,area));ui::LibraryWindow window;wire(bridge,window,false);
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::rssCatalogReady),fetched(&bridge,&app::SessionBridge::rssFetchReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==1,15000);auto* action=window.findChild<QAction*>("classic-rss-sources");action->trigger();QTRY_VERIFY(!bridge.busy()&&catalogs.count()==1);
    window.rssSourcesPage()->findChild<QPushButton*>("rss-fetch")->click();QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(),qsizetype{1},15000);QVERIFY(fetched.count()>0);const auto old=lastFetch(fetched);
    window.findChild<QAction*>("classic-close-tab")->trigger();QVERIFY(!window.rssSourcesPage());action->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&catalogs.count()==2,10000);QCOMPARE(calls.load(),1);
    auto stale=std::make_shared<ui::RssFetchPacket>(*old);stale->state=ui::RssFetchState::Ready;stale->rows.push_back({QStringLiteral("Stale"),{},{},{},{},{}});window.setRssFetchResult(stale);QCOMPARE(window.rssSourcesPage()->findChild<QTableWidget*>("rss-results")->rowCount(),0);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(bridge.openNormal(next.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==2,15000);
    window.setRssFetchResult(stale);QCOMPARE(window.rssSourcesPage()->findChild<QTableWidget*>("rss-results")->rowCount(),0);QCOMPARE(calls.load(),1);
    window.rssSourcesPage()->findChild<QPushButton*>("rss-refresh-sources")->click();QTRY_VERIFY(!bridge.busy()&&catalogs.count()==3);QCOMPARE(calls.load(),1);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),2,15000);auto lock=storage::ProfileLease::acquire(next.path.toStdString());QVERIFY(lock);
  }
};
QTEST_MAIN(RssSourcesBridgeTests)
#include "rss_sources_bridge_tests.moc"
