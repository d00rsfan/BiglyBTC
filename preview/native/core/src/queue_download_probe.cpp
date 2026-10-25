#include "biglybtc/queue_download_probe.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <set>
#include <utility>

namespace biglybtc::core {
std::expected<QueueProbePlan, QueueProbeError> QueueDownloadProbes::observe(
    const QueueProbeConfig& config, std::span<const QueueProbeInput> inputs,
    const QueueProbeContext& context) {
  constexpr auto Max = std::numeric_limits<std::int64_t>::max();
  if (config.testSeconds < 0 || config.testSeconds > std::numeric_limits<std::int32_t>::max()/1000
      || config.retestMinutes < 0 || config.retestMinutes > std::numeric_limits<std::int32_t>::max()/60000) {
    return std::unexpected(QueueProbeError::Configuration);
  }
  if (inputs.size() > 8192) { return std::unexpected(QueueProbeError::Limit); }
  if (context.nowMillis < 0 || context.ownerStartedMillis < 0 || context.nowMillis < context.ownerStartedMillis
      || (lastMillis_ && context.nowMillis < *lastMillis_)
      || (ownerStartedMillis_ && context.ownerStartedMillis!=*ownerStartedMillis_) || context.downloadLimitKiB < 0
      || (context.averagedDownloadKiB && *context.averagedDownloadKiB < 0)) {
    return std::unexpected(QueueProbeError::Clock);
  }
  try {
    std::set<std::uint64_t> tokens;
    for (const auto& input : inputs) {
      if (!input.token || !input.position || !tokens.insert(input.token).second
          || static_cast<unsigned>(input.state)>2 || static_cast<unsigned>(input.scope)>2
          || input.receivedBytes.has_value()!=input.observedMillis.has_value()
          || (input.receivedBytes && *input.receivedBytes<0)
          || (input.observedMillis && (*input.observedMillis<context.ownerStartedMillis || *input.observedMillis>context.nowMillis))
          || (input.smoothedEtaSeconds && *input.smoothedEtaSeconds<0)) {
        return std::unexpected(QueueProbeError::Facts);
      }
    }
    auto next = *this;
    std::vector<Entry> retained;
    retained.reserve(inputs.size());
    for (const auto& input : inputs) {
      const auto prior=std::lower_bound(entries_.begin(),entries_.end(),input.token,
          [](const Entry& value,std::uint64_t token){return value.result.token<token;});
      Entry entry;
      if (prior!=entries_.end() && prior->result.token==input.token) {
        if (prior->result.scope!=input.scope) {
          // A checking-only placeholder had no accepted byte scope/baseline.
          // Once any byte prefix was observed, scope remains immutable.
          if (prior->lastBytes || prior->startedMillis || prior->result.bytesPerSecond) {
            return std::unexpected(QueueProbeError::Facts);
          }
        } else { entry=*prior; }
      }
      if (input.receivedBytes) {
        if ((entry.lastBytes && *input.receivedBytes<*entry.lastBytes)
            ||(entry.lastObservedMillis && *input.observedMillis<*entry.lastObservedMillis)) {
          return std::unexpected(QueueProbeError::Facts);
        }
        entry.lastBytes=input.receivedBytes;entry.lastObservedMillis=input.observedMillis;
      }
      entry.result.token=input.token;entry.result.scope=input.scope;entry.result.active=false;
      retained.push_back(std::move(entry));
    }
    std::sort(retained.begin(),retained.end(),[](const Entry& a,const Entry& b){return a.result.token<b.result.token;});
    next.entries_=std::move(retained);
    const auto entryFor=[&](std::uint64_t token)->Entry& {
      return *std::lower_bound(next.entries_.begin(),next.entries_.end(),token,
          [](const Entry& entry,std::uint64_t key){return entry.result.token<key;});
    };
    const auto usable=[](const QueueProbeInput& input) {
      return input.state!=QueueProbeState::Other && input.receivedBytes && input.observedMillis;
    };
    std::vector<const QueueProbeInput*> eligible;
    for (const auto& input:inputs) { if (usable(input)) { eligible.push_back(&input); } }
    std::stable_sort(eligible.begin(),eligible.end(),[](const auto* a,const auto* b) {
      return a->position!=b->position?a->position<b->position:a->token<b->token;
    });
    const QueueProbeInput* active=nullptr;
    if (next.active_) {
      for (const auto* input:eligible) { if (input->token==*next.active_) { active=input;break; } }
      if (!active) { next.active_.reset(); }
    }
    bool unavailable=false, blocked=false;
    const bool started=context.nowMillis-context.ownerStartedMillis>=30000;
    if (!config.enabled) { next.active_.reset();next.saturatedSince_.reset();active=nullptr; }
    else if (started && eligible.size()>=2) {
      if (context.downloadLimitKiB>0) {
        if (!context.averagedDownloadKiB) {
          unavailable=true;blocked=true;next.active_.reset();active=nullptr;next.saturatedSince_.reset();
        } else if (context.downloadLimitKiB-*context.averagedDownloadKiB<5) {
          if (!next.saturatedSince_) { next.saturatedSince_=context.nowMillis; }
          else if (context.nowMillis-*next.saturatedSince_>=60000) { blocked=true;next.active_.reset();active=nullptr; }
        } else { next.saturatedSince_.reset(); }
      } else { next.saturatedSince_.reset(); }
      if (!blocked && active) {
        auto& entry=entryFor(active->token);
        if (!entry.startedMillis || !entry.baselineBytes || !entry.baselineMillis) {
          unavailable=true;next.active_.reset();active=nullptr;
        } else if (context.nowMillis-*entry.startedMillis>=static_cast<std::int64_t>(config.testSeconds)*1000) {
          const auto elapsed=*active->observedMillis-*entry.baselineMillis;
          if (elapsed<0 || *active->receivedBytes<*entry.baselineBytes) {
            entry.result.bytesPerSecond.reset();entry.result.etaSeconds.reset();unavailable=true;
          } else if (elapsed>=1000) {
            const auto bytes=*active->receivedBytes-*entry.baselineBytes;
            // The admitted ordinary domain has no Java long/int overflow.
            // Outside it, do not publish a wrapped or fabricated speed.
            if (bytes>Max/1000 || bytes*1000/elapsed>std::numeric_limits<std::int32_t>::max()) {
              entry.result.bytesPerSecond.reset();entry.result.etaSeconds.reset();unavailable=true;
            } else {
              entry.result.bytesPerSecond=static_cast<std::int32_t>(bytes*1000/elapsed);
              entry.result.etaSeconds=active->smoothedEtaSeconds;
            }
          }
          next.active_.reset();active=nullptr;
        }
      }
      if (!blocked && !next.active_) {
        const auto adjusted=static_cast<std::int64_t>(config.retestMinutes)*60000
            +static_cast<std::int64_t>(config.testSeconds)*1000*static_cast<std::int64_t>(eligible.size());
        const auto select=[&](QueueProbeState state)->const QueueProbeInput* {
          const QueueProbeInput* oldest=nullptr;std::int64_t oldestAge=-1;
          for (const auto* input:eligible) {
            if (input->state!=state) { continue; }
            const auto& entry=entryFor(input->token);
            if (!entry.startedMillis) { return input; }
            const auto age=context.nowMillis-*entry.startedMillis;
            if (config.retestMinutes>0 && age>=adjusted && age>oldestAge) { oldest=input;oldestAge=age; }
          }
          return oldest;
        };
        active=select(QueueProbeState::Queued);
        if (!active && config.testActive) { active=select(QueueProbeState::Downloading); }
        if (active) {
          auto& entry=entryFor(active->token);entry.startedMillis=context.nowMillis;
          entry.baselineBytes=active->receivedBytes;entry.baselineMillis=active->observedMillis;next.active_=active->token;
        }
      }
    }
    QueueProbePlan result;result.unavailable=unavailable;result.observations.reserve(inputs.size());
    for (const auto& input:inputs) {
      auto& entry=entryFor(input.token);entry.result.active=next.active_&&input.token==*next.active_;
      auto observation=entry.result;
      // Exact source fallback: a never-completed ETA may use actual current
      // smoothed ETA only while downloading. An instant-rate estimate is absent.
      if (!observation.etaSeconds && input.state==QueueProbeState::Downloading
          && input.smoothedEtaSeconds && *input.smoothedEtaSeconds>0) { observation.etaSeconds=input.smoothedEtaSeconds; }
      result.observations.push_back(observation);
    }
    next.lastMillis_=context.nowMillis;
    next.ownerStartedMillis_=context.ownerStartedMillis;
    *this=std::move(next);
    return result;
  } catch (const std::bad_alloc&) { return std::unexpected(QueueProbeError::Memory); }
}
} // namespace biglybtc::core
