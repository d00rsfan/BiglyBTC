// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../services/msgsync/notification_policy.hpp"
#include <QDialog>
#include <QIcon>
#include <QObject>
#include <QProcess>
#include <map>
class QCheckBox;class QLineEdit;
namespace biglybtc::ui {
using ChatNotificationSettings=services::msgsync::ChatNotificationSettings;
QIcon chatNotificationIcon(services::msgsync::ChatNotificationIcon,bool dark);
QByteArray defaultChatNotificationSound();
// Empty means the original bundled sound. This resolves native local files,
// never a Qt-resource path or URI supplied by a received chat message.
QString selectedChatNotificationSound(QString);
class ChatNotificationDialog final:public QDialog {
 Q_OBJECT
public:
 explicit ChatNotificationDialog(const ChatNotificationSettings&,QWidget* parent=nullptr);
 ChatNotificationSettings settings()const;
signals:
 void testSoundRequested(QString);
private:
 QCheckBox* sound_;QCheckBox* flash_;QLineEdit* file_;std::string originalFile_;QString initialFileText_;
};
class ChatNotificationSound final:public QObject {
 Q_OBJECT
public:
 explicit ChatNotificationSound(QObject* parent=nullptr,QString helper=QStringLiteral("/usr/bin/paplay"));
 ~ChatNotificationSound()override;
 bool playing()const noexcept{return !processes_.empty();}
public slots:
 bool play(QString file);
 void stop();
signals:
 void failed(QString);
 void completed(bool);
private:
 QString helper_;std::map<QProcess*,bool> processes_;
};
}

Q_DECLARE_METATYPE(biglybtc::services::msgsync::ChatNotificationSettings)
