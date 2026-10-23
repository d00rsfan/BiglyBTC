#pragma once
#include "biglybtc/bencode.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
namespace biglybtc::core {
enum class SavedSpeedError { Format,Root,Limit,Memory };
struct SavedSpeedSettings {
  std::shared_ptr<const Document> source;
  std::optional<std::string> asNumber,asName,bgpPrefix,historyLeaf;
  std::optional<std::int32_t> addressFamily,algorithm;
  std::optional<std::int64_t> rawAlgorithm,rawEnabled,rawSeedingEnabled;
  std::optional<bool> enabled,seedingEnabled;
  bool asDefaulted=false,malformed=false;
};
struct SavedSpeedEstimate {
  bool present=false,malformed=false,typeDefaulted=false;
  std::optional<std::int64_t> rawSpeed,rawHits,whenMillis;
  std::optional<std::int32_t> bytesPerSecond,hits;
  std::optional<std::string> rawMetric,rawType;
  std::optional<float> metric,type;
};
struct SavedSpeedHistory {
  std::shared_ptr<const Document> source;
  SavedSpeedEstimate upload,download;
  std::optional<std::size_t> savedPingRecords;
  bool malformedPings=false;
};
// Normal4MiB/100000nodes/depth64 document limits. Fresh raw ownership; unknown
// subtrees remain untouched. Selected byte strings cap4096 (numeric text128).
// ASN selector is empty->default or ASCII1..20digits only; unsafe/non-normal
// stored IDs remain raw with no file selector, never paths or directory scans.
std::expected<SavedSpeedSettings,SavedSpeedError> readSavedSpeedSettings(std::string_view);
std::expected<SavedSpeedHistory,SavedSpeedError> readSavedSpeedHistory(std::string_view);
} // namespace biglybtc::core
