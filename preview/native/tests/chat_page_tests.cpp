#include <QToolButton>
#include <QApplication>
#include <QLabel>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_page.hpp"
#include "library_window.hpp"
#include "torrent_detail_pages.hpp"
#include <QAction>
#include <QCheckBox>
#include <QInputDialog>
#include <QFormLayout>
#include <QTimer>
#include <QTabWidget>
#include <QTableView>
#include <QtTest>
#include <QListWidget>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QMenu>
#include <QAction>
#include <QScrollBar>
#include <QTextBrowser>
using namespace biglybtc::ui;
class ChatPageTests final:public QObject {
 Q_OBJECT
private slots:
 void participantSelectionAndContextCommandsSurviveRefresh(){
  ChatPage page;page.resize(1000,600);page.show();ChatViewSnapshot view;view.generation=7;view.sequence=1;view.selected="Generated preference room";view.preferencesReady=true;view.rooms.push_back({view.selected,{},"Connected",true,false,false,true,true,0});for(int index=0;index<80;++index){view.participants.push_back({QByteArray::number(index)+QByteArray(25,'k'),QString("Generated participant %1").arg(index)});}page.setSnapshot(view);auto* roster=page.findChild<QListWidget*>("chat-participants");QVERIFY(roster);roster->setCurrentRow(55);roster->scrollToItem(roster->item(55));QTest::qWait(10);auto* original=roster->currentItem();const auto key=original->data(Qt::UserRole).toByteArray();const auto position=roster->verticalScrollBar()->value();++view.sequence;page.setSnapshot(view);QCOMPARE(roster->currentItem(),original);QCOMPARE(roster->verticalScrollBar()->value(),position);QSignalSpy changed(&page,&ChatPage::participantPreferenceRequested);const auto point=roster->visualItemRect(original).center();QVERIFY(QMetaObject::invokeMethod(roster,"customContextMenuRequested",Q_ARG(QPoint,point)));auto* menu=page.findChild<QMenu*>("chat-participant-menu");QVERIFY(menu&&menu->isVisible());++view.sequence;view.participants[55].nickname="Generated renamed participant";page.setSnapshot(view);QCOMPARE(roster->currentItem(),original);QVERIFY(original->text()!=view.participants[55].nickname);QTest::qWait(350);QVERIFY(menu->actions()[0]->isEnabled());menu->actions()[0]->trigger();QCOMPARE(changed.size(),1);QCOMPARE(changed[0][0].toString(),view.selected);QCOMPARE(changed[0][1].toByteArray(),key);QCOMPARE(changed[0][2].toBool(),false);QCOMPARE(changed[0][3].toBool(),false);QCOMPARE(changed[0][4].toBool(),true);menu->close();QCOMPARE(roster->currentItem(),original);QCOMPARE(original->text(),view.participants[55].nickname);QCOMPARE(roster->verticalScrollBar()->value(),position);
  QSignalSpy display(&page,&ChatPage::displayPreferenceRequested);auto* button=page.findChild<QPushButton*>("chat-display");QVERIFY(button&&button->isEnabled());button->click();auto* choices=page.findChild<QMenu*>("chat-display-menu");QVERIFY(choices&&choices->isVisible());choices->actions()[0]->trigger();QCOMPARE(display.size(),1);QCOMPARE(display[0][0].toString(),view.selected);QCOMPARE(display[0][1].toBool(),true);QCOMPARE(display[0][2].toBool(),false);QCOMPARE(display[0][3].toBool(),true);choices->close();const auto screenshot=qEnvironmentVariable("BIGLYBTC_CHAT_PREFERENCES_SCREENSHOT");if(!screenshot.isEmpty()){view.participants[55].ignored=true;view.participants[55].pinned=true;++view.sequence;page.setSnapshot(view);QVERIFY(page.grab().save(screenshot));}
 }
 void fixedDownloadRoomsRejectAnotherTranscriptAndKeepTheirDraft(){
  ChatPage page;page.setFixedRoom("Generated download A");ChatViewSnapshot view;view.generation=7;view.sequence=1;view.selected="Generated download A";view.rooms.push_back({view.selected,{},"Connected",false,false,false,true,true,0});view.messages.push_back({"generated-id","generated-key","Generated","Generated A content",{},false});page.setSnapshot(view);auto* transcript=page.findChild<QTextBrowser*>("chat-transcript");auto* composer=page.findChild<QPlainTextEdit*>("chat-composer");QVERIFY(transcript&&composer);composer->setPlainText("Generated A draft");auto wrong=view;wrong.sequence=2;wrong.selected="Generated download B";wrong.messages[0].text="Generated wrong content";page.setSnapshot(wrong);QVERIFY(transcript->toPlainText().contains("Generated A content"));QVERIFY(!transcript->toPlainText().contains("wrong"));page.setFixedRoom("Generated download B");QVERIFY(transcript->toPlainText().isEmpty());QVERIFY(!composer->isEnabled());page.setFixedRoom("Generated download A");page.setSnapshot(view);QCOMPARE(composer->toPlainText(),QString("Generated A draft"));QCOMPARE(page.joinedRoom(),view.selected);
 }
 void downloadPropertiesRequestOnlyTheirCurrentChatAndRejectStalePackets(){
  QTabWidget tabs;auto* general=new QWidget(&tabs);general->setLayout(new QFormLayout);tabs.addTab(general,"General");tabs.addTab(new QWidget(&tabs),"Files");TorrentDetailPages pages(&tabs);QSignalSpy open(&pages,&TorrentDetailPages::downloadChatRequested);QSignalSpy observed(&pages,&TorrentDetailPages::downloadChatObservationChanged);pages.setDownloadContext(9,QString(40,'a'));QVERIFY(open.isEmpty());auto* chat=tabs.findChild<ChatPage*>("torrent-details-chat");QVERIFY(chat);tabs.setCurrentWidget(chat);pages.setDownloadContext(9,QString(40,'a'));QCOMPARE(open.size(),1);pages.setDownloadContext(9,QString(40,'a'));QCOMPARE(open.size(),1);QCOMPARE(observed.size(),1);tabs.setCurrentIndex(0);pages.setDownloadContext(9,QString(40,'a'));QCOMPARE(observed.last()[0].toULongLong(),quint64{0});tabs.setCurrentWidget(chat);pages.setDownloadContext(9,QString(40,'a'));QCOMPARE(observed.size(),3);QCOMPARE(observed.last()[1].toString(),QString(40,'a'));QCOMPARE(open.size(),1);ChatViewSnapshot packet;packet.generation=9;packet.sequence=2;packet.selected="Generated download A";packet.rooms.push_back({packet.selected,{},"Connected",false,false,false,true,true,0});packet.messages.push_back({"generated-id","generated-key","Generated","Generated current transcript",{},false});pages.setDownloadChat(9,QString(40,'a'),packet);auto* transcript=chat->findChild<QTextBrowser*>("chat-transcript");QVERIFY(transcript->toPlainText().contains("current transcript"));pages.setDownloadContext(9,QString(40,'b'));QCOMPARE(open.size(),2);QVERIFY(transcript->toPlainText().isEmpty());pages.setDownloadChat(9,QString(40,'a'),packet);QVERIFY(transcript->toPlainText().isEmpty());QVERIFY(pages.readingRoom().isEmpty());const auto image=qEnvironmentVariable("BIGLYBTC_DOWNLOAD_CHAT_SCREENSHOT");if(!image.isEmpty()){packet.selected="Generated download B";packet.rooms[0].key=packet.selected;packet.participants.push_back({"generated-key","Generated"});packet.sequence=3;pages.setDownloadChat(9,QString(40,'b'),packet);tabs.resize(1100,700);tabs.show();QTest::qWait(20);QVERIFY(tabs.grab().save(image));}
 }
 void downloadNotificationsCountOnceAndOpenTheCorrectPropertiesChat(){
  LibraryWindow window;window.beginColumnLayoutSession(9);auto library=std::make_shared<biglybtc::ui::LibraryPresentation>();library->revision=1;library->inlineDetails=false;biglybtc::ui::LibraryItem row;row.key=row.hash=QString(40,'a');row.name="Generated notification download";row.bytes=row.completed=17;row.assumedComplete=true;row.state="Seeding";library->items.push_back(row);window.setPresentation(library);window.resize(1200,700);window.show();window.activateWindow();QTest::qWait(20);ChatViewSnapshot value;value.generation=9;value.sequence=1;ChatRoomView room;room.key="Generated download notification";room.joined=room.canSend=true;room.unread=3;room.downloadHash=row.hash;value.notifications.push_back(room);window.setChatSnapshot(value);auto* indicator=window.findChild<QToolButton*>("chat-unread-indicator");QVERIFY(indicator);QCOMPARE(indicator->property("unreadCount").toLongLong(),3);indicator->click();QCOMPARE(window.mainTabs().currentWidget()->property("BiglyBTC.torrentDetailsHash").toString(),row.hash);auto* chat=window.findChild<ChatPage*>("torrent-details-chat");QVERIFY(chat&&chat->isVisible());value.rooms.push_back(room);++value.sequence;window.setChatSnapshot(value);QCOMPARE(indicator->property("unreadCount").toLongLong(),3);value.rooms.clear();value.notifications.clear();++value.sequence;window.setChatSnapshot(value);QCOMPARE(indicator->property("unreadCount").toLongLong(),0);
 }
 void selectedRoomRetainsNetworkFailureAndUnsavedTranscriptStaysEmpty(){
  ChatPage page;ChatViewSnapshot view;view.generation=1;view.sequence=1;
  view.selected=QStringLiteral("Generated unsaved room");
  view.availability=QStringLiteral("IPv4: Configuration needs attention");
  view.rooms.push_back({view.selected,{},QStringLiteral("Connecting to Public chat network…"),true,false,false,false,true,0});
  page.setSnapshot(view);const auto* status=page.findChild<QLabel*>(QStringLiteral("chat-status"));QVERIFY(status);
  QVERIFY(status->text().contains(view.availability));QVERIFY(status->text().contains(view.rooms[0].status));
  const auto* transcript=page.findChild<QTextBrowser*>(QStringLiteral("chat-transcript"));QVERIFY(transcript);QVERIFY(transcript->toPlainText().isEmpty());
 }
 void statusBarOpensUnreadRoomAndOnlyForegroundChatReportsReading(){
  LibraryWindow window;window.setMessage(QStringLiteral("Generated unread fixture"));window.show();window.activateWindow();QTest::qWait(20);
  QSignalSpy selection(&window,&LibraryWindow::chatSelectRequested),reading(&window,&LibraryWindow::chatReadingChanged);
  ChatViewSnapshot view;view.generation=50;view.sequence=1;view.selected=QStringLiteral("Generated selected room");
  view.rooms.push_back({view.selected,{},QStringLiteral("Connected"),true,false,false,true,true,0});
  view.rooms.push_back({QStringLiteral("Generated unread A"),{},QStringLiteral("Connected"),true,false,false,true,true,2});
  view.rooms.push_back({QStringLiteral("Generated unread B"),{},QStringLiteral("Connected"),true,false,false,true,true,3});
  window.setChatSnapshot(view);auto* indicator=window.findChild<QToolButton*>(QStringLiteral("chat-unread-indicator"));QVERIFY(indicator);QCOMPARE(indicator->property("unreadCount").toLongLong(),5);QVERIFY(indicator->text().contains(QLatin1Char('5')));QVERIFY(reading.isEmpty());
  indicator->click();QCOMPARE(selection.count(),1);QCOMPARE(selection.last()[0].toString(),view.rooms[1].key);QTest::qWait(20);
  // Await the service's selected-room acknowledgement: do not mark the
  // previously selected room read while the unread target is opening.
  for(const auto& call:reading){QVERIFY(call[0].toString().isEmpty());}
  view.selected=view.rooms[1].key;++view.sequence;window.setChatSnapshot(view);QTRY_VERIFY_WITH_TIMEOUT(!reading.isEmpty()&&reading.last()[0].toString()==view.selected,1000);
  QWidget other;other.show();other.activateWindow();QTRY_VERIFY_WITH_TIMEOUT(reading.last()[0].toString().isEmpty(),1000);
  window.activateWindow();QTRY_VERIFY_WITH_TIMEOUT(reading.last()[0].toString()==view.selected,1000);
  window.mainTabs().setCurrentIndex(0);QCOMPARE(reading.last()[0].toString(),QString{});
  auto stale=view;--stale.sequence;stale.rooms[1].unread=999;window.setChatSnapshot(stale);QCOMPARE(indicator->property("unreadCount").toLongLong(),5);
  ChatViewSnapshot closed;closed.generation=view.generation;closed.sequence=view.sequence+1;closed.availability=QStringLiteral("Profile closed");window.setChatSnapshot(closed);QCOMPARE(indicator->property("unreadCount").toLongLong(),0);
 }
 void generalRoomsRenderPlainTextAndNeverSendOnOpen(){
  ChatPage page;QSignalSpy send(&page,&ChatPage::sendRequested),selection(&page,&ChatPage::selectionRequested),options(&page,&ChatPage::optionsRequested);
  ChatViewSnapshot view;view.generation=1;view.sequence=1;view.selected=QStringLiteral("Generated general room");view.rooms.push_back({view.selected,QStringLiteral("Generated"),QStringLiteral("Connected"),true,true,false,true,true,0});
  view.messages.push_back({"message-id","generated-key",QStringLiteral("<b>name</b>"),QStringLiteral("<img src='file:///ignored'> & message"),{},false});view.participants.push_back({"generated-key",QStringLiteral("<i>participant</i>")});page.setSnapshot(view);
  QCOMPARE(send.count(),0);QCOMPARE(selection.count(),0);QCOMPARE(options.count(),0);auto* text=page.findChild<QTextBrowser*>(QStringLiteral("chat-transcript"));QVERIFY(text);QVERIFY(text->toPlainText().contains(view.messages[0].text));QVERIFY(text->toPlainText().contains(view.messages[0].nickname));
  auto* composer=page.findChild<QPlainTextEdit*>(QStringLiteral("chat-composer"));auto* button=page.findChild<QPushButton*>(QStringLiteral("chat-send"));QVERIFY(composer&&button&&composer->isEnabled());composer->setPlainText(QStringLiteral("a new message"));QVERIFY(button->isEnabled());button->click();button->click();QCOMPARE(send.count(),1);QCOMPARE(send[0][0].toString(),view.selected);QCOMPARE(composer->toPlainText(),QStringLiteral("a new message"));
  page.submissionFailed(view.selected);QVERIFY(button->isEnabled());button->click();QCOMPARE(send.count(),2);page.submissionAccepted(view.selected,QStringLiteral("a new message"));QVERIFY(composer->toPlainText().isEmpty());QVERIFY(!button->isEnabled());
  composer->setPlainText(QStringLiteral("keyboard message"));QTest::keyClick(composer,Qt::Key_Return);QCOMPARE(send.count(),3);page.submissionAccepted(view.selected,QStringLiteral("keyboard message"));QTest::keyClick(composer,Qt::Key_Return,Qt::ShiftModifier);QCOMPARE(send.count(),3);QVERIFY(composer->toPlainText().contains(QLatin1Char('\n')));
  view.sequence=2;view.rooms[0].canSend=false;page.setSnapshot(view);QVERIFY(!composer->isEnabled());QCOMPARE(send.count(),3);
 }
 void classicMenusOpenRoomsWithoutTorrentSelectionAndForwardNicknameEdits(){
  LibraryWindow window;window.setMessage(QStringLiteral("Generated UI fixture — no profile opened"));window.resize(1200,800);window.show();QVERIFY(!window.table().selectionModel()->hasSelection());
  auto* action=window.findChild<QAction*>(QStringLiteral("classic-general-chats"));auto* viewAction=window.findChild<QAction*>(QStringLiteral("classic-view-general-chats"));QVERIFY(action&&viewAction&&action->isEnabled()&&viewAction->isEnabled());action->trigger();auto* page=window.findChild<ChatPage*>(QStringLiteral("general-chat-page"));QVERIFY(page);QCOMPARE(window.mainTabs().currentWidget(),page);
  ChatViewSnapshot snapshot;snapshot.generation=10;snapshot.sequence=1;snapshot.selected=QStringLiteral("Generated lounge");snapshot.availability=QStringLiteral("Public chats connected");snapshot.rooms.push_back({snapshot.selected,QStringLiteral("Generated reader"),QStringLiteral("24 room nodes, 12 responding"),true,true,false,true,true,0,true});snapshot.messages.push_back({"generated-1","generated-author",QStringLiteral("Generated author"),QStringLiteral("General rooms are independent of torrent selection. Привіт 🌍"),QDateTime::currentDateTime(),false});snapshot.participants.push_back({"generated-author",QStringLiteral("Generated author")});window.setChatSnapshot(snapshot);
  QSignalSpy mode(&window,&LibraryWindow::chatNicknameModeRequested),nickname(&window,&LibraryWindow::chatNicknameRequested),send(&window,&LibraryWindow::chatSendRequested);
  auto* shared=page->findChild<QCheckBox*>(QStringLiteral("chat-shared-nickname"));QVERIFY(shared&&shared->isChecked());shared->click();QCOMPARE(mode.count(),1);QCOMPARE(mode[0][0].toString(),snapshot.selected);QCOMPARE(mode[0][1].toBool(),false);
  snapshot.rooms[0].sharedNickname=false;++snapshot.sequence;window.setChatSnapshot(snapshot);
  bool edited=false;QTimer::singleShot(200,&window,[&]{if(auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget())){dialog->setTextValue(QStringLiteral("Generated renamed"));edited=true;dialog->accept();}});
  QTimer::singleShot(1000,&window,[]{for(auto* widget:QApplication::topLevelWidgets()){if(auto* dialog=qobject_cast<QInputDialog*>(widget)){dialog->reject();}}});
  auto* editor=page->findChild<QPushButton*>(QStringLiteral("chat-nickname"));QVERIFY(editor);editor->click();QVERIFY(edited);QCOMPARE(nickname.count(),1);QCOMPARE(nickname[0][1].toString(),QStringLiteral("Generated renamed"));QCOMPARE(nickname[0][2].toBool(),false);
  auto* composer=page->findChild<QPlainTextEdit*>(QStringLiteral("chat-composer"));auto* button=page->findChild<QPushButton*>(QStringLiteral("chat-send"));QVERIFY(composer&&button);composer->setPlainText(QStringLiteral("Generated draft"));button->click();QCOMPARE(send.count(),1);window.chatSendFinished(snapshot.selected,QStringLiteral("Generated draft"),true);QVERIFY(composer->toPlainText().isEmpty());
  const auto image=qEnvironmentVariable("BIGLYBTC_CHAT_SCREENSHOT");if(!image.isEmpty()){QVERIFY(window.grab().save(image));}
  ChatViewSnapshot closed;closed.generation=snapshot.generation;closed.sequence=snapshot.sequence+1;closed.availability=QStringLiteral("Profile closed");window.setChatSnapshot(closed);QVERIFY(!composer->isEnabled());QVERIFY(!button->isEnabled());
  const auto index=window.mainTabs().indexOf(page);QVERIFY(index>=0);QVERIFY(QMetaObject::invokeMethod(&window.mainTabs(),"tabCloseRequested",Qt::DirectConnection,Q_ARG(int,index)));QCOMPARE(window.mainTabs().indexOf(page),-1);viewAction->trigger();QVERIFY(window.findChildren<ChatPage*>().size()>=1);
 }
 void standardRoomActionsUseOriginalNamesWithoutSending(){
  LibraryWindow window;QSignalSpy join(&window,&LibraryWindow::chatJoinRequested),send(&window,&LibraryWindow::chatSendRequested);
  for(const auto& item:{std::pair{"classic-chat-help","BiglyBT: General: Help"},std::pair{"classic-chat-beta","BiglyBT: Beta: Chat"}}){auto* action=window.findChild<QAction*>(QString::fromLatin1(item.first));QVERIFY(action);action->trigger();QCOMPARE(join.last()[0].toString(),QString::fromLatin1(item.second));}
  QCOMPARE(join.count(),2);QCOMPARE(send.count(),0);
 }
 void staleUpdatesAndProfileChangesCannotReuseDrafts(){
  ChatPage page;ChatViewSnapshot a;a.generation=4;a.sequence=9;a.selected=QStringLiteral("Generated room A");a.rooms.push_back({a.selected,{},QStringLiteral("A"),false,false,false,true,true,0});auto b=a.rooms[0];b.key=QStringLiteral("Generated room B");a.rooms.push_back(b);page.setSnapshot(a);auto* composer=page.findChild<QPlainTextEdit*>(QStringLiteral("chat-composer"));QVERIFY(composer);composer->setPlainText(QStringLiteral("unsent A"));
  auto next=a;next.sequence=10;next.selected=b.key;page.setSnapshot(next);QVERIFY(composer->toPlainText().isEmpty());composer->setPlainText(QStringLiteral("unsent B"));next.selected=a.selected;++next.sequence;page.setSnapshot(next);QCOMPARE(composer->toPlainText(),QStringLiteral("unsent A"));
  page.setSnapshot(a);QCOMPARE(composer->toPlainText(),QStringLiteral("unsent A"));++next.generation;next.sequence=1;page.setSnapshot(next);QVERIFY(composer->toPlainText().isEmpty());
 }
};
QTEST_MAIN(ChatPageTests)
#include "chat_page_tests.moc"
