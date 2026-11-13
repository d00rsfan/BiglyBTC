// SPDX-License-Identifier: GPL-2.0-only
// Original notice text/history/timing: azrating RatingsUpdater, Aelitis SARL.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
namespace biglybtc::plugins::rating::publication {
struct NoticeMessage {std::u16string_view text;bool own=false;};
std::u16string noticeMessage(std::int32_t score,std::u16string_view comment);
bool shouldSendNotice(bool written,std::span<const NoticeMessage>,std::u16string_view text)noexcept;
// One call per actual original30-second periodic event. The caller schedules
// the returned delay; no wall-clock/network/profile authority is inferred here.
class NoticeWait final {
public:
 enum class Stage {Sync,Delay,Ready,Cancelled};
 static constexpr std::int64_t pollMillis=30000,settleMillis=120000;
 Stage poll(std::int32_t incomingSyncState,bool destroyed)noexcept;
 Stage afterDelay(bool destroyed)noexcept;
 Stage stage()const noexcept{return stage_;}
 unsigned polls()const noexcept{return polls_;}
private:Stage stage_=Stage::Sync;unsigned polls_=0;
};
}
