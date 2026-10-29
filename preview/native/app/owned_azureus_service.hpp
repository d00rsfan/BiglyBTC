#pragma once
#include "owned_transport.hpp"
#include "router.hpp"
#include "azureus_value_versions.hpp"
#include "azureus_contacts.hpp"
#include "../adapters/local_rating.hpp"
#include "../services/azureus_control/lookup.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace biglybtc::app {
namespace azureus=services::azureus_udp;
enum class AzureusServiceError {
  Disabled,Invalid,NoApplication,Start,Closed,Capacity,Dispatch,Memory,Thread,UnknownRequest,Transport,Profile,Readback
};
enum class AzureusServiceState { Starting,Running,Closing,Closed,Failed };
enum class AzureusServiceCommand { AllowPeer,Ping,FindNode,FindValue,Cancel,LookupValue,PublishValue,PublishRating,ReadRatings,IntegrateRouting,ForwardCached,BootstrapSaved,FlushContacts };
struct AzureusServiceOptions {
  bool enabled=false;
  QHostAddress bindAddress{QHostAddress::LocalHost};
  quint16 bindPort=0; // Actual owned bind only; default remains ephemeral.
  bool maintenance=false;
  std::shared_ptr<storage::AzureusContacts> contacts;
  std::int32_t network=4;
  std::int8_t version=55;
  bool servePings=false,enableVivaldi=true;
  std::size_t maximumPending=32,maximumPeers=32,maximumCommands=64;
  // Explicit optional SAME-OFD original version manager; absence keeps all
  // old read-only service behavior. Its network must match this actual actor.
  std::shared_ptr<storage::AzureusValueVersions> valueVersions{};
  // Explicit normal receiving STORE/GET using this actor's real router + DB.
  // Requires valueVersions; direct values only until genuine seeded support.
  bool serveValues=false;
  azureus::RoutingScope routingScope=azureus::RoutingScope::ExactPeers;

};
struct AzureusServiceResult {
  std::uint64_t request=0,targetRequest=0;
  AzureusServiceCommand command=AzureusServiceCommand::Ping;
  bool terminal=true;
  std::optional<AzureusServiceError> error;
  std::optional<azureus::Failure> transportFailure;
};
using AzureusPublicationFlags=services::azureus_db_runtime::LocalPublicationFlags;
enum class AzureusPublishState {Local,Token,Store,Readback,Complete,Failed,Cancelled,Closed};
struct AzureusPublishSnapshot {
  std::uint64_t request=0;
  AzureusPublishState state=AzureusPublishState::Local;
  services::azureus_nodes::NodeId key{};
  std::optional<services::azureus_db_value::DbValue> localValue;
  std::vector<services::azureus_values::Value> readback;
  std::optional<AzureusServiceError> error;
  std::optional<azureus::Failure> transportFailure;
  std::optional<storage::Error> profileFailure;
};
enum class AzureusRatingReadState {Reading,Complete,TimedOut,Cancelled,Closed,Failed};
struct AzureusRatingReadSnapshot {
  std::uint64_t request=0;
  AzureusRatingReadState state=AzureusRatingReadState::Reading;
  std::optional<core::TorrentHash> hash;
  std::array<std::uint8_t,32> sourceRevision{};
  services::azureus_nodes::NodeId key{};
  plugins::rating::RatingResults ratings;
  std::size_t receivedValues=0,decodeErrors=0,ignoredMalformedValues=0;
  // Explicit single owned peer sample, never a complete global rating.
  std::size_t maximumValues=64,originalMaximumValues=256;
  bool wholeNetwork=false,possiblyPartial=true;
  // Last worker-validated source only. No IO is performed by observation().
  // Current values are withdrawn when a subsequent worker fence sees a change;
  // retained old snapshot pointers remain historical receipts, not authority.
  bool current=false,localFallback=false;
  std::optional<AzureusServiceError> error;
  std::optional<azureus::Failure> transportFailure;
  std::optional<storage::Error> profileFailure;
};
struct AzureusMaintenanceObservation {
  services::azureus_udp::MaintenanceFacts routing;
  std::size_t savedRecords=0;
  bool cacheWritable=false,ioPending=false;
  std::uint64_t exports=0;
  std::optional<storage::Error> error;
};
struct AzureusServiceObservation {
  std::uint64_t sequence=0;
  AzureusServiceState state=AzureusServiceState::Starting;
  std::shared_ptr<const azureus::OwnedDhtStatisticsSnapshot> snapshot;
  // False for Closing/Closed/Failed or a failed capture. Retained facts are
  // explicitly last observed, never live or a fabricated zero.
  bool current=false,joined=false;
  std::optional<AzureusServiceError> error;
  std::optional<azureus::Failure> transportFailure;
  std::optional<AzureusServiceResult> lastResult;
  std::size_t outstandingCommands=0;
  // Optional actual router state, including its local contact. current/joined
  // above also qualify this snapshot. Failure does not fail the UDP service.
  std::shared_ptr<const services::azureus_router::RouterStatisticsSnapshot> routerSnapshot;
  std::optional<services::azureus_router::RouterError> routerFailure;
  std::shared_ptr<const services::azureus_control::LookupSnapshot> lookupSnapshot;
  std::optional<AzureusServiceError> lookupFailure;
  std::shared_ptr<const AzureusPublishSnapshot> publishSnapshot;
  std::shared_ptr<const services::azureus_db_runtime::Snapshot> databaseSnapshot;
  std::shared_ptr<const AzureusRatingReadSnapshot> ratingReadSnapshot;
  std::shared_ptr<const azureus::RoutingSnapshot> routingSnapshot;
  std::optional<AzureusMaintenanceObservation> maintenance;
  // Passive bounded IO progress only; no source/packet permission is exposed.
  std::size_t profileIoPending=0,profileIoRunning=0,profileIoReady=0,profileIoHighWater=0;
};
// Explicit Host-owned development service. create(enabled=false) returns before
// any Qt thread/socket exists. A dedicated Qt event loop exclusively constructs,
// calls and destroys the existing numeric-loopback actor. No bootstrap/public
// peer, full DHT controller/DB activation, observer timer or GUI callback is added.
// Optional explicit local publication performs only version reservations on its
// held profile. Explicit serveValues uses the actual same router/local DB;
// generated receiving DB facts never become normal routing authority.
// The optional bounded router records explicit known contacts and qualified
// replies/timeouts only. Its Ping/NodeAdded intents are never executed here.
class OwnedAzureusService final {
public:
  static std::expected<std::unique_ptr<OwnedAzureusService>,AzureusServiceError>
      create(AzureusServiceOptions={});
  ~OwnedAzureusService();
  OwnedAzureusService(const OwnedAzureusService&)=delete;
  OwnedAzureusService& operator=(const OwnedAzureusService&)=delete;
  azureus::Peer endpoint()const;
  // Thread-safe passive bounded mailbox copy. Never posts, reads or starts work.
  AzureusServiceObservation observation()const;
  // These return bounded queue admission IDs, not reply/transaction success.
  // Direct single-RPC requests also allow their supplied same-family loopback
  // peer on this owner. Recursive GET requires prior allowPeer admission.
  // The remote owner must independently allow this endpoint.
  std::expected<std::uint64_t,AzureusServiceError> allowPeer(azureus::Peer);
  std::expected<std::uint64_t,AzureusServiceError> ping(azureus::Peer,
      std::chrono::nanoseconds timeout=std::chrono::seconds(2));
  std::expected<std::uint64_t,AzureusServiceError> findNode(azureus::Peer,
      azureus::find::nodes::NodeId,std::chrono::nanoseconds timeout=std::chrono::seconds(2));
  std::expected<std::uint64_t,AzureusServiceError> findValue(azureus::Peer,
      azureus::lookup::nodes::NodeId,std::int32_t flags,std::int32_t maximumValues,
      std::chrono::nanoseconds timeout=std::chrono::seconds(2));
  // Explicit bounded iterative GET; seed and every discovered child must have
  // succeeded through allowPeer(). Returned contacts do not grant admission.
  // One active lookup; observation() and Statistics never start/refresh it.
  std::expected<std::uint64_t,AzureusServiceError> lookupValue(azureus::Peer seed,
      azureus::lookup::nodes::NodeId target,std::int32_t flags=0,std::int32_t maximumValues=32,
      std::chrono::nanoseconds timeout=std::chrono::seconds(5));
  // One ordinary nonempty value <=512B. SHA1 hashes the unencoded key <=1024B;
  // the peer MUST have already succeeded through allowPeer(). The command
  // creates the local DB value then uses real FIND_NODE token/STORE/GET on the
  // same endpoint. Success requires matching actual readback, never just ACK.
  std::expected<std::uint64_t,AzureusServiceError> publishValue(azureus::Peer,
      std::string applicationKey,std::string payload,
      std::chrono::nanoseconds timeout=std::chrono::seconds(5));
  std::expected<std::uint64_t,AzureusServiceError> publishValue(azureus::Peer,
      std::string applicationKey,std::string payload,std::chrono::nanoseconds timeout,
      AzureusPublicationFlags);
  // Explicit single already-admitted peer, held-profile Public network0 only.
  // The source revision must come from readDistributedRating on this SAME OFD.
  // Publish uses saved RatingData + original ANON0x10, <=512B; no queue/timer.
  // Read preserves actual partial/timeout status, max64 values (original256).
  std::expected<std::uint64_t,AzureusServiceError> publishSavedRating(azureus::Peer,
      core::TorrentHash,std::array<std::uint8_t,32> sourceRevision,
      std::chrono::nanoseconds timeout=std::chrono::seconds(5));
  std::expected<std::uint64_t,AzureusServiceError> readSavedRatings(azureus::Peer,
      core::TorrentHash,std::array<std::uint8_t,32> sourceRevision,
      std::chrono::nanoseconds timeout=std::chrono::seconds(20));
  // Opt-in owned-loopback normal service only. One explicit self-ID NODE
  // integration; response contacts need no individual allowPeer grant.
  std::expected<std::uint64_t,AzureusServiceError> integrateRouting(azureus::Peer seed,
      std::chrono::nanoseconds timeout=std::chrono::seconds(10));
  // Read current original-format contacts as Known, then perform genuine
  // self integration; cached duration never restores Alive/seeded/closest.
  std::expected<std::uint64_t,AzureusServiceError> bootstrapSavedContacts(std::chrono::nanoseconds timeout=std::chrono::seconds(5));
  // Snapshot current actor routing under a source permit and durably export
  // on the profile worker. Explicit acknowledged flush precedes controlled close.
  std::expected<std::uint64_t,AzureusServiceError> flushContacts(std::chrono::nanoseconds timeout=std::chrono::seconds(5));

  // Select exactly one existing nonlocal value at SHA1(applicationKey), keep
  // its original origin/version/flags, and prove actual final remote readback.
  // No local version reservation, automatic republish or supplied value facts.
  std::expected<std::uint64_t,AzureusServiceError> forwardCachedValue(azureus::Peer,
      std::string applicationKey,std::chrono::nanoseconds timeout=std::chrono::seconds(5));
  std::expected<std::uint64_t,AzureusServiceError> cancel(std::uint64_t request);
  // Seals admission and joins even with queued/active work; idempotent. No forced
  // termination or detached actor. Call/destruction must not race object lifetime.
  std::expected<void,AzureusServiceError> close();
private:
  struct Impl;
  struct Command;
  explicit OwnedAzureusService(std::unique_ptr<Impl>);
  std::expected<std::uint64_t,AzureusServiceError> submit(Command);
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::app
