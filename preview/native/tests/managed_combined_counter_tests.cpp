#include "../engine/managed_payload_counters.hpp"
#include "biglybtc/queue_download_probe.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Counters=biglybtc::engine::detail::ManagedPayloadCounters;
void check(bool okay,const char* message){if(!okay){throw std::runtime_error(message);}}
Counters::Epoch epoch(const Counters& value){const auto result=value.epoch();check(result.has_value(),"Missing essential payload epoch");return *result;}
void legacyCannotGainCombinedAuthorityFromHeaderOrStatus(){
  Counters value;value.installed();
  auto result=value.combine(epoch(value),100,10,Counters::Protocol{20,30});
  check(result.state==Counters::Read::Accepted&&result.received==100&&result.sent==10
      &&!result.combinedReceived&&!result.combinedSent,"Legacy status fields cannot authorize protocol");
  value.seal(150,15);
  result=value.combine(epoch(value),5,7,Counters::Protocol{3,4});
  check(result.state==Counters::Read::Accepted&&result.received==155&&result.sent==22
      &&!result.combinedReceived,"Old v1 reset continuity changed");
}
void sameResetEpochSealsBothChannelsAndRetriesCrossingRead(){
  Counters value;value.installed(true);const auto before=epoch(value);
  auto result=value.combine(before,100,10,Counters::Protocol{20,30});
  check(result.combinedReceived==120&&result.combinedSent==40,"Initial payload/protocol pair differs");
  value.seal(150,15,Counters::Protocol{25,35});
  result=value.combine(before,0,0,Counters::Protocol{0,0});
  check(result.state==Counters::Read::Retry&&!result.combinedReceived,"Crossing reset read must not publish either channel");
  result=value.combine(epoch(value),5,7,Counters::Protocol{3,4});
  check(result.state==Counters::Read::Accepted&&result.received==155&&result.sent==22
      &&result.combinedReceived==183&&result.combinedSent==61,"Raw reset tails or next epoch were lost/doubled");
  value.seal(5,7,Counters::Protocol{3,4});
  result=value.combine(epoch(value),0,0,Counters::Protocol{0,0});
  check(result.combinedReceived==183&&result.combinedSent==61,"Immediate repeat pause/resume lost exact tails");
}
void optionalProtocolFailureNeverPoisonsSafePayload(){
  Counters missing;missing.installed(true);
  auto result=missing.combine(epoch(missing),100,10);
  check(result.state==Counters::Read::Accepted&&!result.combinedReceived,"Missing protocol became zero or failed payload");
  result=missing.combine(epoch(missing),110,11,Counters::Protocol{20,30});
  check(result.state==Counters::Read::Accepted&&result.received==110&&!result.combinedReceived,
      "An uncertain admission must not resume combined certification");
  Counters regressed;regressed.installed(true);
  result=regressed.combine(epoch(regressed),100,10,Counters::Protocol{20,30});
  check(result.combinedReceived==120,"Fixture actual prefix unavailable");
  regressed.seal(110,11,Counters::Protocol{19,30});
  result=regressed.combine(epoch(regressed),2,3,Counters::Protocol{0,0});
  check(result.state==Counters::Read::Accepted&&result.received==112&&!result.combinedReceived,
      "Protocol regression must latch unknown while essential checkpoint continues");
  Counters overflow;overflow.installed(true);
  result=overflow.combine(epoch(overflow),100,10,Counters::Protocol{std::numeric_limits<std::int64_t>::max(),0});
  check(result.state==Counters::Read::Accepted&&result.received==100&&!result.combinedReceived,
      "Combined overflow must not wrap/fake zero or fail payload");
  overflow.seal(101,11,Counters::Protocol{0,0});
  check(overflow.epoch().has_value(),"Optional overflow invalidated the essential epoch");
  Counters essential;essential.installed(true);
  check(essential.combine(epoch(essential),100,10,Counters::Protocol{20,30}).state==Counters::Read::Accepted,
      "Essential fixture prefix rejected");
  essential.seal(99,10,Counters::Protocol{20,30});
  check(!essential.epoch(),"Essential payload regression must retain the existing fail-closed behavior");
}
void unknownPlaceholderDoesNotPreauthorizeOrMixCounterScopes(){
  using namespace biglybtc::core;
  QueueDownloadProbes probes;QueueProbeConfig config{true,false,60,0};std::array<QueueProbeInput,2> inputs{};
  for(std::size_t i=0;i<inputs.size();++i){inputs[i].token=i+1;inputs[i].position=static_cast<std::uint32_t>(i+1);}
  auto result=probes.observe(config,inputs,{30000,0,0,{}});
  check(result&&!result->observations[0].active,"Checking placeholder must not initiate a probe");
  for(auto& input:inputs){input.state=QueueProbeState::Queued;input.scope=QueueProbeScope::NativeCombined;input.receivedBytes=0;input.observedMillis=30001;}
  result=probes.observe(config,inputs,{30001,0,0,{}});
  check(result&&result->observations[0].active&&result->observations[0].scope==QueueProbeScope::NativeCombined,
      "First genuine combined observation cannot be replaced by placeholder payload scope");
  inputs[0].scope=QueueProbeScope::NativePayload;
  check(!probes.observe(config,inputs,{30001,0,0,{}}),"An established combined prefix must never switch to payload after a gap");
}
}
int main(){try{
  legacyCannotGainCombinedAuthorityFromHeaderOrStatus();sameResetEpochSealsBothChannelsAndRetriesCrossingRead();
  optionalProtocolFailureNeverPoisonsSafePayload();unknownPlaceholderDoesNotPreauthorizeOrMixCounterScopes();
  std::cout<<"PASS four optional combined-counter/reset/scope families\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
