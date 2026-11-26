// SPDX-License-Identifier: GPL-2.0-or-later
// Source: BiglyBT cb8c6cb9, SpeedManagerAlgorithmProviderPingMap and SpeedLimitMonitor.
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace biglybtc::core {
class Document;

enum class AdaptiveConfidence { None, Low, Medium, High, Fixed };
enum class AdaptiveMode { Downloading, Seeding, DownloadLimitSearch };

struct AutoSpeedV2Settings {
  unsigned ratingInterval=2, uploadChokes=1;
  bool skipAfterAdjustment=true;
  float downloadingUploadFraction=0.6f;
  std::int64_t uploadCapacity=30720, downloadCapacity=61440;
  AdaptiveConfidence uploadConfidence=AdaptiveConfidence::None, downloadConfidence=AdaptiveConfidence::None;
  bool operator==(const AutoSpeedV2Settings&) const = default;
};

struct AutoSpeedV2Preferences {
  bool enabled=false, seedingEnabled=false;
  AutoSpeedV2Settings settings;
  bool active(bool seedingOnly) const { return enabled||(seedingOnly&&seedingEnabled); }
  bool operator==(const AutoSpeedV2Preferences&) const = default;
};

std::expected<std::optional<AutoSpeedV2Preferences>, std::string_view> readAutoSpeedV2Preferences(const Document*);

struct AdaptiveRates {
  std::int64_t upload=0, download=0;
  bool operator==(const AdaptiveRates&) const = default;
};

struct AdaptivePing {
  std::int64_t millis;
  unsigned successful;
};
std::optional<AdaptivePing> adaptivePing(const std::array<std::int32_t,3>& rtt);

struct AdaptiveCapacity {
  std::int64_t bytesPerSecond=0;
  float metric=0, type=-0.1f;
};

struct AdaptiveTrialEstimate {
  std::optional<AdaptiveCapacity> download;
  bool choked=false;
};

class AutoSpeedV2 final {
public:
  explicit AutoSpeedV2(AutoSpeedV2Settings, std::int64_t nowMillis,
      std::optional<AdaptiveCapacity> savedUpload={}, std::optional<AdaptiveCapacity> savedDownload={});
  void observeRates(AdaptiveRates transfer, AdaptiveRates limits, std::int64_t nowMillis);
  void observeCapacity(std::optional<AdaptiveCapacity> upload, std::optional<AdaptiveCapacity> download,
      std::int64_t nowMillis);
  std::optional<AdaptiveRates> adjust(float rating, unsigned successfulPings, std::int64_t nowMillis,
      const AdaptiveTrialEstimate& trial={});
  AdaptiveMode mode() const { return mode_; }
  bool testing() const;

private:
  enum class Saturation { None, Low, Medium, High, AtLimit };
  struct Axis {
    std::int64_t maximum, minimum;
    AdaptiveConfidence confidence;
    Saturation bandwidth=Saturation::None, cap=Saturation::None;
    std::int64_t saturatedSince=0;
    bool pinned=true;
  };
  struct Search {
    std::int64_t lastIncrease=0;
    std::optional<std::int64_t> lastBadPing;
    std::int64_t originalUploadLimit=0, originalCapacity=0;
  };

  static Saturation saturation(std::int64_t rate, std::int64_t limit);
  static std::int64_t chooseCapacity(const Axis&, const AdaptiveCapacity&);
  void updateCaps(std::int64_t upload, std::int64_t download);
  void resetPins(std::int64_t nowMillis);
  float adjustmentMultiplier(float strength, std::int64_t nowMillis);
  bool updatePins(std::int64_t nowMillis);
  AdaptiveRates startSearch(std::int64_t nowMillis);
  AdaptiveRates finishSearch(const AdaptiveTrialEstimate&);
  AdaptiveRates adjustUnpinned(float strength);
  AdaptiveRates adjustPinned(float amount);
  std::optional<AdaptiveRates> constrain() const;

  AutoSpeedV2Settings settings_;
  Axis upload_, download_;
  AdaptiveRates limits_;
  AdaptiveMode mode_=AdaptiveMode::Downloading;
  std::int64_t lastDownload_=0, lastObservation_=0;
  unsigned intervals_=0, consecutiveUp_=0, consecutiveDown_=0, uploadPinCount_=0;
  bool lastAdjusted_=false, unlimitedDownload_=true, ready_=false;
  double uploadFraction_=0.5, downloadFraction_=1.0;
  std::optional<Search> search_;
};
} // namespace biglybtc::core
