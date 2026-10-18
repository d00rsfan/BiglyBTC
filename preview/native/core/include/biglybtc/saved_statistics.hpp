#pragma once

#include "biglybtc/bencode.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string_view>

namespace biglybtc::core {

// OverallStatsImpl, BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
// Counters are bytes, uptime/baseline uptime are seconds, MarkTime is epoch ms.
// These are last-saved facts, not current rates, session counters or history.
enum class SavedStatistic {
  Downloaded, Uploaded, Uptime, MarkTime, MarkDownloaded, MarkUploaded, MarkUptime,
  DhtDownloaded, DhtUploaded, ProtocolUploaded, DataUploaded, ProtocolDownloaded,
  DataDownloaded
};
inline constexpr std::array<std::string_view, 13> SavedStatisticKeys{
    "downloaded", "uploaded", "uptime", "mark_time", "mark_downloaded",
    "mark_uploaded", "mark_uptime", "dht_down", "dht_up", "p_uploaded",
    "d_uploaded", "p_downloaded", "d_downloaded"};
struct SavedStatisticValue {
  std::optional<std::int64_t> value{};
  bool invalidType = false;
};
struct SavedStatistics {
  std::shared_ptr<const Document> source;
  bool hasAll = false;
  std::array<SavedStatisticValue, 13> values{};
  // Invalid enum values are rejected by std::array::at, never unchecked indexing.
  const SavedStatisticValue& get(SavedStatistic field) const;
};
enum class SavedStatisticsError { InvalidDocument, InvalidAll, Limit, Memory };

// Copies only after the 4 MiB input check, then parses an independently owned
// immutable document with the ordinary 100000-node/depth64 limits. Root must be
// a dictionary. Missing all succeeds with hasAll=false; wrong-shaped all fails.
// Missing scalar fields stay absent; nonintegers set invalidType, with no Java
// default-zero fabrication. Every signed integer and unknown source byte survives.
// Duplicate dictionary keys are rejected by the unchanged Document parser.
// No caller tree, IO, clock, recovery rewrite or manager initialization is used.
std::expected<SavedStatistics, SavedStatisticsError> readSavedStatistics(
    std::string_view bytes);

struct SavedSinceMark {
  std::int64_t value;
  bool clamped = false;
};
// Original upper-baseline clamp over admitted nonnegative operands, without
// mutating raw facts. Missing/negative operands are unknown; mark time is not an
// operand. Arithmetic cannot overflow within this admitted domain.
std::optional<SavedSinceMark> deriveSavedSinceMark(
    std::optional<std::int64_t> total, std::optional<std::int64_t> baseline);

} // namespace biglybtc::core
