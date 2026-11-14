// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "chat_page.hpp"
#include <QPointer>
#include <map>
#include <set>
class QTabWidget;
namespace biglybtc::ui {
struct ChatReadContext {quint64 generation=0;QString key;quint64 epoch=0;bool operator==(const ChatReadContext&)const=default;};
class ChatWindows final:public QObject {
 Q_OBJECT
public:
 enum class Mode {Configured,Popout,Tab};
 ChatWindows(QWidget* owner,QTabWidget* tabs);~ChatWindows()override;
 void setCatalog(ChatViewSnapshot);void openRoom(QString key,Mode mode=Mode::Configured);
 void setViewSnapshot(quint64,ChatViewSnapshot);void closeFromOwner(quint64,QString reason={});
 void sendFinished(quint64,QString key,QString text,bool accepted);
 bool closeTab(QWidget*);void clear();ChatReadContext readingContext()const;
signals:
 void acquireRequested(quint64,QString,quint64,quint64);void releaseRequested(quint64,quint64);
 void sendRequested(quint64,quint64,QString);void privateRequested(quint64,quint64,QByteArray);
 void nicknameRequested(quint64,quint64,QString,bool);void nicknameModeRequested(quint64,quint64,bool);
 void displayPreferenceRequested(quint64,quint64,bool,bool,bool);
 void participantPreferenceRequested(quint64,quint64,QByteArray,bool,bool,bool);
 void keepAliveRequested(quint64,quint64,bool,bool);
 void windowSettingsRequested(quint64,quint64,biglybtc::services::msgsync::ChatWindowSettings,biglybtc::services::msgsync::ChatWindowSettings);
 void notificationSettingsRequested(quint64,quint64,biglybtc::services::msgsync::ChatNotificationSettings,biglybtc::services::msgsync::ChatNotificationSettings);
 void privatePolicyRequested(quint64,quint64,int,int);void notificationSoundRequested(QString);
 void publicRoomRequested(quint64,quint64,QString);void externalLinkRequested(QUrl);
 void readingChanged();void notice(QString);
protected:bool eventFilter(QObject*,QEvent*)override;
private:
 struct Entry {quint64 id=0,generation=0,epoch=0;QString key;bool privateChat=false,popout=false,bound=false;QPointer<QWidget> container;QPointer<ChatPage> page;ChatViewSnapshot snapshot;};
 bool valid(quint64,const QString& key={})const;void closeView(quint64,bool closeWidget=true);void connectPage(quint64);
 QPointer<QWidget> owner_;QPointer<QTabWidget> tabs_;ChatViewSnapshot catalog_;quint64 next_=0;
 std::map<quint64,Entry> entries_;std::set<std::pair<QString,quint64>> seenPrivate_;
};
}
