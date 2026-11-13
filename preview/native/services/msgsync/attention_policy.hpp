// SPDX-License-Identifier: GPL-2.0-or-later
// Original BuddyPluginBeta message flags/attention and buddy SWT display rules.
#pragma once
#include "message_origin.hpp"
namespace biglybtc::services::msgsync {
struct ChatDisplayPolicy {bool hideRatings=false,hideSearchSubscriptions=false;bool operator==(const ChatDisplayPolicy&)const=default;};
inline bool messageFlashOverride(std::optional<std::int64_t> flag={})noexcept{return flag&&static_cast<std::uint32_t>(*flag)!=0;}
inline std::int32_t messageFlagType(std::optional<std::int64_t> flag={})noexcept{return flag?std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*flag)):0;}
inline bool hiddenMessageOrigin(std::int32_t origin,ChatDisplayPolicy policy)noexcept{return (origin==1&&policy.hideRatings)||(origin==3&&policy.hideSearchSubscriptions);}
inline bool visibleChatMessage(bool ignored,bool hasText,std::int32_t origin,ChatDisplayPolicy policy)noexcept{return !ignored&&hasText&&!hiddenMessageOrigin(origin,policy);}
inline bool requiresChatAttention(bool local,bool ignored,bool flashOverride,std::int32_t origin,ChatDisplayPolicy policy)noexcept{return (local?flashOverride:!ignored)&&!hiddenMessageOrigin(origin,policy);}
}
