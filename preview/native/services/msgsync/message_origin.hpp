// SPDX-License-Identifier: GPL-2.0-or-later
// BuddyPluginBeta.ChatMessage.getFlagOrigin, original BiglyBT baseline.
#pragma once
#include <bit>
#include <cstdint>
#include <optional>
#include <string_view>
namespace biglybtc::services::msgsync {
inline std::int32_t messageOrigin(std::string_view text,std::optional<std::int64_t> flag={})noexcept{
 if(flag){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*flag));}
 if(text.starts_with("See http://wiki.vuze.com/w/Swarm_Merging")||text.starts_with("See http://wiki.biglybt.com/w/Swarm_Merging")){return 1;}return 0;
}
}
