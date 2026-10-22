#pragma once
#include "biglybtc/runtime_metrics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace biglybtc::ui {

enum class RuntimeServiceMetric : std::size_t {
  QueuedDiskJobs, RunningDiskJobs, FencedDiskJobs, DiskBuffers, OpenFiles,
  ReadOperations, WriteOperations, QueuedHttpAnnounces, TrackerReceived, TrackerSent,
  ConnectedPeers, ConnectingPeers, AllowedUploadSlots, UnchokedUploadPeers, OptimisticUploadPeers,
  UploadRequestPeers, DownloadRequestPeers, UploadBandwidthQueue, DownloadBandwidthQueue,
  Count
};
inline constexpr std::size_t RuntimeServiceMetricCount = static_cast<std::size_t>(RuntimeServiceMetric::Count);
struct RuntimeServiceSummary {
  std::array<std::optional<std::int64_t>, RuntimeServiceMetricCount> values{};
  std::optional<std::int64_t> get(RuntimeServiceMetric field) const noexcept {
    const auto index = static_cast<std::size_t>(field);
    return index < values.size() ? values[index] : std::nullopt;
  }
  bool operator==(const RuntimeServiceSummary&) const = default;
};

// Value-only projection of one already accepted engine response. No snapshot
// alias, arithmetic, allocation, IO or fallback counter is retained. Raw signed
// values remain raw; missing, mismatched or duplicate selected facts are unknown.
// Peer-request facts count peers, not individual requests. Bandwidth queue
// facts count waiting sockets, not bytes or capacity. Allowed upload slots are
// the reported dynamic gauge, not the configured fixed-slot setting.
// Tracker byte counters are engine reports including estimated protocol costs,
// not exact wire accounting. The tracker gauge covers queued HTTP announces.
inline RuntimeServiceSummary projectRuntimeServiceSummary(const core::RuntimeMetrics* snapshot) noexcept {
  RuntimeServiceSummary result;
  if (!snapshot || snapshot->metrics.size() > core::MaximumRuntimeMetrics) { return result; }
  struct Definition { std::string_view name; core::RuntimeMetricType type; core::RuntimeMetricGroup group; };
  using Type = core::RuntimeMetricType; using Group = core::RuntimeMetricGroup;
  constexpr std::array<Definition, RuntimeServiceMetricCount> definitions{{
      {"disk.queued_disk_jobs",Type::Gauge,Group::Disk},
      {"disk.num_running_disk_jobs",Type::Gauge,Group::Disk},
      {"disk.blocked_disk_jobs",Type::Gauge,Group::Disk},
      {"disk.disk_blocks_in_use",Type::Gauge,Group::Disk},
      {"disk.file_pool_size",Type::Gauge,Group::Disk},
      {"disk.num_read_ops",Type::Counter,Group::Disk},
      {"disk.num_write_ops",Type::Counter,Group::Disk},
      {"tracker.num_queued_tracker_announces",Type::Gauge,Group::Tracker},
      {"net.recv_tracker_bytes",Type::Counter,Group::Net},
      {"net.sent_tracker_bytes",Type::Counter,Group::Net},
      {"peer.num_peers_connected",Type::Gauge,Group::Peer},
      {"peer.num_peers_half_open",Type::Gauge,Group::Peer},
      {"ses.num_unchoke_slots",Type::Gauge,Group::Other},
      {"peer.num_peers_up_unchoked_all",Type::Gauge,Group::Peer},
      {"peer.num_peers_up_unchoked_optimistic",Type::Gauge,Group::Peer},
      {"peer.num_peers_up_requests",Type::Gauge,Group::Peer},
      {"peer.num_peers_down_requests",Type::Gauge,Group::Peer},
      {"net.limiter_up_queue",Type::Gauge,Group::Net},
      {"net.limiter_down_queue",Type::Gauge,Group::Net}}};
  std::array<bool, RuntimeServiceMetricCount> seen{};
  for (const auto& metric : snapshot->metrics) {
    for (std::size_t i = 0; i < definitions.size(); ++i) {
      const auto& expected = definitions[i];
      if (metric.name != expected.name) { continue; }
      if (!seen[i] && metric.type == expected.type && metric.group == expected.group) {
        result.values[i] = metric.value;
      } else { result.values[i].reset(); }
      seen[i] = true;
      break;
    }
  }
  return result;
}

} // namespace biglybtc::ui
