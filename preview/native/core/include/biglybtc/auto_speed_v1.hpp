// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace biglybtc::core {

class Document;

struct AutoSpeedV1Settings {
  std::int32_t minimumUpload = 0;
  std::int32_t maximumUpload = 0;
  std::int32_t maximumIncrease = 1024;
  std::int32_t maximumDecrease = 4096;
  std::int32_t chokeMillis = 200;
  std::int32_t latencyFactor = 50;
  std::int32_t calibrationUpload = 4096;
  std::optional<float> downloadRatio;
  bool operator==(const AutoSpeedV1Settings&) const = default;
};

struct AutoSpeedV1Preferences {
  bool enabled = false;
  bool seedingEnabled = false;
  AutoSpeedV1Settings settings;
  bool active(bool seedingOnly) const noexcept { return enabled || (seedingOnly && seedingEnabled); }
  bool operator==(const AutoSpeedV1Preferences&) const = default;
};

// Empty result is another source algorithm, or two disabled switches. A
// malformed active Classic configuration reports the original setting key.
std::expected<std::optional<AutoSpeedV1Preferences>, std::string_view> readAutoSpeedV1Preferences(const Document*);

struct AutoSpeedV1Advice {
  std::int64_t upload;
  std::optional<std::int32_t> download;
  bool operator==(const AutoSpeedV1Advice&) const = default;
};

// Original Classic algorithm. The owner supplies 1-second rate samples, DHT
// response batches and the current limit, with INT64_MAX meaning unlimited as
// in the Java adapter. This class neither measures nor applies any limits.
class AutoSpeedV1 {
public:
  explicit AutoSpeedV1(AutoSpeedV1Settings settings = {}) noexcept;
  void observeRates(std::int32_t payloadUpload, std::int32_t protocolUpload) noexcept;
  void sourceReplaced() noexcept;
  // Negative RTTs represent failed probes. The original uses at most 3 sources.
  std::optional<AutoSpeedV1Advice> adjust(std::span<const std::int32_t> rtts,
                                        std::int64_t currentLimit) noexcept;

private:
  template<std::size_t Periods>
  struct MovingAverage {
    std::array<std::int64_t, Periods> samples{};
    std::int64_t sum = 0;
    std::size_t next = 0;
    std::size_t count = 0;
    void add(std::int64_t value) noexcept {
      sum += value - samples[next];
      samples[next] = value;
      next = (next + 1) % Periods;
      if (count < Periods) { ++count; }
    }
    std::int64_t average() const noexcept {
      return count ? sum / static_cast<std::int64_t>(count) : 0;
    }
  };

  bool calibration_ = false;
  bool establishedIdle_ = false;
  bool increasing_ = true;
  unsigned elapsedTicks_ = 0;
  unsigned modeTicks_ = 0;
  unsigned idleTicks_ = 0;
  unsigned replacedSources_ = 0;
  std::int64_t idleRtt_ = 100;
  std::int64_t savedLimit_ = 0;
  std::int64_t currentUpload_ = 0;
  MovingAverage<5> upload_;
  MovingAverage<2> recentUpload_;
  MovingAverage<2> recentProtocol_;
  MovingAverage<5> delay_;
  MovingAverage<3> chokeSpeed_;
  AutoSpeedV1Settings settings_;
};

} // namespace biglybtc::core
