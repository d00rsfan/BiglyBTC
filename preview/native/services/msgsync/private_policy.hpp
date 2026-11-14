// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
namespace biglybtc::services::msgsync {
// BuddyPluginBeta.ChatInstance.chatRequested: unknown saved integer states use
// the original enabled behaviour. UI changes only write the three known states.
inline std::optional<std::string_view> privateChatRefusal(std::int32_t state,bool statistics,bool known,bool pinned,bool ui=true){
 if(statistics){return "Private chat disabled for statistical channels";}
 if(state==1){return "Private chat disabled by recipient";}
 if(!known){return "Private chat requires you send at least one message to the main chat first";}
 if(state==2&&!pinned){return "Recipient will only accept private chats from pinned participants";}
 if(!ui){return "Chat unavailable";}return {};
}
}
