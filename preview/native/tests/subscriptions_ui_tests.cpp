// SPDX-License-Identifier: GPL-2.0-or-later
#include "library_window.hpp"
#include "subscriptions_page.hpp"
#include <QAbstractItemModel>
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QtTest>
using namespace biglybtc;
namespace {
ui::SubscriptionCatalogPresentation catalog(ui::SubscriptionCatalogKey key){
  auto value=std::make_shared<ui::SubscriptionCatalogPacket>();value->key=key;value->generation=key.request;value->normalNetwork=true;
  value->sources.push_back({{0,QByteArray(10,'a'),"public-key",{},1,false},QStringLiteral("Saved Linux releases"),QStringLiteral("RSS"),true});return value;
}
ui::SubscriptionPresentation ready(ui::SubscriptionCommandKey key,QString title=QStringLiteral("Release <candidate>")){
  auto value=std::make_shared<ui::SubscriptionPacket>();value->key=std::move(key);value->state=ui::SubscriptionState::Ready;value->message=QStringLiteral("Saved results");
  value->rows.push_back({QByteArray(10,'r'),false,std::move(title),QStringLiteral("128 MiB"),QStringLiteral("12"),QStringLiteral("3"),QStringLiteral("2026-11-06T10:30:00Z"),QStringLiteral("https://result.example.org/release.torrent")});return value;
}
ui::SubscriptionCommand command(const QSignalSpy& spy){return qvariant_cast<ui::SubscriptionCommand>(spy.last()[0]);}
}
class SubscriptionsUiTests final:public QObject {
  Q_OBJECT
private slots:
  void classicViewReadsSavedResultsAndUsesExactResultKeysWithoutAutomaticCheck(){
    ui::LibraryWindow window;window.setOfflinePreview(false);window.beginColumnLayoutSession(101);window.resize(1280,800);window.show();
    QSignalSpy catalogs(&window,&ui::LibraryWindow::subscriptionCatalogRequested),commands(&window,&ui::LibraryWindow::subscriptionCommandRequested);
    window.findChild<QAction*>("classic-subscriptions")->trigger();QCOMPARE(catalogs.count(),1);QCOMPARE(commands.count(),0);
    const auto request=qvariant_cast<ui::SubscriptionCatalogKey>(catalogs.last()[0]);auto stale=request;++stale.request;window.setSubscriptionCatalog(catalog(stale));
    auto* page=window.subscriptionsPage();QVERIFY(page);auto* combo=page->findChild<QComboBox*>("subscription-selector");auto* table=page->findChild<QTableWidget*>("subscription-results");QCOMPARE(combo->count(),0);
    auto choices=std::make_shared<ui::SubscriptionCatalogPacket>(*catalog(request));auto other=choices->sources.front();other.key.sourceIndex=1;other.key.sid=QByteArray(10,'b');other.name=QStringLiteral("Another saved subscription");choices->sources.push_back(other);window.setSubscriptionCatalog(choices);QCOMPARE(combo->count(),2);QCOMPARE(commands.count(),1);const auto read=command(commands);QCOMPARE(read.key.action,ui::SubscriptionAction::ReadResults);QVERIFY(read.receiver&&!read.receiver->stop_requested());
    auto wrong=read.key;++wrong.request;window.setSubscriptionResult(ready(wrong,QStringLiteral("Stale")));QCOMPARE(table->rowCount(),0);
    window.setSubscriptionResult(ready(read.key));QCOMPARE(table->rowCount(),1);QCOMPARE(table->item(0,1)->text(),QStringLiteral("Release <candidate>"));
    const auto capture=qEnvironmentVariable("BIGLYBTC_SUBSCRIPTIONS_UI_CAPTURE_PATH");if(!capture.isEmpty()){QCoreApplication::processEvents();QVERIFY2(window.grab().save(capture,"PNG"),"Could not capture the populated classic Subscriptions window");}
    QSignalSpy imports(&window,&ui::LibraryWindow::torrentImportRequested),magnets(&window,&ui::LibraryWindow::magnetImportRequested);table->selectRow(0);QTest::keyClick(table,Qt::Key_Return);QCOMPARE(imports.count(),0);QCOMPARE(magnets.count(),0);
    page->findChild<QPushButton*>("subscriptions-read")->click();QCOMPARE(commands.count(),2);const auto mark=command(commands);QCOMPARE(mark.key.action,ui::SubscriptionAction::MarkRead);QCOMPARE(mark.resultKeys,std::vector<QByteArray>{QByteArray(10,'r')});QVERIFY(mark.key.request>read.key.request);QCOMPARE(mark.key.target,read.key.target);
    auto marked=std::make_shared<ui::SubscriptionPacket>(*ready(mark.key));marked->rows[0].read=true;window.setSubscriptionResult(marked);QCOMPARE(table->item(0,0)->text(),QStringLiteral("Yes"));
    table->selectRow(0);combo->setCurrentIndex(1);QCOMPARE(commands.count(),3);const auto otherRead=command(commands);QCOMPARE(otherRead.key.action,ui::SubscriptionAction::ReadResults);QVERIFY(otherRead.key.target!=read.key.target);QCOMPARE(table->rowCount(),0);window.rejectSubscriptionCommand(otherRead);QCOMPARE(table->rowCount(),0);
    for(const auto* name:{"subscriptions-read","subscriptions-unread","subscriptions-delete"}){QVERIFY(!page->findChild<QPushButton*>(name)->isEnabled());}window.setSubscriptionResult(ready(read.key,QStringLiteral("Old selected subscription")));QCOMPARE(table->rowCount(),0);
    page->findChild<QPushButton*>("subscriptions-check")->click();QCOMPARE(commands.count(),4);QCOMPARE(command(commands).key.action,ui::SubscriptionAction::Check);QVERIFY(command(commands).receiver&&!command(commands).receiver->stop_requested());
  }
  void closingAndChangingSessionStopReceiverAndRejectOldPagePackets(){
    ui::LibraryWindow window;window.setOfflinePreview(false);window.beginColumnLayoutSession(201);QSignalSpy catalogs(&window,&ui::LibraryWindow::subscriptionCatalogRequested),commands(&window,&ui::LibraryWindow::subscriptionCommandRequested);
    auto* action=window.findChild<QAction*>("classic-subscriptions");action->trigger();const auto first=qvariant_cast<ui::SubscriptionCatalogKey>(catalogs.last()[0]);window.setSubscriptionCatalog(catalog(first));window.setSubscriptionResult(ready(command(commands).key));
    window.subscriptionsPage()->findChild<QPushButton*>("subscriptions-check")->click();const auto old=command(commands);QVERIFY(old.receiver&&!old.receiver->stop_requested());
    window.findChild<QAction*>("classic-close-tab")->trigger();QVERIFY(old.receiver->stop_requested());QVERIFY(!window.subscriptionsPage());action->trigger();
    const auto reopened=qvariant_cast<ui::SubscriptionCatalogKey>(catalogs.last()[0]);QVERIFY(reopened.page!=first.page&&reopened.request>old.key.request);window.setSubscriptionCatalog(catalog(reopened));window.setSubscriptionResult(ready(old.key,QStringLiteral("Old page")));QCOMPARE(window.subscriptionsPage()->findChild<QTableWidget*>("subscription-results")->rowCount(),0);
    window.setSubscriptionResult(ready(command(commands).key));window.subscriptionsPage()->findChild<QPushButton*>("subscriptions-check")->click();const auto switched=command(commands);QVERIFY(switched.receiver&&!switched.receiver->stop_requested());window.beginColumnLayoutSession(202);QVERIFY(switched.receiver->stop_requested());
    window.setSubscriptionCatalog(catalog(reopened));window.setSubscriptionResult(ready(switched.key));QCOMPARE(window.subscriptionsPage()->findChild<QComboBox*>("subscription-selector")->count(),0);QCOMPARE(window.subscriptionsPage()->findChild<QTableWidget*>("subscription-results")->rowCount(),0);
    const auto catalogsBefore=catalogs.count(),commandsBefore=commands.count();action->trigger();QCOMPARE(catalogs.count(),catalogsBefore+1);QCOMPARE(commands.count(),commandsBefore);const auto next=qvariant_cast<ui::SubscriptionCatalogKey>(catalogs.last()[0]);QCOMPARE(next.session,quint64{202});QVERIFY(next.request>switched.key.request);window.setSubscriptionCatalog(catalog(next));QCOMPARE(commands.count(),commandsBefore+1);QCOMPARE(command(commands).key.session,quint64{202});QCOMPARE(command(commands).key.action,ui::SubscriptionAction::ReadResults);QVERIFY(command(commands).receiver&&!command(commands).receiver->stop_requested());window.setSubscriptionResult(ready(command(commands).key));QCOMPARE(window.subscriptionsPage()->findChild<QTableWidget*>("subscription-results")->rowCount(),1);
  }
  void previewReadsHistoryButCannotCheckOrMutateEvenWithNormalCatalogHint(){
    ui::LibraryWindow window;window.setOfflinePreview(true);window.beginColumnLayoutSession(301);QSignalSpy catalogs(&window,&ui::LibraryWindow::subscriptionCatalogRequested),commands(&window,&ui::LibraryWindow::subscriptionCommandRequested);
    window.findChild<QAction*>("classic-subscriptions")->trigger();window.setSubscriptionCatalog(catalog(qvariant_cast<ui::SubscriptionCatalogKey>(catalogs.last()[0])));QCOMPARE(commands.count(),1);QCOMPARE(command(commands).key.action,ui::SubscriptionAction::ReadResults);window.setSubscriptionResult(ready(command(commands).key));
    auto* page=window.subscriptionsPage();page->findChild<QTableWidget*>("subscription-results")->selectRow(0);for(const auto* name:{"subscriptions-check","subscriptions-read","subscriptions-unread","subscriptions-delete"}){auto* button=page->findChild<QPushButton*>(name);QVERIFY(button);QVERIFY(!button->isEnabled());button->click();}QCOMPARE(commands.count(),1);
  }
  void sharedCancellationPrecedesSignalAndReentrantSessionCannotRestoreRows(){
    ui::SubscriptionsPage page;page.beginSession(401,1);page.setAvailable(true,false);ui::SubscriptionCommand captured;bool stoppedAtSignal=false;
    QObject::connect(&page,&ui::SubscriptionsPage::commandRequested,&page,[&](ui::SubscriptionCommand value){value.key.request=value.key.action==ui::SubscriptionAction::ReadResults?3:4;captured=value;page.setPending(value);if(value.key.action==ui::SubscriptionAction::ReadResults){page.setResult(ready(value.key));}});
    page.setCatalogPending({401,1,2});page.setCatalog(catalog({401,1,2}));
    QObject::connect(&page,&ui::SubscriptionsPage::cancelRequested,&page,[&](ui::SubscriptionCommandKey){stoppedAtSignal=captured.receiver&&captured.receiver->stop_requested();page.beginSession(402,2);});
    page.findChild<QPushButton*>("subscriptions-check")->click();QVERIFY(captured.receiver);page.findChild<QPushButton*>("subscriptions-cancel")->click();QVERIFY(stoppedAtSignal);QCOMPARE(page.session(),quint64{402});QCOMPARE(page.page(),quint64{2});page.setResult(ready(captured.key));QCOMPARE(page.findChild<QTableWidget*>("subscription-results")->rowCount(),0);
  }
  void comboAndModelReentryCannotFinishAnOldCatalogOrResult(){
    ui::SubscriptionsPage page;page.beginSession(501,1);page.setAvailable(true,false);auto* combo=page.findChild<QComboBox*>("subscription-selector");auto* table=page.findChild<QTableWidget*>("subscription-results");auto* status=page.findChild<QLabel*>("subscriptions-status");QSignalSpy requests(&page,&ui::SubscriptionsPage::commandRequested);
    QMetaObject::Connection change;change=QObject::connect(combo,&QComboBox::currentIndexChanged,&page,[&](int at){if(at>=0){QObject::disconnect(change);page.beginSession(502,2);}});
    page.setCatalogPending({501,1,4});page.setCatalog(catalog({501,1,4}));QCOMPARE(page.session(),quint64{502});QCOMPARE(combo->count(),0);QCOMPARE(requests.count(),0);QCOMPARE(status->text(),QStringLiteral("Refresh to read saved subscriptions."));
    QObject::connect(&page,&ui::SubscriptionsPage::commandRequested,&page,[&](ui::SubscriptionCommand value){value.key.request=6;page.setPending(value);});page.setCatalogPending({502,2,5});page.setCatalog(catalog({502,2,5}));QCOMPARE(requests.count(),1);auto read=command(requests);read.key.request=6;
    QMetaObject::Connection insertion;insertion=QObject::connect(table->model(),&QAbstractItemModel::rowsInserted,&page,[&]{QObject::disconnect(insertion);page.beginSession(503,3);});page.setResult(ready(read.key));QCOMPARE(page.session(),quint64{503});QCOMPARE(table->rowCount(),0);QCOMPARE(combo->count(),0);QCOMPARE(status->text(),QStringLiteral("Refresh to read saved subscriptions."));
    QPointer<ui::SubscriptionsPage> doomed=new ui::SubscriptionsPage;doomed->beginSession(504,4);doomed->setAvailable(true,false);QObject::connect(doomed,&ui::SubscriptionsPage::catalogRequested,doomed,[&](ui::SubscriptionCatalogKey){delete doomed.data();});doomed->refresh();QVERIFY(!doomed);
  }
};
QTEST_MAIN(SubscriptionsUiTests)
#include "subscriptions_ui_tests.moc"
