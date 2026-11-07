// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owned_tracker_endpoint.hpp"
#include "biglybtc/saved_services.hpp"
#include <filesystem>
#include <optional>
#include <stop_token>
#include <vector>
namespace biglybtc::sharing {class SavedShareHandle;}
namespace biglybtc::engine {class PersistentSession;}
namespace biglybtc::tracker {
enum class TrackerServiceMode {Preview,Normal};
struct TrackerEndpointReview {
  std::string numericBind;
  std::uint16_t port=0; // Zero only for explicitly reviewed loopback development.
};
struct TrackerRowKey {
  std::uint64_t generation=0;
  std::size_t sourceIndex=0;
  bool operator==(const TrackerRowKey&)const=default;
};
struct TrackerRuntimeStats {
  std::int64_t completed=0,announces=0,scrapes=0,uploaded=0,downloaded=0,bytesIn=0,bytesOut=0;
  std::uint32_t seeds=0,leechers=0;
};
struct TrackerRuntimeRow {
  TrackerRowKey key;
  bool eligible=false,registered=false,serving=false;
  std::string reason;
  std::optional<TrackerRuntimeStats> runtime;
};
struct TrackerListenerObservation {
  bool running=false,joined=true;
  std::optional<TrackerEndpointDescription> endpoint;
  std::uint64_t acceptedConnections=0,completedRequests=0;
};
struct TrackerCatalog {
  std::uint64_t generation=0;
  std::optional<core::SavedHosting> saved;
  std::optional<storage::RecoverySource> sourceKind;
  std::vector<TrackerRuntimeRow> rows;
  TrackerListenerObservation listener;
  std::optional<TrackerEndpointReview> configuredEndpoint;
  bool needsReview=false;
  std::string failure;
};
struct TrackerCloseReceipt {
  bool listenerJoined=false,connectionsJoined=false,leaseReleased=false;
  std::optional<storage::Error> checkpointFailure;
};
// Serialized actual profile worker only; socket worker owns no filesystem or
// source callback authority. open/catalog never bind; no implicit startup.
class EmbeddedTrackerService final {
public:
  static std::expected<std::unique_ptr<EmbeddedTrackerService>,storage::Error> open(
      const storage::ProfileLease&,std::filesystem::path canonicalProfile,
      std::uint64_t session,TrackerServiceMode);
  ~EmbeddedTrackerService();
  std::expected<TrackerCatalog,storage::Error> catalog(
      const storage::ProfileLease&,std::uint64_t);
  // Explicit reviewed HTTP-sharing configuration; no listener or metainfo.
  std::expected<TrackerCatalog,storage::Error> configure(
      const storage::ProfileLease&,std::uint64_t,TrackerEndpointReview,std::stop_token);
  std::expected<OwnedTrackerEndpoint,storage::Error> startListener(
      const storage::ProfileLease&,std::uint64_t,std::stop_token);
  std::expected<OwnedTrackerEndpoint,storage::Error> endpoint(
      const storage::ProfileLease&,std::uint64_t);
  std::expected<TrackerCatalog,storage::Error> stopListener(
      const storage::ProfileLease&,std::uint64_t,std::stop_token);
  // The actual handle supplies metadata/layout/destination. The genuine engine
  // validates its retained SAMEOFD live verified-seed member before registration.
  std::expected<TrackerCatalog,storage::Error> registerShare(
      const storage::ProfileLease&,std::uint64_t,const sharing::SavedShareHandle&,
      engine::PersistentSession&,std::stop_token);
  std::expected<TrackerCatalog,storage::Error> start(
      const storage::ProfileLease&,std::uint64_t,TrackerRowKey,std::stop_token);
  std::expected<TrackerCatalog,storage::Error> stop(
      const storage::ProfileLease&,std::uint64_t,TrackerRowKey,std::stop_token);
  std::expected<TrackerCatalog,storage::Error> remove(
      const storage::ProfileLease&,std::uint64_t,TrackerRowKey,std::stop_token);
  std::expected<void,storage::Error> poll(const storage::ProfileLease&,std::uint64_t);
  // Drops source aliases only after owned listener/connections join. Saved stats
  // checkpoint failure remains auxiliary and never fabricates a successful save.
  TrackerCloseReceipt close()noexcept;
private:
  struct Impl;explicit EmbeddedTrackerService(std::unique_ptr<Impl>)noexcept;
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::tracker
