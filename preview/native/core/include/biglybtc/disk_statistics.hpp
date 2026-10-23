#pragma once
#include "biglybtc/runtime_metrics.hpp"
#include <array>
#include <expected>
#include <optional>

namespace biglybtc::core {
enum class DiskStatistic:std::size_t {
  QueuedJobs,RunningJobs,FencedJobs,Buffers,OpenFiles,QueuedWriteBytes,
  ReadOperations,WriteOperations,ReadBlocks,WriteBlocks,FilePoolHits,FilePoolMisses,Count
};
inline constexpr std::size_t DiskStatisticCount=static_cast<std::size_t>(DiskStatistic::Count);
struct DiskStatistics {
  std::uint64_t sequence=0;
  std::int64_t nanoseconds=0;
  std::array<std::optional<std::int64_t>,DiskStatisticCount> values{};
  std::optional<std::int64_t> readOperationsPerSecond,writeOperationsPerSecond;
  bool fresh=false,connectedToPrevious=false;
  std::optional<std::int64_t> get(DiskStatistic field)const noexcept {
    const auto index=static_cast<std::size_t>(field);return index<values.size()?values[index]:std::nullopt;
  }
};
enum class DiskStatisticsError {Invalid,Stale};
// Pure selected-metric admission + exact floor(delta*1e9/elapsed). No bytes are
// inferred from block counts. Missing/negative selected facts stay unknown.
class DiskStatisticsReducer final {
public:
  std::expected<DiskStatistics,DiskStatisticsError> observe(const RuntimeMetrics&) noexcept;
  void gap()noexcept{gap_=true;}
private:std::optional<DiskStatistics> previous_;bool gap_=false;
};
inline constexpr std::size_t MaximumDiskActivityPoints=600;
struct DiskActivityPoint {
  std::uint64_t sequence=0;std::int64_t nanoseconds=0;
  std::optional<std::int64_t> readOperationsPerSecond,writeOperationsPerSecond;
  bool connectedToPrevious=false;
};
struct DiskActivityFrame {
  std::size_t count=0;
  std::array<DiskActivityPoint,MaximumDiskActivityPoints> points{};
};
class DiskActivityHistory final {
public:
  void observe(const DiskStatistics&)noexcept;
  void gap()noexcept{gap_=true;}
  DiskActivityFrame snapshot()const noexcept;
private:
  std::array<DiskActivityPoint,MaximumDiskActivityPoints> points_{};
  std::size_t count_=0,start_=0;bool gap_=true;
};
} // namespace biglybtc::core
