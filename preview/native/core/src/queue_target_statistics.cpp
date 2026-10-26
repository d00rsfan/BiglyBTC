#include "biglybtc/queue_target_statistics.hpp"
#include <limits>
#include <utility>

namespace biglybtc::core {
using Error=QueueTargetStatisticsError;
std::expected<std::int64_t,Error> sourceSmoothedEta(std::int64_t remaining,std::int64_t rate,
    std::optional<std::int64_t> completed) noexcept {
  if(remaining<0||rate<0||(completed&&*completed<0)){return std::unexpected(Error::Facts);}
  if(!remaining){
    if(!completed){return std::unexpected(Error::Facts);}
    const auto seconds=*completed/1000;return seconds>1?-seconds:0;
  }
  if(!rate){return SourceEtaInfinity;}
  const auto eta=remaining/rate;return eta?eta:1;
}
std::expected<QueueTargetStatistics,Error> QueueTargetEta::observe(const QueueTargetSample& input) noexcept {
  if(!input.admission||!input.sequence||(input.payloadReceived&&*input.payloadReceived<0)
      ||(input.remainingBytes&&*input.remainingBytes<0)
      ||(input.completedElapsedMillis&&*input.completedElapsedMillis<0)){return std::unexpected(Error::Facts);}
  if(input.observedMillis<0){return std::unexpected(Error::Clock);}
  if(previous_){
    if(input.admission!=previous_->admission){return std::unexpected(Error::Facts);}
    if(input.sequence==previous_->sequence&&input==*previous_){return last_;}
    if(input.sequence<=previous_->sequence){return std::unexpected(Error::Stale);}
    if(input.observedMillis<previous_->observedMillis){return std::unexpected(Error::Clock);}
    if(input.payloadReceived&&previous_->payloadReceived&&*input.payloadReceived<*previous_->payloadReceived){return std::unexpected(Error::Facts);}
  }
  auto next=*this;
  QueueTargetStatistics result{input.admission,input.sequence,input.observedMillis,{},{},policy_};
  if(!input.payloadReceived){
    next.average_.reset();next.lastEtaMillis_.reset();next.lastSmoothedReceived_=0;
  }else if(!next.average_){
    next.policy_=sourceSmoothingPolicy(input.smoothingSeconds);
    auto average=SourceRateAverage::create(next.policy_.intervalSeconds*1000,next.policy_.windowSeconds,input.observedMillis);
    if(!average){return std::unexpected(Error::Clock);}
    next.average_=*average;next.baseline_=*input.payloadReceived;next.firstMillis_=input.observedMillis;
    next.lastSmoothedReceived_=0;next.lastEtaMillis_.reset();
  }else{
    const auto ticks=(input.observedMillis-next.firstMillis_)/1000;
    // Test the OLD interval before applying a new smoothing configuration,
    // exactly as PEPeerManagerStatsImpl.update; pending deltas are retained.
    if(ticks%next.policy_.intervalSeconds==0){
      const auto desired=sourceSmoothingPolicy(input.smoothingSeconds);
      if(desired.windowSeconds!=next.policy_.windowSeconds){
        auto average=SourceRateAverage::create(desired.intervalSeconds*1000,desired.windowSeconds,input.observedMillis);
        if(!average){return std::unexpected(Error::Clock);}next.average_=*average;next.policy_=desired;
      }
      const auto relative=*input.payloadReceived-next.baseline_;
      if(relative<next.lastSmoothedReceived_){return std::unexpected(Error::Facts);}
      if(!next.average_->add(relative-next.lastSmoothedReceived_,input.observedMillis)){return std::unexpected(Error::Clock);}
      next.lastSmoothedReceived_=relative;
    }
    const auto rate=next.average_->average(input.observedMillis);
    if(!rate||*rate<0){return std::unexpected(Error::Facts);}
    result.smoothedPayloadReceiveBytesPerSecond=*rate;
    if(input.remainingBytes){
      if(!next.lastEtaMillis_||input.observedMillis-*next.lastEtaMillis_>900){
        const auto eta=sourceSmoothedEta(*input.remainingBytes,*rate,input.completedElapsedMillis);
        if(eta){result.etaSeconds=*eta;next.lastEtaMillis_=input.observedMillis;}
        else{next.lastEtaMillis_.reset();}
      }else{result.etaSeconds=next.last_.etaSeconds;}
    }else{next.lastEtaMillis_.reset();}
  }
  result.smoothing=next.policy_;next.last_=result;next.previous_=input;*this=std::move(next);return result;
}
std::expected<std::optional<std::int32_t>,Error> QueueStaleSeedTiming::observe(const QueueStaleSeedInput& input) noexcept {
  if(input.nowMillis<0||(now_&&input.nowMillis<*now_)){return std::unexpected(Error::Clock);}
  now_=input.nowMillis;
  if(!input.activityKnown){staleSince_.reset();uncertain_=true;return std::optional<std::int32_t>{};}
  if(uncertain_){
    if(input.thresholdBranch&&!input.wasActive&&input.active){offset_=0;uncertain_=false;}
    else{return std::optional<std::int32_t>{};}
  }
  if(!input.seeding){staleSince_.reset();return std::optional<std::int32_t>{offset_};}
  if(!input.thresholdBranch){staleSince_.reset();}
  else if(input.wasActive!=input.active){
    if(input.active){staleSince_.reset();offset_=0;}else{staleSince_=input.nowMillis;}
  }
  // The original refresh clock starts at0 and is NOT reset on transition.
  // Preserve its strict >60s boundary; do not replace it with stale duration.
  if(staleSince_&&*staleSince_>0&&input.nowMillis-lastRefresh_>60000){
    const auto increment=(input.nowMillis-lastRefresh_)/60000;
    if(increment>std::numeric_limits<std::int32_t>::max()-offset_){return std::unexpected(Error::Facts);}
    offset_+=static_cast<std::int32_t>(increment);lastRefresh_=input.nowMillis;
  }
  return std::optional<std::int32_t>{offset_};
}
} // namespace biglybtc::core
