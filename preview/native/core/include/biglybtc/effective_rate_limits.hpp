#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace biglybtc::core {

enum class RateLimitState { Unavailable, Unlimited, Limited };
struct EffectiveRateLimit {
  RateLimitState state = RateLimitState::Unavailable;
  std::int64_t bytesPerSecond = 0;
  bool operator==(const EffectiveRateLimit&) const = default;
};
enum class UploadSlotState { Unavailable, Disabled, Unlimited, Limited, Automatic };
struct EffectiveUploadSlots {
  UploadSlotState state = UploadSlotState::Unavailable;
  std::int32_t count = 0;
  bool operator==(const EffectiveUploadSlots&) const = default;
};
// Authentication variants are reduced to their protocol family. No proxy
// hostname, port, credentials or per-connection routing claim is retained.
enum class SessionProxyMode { Unavailable, Disabled, Socks4, Socks5, Http };
struct SessionConnectionSettings {
  std::optional<std::int32_t> connectionLimit{};
  EffectiveUploadSlots uploadSlots{};
  SessionProxyMode proxy = SessionProxyMode::Unavailable;
  bool operator==(const SessionConnectionSettings&) const = default;
};

struct SessionRateLimits {
  EffectiveRateLimit download{}, upload{};
  SessionConnectionSettings connections{};
  bool operator==(const SessionRateLimits&) const = default;
};

// Applied settings only. LT normalizes negative requested settings to0 before
// get_settings(), so a negative effective value is inconsistent/unavailable.
// The global peer-class setter clamps positive INT32_MAX to INT32_MAX-1.
inline EffectiveRateLimit effectiveSessionRateLimit(std::optional<std::int32_t> value) noexcept {
  if (!value || *value < 0) { return {}; }
  if (*value == 0) { return {RateLimitState::Unlimited, 0}; }
  return {RateLimitState::Limited, std::min<std::int64_t>(*value, std::numeric_limits<std::int32_t>::max() - 1)};
}
inline bool validEffectiveRateLimit(const EffectiveRateLimit& value) noexcept {
  switch (value.state) {
    case RateLimitState::Unavailable: case RateLimitState::Unlimited: return value.bytesPerSecond == 0;
    case RateLimitState::Limited:
      return value.bytesPerSecond > 0 && value.bytesPerSecond < std::numeric_limits<std::int32_t>::max();
  }
  return false;
}
// This input is already from get_settings(), after LT normalizes <=0
// connection requests and caps them against available file descriptors. Do
// not turn an inconsistent nonpositive snapshot into unlimited or disabled.
inline std::optional<std::int32_t> effectiveSessionConnectionLimit(std::optional<std::int32_t> value) noexcept {
  return value && *value > 0 ? value : std::nullopt;
}
// nullopt automatic means absent/unsupported choking algorithm. Rate-based
// choking computes its slots independently; fixed choking maps negative and
// INT32_MAX to unlimited. Zero means no fixed slots, not no peer traffic.
inline EffectiveUploadSlots effectiveSessionUploadSlots(std::optional<std::int32_t> slotLimit,
    std::optional<bool> automatic) noexcept {
  if (!automatic) { return {}; }
  if (*automatic) { return {UploadSlotState::Automatic, 0}; }
  if (!slotLimit) { return {}; }
  if (*slotLimit < 0 || *slotLimit == std::numeric_limits<std::int32_t>::max()) { return {UploadSlotState::Unlimited, 0}; }
  if (*slotLimit == 0) { return {UploadSlotState::Disabled, 0}; }
  return {UploadSlotState::Limited, *slotLimit};
}
inline bool validSessionConnectionSettings(const SessionConnectionSettings& value) noexcept {
  if (value.connectionLimit && *value.connectionLimit <= 0) { return false; }
  switch (value.uploadSlots.state) {
    case UploadSlotState::Unavailable: case UploadSlotState::Disabled:
    case UploadSlotState::Unlimited: case UploadSlotState::Automatic:
      if (value.uploadSlots.count != 0) { return false; } break;
    case UploadSlotState::Limited:
      if (value.uploadSlots.count <= 0 || value.uploadSlots.count == std::numeric_limits<std::int32_t>::max()) { return false; }
      break;
    default: return false;
  }
  switch (value.proxy) {
    case SessionProxyMode::Unavailable: case SessionProxyMode::Disabled:
    case SessionProxyMode::Socks4: case SessionProxyMode::Socks5: case SessionProxyMode::Http: return true;
  }
  return false;
}
inline bool validSessionRateLimits(const SessionRateLimits& value) noexcept {
  return validEffectiveRateLimit(value.download) && validEffectiveRateLimit(value.upload)
      && validSessionConnectionSettings(value.connections);
}

} // namespace biglybtc::core
