// SPDX-License-Identifier: GPL-2.0-or-later
// Source policy: Azureus Software, Inc. SpeedManagerPingMapperImpl (cb8c6cb9).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <memory>
#include <string_view>
#include <vector>

namespace biglybtc::core {
class Document;
enum class PassiveSpeedError { Missing,Invalid,Clock,Overflow,Memory };
enum class PassiveSpeedState { Empty,Ready,Gap,Invalid,Clock,Overflow,Memory };
struct PassiveSpeedObservation {
  std::int64_t observedMillis=0;
  std::optional<std::int64_t> uploadBytesPerSecond,downloadBytesPerSecond,rttMillis;
  bool rebase=false;
  bool operator==(const PassiveSpeedObservation&)const=default;
};
struct PassiveSpeedPoint {
  PassiveSpeedObservation input;
  std::uint16_t uploadBin=0,downloadBin=0,metric=0;
  std::int32_t normalizedRttMillis=0;
  bool connectedToPrevious=false;
  bool operator==(const PassiveSpeedPoint&)const=default;
};
struct PassiveSpeedZone {
  std::int32_t uploadStart=0,uploadEnd=0,downloadStart=0,downloadEnd=0;
  std::uint16_t metric=0;
  // Original short comparisons precede unsigned getters; extents may reverse.
  bool reversedUpload=false,reversedDownload=false;
};
struct PassiveSpeedSegment {
  std::int32_t averageMetric=0,start=0,end=0;
  std::uint16_t varianceType=0,hits=0;
};
struct PassiveSpeedEstimate {
  std::int32_t bytesPerSecond=0;
  float metricRating=0,estimateType=0; // Source TYPE_ESTIMATED; never capacity.
  std::uint16_t hits=0;
  std::int64_t whenMillis=0;
  std::vector<PassiveSpeedSegment> segments;
};
struct PassiveSpeedLimitRecord {
  std::int64_t bytesPerSecond=0;
  float metricRating=0,estimateType=0;
  std::int32_t hits=0;
  std::int64_t whenMillis=0;
  bool fromSaved=false;
  bool operator==(const PassiveSpeedLimitRecord&)const=default;
};
enum class PassiveCapacityOrigin { Saved,Sustained,Choke };
struct PassiveSpeedCapacity {
  PassiveSpeedLimitRecord value;
  PassiveCapacityOrigin origin=PassiveCapacityOrigin::Saved;
  std::optional<std::int64_t> lastDerivedMillis;
  bool current=false;
};
struct PassiveSpeedRateObservation {
  std::int64_t observedMillis=0;
  std::optional<std::int64_t> uploadBytesPerSecond,downloadBytesPerSecond;
};
struct PassiveSpeedSnapshot {
  PassiveSpeedState state=PassiveSpeedState::Empty;
  std::uint64_t accepted=0,dropped=0,gaps=0;
  std::optional<PassiveSpeedObservation> lastAttempt;
  std::vector<PassiveSpeedPoint> history;
  std::vector<PassiveSpeedZone> zones;
  std::optional<float> currentMetricRating;
  std::optional<PassiveSpeedEstimate> upload,download;
  std::optional<PassiveSpeedEstimate> persistentUpload,persistentDownload;
  bool persistentUploadFromSaved=false,persistentDownloadFromSaved=false;
  std::optional<PassiveSpeedCapacity> uploadCapacity,downloadCapacity;
  std::vector<PassiveSpeedLimitRecord> badUpload,badDownload;
  std::optional<PassiveSpeedLimitRecord> lastBadUpload,lastBadDownload,bestGoodUpload,bestGoodDownload;
  unsigned badUploadCountdown=0,badDownloadCountdown=0,sustainedSamples=0;
  std::uint64_t speedOnlyAccepted=0;
  bool lastEventWasSpeedOnly=false;
  std::shared_ptr<const Document> savedSource;
};
// Passive, single-owner, Qt/IO-free variance mapper. Only explicit qualified
// combined-rate/RTT observations enter it; there is no probe/clock or activation.
// The source's 1200-entry default and 10% oldest-point eviction are preserved.
// Missing/invalid/non-increasing time/checked arithmetic failure breaks current
// continuity and makes current estimates unknown, retaining earlier actual data.
class PassiveSpeedMapper final {
public:
  static constexpr std::size_t MaximumPoints=1200,MaximumBins=65536;
  static constexpr std::size_t SustainedSamples=60,MaximumBadHistory=16;
  std::expected<void,PassiveSpeedError> observe(PassiveSpeedObservation)noexcept;
  // Original addSpeed path: explicit qualified combined-rate tick, no RTT/zone.
  // Caller chooses this OR observe for one source tick; never double-feed both.
  std::expected<void,PassiveSpeedError> observeSpeed(PassiveSpeedRateObservation)noexcept;
  // Fresh-owner-only read of original capacity/bias fields. Does not restore
  // saved ping points as live measurements, edit a Document or perform IO.
  std::expected<void,PassiveSpeedError> restoreSaved(std::string_view);
  // Fresh owned vectors; no alias to mutable mapper storage. Memory failure is
  // explicit. Snapshotting does not add observations or change the state.
  std::expected<PassiveSpeedSnapshot,PassiveSpeedError> snapshot()const;
private:
  struct Axis {
    std::optional<PassiveSpeedCapacity> capacity;
    std::array<PassiveSpeedLimitRecord,MaximumBadHistory> bad{};
    std::size_t badCount=0;
    std::optional<PassiveSpeedLimitRecord> lastBad,bestGood;
    unsigned countdown=0;
  };
  std::expected<void,PassiveSpeedError> updateAfterPing();
  static std::expected<void,PassiveSpeedError> updateAxis(Axis&,const std::optional<PassiveSpeedEstimate>&,float,std::int64_t);
  static std::expected<void,PassiveSpeedError> decrease(Axis&,std::int64_t);
  static void sustain(Axis&,std::uint16_t,std::int64_t,bool)noexcept;
  void resetWarmup()noexcept;
  std::expected<void,PassiveSpeedError> gap(PassiveSpeedObservation,PassiveSpeedError)noexcept;
  std::array<PassiveSpeedPoint,MaximumPoints> points_{};
  std::size_t count_=0;
  std::uint16_t lastUpload_=0,lastDownload_=0;
  std::array<std::int32_t,3> recent_{};
  unsigned recentNext_=0,recentCount_=0,contiguous_=0;
  std::optional<std::int64_t> lastTime_;
  std::optional<PassiveSpeedObservation> lastAttempt_;
  PassiveSpeedState state_=PassiveSpeedState::Empty;
  std::uint64_t accepted_=0,dropped_=0,gaps_=0,speedOnlyAccepted_=0;
  Axis up_,down_;
  std::array<std::uint16_t,SustainedSamples> uploadSpeeds_{},downloadSpeeds_{};
  unsigned speedNext_=0,speedCount_=0;
  bool lastEventWasSpeedOnly_=false,restored_=false;
  std::optional<PassiveSpeedEstimate> rawUpload_,rawDownload_;
  std::shared_ptr<const Document> savedSource_;
};
} // namespace biglybtc::core
