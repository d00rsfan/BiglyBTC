// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QWidget>
#include <QDateTime>
#include <QMap>
#include <QVector>
#include <cstdint>
class QListWidget;class QLabel;class QPlainTextEdit;class QTextBrowser;class QPushButton;class QCheckBox;
namespace biglybtc::ui {
struct ChatRoomView {QString key,nickname,status;bool favourite=false,saveHistory=false,readOnly=false,joined=false,canSend=false;int unread=0;bool sharedNickname=true;QString downloadHash={};};
struct ChatMessageView {QByteArray id,publicKey;QString nickname,text;QDateTime time;bool local=false,action=false;bool operator==(const ChatMessageView&)const=default;};
struct ChatParticipantView {QByteArray publicKey;QString nickname;bool ignored=false,pinned=false,local=false;bool operator==(const ChatParticipantView&)const=default;};
struct ChatViewSnapshot {std::uint64_t generation=0,sequence=0;QString availability,selected,dhtStatus;QVector<ChatRoomView> rooms;QVector<ChatMessageView> messages;QVector<ChatParticipantView> participants;QVector<ChatRoomView> notifications={};bool preferencesReady=false,hideRatings=false,hideSubscriptions=false;};
class ChatPage final:public QWidget {
 Q_OBJECT
public:
 explicit ChatPage(QWidget* parent=nullptr);
 void setSnapshot(ChatViewSnapshot);
 void setFixedRoom(QString);QString joinedRoom()const;
 void submissionFailed(const QString& key);
 void submissionAccepted(const QString& key,const QString& text);
signals:
 void joinRequested(QString key);
 void selectionRequested(QString key);
 void leaveRequested(QString key);
 void sendRequested(QString key,QString text);
 void optionsRequested(QString key,bool favourite,bool saveHistory);
 void displayPreferenceRequested(QString key,bool ratings,bool expected,bool value);
 void participantPreferenceRequested(QString key,QByteArray publicKey,bool pinned,bool expected,bool value);
 void nicknameModeRequested(QString key,bool shared);
 void nicknameRequested(QString key,QString nickname,bool expectedShared);
private:
 void updateSend();void rememberDraft();
 bool participantMenuOpen_=false;void updateParticipants();
 bool fixed_=false;QString fixedKey_;
 ChatViewSnapshot value_;QMap<QString,QString> drafts_,pending_;
 QListWidget* rooms_;QListWidget* participants_;QLabel* status_;QTextBrowser* transcript_;QPlainTextEdit* composer_;
 QPushButton* join_;QPushButton* leave_;QPushButton* send_;QCheckBox* favourite_;QCheckBox* history_;QCheckBox* sharedNickname_;QPushButton* nickname_;QPushButton* display_;
};
}
