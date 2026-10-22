#pragma once
#include "biglybtc/engine_statistics.hpp"
#include "biglybtc/source_rate_values.hpp"

#include <array>
#include <limits>
#include <optional>

namespace biglybtc::ui {
enum class NativeActivityRateMode { Source10s = 0, Interval = 1, Smoothed = 2 };
using NativeActivitySeries = std::array<std::optional<std::int64_t>,3>;
// Presentation of already accepted primitive values only. Missing source values
// never fall back to interval rates. Smoothing supplies combined values only;
// it cannot manufacture smoothed payload/protocol components.
inline NativeActivitySeries nativeActivityRates(const std::optional<core::EngineRates>& interval,
    const std::optional<core::SourceRateValues>& source, NativeActivityRateMode mode, bool sending) noexcept {
  const auto nonnegative=[](std::int64_t value)->std::optional<std::int64_t>{
    return value>=0?std::optional{value}:std::nullopt;
  };
  switch(mode) {
    case NativeActivityRateMode::Interval:
      if(!interval){return {};}
      return sending?NativeActivitySeries{nonnegative(interval->sent),nonnegative(interval->payloadSent),nonnegative(interval->protocolSent)}
          :NativeActivitySeries{nonnegative(interval->received),nonnegative(interval->payloadReceived),nonnegative(interval->protocolReceived)};
    case NativeActivityRateMode::Source10s: {
      if(!source){return {};}
      const auto payload=nonnegative(sending?source->payloadSent:source->payloadReceived);
      const auto protocol=nonnegative(sending?source->protocolSent:source->protocolReceived);
      std::optional<std::int64_t> combined;
      if(payload && protocol && *payload<=std::numeric_limits<std::int64_t>::max()-*protocol){combined=*payload+*protocol;}
      return {combined,payload,protocol};
    }
    case NativeActivityRateMode::Smoothed:
      if(!source){return {};}
      return {nonnegative(sending?source->smoothedSent:source->smoothedReceived),std::nullopt,std::nullopt};
  }
  return {};
}
} // namespace biglybtc::ui
