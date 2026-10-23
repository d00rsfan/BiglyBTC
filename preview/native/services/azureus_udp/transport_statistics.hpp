// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace biglybtc::services::azureus_udp {
struct Statistics {
  std::uint64_t received=0,sent=0,requests=0,replies=0,malformed=0,oversized=0,unowned=0,foreign=0,
      timeouts=0,cancelled=0,abandoned=0,callbackErrors=0,ioErrors=0,readTurns=0,deliveryTurns=0,
      serviceCapacityDrops=0,serviceAddressRejected=0,authorityErrors=0,policyErrors=0;
  // Actual successful socket-write return values / dequeued UDP payload sizes.
  // No IP/UDP overhead or packet-count estimate. Overflow/failed read/discard is
  // sticky unknown; existing packet/outcome counters retain their semantics.
  std::optional<std::uint64_t> sentPayloadBytes{0},receivedPayloadBytes{0};
};
inline void observeSocketPayloadBytes(std::optional<std::uint64_t>& total,std::int64_t actualBytes)noexcept{
  // A failed write/read reports no successful payload observation. An unknown
  // cumulative total cannot become known again from later small observations.
  if(actualBytes<0||!total){return;}
  const auto bytes=static_cast<std::uint64_t>(actualBytes);
  if(bytes>std::numeric_limits<std::uint64_t>::max()-*total){total.reset();}
  else{*total+=bytes;}
}
enum class TransportStatisticsState { NotStarted, Bound, Closed };
enum class TransportStatisticsFamily { Unbound, IPv4, IPv6 };
class OwnedPingTransport;
// A fixed owned pull of ONE existing development transport. This is not a DHT
// router/DB snapshot, an original UI provider or global/session byte accounting.
// Generation is process-local actor identity; publication counts explicit pulls,
// not packet events. Existing unsigned counters retain their legacy semantics.
class TransportStatisticsSnapshot final {
public:
  TransportStatisticsSnapshot(const TransportStatisticsSnapshot&) = default;
  TransportStatisticsSnapshot& operator=(const TransportStatisticsSnapshot&) = default;
  std::uint64_t generation() const noexcept { return generation_; }
  std::uint64_t publication() const noexcept { return publication_; }
  std::int32_t network() const noexcept { return network_; }
  std::int8_t protocolVersion() const noexcept { return protocolVersion_; }
  TransportStatisticsFamily family() const noexcept { return family_; }
  TransportStatisticsState state() const noexcept { return state_; }
  const Statistics& counters() const noexcept { return counters_; }
  std::size_t pending() const noexcept { return pending_; }
  std::size_t completions() const noexcept { return completions_; }
  std::size_t allowedPeers() const noexcept { return allowedPeers_; }
private:
  friend class OwnedPingTransport;
  TransportStatisticsSnapshot(std::uint64_t generation,std::uint64_t publication,
      std::int32_t network,std::int8_t version,TransportStatisticsFamily family,
      TransportStatisticsState state,Statistics counters,std::size_t pending,
      std::size_t completions,std::size_t allowedPeers) noexcept
      :generation_(generation),publication_(publication),network_(network),protocolVersion_(version),
       family_(family),state_(state),counters_(counters),pending_(pending),completions_(completions),allowedPeers_(allowedPeers) {}
  std::uint64_t generation_,publication_;
  std::int32_t network_;
  std::int8_t protocolVersion_;
  TransportStatisticsFamily family_;
  TransportStatisticsState state_;
  Statistics counters_;
  std::size_t pending_,completions_,allowedPeers_;
};
} // namespace biglybtc::services::azureus_udp
