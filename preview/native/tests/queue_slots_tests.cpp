#include "biglybtc/queue_slots.hpp"
#include "biglybtc/queue_policy.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace biglybtc::core;
namespace {
void check(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,QueueSlotError> value){if(!value){throw std::runtime_error(value.error().field);}return std::move(*value);}
QueueLightTransition light(const QueueLightMemory& memory,const QueueLightInput& input,std::int64_t now){return take(updateQueueLightSeed(memory,input,now));}
QueueTorrentFacts seed(std::uint64_t token,bool running){QueueTorrentFacts f;f.token=token;f.position=static_cast<std::uint32_t>(token);f.complete=true;f.state=running?QueueTorrentState::Seeding:QueueTorrentState::Queued;f.shareRatio=1000;f.secondsDownloading=100;f.secondsSeeding=1000;f.runningMillis=1000000;f.seedingMillis=1000000;f.swarm=QueueSwarm{running?100:0,5,true};f.downloadBytesPerSecond=0;f.uploadBytesPerSecond=1024;f.smoothedUploadBytesPerSecond=1024;f.diskReadLatencyMillis=f.diskWriteLatencyMillis=0;f.activity.seeding=running;return f;}
QueuePlan queue(const QueuePolicy& p,std::span<const QueueTorrentFacts> facts){QueueContext c;c.nowMillis=1000000;c.smoothedUploadBytesPerSecond=1024;auto result=evaluateQueue(p,facts,c);check(bool(result),"valid queue trace");return std::move(*result);}
}
int main(){try{
  QueueSlotCounts counts{4,1,6,0,6,0,0,1,2,3};auto budget=take(evaluateQueueSlotBudget(counts));
  check(budget.maximumSeeders==5&&budget.currentLimit==6&&budget.globalLimit==9,"source maxActive-downloads+reserved and global-only flexible arithmetic");
  check(!budget.underCurrent&&!budget.overCurrent&&budget.underGlobal&&!budget.overGlobal,"equal current cap is neither under nor over");
  counts.globalActive=9;budget=take(evaluateQueueSlotBudget(counts));check(!budget.underGlobal&&!budget.overGlobal,"global equality uses strict source comparisons");
  counts.globalActive=10;check(take(evaluateQueueSlotBudget(counts)).overGlobal,"global strict over-limit boundary");counts.maximumActive=0;budget=take(evaluateQueueSlotBudget(counts));check(budget.maximumSeeders==999999&&budget.underCurrent&&budget.underGlobal&&!budget.overGlobal,"source unlimited sentinel/bypass");
  counts.maximumActive=4;counts.reserved=std::numeric_limits<std::int32_t>::max();check(!evaluateQueueSlotBudget(counts),"signed32 budget overflow never creates unbounded authority");counts.reserved=0;counts.currentStalled=-1;check(!evaluateQueueSlotBudget(counts),"negative observed counts reject without prefix");
  const auto excessive=Document::parse("d19:Flexible Seed Slotsi2147483647ee");check(bool(excessive)&&!readQueuePolicy(&*excessive),"unrepresentable slot policy rejected by pre-save config reader");
  QueueLightInput input{true,true,-7,0,true};auto state=light({},input,1000);check(state.known&&state.changed&&state.recentlyEligible&&state.memory.phase==QueueLightPhase::Eligible&&state.eligibilityMillis==0,"private known provider may establish initial eligibility");
  state=light(state.memory,input,1001);check(!state.changed&&state.memory.phase==QueueLightPhase::Eligible,"same eligibility is not a new event");
  input.rank=10;state=light(state.memory,input,2000);check(state.changed&&state.eligibilityMillis==0&&state.memory.phase==QueueLightPhase::Cooldown,"actual rank transition starts recent-eligibility grace");
  state=light(state.memory,input,31999);check(state.recentlyEligible&&state.eligibilityMillis==29999,"reservation grace below30s");state=light(state.memory,input,32000);check(!state.recentlyEligible&&state.eligibilityMillis==30000,"reservation grace excludes exact30s");
  input.rank=-7;state=light(state.memory,input,121999);check(!state.changed&&state.memory.phase==QueueLightPhase::Cooldown,"requalification waits full120s");state=light(state.memory,input,122000);check(state.changed&&state.memory.phase==QueueLightPhase::Eligible,"exact120s requalification");
  input.providerAvailable=false;state=light(state.memory,input,122001);check(!state.known&&!state.recentlyEligible&&state.memory.phase==QueueLightPhase::Cooldown,"provider loss revokes usable eligibility without fabricated service");
  input.providerAvailable=true;state=light(state.memory,input,122002);check(state.memory.phase==QueueLightPhase::Cooldown,"provider recovery does not bypass cooldown");
  input.privateTorrent=false;check(light({},input,1).memory.phase==QueueLightPhase::Never,"public torrent not a private light seed");input.privateTorrent.reset();check(!light({},input,1).known,"missing private fact is unknown");input.privateTorrent=true;input.rankIgnoringZeroPeers=-8;check(light({},input,1).memory.phase==QueueLightPhase::Never,"otherwise ignored rank cannot become eligible");input.rankIgnoringZeroPeers.reset();check(!light({},input,1).known,"missing alternate rank stays unknown");
  check(!updateQueueLightSeed(state.memory,input,122000),"clock regression rejects unchanged memory");QueueLightMemory invalid;invalid.phase=QueueLightPhase::Cooldown;check(!updateQueueLightSeed(invalid,input,1),"hostile memory shape rejected");
  QueueReservedState pool{{11,12}};std::array observations{QueueReservedObservation{11,true,false,false},QueueReservedObservation{12,false,false,false},QueueReservedObservation{13,false,true,true},QueueReservedObservation{14,false,true,true}};
  auto reserved=take(planQueueReservedSlots(2,pool,observations,true));check(reserved.state.holders==std::vector<std::uint64_t>{11,13}&&reserved.granted==std::vector<std::uint64_t>{13}&&reserved.available==0,"reclaim inactive actual owner then allocate once in source order");
  auto unknown=take(planQueueReservedSlots(2,pool,observations,false));check(!unknown.coverageKnown&&unknown.state.holders==pool.holders&&unknown.granted.empty(),"incomplete owner coverage cannot reclaim or grant");
  std::array active{QueueReservedObservation{11,true,false,false},QueueReservedObservation{12,true,false,false}};reserved=take(planQueueReservedSlots(0,pool,active,true));check(reserved.state.holders==pool.holders&&reserved.available==0,"shrink keeps active holders until actual inactivity");active[0].retain=active[1].retain=false;check(take(planQueueReservedSlots(0,pool,active,true)).state.holders.empty(),"retirement releases shrunk slots without resume");
  observations[2].recentlyEligible=false;check(!planQueueReservedSlots(2,pool,observations,true),"request without recent authority rejects");check(!planQueueReservedSlots(2,{{11,11}},active,true),"duplicate held token rejects");
  QueuePolicy policy;policy.maximumActive=policy.maximumDownloads=1;policy.minimumDownloads=0;policy.minimumSeedSeconds=policy.minimumSeedActiveSeconds=0;policy.firstPriorityShareRatio=0;policy.maximumStalledSeeds=1;policy.lightSeedingEnabled=false;policy.reservedLightSeedSlots=0;
  const std::array facts{seed(1,false),seed(2,true),seed(3,true),seed(4,true),seed(5,true)};
  policy.flexibleSeedSlots=0;auto trace=queue(policy,facts);check(trace.decisions[0].action==QueueAction::Queue&&trace.decisions[0].reason==QueueReason::ActiveLimit,"actual global load prevents replacement before lower ranks release");
  policy.flexibleSeedSlots=2;check(queue(policy,facts).decisions[0].action==QueueAction::Queue,"flexible equality still blocks");
  policy.flexibleSeedSlots=3;trace=queue(policy,facts);check(trace.decisions[0].action==QueueAction::Run&&!(trace.unavailableInputs&4U),"known flexible global headroom permits ordered replacement; no unavailable flex provider");
  const std::array queued{seed(21,false),seed(22,false),seed(23,false)};policy.flexibleSeedSlots=100;trace=queue(policy,queued);unsigned running=0;for(const auto& decision:trace.decisions){running+=decision.action==QueueAction::Run;}check(running==1,"flexible headroom never increases ordinary quota");
  policy.lightSeedingEnabled=true;policy.reservedLightSeedSlots=4;trace=queue(policy,queued);check((trace.unavailableInputs&4U)&&trace.decisions[0].action==QueueAction::Run&&trace.decisions[1].action==QueueAction::Queue,"light provider remains explicitly unavailable; placeholders do not fabricate extra engine seeds");
  std::cout<<"queue slot budgets, eligibility, reservation and actual-fact global traces passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
