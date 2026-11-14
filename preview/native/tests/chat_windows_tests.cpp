// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_windows.hpp"
#include "library_window.hpp"
#include <QApplication>
#include <QWindow>
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
 void cleanup(){QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
 void simultaneousViewsDeduplicateWindowsAndReleaseOnlyTheirOwnReference(){
  QWidget owner;QTabWidget tabs(&owner);ChatWindows manager(&owner,&tabs);QSignalSpy acquire(&manager,&ChatWindows::acquireRequested),release(&manager,&ChatWindows::releaseRequested);const auto v=catalog();manager.setCatalog(v);QCOMPARE(acquire.size(),1);const auto id=acquire[0][3].toULongLong();QVERIFY(viewWidget(id));QVERIFY(viewWidget(id)->windowFlags().testFlag(Qt::WindowStaysOnTopHint));QCOMPARE(viewWidget(id)->parentWidget(),&owner);manager.openRoom(v.rooms[1].key);QCOMPARE(acquire.size(),1);
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
