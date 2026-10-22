#pragma once
#include "biglybtc/overall_statistics.hpp"

#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace biglybtc::ui {

struct OverallTransferSummary {
  std::int32_t averageDownload = 0, averageUpload = 0;
  std::int64_t sinceMarkAverageDownload = 0, sinceMarkAverageUpload = 0;
  std::int64_t shareRatioThousandths = 0, sinceMarkShareRatioThousandths = 0;
  bool operator==(const OverallTransferSummary&) const = default;
};

// TransferStatsView.refreshGeneral at original cb8c6c: Java long multiply
// wraps before division, zero download uses denominator 1, and MIN/-1 wraps.
// Known zero download is therefore finite, never infinity or unavailable.
inline std::int64_t overallShareRatio(std::int64_t uploaded, std::int64_t downloaded) noexcept {
  const auto product = std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(uploaded) * std::uint64_t{1000});
  const auto denominator = downloaded == 0 ? std::int64_t{1} : downloaded;
  if (product == std::numeric_limits<std::int64_t>::min() && denominator == -1) { return product; }
  return product / denominator;
}

inline std::string overallShareRatioText(std::int64_t thousandths) {
  // Preserve the original signed remainder padding, including wrapped-negative
  // quirks (e.g. raw -1 becomes "0.0-1"); do not silently take absolute values.
  auto remainder = std::to_string(thousandths % 1000);
  if (remainder.size() < 3) { remainder.insert(0, 3 - remainder.size(), '0'); }
  return std::to_string(thousandths / 1000) + '.' + remainder;
}

// Pure, no Qt/clock/IO. The packet's projected immutable totals and stored
// uptime/marks are the only inputs. Reuse the qualified source queries on a
// private value copy: their upper-mark clamps never mutate the supplied values.
// Constructor zero clocks initialize unused reducer state; no update/session
// uptime query is called and no ongoing application uptime is invented.
inline std::optional<OverallTransferSummary> overallTransferSummary(const core::OverallValues& values) noexcept {
  core::OverallStatistics arithmetic(values, {}, 0, 0);
  const auto totals = arithmetic.observe();
  const auto downloaded = arithmetic.querySince(core::OverallSince::Downloaded);
  const auto uploaded = arithmetic.querySince(core::OverallSince::Uploaded);
  const auto averageDownload = arithmetic.querySince(core::OverallSince::AverageDownload);
  const auto averageUpload = arithmetic.querySince(core::OverallSince::AverageUpload);
  if (!downloaded || !uploaded || !averageDownload || !averageUpload) { return {}; }
  return OverallTransferSummary{totals.averageDownload, totals.averageUpload,
      averageDownload->value, averageUpload->value,
      overallShareRatio(values[static_cast<std::size_t>(core::SavedStatistic::Uploaded)],
          values[static_cast<std::size_t>(core::SavedStatistic::Downloaded)]),
      overallShareRatio(uploaded->value, downloaded->value)};
}

} // namespace biglybtc::ui
