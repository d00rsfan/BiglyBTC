#pragma once
#include "owned_transport.hpp"
#include "router.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>

namespace biglybtc::app {
namespace azureus=services::azureus_udp;
enum class AzureusServiceError {
  Disabled,Invalid,NoApplication,Start,Closed,Capacity,Dispatch,Memory,Thread,UnknownRequest,Transport
};
enum class AzureusServiceState { Starting,Running,Closing,Closed,Failed };
enum class AzureusServiceCommand { AllowPeer,Ping,FindNode,FindValue,Cancel };
struct AzureusServiceOptions {
  bool enabled=false;
  QHostAddress bindAddress{QHostAddress::LocalHost};
  std::int32_t network=4;
  std::int8_t version=55;
  bool servePings=false,enableVivaldi=true;
  std::size_t maximumPending=32,maximumPeers=32,maximumCommands=64;
};
struct AzureusServiceResult {
  std::uint64_t request=0,targetRequest=0;
  AzureusServiceCommand command=AzureusServiceCommand::Ping;
  bool terminal=true;
  std::optional<AzureusServiceError> error;
  std::optional<azureus::Failure> transportFailure;
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
};
// Explicit Host-owned development service. create(enabled=false) returns before
// any Qt thread/socket exists. A dedicated Qt event loop exclusively constructs,
// calls and destroys the existing numeric-loopback actor. No bootstrap/public
// peer, routing controller/DB activation, profile IO, observer timer or GUI callback is added.
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
  // An explicit request also allows its supplied same-family loopback peer on
  // this owner. The remote owner must independently allow this endpoint.
  std::expected<std::uint64_t,AzureusServiceError> allowPeer(azureus::Peer);
  std::expected<std::uint64_t,AzureusServiceError> ping(azureus::Peer,
      std::chrono::nanoseconds timeout=std::chrono::seconds(2));
  std::expected<std::uint64_t,AzureusServiceError> findNode(azureus::Peer,
      azureus::find::nodes::NodeId,std::chrono::nanoseconds timeout=std::chrono::seconds(2));
  std::expected<std::uint64_t,AzureusServiceError> findValue(azureus::Peer,
      azureus::lookup::nodes::NodeId,std::int32_t flags,std::int32_t maximumValues,
      std::chrono::nanoseconds timeout=std::chrono::seconds(2));
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
