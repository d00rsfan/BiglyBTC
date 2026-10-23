// SPDX-License-Identifier: GPL-2.0-or-later
// Source-defined saved SpeedManager ping records; no live mapper/service.
#pragma once
#include "biglybtc/bencode.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace biglybtc::core {
inline constexpr std::size_t SavedSpeedMapMaximumRecords=1200;
enum class SavedSpeedMapMode {Variance,Rtt};
enum class SavedSpeedMapPresence {Missing,Malformed,Present};
enum class SavedSpeedMapError {Format,Root,Limit,Memory,Mode,EditedDocument};
struct SavedSpeedMapPoint {
  std::uint16_t x=0,y=0,metric=0; // Source unsigned short grid/metric getters.
  std::uint32_t uploadBytesPerSecond=0,downloadBytesPerSecond=0;
  bool metricClamped=false;
  std::optional<double> varianceRating;
};
struct SavedSpeedMapRecord {
  std::size_t sourceIndex=0;
  std::optional<std::int64_t> rawX,rawY,rawMetric;
  std::optional<SavedSpeedMapPoint> point;
  bool malformed=false;
};
struct SavedSpeedMapZone {
  std::size_t firstRecord=0,secondRecord=0;
  std::uint32_t uploadStartBytesPerSecond=0,uploadEndBytesPerSecond=0;
  std::uint32_t downloadStartBytesPerSecond=0,downloadEndBytesPerSecond=0;
  std::uint16_t metric=0;
  // Source compares signed shorts, then exposes unsigned endpoints. Preserve
  // those unusual reversed extents instead of silently changing their order.
  bool reversedUpload=false,reversedDownload=false;
};
struct SavedSpeedMap {
  std::shared_ptr<const Document> source;
  SavedSpeedMapMode mode=SavedSpeedMapMode::Variance;
  SavedSpeedMapPresence presence=SavedSpeedMapPresence::Missing;
  std::vector<SavedSpeedMapRecord> records;
  std::vector<SavedSpeedMapZone> zones;
  std::size_t malformedRecords=0;
  std::optional<std::uint32_t> maxUploadBytesPerSecond,maxDownloadBytesPerSecond;
  std::optional<std::uint16_t> maxMetric;
  std::optional<double> currentMetricRating;
};
// The owned overload reuses SavedSpeedHistory.source after bounded pristine/raw
// span validation. The byte overload parses a fresh bounded owned Document.
// All unknown subtrees remain owned/untouched. No IO, mutation or service.
// The saved default pm_<AS>.dat mapper is Variance, not raw RTT milliseconds.
// Valid fields: Java Long.intValue(), metric upper-clamp1500/30000, then short
// narrowing and unsigned getters; speeds multiply grid coordinates by256.
// Native malformed slots remain explicit gaps; zones never bridge them. The
// Java loader instead catches an exception after retaining its valid prefix.
// Native lists above the default writer's1200-entry bound fail atomically;
// Java's larger-input rolling eviction/recovery is outside this projection.
std::expected<SavedSpeedMap,SavedSpeedMapError> readSavedSpeedMap(
    std::string_view bytes,SavedSpeedMapMode=SavedSpeedMapMode::Variance);
std::expected<SavedSpeedMap,SavedSpeedMapError> readSavedSpeedMap(
    std::shared_ptr<const Document> source,SavedSpeedMapMode=SavedSpeedMapMode::Variance);
} // namespace biglybtc::core
