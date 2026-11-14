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
 void notifications(ChatNotificationSettings expected,ChatNotificationSettings value);
 void privateChat(QString,quint64,QByteArray);
 void privateChatPolicy(int expected,int value);
 void indications(QString key,quint64 epoch,bool expected,bool value);
 void displayPreference(QString,bool,bool,bool);void participantPreference(QString,QByteArray,bool,bool,bool);
 void nicknameMode(QString,bool);void nickname(QString,QString,bool);
 void watchDownload(quint64,QString);void openDownload(quint64,QString);
 void watchView(quint64 generation,QString key,quint64 epoch,quint64 view);
 void releaseView(quint64 generation,quint64 view);
 void sendView(quint64 generation,quint64 view,QString text);
 void nicknameView(quint64 generation,quint64 view,QString nickname,bool expectedShared);
 void nicknameModeView(quint64 generation,quint64 view,bool shared);
 void displayPreferenceView(quint64 generation,quint64 view,bool ratings,bool expected,bool value);
 void participantPreferenceView(quint64 generation,quint64 view,QByteArray key,bool pinned,bool expected,bool value);
 void keepAlive(quint64 generation,QString key,quint64 epoch,bool expected,bool value);
 void windowSettings(services::msgsync::ChatWindowSettings expected,services::msgsync::ChatWindowSettings value);
 void readingRoomScoped(quint64 generation,QString key,quint64 epoch);

 void privateChatView(quint64 generation,quint64 view,QByteArray key);
 void keepAliveView(quint64 generation,quint64 view,bool expected,bool value);
 void windowSettingsView(quint64 generation,quint64 view,ChatWindowSettings expected,ChatWindowSettings value);
 void windowSettingsScoped(quint64 generation,ChatWindowSettings expected,ChatWindowSettings value);
 void notificationsView(quint64 generation,quint64 view,ChatNotificationSettings expected,ChatNotificationSettings value);
 void privatePolicyView(quint64 generation,quint64 view,int expected,int value);
 void publicRoomView(quint64 generation,quint64 view,QString key);
 void requestStop()noexcept;
signals:
 void distributedRatingsReady(ui::DistributedRatingsPresentation);
 void downloadSnapshotReady(quint64,QString,biglybtc::ui::ChatViewSnapshot);
 void snapshotReady(biglybtc::ui::ChatViewSnapshot);
 void viewSnapshotReady(quint64,biglybtc::ui::ChatViewSnapshot);
 void viewClosed(quint64,QString reason);
 void viewSendFinished(quint64,QString key,QString text,bool accepted);
 void roomOpened(QString);
 void sendFinished(QString key,QString text,bool accepted);
 void notice(QString);
private:
 struct ViewBinding {quint64 generation=0,epoch=0,id=0,sequence=0;QString key;bool privateChat=false,closing=false,acquired=false;std::optional<std::uint64_t> request={};};
 struct ViewProjection {std::shared_ptr<const std::vector<services::msgsync::RoomMessage>> source;std::uint64_t policy=0;QVector<ui::ChatMessageView> messages;QVector<ui::ChatParticipantView> participants;std::map<QByteArray,QDateTime> times;};
 std::map<std::uint64_t,quint64> viewSends_;
 std::map<quint64,ViewBinding> views_;std::map<std::uint64_t,std::pair<quint64,bool>> viewJobs_;
 std::map<std::pair<QString,quint64>,ViewProjection> viewProjections_;std::optional<ChatPreferences> viewPreferences_;
 bool viewsDirty_=false;std::uint64_t viewPolicy_=0;quint64 readingEpoch_=0;
 std::optional<ViewBinding> currentView(quint64,quint64,bool sending=false)const;bool viewSendResult(const GeneralChatResult&);
 void clearViews();void retryViews();bool viewResult(const GeneralChatResult&);void publishViews(const std::shared_ptr<const GeneralChatSnapshot>&);
 std::vector<std::shared_ptr<const SharedRatingSnapshot>> ratingViews_;
 void poll();void publishDownload(const std::shared_ptr<const GeneralChatSnapshot>&);bool available();
 std::shared_ptr<GeneralChats> service_;QTimer timer_;QString readingRoom_;
 std::uint64_t generation_=0,sequence_=0;
 quint64 downloadSession_=0;QString downloadHash_;std::map<QByteArray,QDateTime> downloadTimes_;
 std::map<QByteArray,QDateTime> times_;
};
}
