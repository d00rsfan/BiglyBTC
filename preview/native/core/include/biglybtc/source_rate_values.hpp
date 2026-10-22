#pragma once

#include <cstdint>

namespace biglybtc::core {

// Bytes/second from source-style windows over cumulative sample-delivery
// differences of ONE engine owner. Separate from observed raw interval rates,
// original event timing, global/lifetime traffic and visual smoothing.
struct SourceRateValues {
  std::int64_t payloadReceived = 0;
  std::int64_t protocolReceived = 0;
  std::int64_t payloadSent = 0;
  std::int64_t protocolSent = 0;
  std::int64_t smoothedReceived = 0;
  std::int64_t smoothedSent = 0;
  bool operator==(const SourceRateValues&) const = default;
};

} // namespace biglybtc::core
