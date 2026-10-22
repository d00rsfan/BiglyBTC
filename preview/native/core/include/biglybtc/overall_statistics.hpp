#pragma once

#include "biglybtc/saved_statistics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace biglybtc::core {

// OverallStatsImpl at cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
// Explicit inputs only: no engine, IO, timer, callback or clock is owned here.
// Integers deliberately implement Java signed-long wrap/narrow semantics.
using OverallValues = std::array<std::int64_t, SavedStatisticKeys.size()>;
struct OverallPeerCounters {
  std::int64_t dataReceived = 0;
  std::int64_t protocolReceived = 0;
  std::int64_t dataSent = 0;
  std::int64_t protocolSent = 0;
  bool operator==(const OverallPeerCounters&) const = default;
};
struct OverallTransportCounters {
  std::int64_t sent = 0;
  std::int64_t received = 0;
  bool operator==(const OverallTransportCounters&) const = default;
};
enum class OverallDhtResolution { Retry, Empty, Ready };
struct OverallDhtInput {
  OverallDhtResolution resolution = OverallDhtResolution::Retry;
  // Latest cells for the original cached transport indices. nullopt means the
  // actual transport could not be sampled. Borrowed only during update().
  std::span<const std::optional<OverallTransportCounters>> transports{};
};
enum class OverallError { InvalidInput, TransportUnavailable };
enum class OverallUpdateDisposition { ClockBackwards, ClockGap, Accepted };
struct OverallUpdate {
  OverallUpdateDisposition disposition;
  // A source-compatible save intention, never a filesystem-success receipt.
  std::optional<OverallValues> save;
};
enum class OverallSince { Downloaded, Uploaded, Uptime, AverageDownload, AverageUpload };
enum class OverallScalarKind { Long, Integer };
struct OverallScalar {
  OverallScalarKind kind;
  std::int64_t value;
  bool operator==(const OverallScalar&) const = default;
};
struct OverallObservation {
  std::int64_t downloaded;
  std::int64_t uploaded;
  std::int64_t uptime;
  std::int64_t markTime;
  std::int32_t averageDownload;
  std::int32_t averageUpload;
  std::array<std::int64_t, 12> snapshot;
  bool operator==(const OverallObservation&) const = default;
};
inline constexpr std::array<std::string_view, 4> OverallCoreKeys{
    "xfer.upload.protocol.bytes.total", "xfer.upload.data.bytes.total",
    "xfer.download.protocol.bytes.total", "xfer.download.data.bytes.total"};

class OverallStatistics final {
public:
  static constexpr std::size_t MaximumDhtTransports = 16;
  // The two supplied constructor reads remain distinct. Delta baselines start
  // at Java zero, even though initialPeer already appears in initial snapshot.
  OverallStatistics(OverallValues loaded, OverallPeerCounters initialPeer,
      std::int64_t sessionStartMillis, std::int64_t loadMillis) noexcept;

  // InvalidInput is validation-first/no mutation. TransportUnavailable follows
  // the original partial-update order: peer totals/baselines and a discovered
  // transport count may already change; DHT/time/snapshot/tick/save do not.
  // Caller failures after consuming save do not roll back this reducer.
  std::expected<OverallUpdate, OverallError> update(OverallPeerCounters peer,
      OverallDhtInput dht, std::int64_t nowMillis, bool force) noexcept;
  void setMark(std::int64_t nowMillis) noexcept;
  void clearMark() noexcept;
  std::expected<OverallScalar, OverallError> querySince(OverallSince query) noexcept;
  std::int64_t sessionUptime(std::int64_t nowMillis) const noexcept;
  OverallObservation observe() const noexcept;
  std::optional<std::array<std::int64_t, 4>> coreStatistics(
      OverallPeerCounters peer, bool started) const noexcept;
  const OverallValues& values() const noexcept { return values_; }

private:
  std::int64_t since(SavedStatistic total, SavedStatistic baseline) noexcept;
  OverallValues values_{};
  OverallPeerCounters lastPeer_{};
  std::int64_t lastDownloaded_ = 0;
  std::int64_t lastUploaded_ = 0;
  OverallTransportCounters lastDht_{};
  std::int64_t lastUptime_ = 0;
  std::int64_t sessionStart_ = 0;
  std::optional<std::size_t> cachedDhtCount_{};
  std::array<std::int64_t, 12> snapshot_{};
  std::uint32_t acceptedTicks_ = 0;
};

} // namespace biglybtc::core
