#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>

namespace biglybtc::engine::detail {
enum class DiskIoAxis { Read, Write };
struct DiskLatencySnapshot {
  std::int64_t observedMillis=0;
  // Original DMReader/Writer: max(last successful completion duration,
  // outstanding request age), including executor queue wait. Not an average.
  std::optional<std::int64_t> readMillis{},writeMillis{};
  std::size_t pendingReads=0,pendingWrites=0;
  std::uint64_t completedReads=0,completedWrites=0;
  bool measurementLost=false;
};
// One authenticated admission. No I/O, owner retention, callback, clock thread
// or timer. Network admission, worker completion and owner observation share
// steady_clock. Optional measurement failure cannot change the disk result.
class DiskLatency final {
public:
  static constexpr std::size_t MaximumPending=65536;
  static std::int64_t nowMillis() noexcept;
  std::uint64_t begin(DiskIoAxis) noexcept;
  void finish(std::uint64_t,bool success) noexcept;
  DiskLatencySnapshot snapshot(std::int64_t observedMillis) const noexcept;
private:
  struct Request {DiskIoAxis axis;std::int64_t started;};
  mutable std::mutex mutex_;
  std::map<std::uint64_t,Request> pending_;
  std::uint64_t issued_=0,reads_=0,writes_=0;
  std::int64_t readMillis_=0,writeMillis_=0,lastMutation_=0;
  mutable std::atomic_bool lost_=false;
};
} // namespace biglybtc::engine::detail
