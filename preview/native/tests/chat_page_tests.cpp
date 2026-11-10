// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_page.hpp"
#include "library_window.hpp"
#include <QAction>
#include <QCheckBox>
#include <QInputDialog>
#include <QTimer>
#include <QTabWidget>
#include <QTableView>
#include <QtTest>
#include <QListWidget>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTextBrowser>
using namespace biglybtc::ui;
class ChatPageTests final:public QObject {
 Q_OBJECT
private slots:
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
