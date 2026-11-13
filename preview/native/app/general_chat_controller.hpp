// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "chat_page.hpp"
#include "general_chat_service.hpp"
#include "local_rating_presentation.hpp"
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
 void displayPreference(QString,bool,bool,bool);void participantPreference(QString,QByteArray,bool,bool,bool);
 void nicknameMode(QString,bool);void nickname(QString,QString,bool);
 void watchDownload(quint64,QString);void openDownload(quint64,QString);
 void requestStop()noexcept;
signals:
 void distributedRatingsReady(ui::DistributedRatingsPresentation);
 void downloadSnapshotReady(quint64,QString,biglybtc::ui::ChatViewSnapshot);
 void snapshotReady(biglybtc::ui::ChatViewSnapshot);
 void sendFinished(QString key,QString text,bool accepted);
 void notice(QString);
private:
 std::vector<std::shared_ptr<const SharedRatingSnapshot>> ratingViews_;
 void poll();void publishDownload(const std::shared_ptr<const GeneralChatSnapshot>&);bool available();
 std::shared_ptr<GeneralChats> service_;QTimer timer_;QString readingRoom_;
 std::uint64_t generation_=0,sequence_=0;
 quint64 downloadSession_=0;QString downloadHash_;std::map<QByteArray,QDateTime> downloadTimes_;
 std::map<QByteArray,QDateTime> times_;
};
}
