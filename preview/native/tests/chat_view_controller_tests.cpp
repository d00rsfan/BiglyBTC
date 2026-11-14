// SPDX-License-Identifier: GPL-2.0-or-later
#include "nat_session_fixture.hpp"
#include "chat_owner_fixture.hpp"
#include "general_chat_controller.hpp"
#include <QSignalSpy>
using namespace biglybtc;using namespace std::chrono_literals;
class ChatViewControllerTests final:public QObject {
 Q_OBJECT
private slots:
 void independentProjectionAcknowledgementsAndExpiredViewCommands(){
  Actor seed(QHostAddress("127.122.65.1"),false,0,false,4);generated_chat::GeneratedClient fixture(QHostAddress("127.122.65.2"),seed.actualEndpoint);auto service=std::shared_ptr<app::GeneralChats>(fixture.chats.release());const std::string a="Generated view A",b="Generated view B";QVERIFY(service->joinRoom(a));QVERIFY(service->joinRoom(b));const auto room=[&](const std::string& key)->std::optional<app::GeneralChatRoom>{for(const auto& r:service->snapshot()->rooms){if(r.settings.key==key){return r;}}return {};};QTRY_VERIFY_WITH_TIMEOUT(room(a)&&room(a)->canSend&&room(b)&&room(b)->canSend,7000);const auto epoch=room(a)->epoch;
  app::GeneralChatController controller;QSignalSpy snapshots(&controller,&app::GeneralChatController::viewSnapshotReady),sent(&controller,&app::GeneralChatController::viewSendFinished),closed(&controller,&app::GeneralChatController::viewClosed);controller.setService(service);controller.watchView(1,QString::fromStdString(a),epoch,11);controller.watchView(1,QString::fromStdString(a),epoch,12);controller.selectRoom(QString::fromStdString(b));
  const auto bound=[&](quint64 id){return std::ranges::any_of(snapshots,[&](const auto& args){return args[0].toULongLong()==id&&qvariant_cast<ui::ChatViewSnapshot>(args[1]).boundView==id;});};QTRY_VERIFY_WITH_TIMEOUT(bound(11)&&bound(12),3000);controller.sendView(1,11,"Generated scoped message");QTRY_COMPARE_WITH_TIMEOUT(sent.size(),1,3000);QCOMPARE(sent[0][0].toULongLong(),quint64(11));QVERIFY(sent[0][3].toBool());
  const auto projected=[&](quint64 id){return std::ranges::any_of(snapshots,[&](const auto& args){const auto v=qvariant_cast<ui::ChatViewSnapshot>(args[1]);return args[0].toULongLong()==id&&v.selected==QString::fromStdString(a)&&v.messages.size()==1&&v.messages[0].text=="Generated scoped message";});};QTRY_VERIFY_WITH_TIMEOUT(projected(11)&&projected(12),3000);QCOMPARE(service->snapshot()->selected,b);
  controller.releaseView(1,11);QTRY_COMPARE_WITH_TIMEOUT(room(a)->viewCount,std::size_t(1),3000);const auto request=service->send(a,"Forbidden released-view message",app::ChatViewTarget{1,epoch,11});QVERIFY(request);controller.sendView(1,11,"Also forbidden");QCOMPARE(sent.size(),2);QVERIFY(!sent[1][3].toBool());controller.sendView(1,12,"Generated surviving-view message");QTRY_COMPARE_WITH_TIMEOUT(sent.size(),3,3000);QVERIFY(sent[2][3].toBool());QTRY_VERIFY_WITH_TIMEOUT(([&]{const auto snapshot=service->snapshot();for(const auto& t:snapshot->transcripts){if(t.key==a&&t.messages){return t.messages->size()==2;}}return false;})(),3000);
  QVERIFY(service->leaveRoom(a));QTRY_VERIFY_WITH_TIMEOUT(room(a)&&!room(a)->joined,3000);QTRY_VERIFY_WITH_TIMEOUT(!closed.isEmpty(),3000);QVERIFY(service->joinRoom(a));QTRY_VERIFY_WITH_TIMEOUT(room(a)&&room(a)->canSend,3000);QVERIFY(room(a)->epoch!=epoch);controller.watchView(1,QString::fromStdString(a),epoch,13);QVERIFY(closed.size()>=2);controller.setService({});service->requestStop();service->join();
 }
};
QTEST_MAIN(ChatViewControllerTests)
#include "chat_view_controller_tests.moc"
