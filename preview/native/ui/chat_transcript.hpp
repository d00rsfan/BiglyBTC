// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QWidget>
#include <QUrl>
#include <memory>
#include <span>
#include <cstdint>
namespace biglybtc::ui {
struct ChatMessageView;
class ChatTranscript final:public QWidget {
 Q_OBJECT
public:
 explicit ChatTranscript(QWidget* shortcutScope,QWidget* parent=nullptr);
 ~ChatTranscript()override;
 void bindShortcuts(QWidget*);
 void setMessages(std::uint64_t generation,QString room,std::span<const ChatMessageView>,QString ownNickname={});
signals:
 void publicRoomRequested(QString);
 void externalLinkRequested(QUrl);
private:
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
