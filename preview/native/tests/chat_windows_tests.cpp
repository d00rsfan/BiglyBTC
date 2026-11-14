// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_windows.hpp"
#include "library_window.hpp"
#include <QApplication>
#include <QWindow>
#include <QScreen>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QAction>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QtTest>
#include <algorithm>
using namespace biglybtc::ui;
namespace {
ChatViewSnapshot catalog(){ChatViewSnapshot v;v.generation=71;v.sequence=400;v.preferencesReady=true;v.selected="Generated Public parent";ChatRoomView r;r.key=v.selected;r.joined=r.canSend=true;r.ownerEpoch=301;v.rooms.push_back(r);r.key="Generated private instance";r.privateChat=true;r.ownerEpoch=302;v.rooms.push_back(r);return v;}
ChatViewSnapshot bound(ChatViewSnapshot v,QString key,quint64 epoch,quint64 id,quint64 sequence=1){const auto room=std::ranges::find(v.rooms,key,&ChatRoomView::key);Q_ASSERT(room!=v.rooms.end());auto r=*room;r.ownerEpoch=epoch;v.rooms={r};v.selected=key;v.sequence=sequence;v.boundView=id;ChatMessageView message;message.id="generated-id";message.nickname="Generated author";message.text="Generated private transcript";v.messages={message};return v;}
QWidget* viewWidget(quint64 id){for(auto* w:QApplication::allWidgets()){if(w->objectName()=="chat-window"&&w->property("BiglyBTC.chatViewID").toULongLong()==id){return w;}}return nullptr;}
}
class ChatWindowsTests final:public QObject {
 Q_OBJECT
private slots:
 void privateCatalogueReopensKeptAliveInstanceAndRefusesStaleMenuScope(){
  LibraryWindow window;QSignalSpy acquire(&window,&LibraryWindow::chatViewAcquireRequested),join(&window,&LibraryWindow::chatJoinRequested);auto v=catalog();v.windowSettings.standalone=true;v.rooms[1].keepAlive=true;window.setChatSnapshot(v);QCOMPARE(acquire.size(),1);auto* popup=viewWidget(acquire[0][3].toULongLong());QVERIFY(popup);popup->close();auto* menu=window.findChild<QMenu*>("classic-private-chats");QVERIFY(menu);QVERIFY(QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection));QCOMPARE(menu->actions().size(),1);QCOMPARE(menu->actions()[0]->data().toString(),v.rooms[1].key);menu->actions()[0]->trigger();QCOMPARE(acquire.size(),2);QVERIFY(join.isEmpty());QPointer<QAction> stale=menu->actions()[0];++v.generation;v.sequence=1;v.rooms[1].ownerEpoch=303;window.setChatSnapshot(v);const auto count=acquire.size();QVERIFY(stale);stale->trigger();QCOMPARE(acquire.size(),count);QVERIFY(join.isEmpty());++v.sequence;v.rooms.removeLast();window.setChatSnapshot(v);QVERIFY(QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection));QCOMPARE(menu->actions().size(),1);QVERIFY(!menu->actions()[0]->isEnabled());
 }

 void pendingWindowsCancelExactAttemptAndAdoptOnlyTypedPrivateIdentity(){
  QWidget owner;QTabWidget tabs(&owner);ChatWindows manager(&owner,&tabs);QSignalSpy acquire(&manager,&ChatWindows::acquireRequested),cancel(&manager,&ChatWindows::cancelPrivateRequested),release(&manager,&ChatWindows::releaseRequested);auto v=catalog();const auto child=v.rooms[1];v.rooms.removeLast();v.privateAttempts.push_back({1001,301,v.rooms[0].key,QByteArray(25,'g'),"Generated peer",{}});manager.setCatalog(v);auto* popup=viewWidget(1);QVERIFY(popup);QVERIFY(acquire.isEmpty());QVERIFY(manager.readingContext().key.isEmpty());QVERIFY(!popup->findChild<QPushButton*>("chat-send")->isEnabled());QCoreApplication::processEvents();QVERIFY(popup->findChild<QTextBrowser*>()->width()>popup->findChild<QListWidget*>("chat-participants")->width());const auto connectingImage=qEnvironmentVariable("BIGLYBTC_CHAT_CONNECTING_SCREENSHOT");if(!connectingImage.isEmpty()){QVERIFY(popup->grab().save(connectingImage));}popup->close();QCOMPARE(cancel.size(),1);QCOMPARE(cancel[0][0].toULongLong(),quint64(71));QCOMPARE(cancel[0][1].toULongLong(),quint64(1001));QCOMPARE(cancel[0][2].toString(),v.rooms[0].key);QCOMPARE(cancel[0][3].toULongLong(),quint64(301));QVERIFY(release.isEmpty());
  ++v.sequence;v.privateAttempts[0].request=1002;manager.setCatalog(v);popup=viewWidget(2);QVERIFY(popup);QVERIFY(acquire.isEmpty());++v.sequence;v.rooms.push_back(child);v.privateAttempts[0].openedRoom=child.key;manager.setCatalog(v);QCOMPARE(acquire.size(),1);QCOMPARE(acquire[0][3].toULongLong(),quint64(2));QCOMPARE(popup->property("chat-epoch").toULongLong(),quint64(302));manager.setViewSnapshot(2,bound(v,child.key,302,2));QVERIFY(popup->findChild<QTextBrowser*>()->toPlainText().contains("Generated private transcript"));const auto connectedImage=qEnvironmentVariable("BIGLYBTC_CHAT_WINDOW_SCREENSHOT");if(!connectedImage.isEmpty()){QVERIFY(popup->grab().save(connectedImage));}popup->close();QCOMPARE(cancel.size(),1);QCOMPARE(release.size(),1);
 }
 void pendingFailureCannotBecomeAPublicSendTarget(){
  QWidget owner;QTabWidget tabs(&owner);ChatWindows manager(&owner,&tabs);QSignalSpy acquire(&manager,&ChatWindows::acquireRequested);auto v=catalog();v.rooms.removeLast();v.privateAttempts.push_back({1003,301,v.rooms[0].key,QByteArray(25,'g'),"Generated peer",{}});manager.setCatalog(v);auto* popup=viewWidget(1);QVERIFY(popup);manager.finishAttempt(71,1003,v.rooms[0].key,{});QVERIFY(acquire.isEmpty());QVERIFY(popup->findChild<QLabel*>("chat-status")->text().contains("source changed"));QVERIFY(!popup->findChild<QPushButton*>("chat-send")->isEnabled());manager.clear();
 }
 void originalGeometryIsClampedDebouncedAndEscapeClosesThePopout(){
  QWidget owner;QTabWidget tabs(&owner);ChatWindows manager(&owner,&tabs);QSignalSpy changed(&manager,&ChatWindows::geometryRequested),released(&manager,&ChatWindows::releaseRequested);auto v=catalog();v.windowSettings.standalone=true;v.windowSettings.position="2147483647,-2147483648,2147483647,2147483647";manager.setCatalog(v);auto* popup=viewWidget(1);QVERIFY(popup);QVERIFY(QTest::qWaitForWindowExposed(popup));const auto available=popup->screen()->availableGeometry();QTRY_COMPARE(popup->frameGeometry().intersected(available),popup->frameGeometry());popup->resize(std::min(450,available.width()),std::min(300,available.height()));popup->move(available.topLeft());QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(),1600);const auto next=qvariant_cast<biglybtc::services::msgsync::ChatWindowSettings>(changed.last()[2]);QCOMPARE(QString::fromStdString(next.position).split(',').size(),4);QVERIFY(next.standalone);popup->windowHandle()->requestActivate();QVERIFY(QTest::qWaitForWindowActive(popup));auto* search=popup->findChild<QLineEdit*>("chat-search");QVERIFY(search);search->setText("Generated find");search->setFocus();QTest::keyClick(search,Qt::Key_Escape);QVERIFY(search->text().isEmpty());QVERIFY(released.isEmpty());QVERIFY(popup->isVisible());QTest::keyClick(popup,Qt::Key_Escape);QCOMPARE(released.size(),1);
 }

 void cleanup(){QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
 void simultaneousViewsDeduplicateWindowsAndReleaseOnlyTheirOwnReference(){
  QWidget owner;QTabWidget tabs(&owner);ChatWindows manager(&owner,&tabs);QSignalSpy acquire(&manager,&ChatWindows::acquireRequested),release(&manager,&ChatWindows::releaseRequested);const auto v=catalog();manager.setCatalog(v);QCOMPARE(acquire.size(),1);const auto id=acquire[0][3].toULongLong();QVERIFY(viewWidget(id));QVERIFY(viewWidget(id)->windowFlags().testFlag(Qt::WindowStaysOnTopHint));QCOMPARE(viewWidget(id)->parentWidget(),&owner);manager.openRoom(v.rooms[1].key);QCOMPARE(acquire.size(),1);auto redirected=v;++redirected.sequence;redirected.windowSettings.toSidebar=true;manager.setCatalog(redirected);manager.openRoom(v.rooms[1].key);QCOMPARE(acquire.size(),1);
  manager.openRoom(v.rooms[1].key,ChatWindows::Mode::Tab);QCOMPARE(acquire.size(),2);QCOMPARE(tabs.count(),1);const auto tabID=acquire[1][3].toULongLong();QVERIFY(tabID!=id);manager.setViewSnapshot(id,bound(v,v.rooms[1].key,302,id));manager.setViewSnapshot(tabID,bound(v,v.rooms[1].key,302,tabID));auto* page=viewWidget(id)->findChild<ChatPage*>();QVERIFY(page);QCOMPARE(page->joinedEpoch(),quint64(302));QVERIFY(page->findChild<QTextBrowser*>()->toPlainText().contains("Generated private transcript"));
  QVERIFY(manager.closeTab(tabs.widget(0)));QCOMPARE(release.size(),1);QCOMPARE(release[0][1].toULongLong(),tabID);QVERIFY(viewWidget(id));viewWidget(id)->close();QCOMPARE(release.size(),2);QCOMPARE(release[1][1].toULongLong(),id);manager.clear();QCOMPARE(release.size(),2);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
 }
 void draftsAcknowledgementsAndStaleIdentityAreIsolated(){
  QWidget owner;QTabWidget tabs(&owner);ChatWindows manager(&owner,&tabs);QSignalSpy acquire(&manager,&ChatWindows::acquireRequested),send(&manager,&ChatWindows::sendRequested);auto v=catalog();manager.setCatalog(v);const auto first=acquire[0][3].toULongLong();manager.openRoom(v.rooms[1].key,ChatWindows::Mode::Tab);const auto second=acquire[1][3].toULongLong();for(auto id:{first,second}){manager.setViewSnapshot(id,bound(v,v.rooms[1].key,302,id));}
  auto* a=viewWidget(first)->findChild<QPlainTextEdit*>("chat-composer");auto* b=viewWidget(second)->findChild<QPlainTextEdit*>("chat-composer");a->setPlainText("Generated first draft");b->setPlainText("Generated second draft");auto* button=viewWidget(first)->findChild<QPushButton*>("chat-send");QVERIFY(button&&button->isEnabled());button->click();QCOMPARE(send.size(),1);QCOMPARE(send[0][1].toULongLong(),first);manager.sendFinished(first,v.rooms[1].key,"Generated first draft",true);QVERIFY(a->toPlainText().isEmpty());QCOMPARE(b->toPlainText(),QString("Generated second draft"));
  auto bad=bound(v,v.rooms[1].key,302,second,3);bad.generation=72;manager.setViewSnapshot(second,bad);bad.generation=71;bad.rooms[0].ownerEpoch=303;manager.setViewSnapshot(second,bad);bad.rooms[0].ownerEpoch=302;bad.rooms[0].privateChat=false;manager.setViewSnapshot(second,bad);QCOMPARE(viewWidget(second)->findChild<ChatPage*>()->joinedEpoch(),quint64(302));QCOMPARE(b->toPlainText(),QString("Generated second draft"));++v.sequence;v.rooms[1].ownerEpoch=303;manager.setCatalog(v);QCOMPARE(acquire.size(),3);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!viewWidget(first));QVERIFY(!viewWidget(second));manager.clear();
 }
 void readFocusRequiresVisibleBoundWindowAndCorrectEpoch(){
  QWidget owner;QTabWidget tabs(&owner);owner.resize(800,600);owner.show();
  ChatWindows manager(&owner,&tabs);QSignalSpy acquire(&manager,&ChatWindows::acquireRequested);
  auto v=catalog();v.windowSettings.standalone=true;manager.setCatalog(v);
  const auto id=acquire[0][3].toULongLong();auto* popup=viewWidget(id);QVERIFY(popup);
  QVERIFY(manager.readingContext().key.isEmpty());manager.setViewSnapshot(id,bound(v,v.rooms[1].key,302,id));
  QVERIFY(QTest::qWaitForWindowExposed(popup));popup->windowHandle()->requestActivate();
  QVERIFY(QTest::qWaitForWindowActive(popup));QCOMPARE(QApplication::activeWindow(),popup);QCOMPARE(manager.readingContext().key,v.rooms[1].key);
  QCOMPARE(manager.readingContext().epoch,quint64(302));popup->hide();QVERIFY(manager.readingContext().key.isEmpty());
  owner.windowHandle()->requestActivate();QVERIFY(QTest::qWaitForWindowActive(&owner));
  popup->showNormal();QVERIFY(QTest::qWaitForWindowExposed(popup));popup->windowHandle()->requestActivate();
  QVERIFY(QTest::qWaitForWindowActive(popup));QCOMPARE(QApplication::activeWindow(),popup);QCOMPARE(manager.readingContext().key,v.rooms[1].key);
  popup->showMinimized();QTRY_VERIFY(manager.readingContext().key.isEmpty());manager.clear();
 }

 void originalWindowOptionsSelectIndependentWindowsOrTabs(){
  QWidget owner;QTabWidget tabs(&owner);ChatWindows manager(&owner,&tabs);QSignalSpy acquire(&manager,&ChatWindows::acquireRequested);auto v=catalog();v.windowSettings={true,true};manager.setCatalog(v);QCOMPARE(tabs.count(),1);manager.openRoom(v.rooms[1].key,ChatWindows::Mode::Popout);QCOMPARE(acquire.size(),2);auto* popup=viewWidget(acquire[1][3].toULongLong());QVERIFY(popup);QVERIFY(!popup->parentWidget());QVERIFY(!popup->windowFlags().testFlag(Qt::WindowStaysOnTopHint));manager.clear();
 }
 void privateTranscriptNeverAppearsInThePublicBrowser(){
  LibraryWindow window;auto v=catalog();v.selected=v.rooms[0].key;window.setChatSnapshot(v);window.showPublicChat(v.selected);auto* page=qobject_cast<ChatPage*>(window.mainTabs().currentWidget());QVERIFY(page);const QPointer<ChatPage> mainPage(page);QSignalSpy acquire(&window,&LibraryWindow::chatViewAcquireRequested),join(&window,&LibraryWindow::chatJoinRequested);++v.sequence;v.selected=v.rooms[1].key;ChatMessageView message;message.id="private-id";message.nickname="Generated";message.text="Generated confidential text";v.messages={message};window.setChatSnapshot(v);QVERIFY(mainPage);QVERIFY(!mainPage->findChild<QTextBrowser*>()->toPlainText().contains(message.text));const auto* list=mainPage->findChild<QListWidget*>("chat-rooms");QVERIFY(list);QCOMPARE(list->count(),1);QVERIFY(!list->item(0)->text().contains("private"));window.showPublicChat(v.selected);QCOMPARE(join.size(),0);window.showPublicChat("Unknown private completion");QCOMPARE(join.size(),0);
 }
};
QTEST_MAIN(ChatWindowsTests)
#include "chat_windows_tests.moc"
