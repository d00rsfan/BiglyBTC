#include "biglybtc/queue_download_probe.hpp"
#include <array>
#include <charconv>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <string>
#include <vector>

namespace {
using namespace biglybtc::core;
std::int64_t number(const std::string& text){
  std::int64_t value=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
  if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()){throw std::runtime_error("Invalid explicit scalar");}return value;
}
std::int64_t combined(std::int64_t payload,std::int64_t protocol){
  if(payload<0||protocol<0||protocol>std::numeric_limits<std::int64_t>::max()-payload){throw std::runtime_error("Unadmitted combined counter domain");}
  return payload+protocol;
}
}
int main(){try{
  unsigned cases=0;
  for(std::string line;std::getline(std::cin,line);){
    if(line.empty()||line.front()=='#'){continue;}
    std::istringstream input(line);std::vector<std::string> f;
    for(std::string field;std::getline(input,field,'\t');){f.push_back(std::move(field));}
    if(f.size()!=10){throw std::runtime_error("Invalid probe tape row");}
    const auto start=number(f[1]),end=number(f[2]),eta=number(f[7]),fallback=number(f[9]);
    if(start<30000||end<start||eta< -1||fallback< -1||(f[8]!="running"&&f[8]!="queued")){throw std::runtime_error("Unadmitted probe fixture");}
    QueueDownloadProbes probes;QueueProbeConfig config{true,true,0,0};std::array<QueueProbeInput,2> facts{};
    for(std::size_t i=0;i<facts.size();++i){facts[i].token=i+1;facts[i].position=static_cast<std::uint32_t>(i+1);facts[i].scope=QueueProbeScope::OriginalCombined;
      facts[i].state=QueueProbeState::Queued;facts[i].receivedBytes=0;facts[i].observedMillis=start;}
    facts[0].receivedBytes=combined(number(f[3]),number(f[4]));
    // Queued priority makes the subject the actual selected probe even when
    // its provider state becomes Downloading before the completion observation.
    auto result=probes.observe(config,facts,{start,0,0,{}});
    if(!result||!result->observations[0].active){throw std::runtime_error("Subject probe did not start");}
    facts[0].receivedBytes=combined(number(f[5]),number(f[6]));facts[0].observedMillis=end;facts[1].observedMillis=end;
    if(eta>=0){facts[0].smoothedEtaSeconds=eta;}
    facts[0].state=f[8]=="running"?QueueProbeState::Downloading:QueueProbeState::Queued;
    result=probes.observe(config,facts,{end,0,0,{}});if(!result){throw std::runtime_error("Subject completion rejected");}
    // A separate unchanged observation exercises the original untested ETA
    // getter fallback AFTER completion without injecting a new measurement.
    facts[0].smoothedEtaSeconds=fallback>=0?std::optional<std::int64_t>{fallback}:std::nullopt;
    result=probes.observe(config,facts,{end,0,0,{}});if(!result){throw std::runtime_error("Subject fallback rejected");}
    const auto& actual=result->observations[0];
    std::cout<<"OBS\t"<<f[0]<<"\tspeed\t"<<actual.bytesPerSecond.value_or(-1)<<'\n';
    std::cout<<"OBS\t"<<f[0]<<"\teta\t"<<actual.etaSeconds.value_or(-1)<<'\n';++cases;
  }
  std::cout<<"PASS_QUEUE_PROBE_NATIVE\t"<<cases<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
