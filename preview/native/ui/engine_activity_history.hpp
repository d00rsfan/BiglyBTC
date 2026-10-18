#pragma once

#include "engine_activity_presentation.hpp"
#include "engine_statistics_presentation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace biglybtc::ui {

struct EngineActivityPoint {
  std::int64_t elapsedNanoseconds = 0;
  core::EngineRates rates{};
  bool connectedToPrevious = false;
  bool operator==(const EngineActivityPoint&) const = default;
};

// Owned immutable view of actual accepted rate samples, never inferred zeros.
// A snapshot remains valid after its History is changed, cleared or destroyed.
class EngineActivitySnapshot final {
public:
  const EngineStatisticsPresentation& latest() const noexcept { return latest_; }
  std::span<const EngineActivityPoint> points() const noexcept { return points_; }
private:
  friend class EngineActivityHistory;
  EngineActivitySnapshot(EngineStatisticsPresentation,
      std::vector<EngineActivityPoint>);
  EngineStatisticsPresentation latest_;
  std::vector<EngineActivityPoint> points_;
};

// Owner-confined history for one session and its increasing operation IDs.
// A different session cannot replace a nonempty history: the caller clears it.
class EngineActivityHistory final {
public:
  static constexpr std::size_t MaximumPoints = 600;
  // Reuses packet admission before mutation. Missing terminal samples retain
  // only this operation's actual last sample; they add no rate point.
  // Allocation during terminal normalization may throw before any mutation.
  bool observe(EngineStatisticsPresentation, quint64 expectedSession);
  // Copies at most MaximumPoints; allocation failure leaves history unchanged.
  EngineActivityPresentation snapshot() const;
  void clear() noexcept;
private:
  EngineStatisticsPresentation latest_;
  std::array<EngineActivityPoint, MaximumPoints> points_{};
  std::size_t begin_ = 0;
  std::size_t size_ = 0;
};

} // namespace biglybtc::ui
