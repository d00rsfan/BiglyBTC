// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "chat_page.hpp"
#include "general_chat_service.hpp"
#include <QObject>
#include <QTimer>
#include <map>
namespace biglybtc::app {
class GeneralChatController final:public QObject {
 Q_OBJECT
public:
 explicit GeneralChatController(QObject* parent=nullptr);
 void setService(std::shared_ptr<GeneralChats>);
 void joinRoom(QString);void selectRoom(QString);void leaveRoom(QString);void readingRoom(QString);
 void send(QString,QString);void options(QString,bool,bool);
 void nicknameMode(QString,bool);void nickname(QString,QString,bool);
 void requestStop()noexcept;
signals:
 void snapshotReady(biglybtc::ui::ChatViewSnapshot);
 void sendFinished(QString key,QString text,bool accepted);
 void notice(QString);
private:
 void poll();bool available();
 std::shared_ptr<GeneralChats> service_;QTimer timer_;QString readingRoom_;
 std::uint64_t generation_=0,sequence_=0;
 std::map<QByteArray,QDateTime> times_;
};
}
