// SPDX-License-Identifier: GPL-2.0-only
// Original MsgSyncHandler.getMessageCounts incoming status, Vuze, Inc.
#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
namespace biglybtc::services::msgsync {
class IncomingSync final {
public:
 void attempt(std::int64_t now)noexcept{if(!first_){first_=now;}}
 void reply(std::int32_t more,std::int64_t now)noexcept{last_=now;more_=more;quiet_=more>0?0:std::min(3U,quiet_+1);}
 std::int32_t state(std::int64_t now)const noexcept{
  if(cached_&&now-cacheTime_<10000){return *cached_;}
  std::int32_t pending=0;if(quiet_<3){pending=more_;if(pending==0){if(!first_||now-(last_?*last_:*first_)<120000){pending=-1;}}}
  cached_=pending;cacheTime_=now;return pending;
 }
private:
 std::optional<std::int64_t> first_,last_;std::int32_t more_=0;unsigned quiet_=0;
 mutable std::optional<std::int32_t> cached_;mutable std::int64_t cacheTime_=0;
};
}
