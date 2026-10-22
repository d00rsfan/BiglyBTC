#pragma once

#include "biglybtc/overall_statistics.hpp"
#include "overall_statistics_view.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <stop_token>

namespace biglybtc::app {
struct OverallOwnerIdentity {
  std::uint64_t applicationRun = 0;
  std::uint64_t profileSession = 0;
  std::uint64_t engineEpoch = 0;
  bool operator==(const OverallOwnerIdentity&) const = default;
};
enum class OverallSampleQuality { Observed, SettledCheckpoint, ClosedFinal };
struct OverallOwnerSample {
  OverallOwnerIdentity identity;
  std::uint64_t sequence = 0;
  core::OverallPeerCounters absolute;
  // ClosedFinal is supplied ONLY from the real worker close receipt after
  // joined+storageReleased+peerCounters, never from a GUI Closed packet.
  OverallSampleQuality quality = OverallSampleQuality::Observed;
  bool operator==(const OverallOwnerSample&) const = default;
};
enum class OverallServiceError {
  WrongThread, InvalidInput, NoObservation, StaleSample, CounterRegression,
  Uncertified, Closed, WriteBlocked, Domain, Storage, Memory, Cancelled
};
enum class OverallFlushDisposition { NothingPending, Unchanged, Committed };

// Qt-free, worker-affine service for ONE actual persistent owner epoch. Construct
// only after that owner succeeds; offline/unqualified fallback remains read-only.
// The borrowed held lease must outlive this service. Destructor does no IO.
class OverallStatisticsService final {
public:
  static std::expected<std::unique_ptr<OverallStatisticsService>, storage::Error> open(
      const storage::ProfileLease&, OverallOwnerIdentity,
      std::int64_t sessionStartMillis, std::int64_t loadMillis,
      std::stop_token = {});
  ~OverallStatisticsService();
  OverallStatisticsService(const OverallStatisticsService&) = delete;
  OverallStatisticsService& operator=(const OverallStatisticsService&) = delete;

  // No allocation, callback or IO: retain essential actual primitives before
  // any optional UI packet allocation. Epoch transitions are rejected.
  std::expected<void, OverallServiceError> capture(OverallOwnerSample) noexcept;
  std::expected<void, OverallServiceError> invalidate(OverallOwnerIdentity) noexcept;
  // Explicit Host wall clock and actual DHT state. Native isolatedSettings DHT
  // is disabled, so Host may explicitly supply Empty; unavailable is not zero.
  // Sample capture itself does not advance the original periodic tick cadence.
  // Tick stages save intentions only; Host calls flush after a due/forced tick.
  std::expected<core::OverallUpdate, OverallServiceError> tick(
      std::int64_t nowMillis, core::OverallDhtInput, bool force);
  std::expected<OverallFlushDisposition, OverallServiceError> flush(std::stop_token = {});
  // Mark copies current reducer values; caller may force tick immediately
  // before mark. These commands save once immediately, with no intermediate IO.
  std::expected<OverallFlushDisposition, OverallServiceError> setMark(
      std::int64_t nowMillis, std::stop_token = {});
  std::expected<OverallFlushDisposition, OverallServiceError> clearMark(std::stop_token = {});
  std::expected<core::OverallScalar, OverallServiceError> querySince(core::OverallSince) noexcept;
  // Explicit native persistence policy, distinct from Java's early-returning
  // force tick; no new sample/delta is calculated here.
  std::expected<OverallFlushDisposition, OverallServiceError> saveCurrent(std::stop_token = {});

  const core::OverallValues& values() const noexcept;
  core::OverallObservation observation() const noexcept;
  // Getter-only live peer projection using actual reducer counter baselines.
  // Never feed it back as a new sample or persisted delta.
  core::OverallValues projectedValues() const noexcept;
  const std::optional<OverallOwnerSample>& latestSample() const noexcept;
  const storage::OverallStatisticsView& baseline() const noexcept;
  bool dirty() const noexcept;
  bool writesBlocked() const noexcept;
  bool recoveredReadOnly() const noexcept;
  bool uncertified() const noexcept;
  bool finalCountersApplied() const noexcept;
  // Exact original IO error retained after first failure. No automatic retry,
  // reconcile, rebase or inferred durability success is provided.
  const storage::Error* lastStorageError() const noexcept;
  const core::OverallDocument* pendingDocument() const noexcept;
  const core::OverallValues* pendingValues() const noexcept;

private:
  struct Impl;
  explicit OverallStatisticsService(std::unique_ptr<Impl>) noexcept;
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::app
