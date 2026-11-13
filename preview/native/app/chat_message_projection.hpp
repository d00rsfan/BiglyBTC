// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "chat_page.hpp"
#include "room.hpp"
#include <map>
#include <span>
namespace biglybtc::app {
// Pure presentation: no service, profile, crypto, network or send operation.
void projectChatMessages(ui::ChatViewSnapshot&,std::span<const services::msgsync::RoomMessage>,std::map<QByteArray,QDateTime>&,const QDateTime& now);
}
