#include "live_speed_mapper_binding.hpp"
#include <algorithm>
#include <limits>

namespace biglybtc::app {
namespace {
bool monotone(const core::EngineCounters& a,const core::EngineCounters& b)noexcept{
  return b.received>=a.received&&b.sent>=a.sent&&b.payloadReceived>=a.payloadReceived&&b.payloadSent>=a.payloadSent
      &&b.received-b.payloadReceived>=a.received-a.payloadReceived&&b.sent-b.payloadSent>=a.sent-a.payloadSent;
}
bool running(engine::AccountingState value)noexcept{
  return value==engine::AccountingState::Idle||value==engine::AccountingState::Active||value==engine::AccountingState::Retiring;
}
}
LiveSpeedMapperBinding::LiveSpeedMapperBinding(std::uint64_t profile):profile_(profile),mapper_(std::make_unique<core::PassiveSpeedMapper>()){}
void LiveSpeedMapperBinding::advance()noexcept{
  if(status_.revision==std::numeric_limits<std::uint64_t>::max()){failed_=true;status_.current=false;status_.failure=LiveSpeedFailure::Input;return;}++status_.revision;
}
void LiveSpeedMapperBinding::capture(bool current,LiveSpeedFailure failure)noexcept{
  try{
    auto value=mapper_->snapshot();if(!value){failed_=true;status_.current=false;status_.failure=LiveSpeedFailure::Memory;advance();return;}
    status_.snapshot=std::make_shared<const core::PassiveSpeedSnapshot>(std::move(*value));
    status_.current=current;status_.failure=failure;advance();
  }catch(...){failed_=true;status_.current=false;status_.failure=LiveSpeedFailure::Memory;advance();}
}
void LiveSpeedMapperBinding::gap(std::int64_t now,LiveSpeedFailure failure)noexcept{
  if(!status_.epoch||failed_){return;}
  if(status_.failure==failure&&!status_.current){return;}
  static_cast<void>(mapper_->observe({now,{},{},{},false}));lastMapperTime_=now;
  capture(false,failure);
}
bool LiveSpeedMapperBinding::rateValues(std::int64_t time,std::int64_t& upload,std::int64_t& download)noexcept{
  if(!engineCurrent_||!differences_||!previous_||!dataReceived_||time<0){return false;}
  // Average excludes the current1000ms bucket. Querying a receipt behind a
  // newer addition is equivalent ONLY while both are in that same bucket.
  // Never rewind a ring across completed buckets or guess the missing window.
  const auto query=std::max(time,rateClock_);
  if(query/1000!=time/1000||(time>=lastCounterClock_?time-lastCounterClock_>2000:lastCounterClock_-time>999)){return false;}
  const auto dr=dataReceived_->averageForMillis(3000,query),pr=protocolReceived_->averageForMillis(3000,query);
  const auto ds=dataSent_->averageForMillis(3000,query),ps=protocolSent_->averageForMillis(3000,query);
  rateClock_=query;
  if(!dr||!pr||!ds||!ps){return false;}
  // Source CoreImpl adapter returns payload as Java long and protocol as int.
  upload=core::sourceActivityCombinedRate(*ds,core::narrowSourceRate(*ps));
  download=core::sourceActivityCombinedRate(*dr,core::narrowSourceRate(*pr));
  return upload>=0&&download>=0;
}
void LiveSpeedMapperBinding::observeEngine(const engine::ApplicationPeerObservation& value,std::int64_t now)noexcept{
  if(failed_||!profile_||value.identity.profileSession!=profile_||!value.identity.applicationRun||!value.identity.engineEpoch
      ||!value.sequence||now<0){return;}
  if(identity_.engineEpoch&&value.identity.applicationRun!=identity_.applicationRun){return;}
  if(value.identity.engineEpoch<identity_.engineEpoch||(value.identity.engineEpoch==identity_.engineEpoch&&value.sequence<=engineSequence_)){return;}
  if(value.identity.engineEpoch!=identity_.engineEpoch){
    try{mapper_=std::make_unique<core::PassiveSpeedMapper>();}catch(...){failed_=true;return;}
    identity_=value.identity;status_.epoch=value.identity.engineEpoch;previous_.reset();dataReceived_.reset();protocolReceived_.reset();dataSent_.reset();protocolSent_.reset();
    differences_=false;sourceTick_.reset();lastMapperTime_.reset();previousRttPeer_.reset();rttGeneration_=0;rttSequence_=0;
    capture(false,LiveSpeedFailure::None);
  }
  engineSequence_=value.sequence;
  if(!running(value.state)||!value.observed){
    engineCurrent_=false;previous_.reset();differences_=false;gap(now,LiveSpeedFailure::EngineUnavailable);return;
  }
  const auto& sample=*value.observed;
  if(!core::validEngineStatistics(sample)||sample.elapsedNanoseconds>value.ownerElapsedNanoseconds||now<rateClock_){
    engineCurrent_=false;previous_.reset();differences_=false;gap(now,LiveSpeedFailure::Input);return;
  }
  if(previous_){
    if(sample.elapsedNanoseconds<previous_->elapsedNanoseconds||!monotone(previous_->counters,sample.counters)){
      engineCurrent_=false;previous_.reset();differences_=false;gap(now,LiveSpeedFailure::Input);return;
    }
    if(sample.elapsedNanoseconds==previous_->elapsedNanoseconds){
      if(sample!=*previous_){engineCurrent_=false;gap(now,LiveSpeedFailure::Input);}return;
    }
  }
  engineCurrent_=true;
  if(!previous_){
    auto average=core::SourceRateAverage::create(1000,10,now);
    if(!average){engineCurrent_=false;gap(now,LiveSpeedFailure::Clock);return;}
    dataReceived_=*average;protocolReceived_=*average;dataSent_=*average;protocolSent_=*average;
    previous_=sample;rateClock_=lastCounterClock_=now;return; // Baseline only.
  }
  const auto& a=previous_->counters;const auto& b=sample.counters;
  if(!dataReceived_->add(b.payloadReceived-a.payloadReceived,now)
      ||!protocolReceived_->add((b.received-b.payloadReceived)-(a.received-a.payloadReceived),now)
      ||!dataSent_->add(b.payloadSent-a.payloadSent,now)
      ||!protocolSent_->add((b.sent-b.payloadSent)-(a.sent-a.payloadSent),now)){
    engineCurrent_=false;previous_.reset();differences_=false;gap(now,LiveSpeedFailure::Clock);return;
  }
  previous_=sample;rateClock_=lastCounterClock_=now;differences_=true;
  const auto tick=now/3000;
  if(sourceTick_&&*sourceTick_>=tick){return;}
  std::int64_t upload=0,download=0;if(!rateValues(now,upload,download)){gap(now,LiveSpeedFailure::Alignment);return;}
  // A genuine worker counter delivery drives at most one rate-only source tick
  // per3000ms bucket. No repeated/catch-up ticks or invented ping are generated.
  const auto result=mapper_->observeSpeed({now,upload,download});sourceTick_=tick;lastMapperTime_=now;
  capture(bool(result),result?LiveSpeedFailure::None:LiveSpeedFailure::Input);
}
void LiveSpeedMapperBinding::observeAzureus(const std::optional<AzureusServiceObservation>& value,std::int64_t now)noexcept{
  if(failed_||now<0){return;}
  const bool active=value&&value->current&&value->snapshot;
  if(!active){if(actorCurrent_){gap(now,LiveSpeedFailure::ReceiptLoss);}actorCurrent_=false;return;}
  actorCurrent_=true;const auto& source=*value->snapshot;
  if(source.pingObservationsLost()){gap(now,LiveSpeedFailure::ReceiptLoss);return;}
  if(!source.pingObservation()){return;}const auto& receipt=*source.pingObservation();
  const auto generation=receipt.generation(),sequence=receipt.sequence();
  if(!generation||generation!=source.transport().generation()||!sequence||generation<rttGeneration_){return;}
  if(generation==rttGeneration_&&sequence<=rttSequence_){return;}
  const bool skipped=(generation==rttGeneration_&&sequence-rttSequence_>1)||(generation!=rttGeneration_&&sequence!=1);
  const auto previousGeneration=rttGeneration_;rttGeneration_=generation;rttSequence_=sequence;
  const auto observed=receipt.observedSteadyNanoseconds()/1000000;
  if(receipt.observedSteadyNanoseconds()<0||observed>now||now-observed>2000){gap(now,LiveSpeedFailure::Clock);return;}
  if(skipped||(previousGeneration&&previousGeneration!=generation)){gap(observed,LiveSpeedFailure::ReceiptLoss);return;}
  if(receipt.kind()!=services::azureus_udp::PingObservationKind::Reply||!receipt.rttNanoseconds()){
    gap(observed,LiveSpeedFailure::ReceiptLoss);return;
  }
  const auto rtt=*receipt.rttNanoseconds();
  if(rtt<0||rtt/1000000>std::numeric_limits<std::int32_t>::max()){gap(observed,LiveSpeedFailure::Input);return;}
  if(lastMapperTime_&&observed<=*lastMapperTime_){gap(observed,LiveSpeedFailure::Clock);return;}
  std::int64_t upload=0,download=0;if(!rateValues(observed,upload,download)){gap(observed,LiveSpeedFailure::Alignment);return;}
  const bool rebase=!previousRttPeer_||*previousRttPeer_!=receipt.endpoint();
  previousRttPeer_=receipt.endpoint();
  const auto result=mapper_->observe({observed,upload,download,rtt/1000000,rebase});
  sourceTick_=observed/3000;lastMapperTime_=observed;
  capture(bool(result),result?LiveSpeedFailure::None:LiveSpeedFailure::Input);
}
void LiveSpeedMapperBinding::close()noexcept{
  engineCurrent_=false;actorCurrent_=false;status_.current=false;status_.failure=LiveSpeedFailure::EngineUnavailable;advance();
}
} // namespace biglybtc::app
