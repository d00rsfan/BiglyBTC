// SPDX-License-Identifier: GPL-2.0-or-later
#include "automatic_speed_control.hpp"
#include "general_chat_service.hpp"
#include "live_speed_mapper_binding.hpp"
#include "../adapters/saved_speed_manager_view.hpp"

#include <algorithm>
#include <limits>

namespace biglybtc::app {

void AutomaticSpeedControl::step(const storage::ProfileLease& lease,const engine::NetworkPolicy& policy,
    engine::PersistentSession& owner,GeneralChats* chats,LiveSpeedMapperBinding* rateWindows,
    bool schedulerEnabled,std::int64_t now) {
  auto& state=state_;
  const auto estimate=[](const auto& value) -> core::AdaptiveCapacity {
    return {value.bytesPerSecond,value.metricRating,value.estimateType};
  };
  const auto capacityIdentity=[](const auto& value) {
    return std::pair{value.bytesPerSecond,value.estimateType};
  };
  if(now<state.pollAfter){return;}state.pollAfter=now+1000;
  const auto restore=[&] {
    if(state.applied&&owner.applyLiveSettings(policy)){state.applied=false;}
  };
  const auto stop=[&] {
    if(chats&&(state.controller||state.adaptive)){chats->setAutoSpeedProbeEpoch(0);}
    state.controller.reset();state.adaptive.reset();state.mapper.reset();state.trial.reset();
    state.preferences.reset();state.adaptivePreferences.reset();
    state.source.reset();state.lastRate.reset();state.lastAdvice.reset();state.lastProbeRoute=0;restore();
  };
  if((!policy.classicAutoSpeed&&!policy.adaptiveAutoSpeed)||!chats
      ||schedulerEnabled){
    stop();return;
  }
  const auto applied=owner.liveSettings();
  if(!applied||!policy.autoSpeedActive(applied->seedingOnly)
      ||applied->rateLimits.upload.state==core::RateLimitState::Unavailable
      ||(policy.adaptiveAutoSpeed&&(!rateWindows
          ||applied->rateLimits.download.state==core::RateLimitState::Unavailable))){
    stop();return;
  }
  constexpr std::int64_t SampleLifetime=12000;
  if((!state.controller&&!state.adaptive)||state.preferences!=policy.classicAutoSpeed
      ||state.adaptivePreferences!=policy.adaptiveAutoSpeed||state.seedingUpload!=applied->seedingUploadActive
      ||(state.lastAdvice&&now-*state.lastAdvice>=SampleLifetime)){
    stop();
    if(state.applied||state.epoch==UINT64_MAX){return;}
    state.preferences=policy.classicAutoSpeed;
    state.adaptivePreferences=policy.adaptiveAutoSpeed;
    if(state.adaptivePreferences){
      auto mapper=std::make_unique<core::PassiveSpeedMapper>();
      const auto config=storage::ConfigFile::load(lease,"biglybt.config");
      if(config){
        const auto saved=storage::readSavedSpeedManagerView(lease,config->document().originalBytes());
        if(saved&&saved->history&&saved->history->source){
          static_cast<void>(mapper->restoreSaved(saved->history->source->originalBytes()));
        }
      }
      std::optional<core::AdaptiveCapacity> upload,download;
      state.uploadCapacity.reset();state.downloadCapacity.reset();
      if(const auto saved=mapper->snapshot()){
        if(saved->uploadCapacity){
          upload=estimate(saved->uploadCapacity->value);
          state.uploadCapacity=capacityIdentity(saved->uploadCapacity->value);
        }
        if(saved->downloadCapacity){
          download=estimate(saved->downloadCapacity->value);
          state.downloadCapacity=capacityIdentity(saved->downloadCapacity->value);
        }
      }
      state.adaptive.emplace(state.adaptivePreferences->settings,now,upload,download);
      state.mapper=std::move(mapper);
    }else{
      state.controller.emplace(state.preferences->settings);
    }
    state.lastProbe=0;
    state.seedingUpload=applied->seedingUploadActive;
    chats->setAutoSpeedProbeEpoch(++state.epoch);
    return;
  }

  const auto snapshot=chats->snapshot();
  const auto* probe=snapshot&&!snapshot->closing&&snapshot->autoSpeedProbe
      ? &*snapshot->autoSpeedProbe : nullptr;
  if(state.lastProbeRoute&&(!probe||(probe->epoch==state.epoch&&probe->route!=state.lastProbeRoute))){
    stop();
    return;
  }
  const auto observation=owner.latest();
  if(!observation||!observation->observed||!observation->observed->rates
      ||(observation->state!=engine::AccountingState::Active&&observation->state!=engine::AccountingState::Idle)){
    return;
  }
  if(state.source&&*state.source!=observation->identity){stop();return;}
  state.source=observation->identity;
  const auto& observed=*observation->observed;
  const auto& rates=*observed.rates;
  constexpr auto Maximum=std::numeric_limits<std::int32_t>::max();
  const core::AdaptiveRates current{applied->rateLimits.upload.bytesPerSecond,applied->rateLimits.download.bytesPerSecond};
  const bool newRates=!state.lastRate||observed.elapsedNanoseconds>*state.lastRate;
  if(newRates){
    state.lastRate=observed.elapsedNanoseconds;
    if(state.adaptive){
      state.adaptive->observeRates({rates.sent,rates.received},current,now);
    }else if(rates.payloadSent>=0&&rates.protocolSent>=0&&rates.payloadSent<=Maximum&&rates.protocolSent<=Maximum){
      state.controller->observeRates(static_cast<std::int32_t>(rates.payloadSent),static_cast<std::int32_t>(rates.protocolSent));
    }
  }else if(state.controller){return;}
  if(!probe){return;}
  const auto& sample=*probe;
  if(sample.epoch!=state.epoch||!sample.route||sample.sequence<=state.lastProbe||sample.observedMillis>now
      ||now-sample.observedMillis>5000){return;}
  state.lastProbeRoute=sample.route;
  std::optional<core::AdaptiveRates> recommendation;
  if(state.adaptive){
    const auto ping=core::adaptivePing(sample.rtt);
    if(!ping){return;}
    const auto window=rateWindows->rateWindow(sample.observedMillis);
    if(!window){return;}
    const core::PassiveSpeedObservation input{sample.observedMillis,window->upload,window->download,
        ping->millis,sample.replacements>0||sample.sequence-state.lastProbe!=1||!state.lastProbe};
    const auto previous=state.mapper->snapshot();
    if(!state.mapper->observe(input)){state.lastProbe=sample.sequence;return;}
    core::AdaptiveTrialEstimate trialEstimate;
    if(state.trial){
      if(const auto trial=state.trial->snapshot()){
        if(trial->lastBadDownload){trialEstimate.download=estimate(*trial->lastBadDownload);}
        else if(trial->download){trialEstimate.download=estimate(*trial->download);}
      }
      trialEstimate.choked=previous&&(previous->lastBadUpload||previous->lastBadDownload);
    }
    if(previous&&!state.adaptive->testing()){
      const auto changedCapacity=[&](const auto& capacity,auto& last) -> std::optional<core::AdaptiveCapacity> {
        if(!capacity||last==capacityIdentity(capacity->value)){return {};}
        last=capacityIdentity(capacity->value);
        return estimate(capacity->value);
      };
      state.adaptive->observeCapacity(changedCapacity(previous->uploadCapacity,state.uploadCapacity),
          changedCapacity(previous->downloadCapacity,state.downloadCapacity),now);
    }
    if(previous&&(previous->currentMetricRating||!previous->accepted)){
      recommendation=state.adaptive->adjust(previous->currentMetricRating.value_or(0),ping->successful,now,trialEstimate);
    }
    if(state.adaptive->testing()){
      if(!state.trial){state.trial=std::make_unique<core::PassiveSpeedMapper>();}
      static_cast<void>(state.trial->observe(input));
    }else{state.trial.reset();}
    state.lastProbe=sample.sequence;
  }else{
    state.lastProbe=sample.sequence;
    if(rates.payloadSent<0||rates.protocolSent<0||rates.payloadSent>Maximum||rates.protocolSent>Maximum){return;}
    for(unsigned i=0;i<sample.replacements;++i){state.controller->sourceReplaced();}
    const auto limit=applied->rateLimits.upload.state==core::RateLimitState::Unlimited
        ? std::numeric_limits<std::int64_t>::max():current.upload;
    const auto classic=state.controller->adjust(sample.rtt,limit);
    if(!classic||classic->upload<=0){return;}
    constexpr auto MaximumKiB=(Maximum/1024)*1024;
    recommendation=core::AdaptiveRates{
        std::clamp(classic->upload,std::int64_t{10*1024},std::int64_t{MaximumKiB}),current.download};
    if(classic->download&&*classic->download>=0){
      const int value=*classic->download;
      recommendation->download=value==Maximum?0:value>0?std::min(MaximumKiB,std::max(300*1024,value)):0;
    }
  }
  state.lastAdvice=now;
  if(!recommendation||*recommendation==current){return;}
  auto effective=policy;
  const auto upload=static_cast<int>(recommendation->upload);
  if(applied->seedingUploadActive){effective.seedingUploadBytesPerSecond=upload;}
  else{effective.uploadBytesPerSecond=upload;}
  effective.downloadBytesPerSecond=static_cast<int>(recommendation->download);
  state.applied=true;
  if(!owner.applyLiveSettings(effective)){stop();}
}

} // namespace biglybtc::app
