// SPDX-License-Identifier: GPL-2.0-or-later
#include "library_window.hpp"
#include "rss_sources_page.hpp"
#include <QAbstractItemModel>
#include <QAction>
#include <QComboBox>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QLabel>
#include <QSignalSpy>
#include <QTableWidget>
#include <QtTest>
using namespace biglybtc;
namespace {
ui::RssCatalogPresentation catalog(ui::RssCatalogRequest key,QString name=QStringLiteral("Owned")){
  auto packet=std::make_shared<ui::RssCatalogPacket>();packet->key=key;packet->generation=key.request;packet->normalNetwork=true;
  packet->sources.push_back({{0,7,"uid"},std::move(name),QStringLiteral("https://feeds.example.org"),true});return packet;
}
ui::RssFetchPresentation ready(ui::RssFetchKey key,QString title=QStringLiteral("Owned result")){
  auto packet=std::make_shared<ui::RssFetchPacket>();packet->key=std::move(key);packet->state=ui::RssFetchState::Ready;
  packet->rows.push_back({std::move(title),QStringLiteral("12"),QStringLiteral("2"),QStringLiteral("3"),QStringLiteral("raw date"),QStringLiteral("https://result.example.org/item")});packet->message=QStringLiteral("Complete");return packet;
}
}
class RssSourcesUiTests final:public QObject {
  Q_OBJECT
private slots:
  void manualViewRejectsStaleCatalogAndResultPacketsWithoutAutoFetch(){
    ui::LibraryWindow window;window.setOfflinePreview(false);window.beginColumnLayoutSession(101);window.show();
    QSignalSpy catalogs(&window,&ui::LibraryWindow::rssCatalogRequested),fetches(&window,&ui::LibraryWindow::rssFetchRequested);
    window.findChild<QAction*>("classic-rss-sources")->trigger();QCOMPARE(catalogs.count(),1);QCOMPARE(fetches.count(),0);
    const auto request=qvariant_cast<ui::RssCatalogRequest>(catalogs.last()[0]);auto stale=request;++stale.request;window.setRssCatalog(catalog(stale,QStringLiteral("Stale")));
    auto* page=window.rssSourcesPage();QVERIFY(page);auto* combo=page->findChild<QComboBox*>("rss-source-selector");QCOMPARE(combo->count(),0);
    window.setRssCatalog(catalog(request));QCOMPARE(combo->count(),1);QCOMPARE(fetches.count(),0);combo->setCurrentIndex(0);QCOMPARE(fetches.count(),0);
    page->findChild<QPushButton*>("rss-fetch")->click();QCOMPARE(fetches.count(),1);const auto command=qvariant_cast<ui::RssFetchCommand>(fetches.last()[0]);
    auto wrong=command.key;++wrong.request;window.setRssFetchResult(ready(wrong,QStringLiteral("Stale")));auto* table=page->findChild<QTableWidget*>("rss-results");QCOMPARE(table->rowCount(),0);
    window.setRssFetchResult(ready(command.key));QCOMPARE(table->rowCount(),1);QCOMPARE(table->item(0,0)->text(),QStringLiteral("Owned result"));
    const auto capture=qEnvironmentVariable("BIGLYBTC_RSS_UI_CAPTURE_PATH");if(!capture.isEmpty()){QCoreApplication::processEvents();QVERIFY2(window.grab().save(capture,"PNG"),"Could not save the optional populated classic RSS window capture");}
    QSignalSpy imports(&window,&ui::LibraryWindow::torrentImportRequested),magnets(&window,&ui::LibraryWindow::magnetImportRequested);
    table->selectRow(0);QTest::keyClick(table,Qt::Key_Return);QCOMPARE(imports.count(),0);QCOMPARE(magnets.count(),0);
  }
  void closeReopenAndSessionSwitchInvalidateTargetsAndStopReceiver(){
    ui::LibraryWindow window;window.setOfflinePreview(false);window.beginColumnLayoutSession(201);QSignalSpy catalogs(&window,&ui::LibraryWindow::rssCatalogRequested),fetches(&window,&ui::LibraryWindow::rssFetchRequested);
    auto* action=window.findChild<QAction*>("classic-rss-sources");action->trigger();auto first=qvariant_cast<ui::RssCatalogRequest>(catalogs.last()[0]);window.setRssCatalog(catalog(first));
    window.rssSourcesPage()->findChild<QPushButton*>("rss-fetch")->click();const auto old=qvariant_cast<ui::RssFetchCommand>(fetches.last()[0]);QVERIFY(!old.receiver->stop_requested());
    window.findChild<QAction*>("classic-close-tab")->trigger();QVERIFY(old.receiver->stop_requested());QVERIFY(!window.rssSourcesPage());action->trigger();
    const auto reopened=qvariant_cast<ui::RssCatalogRequest>(catalogs.last()[0]);QVERIFY(reopened.page!=first.page&&reopened.request>old.key.request);
    window.setRssCatalog(catalog(reopened,QStringLiteral("New page")));window.setRssFetchResult(ready(old.key,QStringLiteral("Old result")));
    QCOMPARE(window.rssSourcesPage()->findChild<QTableWidget*>("rss-results")->rowCount(),0);
    window.beginColumnLayoutSession(202);window.setRssCatalog(catalog(reopened));QCOMPARE(window.rssSourcesPage()->findChild<QComboBox*>("rss-source-selector")->count(),0);
    window.setRssFetchResult(ready(old.key));QCOMPARE(window.rssSourcesPage()->findChild<QTableWidget*>("rss-results")->rowCount(),0);
  }
  void reentrantCancelSeesNewSessionAndCannotRestoreAnOldResult(){
    ui::RssSourcesPage page;page.beginSession(301,1);page.setAvailable(true,false);page.setCatalogPending({301,1,4});page.setCatalog(catalog({301,1,4}));
    ui::RssFetchCommand captured;
    QObject::connect(&page,&ui::RssSourcesPage::cancelRequested,&page,[&](ui::RssFetchKey){page.beginSession(302,2);});
    QObject::connect(&page,&ui::RssSourcesPage::fetchRequested,&page,[&](ui::RssFetchCommand command){command.key.request=5;captured=command;page.setFetchPending(command);page.findChild<QPushButton*>("rss-cancel")->click();});
    page.findChild<QPushButton*>("rss-fetch")->click();QVERIFY(captured.receiver&&captured.receiver->stop_requested());QCOMPARE(page.session(),quint64{302});QCOMPARE(page.page(),quint64{2});
    page.setFetchResult(ready(captured.key));QCOMPARE(page.findChild<QTableWidget*>("rss-results")->rowCount(),0);
  }
  void previewIgnoresForgedNormalHintAndReadSignalCanDeleteItsPage(){
    ui::RssSourcesPage page;page.beginSession(401,1);page.setAvailable(true,true);page.setCatalogPending({401,1,2});page.setCatalog(catalog({401,1,2}));
    QSignalSpy fetches(&page,&ui::RssSourcesPage::fetchRequested);QVERIFY(!page.findChild<QPushButton*>("rss-fetch")->isEnabled());page.findChild<QPushButton*>("rss-fetch")->click();QCOMPARE(fetches.count(),0);
    QPointer<ui::RssSourcesPage> doomed=new ui::RssSourcesPage;doomed->beginSession(402,2);doomed->setAvailable(true,false);
    QObject::connect(doomed,&ui::RssSourcesPage::catalogRequested,doomed,[&](ui::RssCatalogRequest){delete doomed.data();});
    doomed->refreshSources();QVERIFY(!doomed);
  }
  void widgetSignalReentryCannotFinishAnOldCatalogOrResult(){
    ui::RssSourcesPage page;page.beginSession(501,1);page.setAvailable(true,false);
    auto* combo=page.findChild<QComboBox*>("rss-source-selector");auto* table=page.findChild<QTableWidget*>("rss-results");auto* status=page.findChild<QLabel*>("rss-status");
    QMetaObject::Connection change;
    change=QObject::connect(combo,&QComboBox::currentIndexChanged,&page,[&](int index){if(index>=0){QObject::disconnect(change);page.beginSession(502,2);}});
    page.setCatalogPending({501,1,4});auto oldCatalog=std::make_shared<ui::RssCatalogPacket>(*catalog({501,1,4}));oldCatalog->sources.push_back({{1,8,"second"},QStringLiteral("Second old source"),QStringLiteral("https://second.example.org"),true});page.setCatalog(std::move(oldCatalog));
    QCOMPARE(page.session(),quint64{502});QCOMPARE(combo->count(),0);QCOMPARE(status->text(),QStringLiteral("Refresh to read configured RSS sources."));
    page.setCatalogPending({502,2,5});page.setCatalog(catalog({502,2,5}));ui::RssFetchCommand command;
    QObject::connect(&page,&ui::RssSourcesPage::fetchRequested,&page,[&](ui::RssFetchCommand value){value.key.request=6;command=value;page.setFetchPending(value);});
    page.findChild<QPushButton*>("rss-fetch")->click();QVERIFY(command.receiver);
    QObject::connect(table->model(),&QAbstractItemModel::rowsInserted,&page,[&]{page.beginSession(503,3);});
    page.setFetchResult(ready(command.key,QStringLiteral("Old result")));
    QCOMPARE(page.session(),quint64{503});QCOMPARE(table->rowCount(),0);QCOMPARE(combo->count(),0);QCOMPARE(status->text(),QStringLiteral("Refresh to read configured RSS sources."));
  }
};
QTEST_MAIN(RssSourcesUiTests)
#include "rss_sources_ui_tests.moc"
