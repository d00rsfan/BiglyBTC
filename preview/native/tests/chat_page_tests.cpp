// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_page.hpp"
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
  view.sequence=2;view.rooms[0].canSend=false;page.setSnapshot(view);QVERIFY(!composer->isEnabled());QCOMPARE(send.count(),2);
 }
 void staleUpdatesAndProfileChangesCannotReuseDrafts(){
  ChatPage page;ChatViewSnapshot a;a.generation=4;a.sequence=9;a.selected=QStringLiteral("Generated room A");a.rooms.push_back({a.selected,{},QStringLiteral("A"),false,false,false,true,true,0});auto b=a.rooms[0];b.key=QStringLiteral("Generated room B");a.rooms.push_back(b);page.setSnapshot(a);auto* composer=page.findChild<QPlainTextEdit*>(QStringLiteral("chat-composer"));QVERIFY(composer);composer->setPlainText(QStringLiteral("unsent A"));
  auto next=a;next.sequence=10;next.selected=b.key;page.setSnapshot(next);QVERIFY(composer->toPlainText().isEmpty());composer->setPlainText(QStringLiteral("unsent B"));next.selected=a.selected;++next.sequence;page.setSnapshot(next);QCOMPARE(composer->toPlainText(),QStringLiteral("unsent A"));
  page.setSnapshot(a);QCOMPARE(composer->toPlainText(),QStringLiteral("unsent A"));++next.generation;next.sequence=1;page.setSnapshot(next);QVERIFY(composer->toPlainText().isEmpty());
 }
};
QTEST_MAIN(ChatPageTests)
#include "chat_page_tests.moc"
