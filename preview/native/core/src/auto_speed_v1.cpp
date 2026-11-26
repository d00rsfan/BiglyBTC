// SPDX-License-Identifier: GPL-2.0-or-later
// Based on BiglyBT SpeedManagerAlgorithmProviderV1 at cb8c6cb9.
#include "biglybtc/auto_speed_v1.hpp"
#include "biglybtc/bencode.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>

namespace biglybtc::core {
namespace {
constexpr std::int64_t KiB = 1024;
constexpr unsigned CalibrationTicks = 60;
constexpr unsigned IdleSamples = 5;
constexpr std::int64_t MinimumIdleMillis = 50;

// The source does long addition with wraparound, including its unlimited
// sentinel. Keep that defined in C++ and leave SDK conversion to the owner.
std::int64_t javaAdd(std::int64_t left, std::int64_t right) noexcept {
  return static_cast<std::int64_t>(static_cast<std::uint64_t>(left) + static_cast<std::uint64_t>(right));
}

std::optional<std::int64_t> combinedPing(std::span<const std::int32_t> rtts) noexcept {
  if (rtts.empty() || rtts.size() > 3) { return {}; }
  auto minimum = std::numeric_limits<std::int32_t>::max();
  for (const auto rtt : rtts) {
    if (rtt >= 0) { minimum = std::min(minimum, rtt); }
  }
  std::int64_t total = 0;
  std::int64_t accepted = 0;
  const auto bound = std::int64_t{5} * std::max(minimum, 75);
  for (const auto rtt : rtts) {
    if (rtt >= 0 && rtt < bound) { total += rtt; ++accepted; }
  }
  if (!accepted) { return {}; }
  return (total / accepted + minimum) / 2;
}

std::int32_t javaFloatToInt(float value) noexcept {
  if (value >= 0x1p31f) { return std::numeric_limits<std::int32_t>::max(); }
  if (value <= -0x1p31f) { return std::numeric_limits<std::int32_t>::min(); }
  return static_cast<std::int32_t>(value);
}
} // namespace

std::expected<std::optional<AutoSpeedV1Preferences>, std::string_view> readAutoSpeedV1Preferences(const Document* config) {
  if (!config) { return std::optional<AutoSpeedV1Preferences>{}; }
  const auto get = [&](std::string_view key, std::int32_t fallback, std::int32_t maximum) {
    const auto* node = config->find(key);
    if (!node) { return fallback; }
    if (node->type != Node::Type::Integer || node->integer < 0 || node->integer > maximum) { throw key; }
    return static_cast<std::int32_t>(node->integer);
  };
  try {
    constexpr auto Maximum = std::numeric_limits<std::int32_t>::max();
    AutoSpeedV1Preferences result;
    result.enabled = get("Auto Upload Speed Enabled", 0, Maximum) != 0;
    result.seedingEnabled = get("Auto Upload Speed Seeding Enabled", 0, Maximum) != 0;
    if ((!result.enabled && !result.seedingEnabled) || get("Auto Upload Speed Version", 2, Maximum) != 1) {
      return std::optional<AutoSpeedV1Preferences>{};
    }
    const auto rate = [&](std::string_view key, std::int32_t fallback) {
      return static_cast<std::int32_t>(get(key, fallback, static_cast<std::int32_t>(Maximum / KiB)) * KiB);
    };
    auto& policy = result.settings;
    policy.minimumUpload = rate("AutoSpeed Min Upload KBs", 0);
    policy.maximumUpload = rate("AutoSpeed Max Upload KBs", 0);
    policy.maximumIncrease = rate("AutoSpeed Max Increment KBs", 1);
    policy.maximumDecrease = rate("AutoSpeed Max Decrement KBs", 4);
    policy.chokeMillis = get("AutoSpeed Choking Ping Millis", 200, Maximum);
    policy.latencyFactor = get("AutoSpeed Latency Factor", 50, Maximum);
    policy.calibrationUpload = rate("AutoSpeed Forced Min KBs", 4);
    if (get("AutoSpeed Download Adj Enable", 0, Maximum)) {
      constexpr std::string_view Key = "AutoSpeed Download Adj Ratio";
      policy.downloadRatio = 1.f;
      if (const auto* ratio = config->find(Key)) {
        if (ratio->type != Node::Type::Bytes || ratio->bytes.size() > 64) { return std::unexpected(Key); }
        std::istringstream input(std::string(ratio->bytes));
        input.imbue(std::locale::classic());
        float parsed = 0;
        if (!(input >> parsed) || !std::isfinite(parsed) || parsed < 0 || !(input >> std::ws).eof()) {
          return std::unexpected(Key);
        }
        policy.downloadRatio = parsed;
      }
    }
    return std::optional{result};
  } catch (std::string_view key) {
    return std::unexpected(key);
  }
}

AutoSpeedV1::AutoSpeedV1(AutoSpeedV1Settings settings) noexcept : settings_(settings) {
  settings_.latencyFactor = std::max(settings_.latencyFactor, 1);
  settings_.calibrationUpload = std::max(settings_.calibrationUpload, 1024);
  if (settings_.downloadRatio && !std::isfinite(*settings_.downloadRatio)) { settings_.downloadRatio.reset(); }
}

void AutoSpeedV1::observeRates(std::int32_t payloadUpload, std::int32_t protocolUpload) noexcept {
  if (payloadUpload < 0 || protocolUpload < 0) { return; }
  currentUpload_ = std::int64_t{payloadUpload} + protocolUpload;
  upload_.add(currentUpload_);
  recentUpload_.add(currentUpload_);
  recentProtocol_.add(protocolUpload);
  if (elapsedTicks_ <= CalibrationTicks) { ++elapsedTicks_; }
  if (modeTicks_ <= CalibrationTicks) { ++modeTicks_; }
}

void AutoSpeedV1::sourceReplaced() noexcept {
  if (replacedSources_ < 2) { ++replacedSources_; }
}

std::optional<AutoSpeedV1Advice> AutoSpeedV1::adjust(std::span<const std::int32_t> rtts,
                                                    std::int64_t currentLimit) noexcept {
  const auto ping = combinedPing(rtts);
  if (!ping || !elapsedTicks_ || currentLimit < 0) { return {}; }
  delay_.add(*ping);
  const auto runningDelay = delay_.average();
  if (upload_.average() <= 5 * KiB || (runningDelay < idleRtt_ && !establishedIdle_)) {
    if (idleTicks_ < IdleSamples) { ++idleTicks_; }
    if (idleTicks_ >= IdleSamples) {
      idleRtt_ = std::max(runningDelay, MinimumIdleMillis);
      establishedIdle_ = true;
    }
  } else {
    idleTicks_ = 0;
  }
  if (establishedIdle_ && runningDelay < idleRtt_) {
    idleRtt_ = std::max(runningDelay, MinimumIdleMillis);
  }

  auto recommended = currentLimit;
  if (calibration_) {
    if (establishedIdle_ || modeTicks_ > CalibrationTicks) {
      if (!establishedIdle_) { idleRtt_ = std::max(runningDelay, MinimumIdleMillis); }
      establishedIdle_ = true;
      calibration_ = false;
      modeTicks_ = 0;
      currentLimit = recommended = savedLimit_;
    } else if (modeTicks_ == 5) {
      delay_ = {};
    }
  }

  if (!calibration_) {
    if ((elapsedTicks_ > CalibrationTicks && !establishedIdle_) || (replacedSources_ >= 2 && establishedIdle_)) {
      calibration_ = true;
      modeTicks_ = 0;
      savedLimit_ = currentLimit;
      establishedIdle_ = false;
      idleTicks_ = 0;
      replacedSources_ = 0;
      recommended = settings_.calibrationUpload;
    } else if (runningDelay < 2 * idleRtt_ && *ping < settings_.chokeMillis) {
      increasing_ = true;
      const auto increase = KiB * (std::max(runningDelay - idleRtt_, std::int64_t{100}) / settings_.latencyFactor);
      std::int64_t maximum = settings_.maximumIncrease;
      if (javaAdd(recommended, 2 * KiB) > chokeSpeed_.average()) { maximum = KiB; }
      else if (javaAdd(recommended, 5 * KiB) > chokeSpeed_.average()) { maximum += 3 * KiB; }
      recommended = javaAdd(recommended, std::min(increase, maximum));
    } else if (*ping > 4 * idleRtt_ || *ping > settings_.chokeMillis) {
      if (increasing_ && establishedIdle_) { chokeSpeed_.add(recentUpload_.average()); }
      increasing_ = false;
      const auto decrease = KiB * ((*ping - 3 * idleRtt_) / settings_.latencyFactor);
      recommended = javaAdd(recommended, -std::min(decrease, static_cast<std::int64_t>(settings_.maximumDecrease)));
      recommended = std::max(recommended, recentProtocol_.average() + KiB);
    }
    if (!calibration_) { recommended = std::max(recommended, KiB); }
    if (settings_.minimumUpload > 0 && recommended < settings_.minimumUpload && !calibration_) {
      recommended = settings_.minimumUpload;
    } else if (settings_.maximumUpload > 0 && recommended > settings_.maximumUpload) {
      recommended = settings_.maximumUpload;
    }
    if (recommended > currentLimit && currentUpload_ < currentLimit - 10 * KiB) { recommended = currentLimit; }
  }

  recommended = (javaAdd(recommended, KiB - 1) / KiB) * KiB;
  AutoSpeedV1Advice advice{recommended, {}};
  if (settings_.downloadRatio) {
    advice.download = javaFloatToInt(static_cast<float>(recommended) * *settings_.downloadRatio);
  }
  return advice;
}

} // namespace biglybtc::core
