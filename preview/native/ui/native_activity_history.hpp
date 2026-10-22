#pragma once
#include "native_activity_presentation.hpp"
#include "native_session_presentation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace biglybtc::ui {
struct NativeActivityPoint {
  std::int64_t elapsedNanoseconds = 0;
  core::EngineRates rates{};
  bool connectedToPrevious = false;
  core::SessionRateLimits rateLimits{}; // Compact two-limit value; never full metrics per point.
  bool operator==(const NativeActivityPoint&) const = default;
};
class NativeActivitySnapshot final {
public:
  const NativeSessionPresentation& latest() const noexcept { return latest_; }
  std::span<const NativeActivityPoint> points() const noexcept { return points_; }
private:
  friend class NativeActivityHistory;
  NativeActivitySnapshot(NativeSessionPresentation, std::vector<NativeActivityPoint>);
  NativeSessionPresentation latest_;
  std::vector<NativeActivityPoint> points_;
};

// GUI-thread confined. Root retains this owner at Window scope, independently
// of Statistics widget lifetime and all operation-scoped history owners.
class NativeActivityHistory final {
public:
  static constexpr std::size_t MaximumPoints = 600;
  bool observe(NativeSessionPresentation, quint64 expectedSession);
  NativeActivityPresentation snapshot() const;
  void clear() noexcept;
private:
  NativeSessionPresentation latest_;
  std::array<NativeActivityPoint, MaximumPoints> points_{};
  std::size_t begin_ = 0, size_ = 0;
  bool connectable_ = false;
};
} // namespace biglybtc::ui
