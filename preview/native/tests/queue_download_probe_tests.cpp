#include "biglybtc/queue_download_probe.hpp"
#include "biglybtc/queue_policy.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
using namespace biglybtc::core;
void check(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
QueueProbePlan observe(QueueDownloadProbes& probes,const QueueProbeConfig& config,
    std::span<const QueueProbeInput> inputs,QueueProbeContext context){
  auto result=probes.observe(config,inputs,context);check(result.has_value(),"Valid probe observation rejected");return std::move(*result);
}
std::array<QueueProbeInput,2> inputs(){
  std::array<QueueProbeInput,2> result{};
  for(std::size_t i=0;i<result.size();++i){
    result[i].token=i+1;result[i].position=static_cast<std::uint32_t>(i+1);
    result[i].state=QueueProbeState::Queued;result[i].scope=QueueProbeScope::OriginalCombined;
    result[i].receivedBytes=100;result[i].observedMillis=30000;
  }
  return result;
}
void stamp(std::span<QueueProbeInput> facts,std::int64_t time){for(auto& fact:facts){fact.observedMillis=time;}}
bool active(const QueueProbePlan& result,std::uint64_t token){
  for(const auto& value:result.observations){if(value.token==token){return value.active;}}
  throw std::runtime_error("Missing observation identity");
}
void startupPositionAndActualCounterEndpoints(){
  QueueDownloadProbes probes;QueueProbeConfig config{true,false,120,30};auto facts=inputs();
  facts[0].position=2;facts[1].position=1;stamp(facts,29999);
  auto result=observe(probes,config,facts,{29999,0,0,{}});
  check(!active(result,1)&&!active(result,2),"Original 30-second startup wait bypassed");
  stamp(facts,30000);result=observe(probes,config,facts,{30000,0,0,{}});
  check(active(result,2)&&!active(result,1),"First queued probe must follow saved position");
  stamp(facts,149999);facts[1].receivedBytes=360100;
  result=observe(probes,config,facts,{149999,0,0,{}});
  check(active(result,2)&&!result.observations[1].bytesPerSecond,"Incomplete test manufactured a speed");
  stamp(facts,150000);facts[1].smoothedEtaSeconds=77;
  result=observe(probes,config,facts,{150000,0,0,{}});
  check(result.observations[1].bytesPerSecond==3000&&result.observations[1].etaSeconds==77
      &&active(result,1),"Actual endpoint delta/duration or next queued probe differs");
  check(result.observations[0].token==1&&result.observations[1].token==2,"Input result order must not become sorted row authority");
}
void adjustedRetestAndActiveOptIn(){
  QueueDownloadProbes probes;QueueProbeConfig config{true,false,60,1};auto facts=inputs();
  observe(probes,config,facts,{30000,0,0,{}});
  stamp(facts,90000);facts[0].receivedBytes=60100;observe(probes,config,facts,{90000,0,0,{}});
  stamp(facts,150000);facts[1].receivedBytes=120100;
  auto result=observe(probes,config,facts,{150000,0,0,{}});
  check(!active(result,1)&&!active(result,2),"Completed probes must respect adjusted retest cycle");
  stamp(facts,209999);result=observe(probes,config,facts,{209999,0,0,{}});
  check(!active(result,1),"Adjusted retest fired before minutes plus full test-cycle duration");
  stamp(facts,210000);result=observe(probes,config,facts,{210000,0,0,{}});
  check(active(result,1),"Oldest eligible probe did not restart at exact retest boundary");
  QueueDownloadProbes running;facts=inputs();for(auto& fact:facts){fact.state=QueueProbeState::Downloading;}
  config.retestMinutes=0;result=observe(running,config,facts,{30000,0,0,{}});
  check(!active(result,1)&&!active(result,2),"Default must not probe active downloads");
  config.testActive=true;result=observe(running,config,facts,{30000,0,0,{}});
  check(active(result,1),"Explicit active-test opt-in ignored");
}
void saturationAndUnavailableProviders(){
  QueueDownloadProbes probes;QueueProbeConfig config{true,false,120,30};auto facts=inputs();
  auto result=observe(probes,config,facts,{30000,0,64,{}});
  check(result.unavailable&&!active(result,1),"Missing actual global average became an unlimited/zero observation");
  result=observe(probes,config,facts,{30000,0,64,60});check(active(result,1),"Initial saturation interval should still allow a test");
  stamp(facts,89999);result=observe(probes,config,facts,{89999,0,64,60});check(active(result,1),"Saturation aborted before actual 60-second boundary");
  stamp(facts,90000);result=observe(probes,config,facts,{90000,0,64,60});check(!active(result,1)&&!active(result,2),"Saturated test must not publish a misleading speed");
  stamp(facts,90001);result=observe(probes,config,facts,{90001,0,64,0});check(active(result,2),"Real headroom did not release saturation gate");
  config.enabled=false;result=observe(probes,config,facts,{90001,0,64,0});
  check(!active(result,1)&&!active(result,2),"Changing order must withdraw the temporary probe authority");
}
void gapsEpochsOverflowAndTransactionalRejection(){
  QueueDownloadProbes probes;QueueProbeConfig config{true,false,1,0};auto facts=inputs();
  observe(probes,config,facts,{30000,0,0,{}});
  auto bad=facts;bad[0].receivedBytes=99;
  check(!probes.observe(config,bad,{30000,0,0,{}}),"Counter regression must not look like a reset/new byte prefix");
  bad=facts;bad[0].scope=QueueProbeScope::NativePayload;
  check(!probes.observe(config,bad,{30000,0,0,{}}),"One admission cannot mix original combined and native payload prefixes");
  check(!probes.observe(config,facts,{29999,0,0,{}}),"Clock regression admitted");
  check(!probes.observe(config,facts,{30000,1,0,{}}),"Another owner clock reused the prior admission state");
  stamp(facts,31000);facts[0].receivedBytes=1100;
  auto result=observe(probes,config,facts,{31000,0,0,{}});
  check(result.observations[0].bytesPerSecond==1000,"Rejected input mutated the accepted baseline");
  facts[1].state=QueueProbeState::Other;result=observe(probes,config,facts,{31000,0,0,{}});
  check(!active(result,2),"Manual/forced/checking/removal exclusion did not abort its active probe");
  facts[0].token=3;facts[0].receivedBytes=0;result=observe(probes,config,facts,{31000,0,0,{}});
  check(!result.observations[0].bytesPerSecond,"A fresh admission inherited an old byte test");
  QueueDownloadProbes large;facts=inputs();observe(large,config,facts,{30000,0,0,{}});
  stamp(facts,31000);facts[0].receivedBytes=std::numeric_limits<std::int64_t>::max();
  result=observe(large,config,facts,{31000,0,0,{}});
  check(result.unavailable&&!result.observations[0].bytesPerSecond,"Overflow must remain unknown without signed overflow or fake zero");
}
void shortTestsAndActualEtaFallback(){
  QueueDownloadProbes probes;QueueProbeConfig config{true,false,0,0};auto facts=inputs();
  observe(probes,config,facts,{30000,0,0,{}});stamp(facts,30500);facts[0].receivedBytes=5100;
  auto result=observe(probes,config,facts,{30500,0,0,{}});
  check(!result.observations[0].bytesPerSecond,"Subsecond sample became a completed speed");
  facts[0].state=QueueProbeState::Downloading;facts[0].smoothedEtaSeconds=42;
  result=observe(probes,config,facts,{30500,0,0,{}});
  check(result.observations[0].etaSeconds==42&&!result.observations[1].etaSeconds,"ETA fallback must use only supplied actual downloading smoothed ETA");
}
void queueEffectsAndConfiguration(){
  QueuePolicy policy;policy.maximumDownloads=1;policy.maximumActive=2;policy.minimumDownloads=0;
  policy.downloadOrder=QueueDownloadOrder::Speed;
  std::array<QueueTorrentFacts,2> facts{};
  for(std::size_t i=0;i<facts.size();++i){
    facts[i].token=i+1;facts[i].position=static_cast<std::uint32_t>(i+1);
    facts[i].downloadProbe=QueueProbeObservation{i+1,false,QueueProbeScope::OriginalCombined,100,60};
  }
  facts[1].downloadProbe->active=true;QueueContext context;context.nowMillis=30000;
  auto result=evaluateQueue(policy,facts,context);check(result.has_value(),"Valid probe plan rejected");
  check(result->decisions[0].action==QueueAction::Run&&result->decisions[1].action==QueueAction::Run,
      "One active probe must allow the original temporary download slot within active capacity");
  policy.maximumActive=1;result=evaluateQueue(policy,facts,context);
  check(result&&result->decisions[1].action==QueueAction::Run&&result->decisions[0].action==QueueAction::Queue,
      "Active probe must rank first without violating overall active capacity");
  facts[1].manuallyPaused=true;check(!evaluateQueue(policy,facts,context),"A probe cannot mint authority over manual pause");
  facts[1].downloadProbe->active=false;result=evaluateQueue(policy,facts,context);
  check(result&&result->decisions[1].action==QueueAction::Preserve,"Manual hold changed by completed probe result");
  // Build keys structurally to keep the configuration test independent of literal lengths.
  auto doc=Document::parse("de");check(doc.has_value(),"Empty configuration parse failed");
  check(doc->replaceValue("StartStopManager_Downloading_iTestTimeSecs","i60e").has_value()
      &&doc->replaceValue("StartStopManager_Downloading_iRetestTimeMins","i0e").has_value()
      &&doc->replaceValue("StartStopManager_Downloading_bTestActive","i1e").has_value(),"Fixture configuration edit failed");
  const auto read=readQueuePolicy(&*doc);
  check(read&&read->downloadProbeSeconds==60&&read->downloadProbeRetestMinutes==0&&read->downloadProbeActive,"Actual sparse probe settings not read");
  check(doc->replaceValue("StartStopManager_Downloading_iTestTimeSecs","i2147483647e").has_value(),"Fixture invalid edit failed");
  const auto invalid=readQueuePolicy(&*doc);
  check(!invalid&&invalid.error().key=="StartStopManager_Downloading_iTestTimeSecs","Overflowing original millisecond conversion must carry its exact setting key");
}
} // namespace
int main(){try{
  startupPositionAndActualCounterEndpoints();adjustedRetestAndActiveOptIn();saturationAndUnavailableProviders();
  gapsEpochsOverflowAndTransactionalRejection();shortTestsAndActualEtaFallback();queueEffectsAndConfiguration();
  std::cout<<"PASS six ordinary queue probe families; explicit clocks and source scopes\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
