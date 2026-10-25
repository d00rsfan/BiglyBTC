#include "biglybtc/queue_slots.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <set>
namespace biglybtc::core {
namespace {
using Code=QueueSlotErrorCode;
struct Failure{QueueSlotError error;};
void require(bool value,Code code,const char* field){if(!value){throw Failure{{code,field}};}}
constexpr auto Max=std::numeric_limits<std::int32_t>::max();
std::int64_t signed32(std::int64_t value){require(value>=std::numeric_limits<std::int32_t>::min()&&value<=Max,Code::Limit,"queue slot arithmetic");return value;}
}
std::expected<QueueSlotBudget,QueueSlotError> evaluateQueueSlotBudget(const QueueSlotCounts& c){try{
  for(const auto value:{c.maximumActive,c.downloads,c.currentWaitingOrSeeding,c.currentStalled,c.globalActive,c.globalWaiting,c.globalStalled,static_cast<std::int64_t>(c.maximumStalled),static_cast<std::int64_t>(c.reserved),static_cast<std::int64_t>(c.flexible)}){require(value>=0&&value<=Max,Code::InvalidFacts,"queue slot counts");}
  QueueSlotBudget r;r.maximumSeeders=c.maximumActive==0?999999:signed32(c.maximumActive-c.downloads+c.reserved);
  r.currentLimit=signed32(r.maximumSeeders+c.maximumStalled);r.globalLimit=signed32(r.currentLimit+c.flexible);
  r.currentUsed=signed32(c.currentWaitingOrSeeding+c.currentStalled);r.globalUsed=signed32(c.globalActive+c.globalWaiting+c.globalStalled);
  r.underCurrent=c.maximumActive==0||r.currentUsed<r.currentLimit;r.underGlobal=c.maximumActive==0||r.globalUsed<r.globalLimit;
  r.overCurrent=c.maximumActive!=0&&r.currentUsed>r.currentLimit;r.overGlobal=c.maximumActive!=0&&r.globalUsed>r.globalLimit;return r;
}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(QueueSlotError{Code::Memory,"queue slot budget"});}}
std::expected<QueueLightTransition,QueueSlotError> updateQueueLightSeed(const QueueLightMemory& before,const QueueLightInput& in,std::int64_t now){try{
  require(now>=0&&static_cast<unsigned>(before.phase)<=2,Code::InvalidFacts,"light seed clock/state");
  require(!before.lastObservationMillis||(*before.lastObservationMillis>=0&&*before.lastObservationMillis<=now),Code::InvalidFacts,"light seed clock regression");
  require(before.phase==QueueLightPhase::Cooldown?before.lostEligibilityMillis&&*before.lostEligibilityMillis>=0&&*before.lostEligibilityMillis<=now:!before.lostEligibilityMillis,Code::InvalidFacts,"light seed cooldown state");
  QueueLightTransition result;result.memory=before;result.known=in.providerAvailable&&in.rankIgnoringZeroPeers.has_value()&&in.privateTorrent.has_value();result.memory.lastObservationMillis=now;
  const auto lose=[&]{if(result.memory.phase==QueueLightPhase::Eligible){result.memory.phase=QueueLightPhase::Cooldown;result.memory.lostEligibilityMillis=now;result.changed=true;}};
  // Original SR.update invalidates an eligibility grant before the cycle's
  // availability scan when rank/no-zero-peer rank no longer qualify.
  if(!result.known||in.rank!=-7||*in.rankIgnoringZeroPeers< -1){lose();}
  const bool available=result.known&&in.hasFreeReservedSlot&&in.rank==-7&&*in.rankIgnoringZeroPeers>= -1&&*in.privateTorrent;
  if(available){
    if(result.memory.phase==QueueLightPhase::Never||(result.memory.phase==QueueLightPhase::Cooldown&&now-*result.memory.lostEligibilityMillis>=120000)){
      result.memory.phase=QueueLightPhase::Eligible;result.memory.lostEligibilityMillis.reset();result.changed=true;
    }
  }else{lose();}
  if(result.memory.phase==QueueLightPhase::Eligible){result.eligibilityMillis=0;}
  else if(result.memory.phase==QueueLightPhase::Cooldown){result.eligibilityMillis=now-*result.memory.lostEligibilityMillis;}
  result.recentlyEligible=result.known&&result.eligibilityMillis<30000;return result;
}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(QueueSlotError{Code::Memory,"light seed transition"});}}
std::expected<QueueReservedPlan,QueueSlotError> planQueueReservedSlots(std::int32_t configured,const QueueReservedState& before,std::span<const QueueReservedObservation> inputs,bool complete){try{
  require(configured>=0,Code::InvalidFacts,"negative reserved seed slots");require(before.holders.size()<=MaximumQueueSlotOwners&&inputs.size()<=MaximumQueueSlotOwners,Code::Limit,"reserved seed owner bound");
  std::set<std::uint64_t> held,seen;for(auto token:before.holders){require(token&&held.insert(token).second,Code::InvalidFacts,"reserved seed identity");}
  for(const auto& row:inputs){require(row.token&&seen.insert(row.token).second,Code::InvalidFacts,"reserved seed observation identity");require(!row.request||(!row.retain&&row.recentlyEligible),Code::InvalidFacts,"reserved seed request authority");}
  QueueReservedPlan r;r.state=before;r.coverageKnown=complete;
  if(!complete){r.available=std::max<std::int64_t>(0,static_cast<std::int64_t>(configured)-static_cast<std::int64_t>(r.state.holders.size()));return r;}
  std::erase_if(r.state.holders,[&](auto token){const auto found=std::ranges::find_if(inputs,[&](const auto& row){return row.token==token;});return found==inputs.end()||!found->retain;});
  for(const auto& row:inputs){if(!row.request){continue;}require(std::ranges::find(r.state.holders,row.token)==r.state.holders.end(),Code::InvalidFacts,"duplicate reserved seed request");
    if(r.state.holders.size()<static_cast<std::size_t>(configured)){
      require(r.state.holders.size()<MaximumQueueSlotOwners,Code::Limit,"reserved seed owner bound");r.state.holders.push_back(row.token);r.granted.push_back(row.token);
    }
  }
  r.available=std::max<std::int64_t>(0,static_cast<std::int64_t>(configured)-static_cast<std::int64_t>(r.state.holders.size()));return r;
}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(QueueSlotError{Code::Memory,"reserved seed plan"});}}
}
