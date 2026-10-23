// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "transport_statistics.hpp"
#include "headers.hpp"
#include "vivaldi_position.hpp"
#include <array>
#include <optional>
#include <span>
namespace biglybtc::services::azureus_udp {
enum class PingObservationKind {Reply,Timeout,Cancelled,Closed,RemoteError,Failed};
// Minted only at the sole actor's real pending-PING completion boundary. This
// is endpoint/transaction/connection/action correlation, not remote crypto ID.
class OwnedPingObservation final {
public:
  OwnedPingObservation(const OwnedPingObservation&)=default;
  OwnedPingObservation& operator=(const OwnedPingObservation&)=default;
  std::uint64_t generation()const noexcept{return generation_;}
  std::uint64_t sequence()const noexcept{return sequence_;}
  const azureus_dht::Address& endpoint()const noexcept{return endpoint_;}
  PingObservationKind kind()const noexcept{return kind_;}
  std::int64_t observedSteadyNanoseconds()const noexcept{return observed_;}
  const std::optional<std::int64_t>& rttNanoseconds()const noexcept{return rtt_;}
private:
  friend class OwnedPingTransport;
  OwnedPingObservation()=default;
  std::uint64_t generation_=0,sequence_=0;
  azureus_dht::Address endpoint_;
  PingObservationKind kind_=PingObservationKind::Failed;
  std::int64_t observed_=0;
  std::optional<std::int64_t> rtt_;
};
// An actually received, endpoint-correlated V1 report. Report identity is
// separate from sealed PING completion receipts and never asserts routing,
// geographical location, remote authentication or continued peer liveness.
struct VivaldiPeerObservation {
  azureus_dht::Address endpoint;
  VivaldiValue position;
  std::uint64_t generation=0,sequence=0,ticket=0;
  std::int64_t observedSteadyNanoseconds=0,rttNanoseconds=0;
  bool localUpdateAccepted=false;
};
enum class OperationEventKind { Sent,PartialReply,Reply,RemoteError,Timeout,Cancelled,Closed,Abandoned,Capacity,SendFailed };
struct OperationEvent {
  std::uint64_t serial=0,ticket=0;std::int32_t requestAction=0;
  OperationEventKind kind=OperationEventKind::Sent;
  std::optional<std::int64_t> elapsedNanoseconds;
};
class OwnedDhtStatisticsSnapshot final {
public:
  OwnedDhtStatisticsSnapshot(const OwnedDhtStatisticsSnapshot&)=default;
  OwnedDhtStatisticsSnapshot& operator=(const OwnedDhtStatisticsSnapshot&)=default;
  static constexpr std::size_t MaximumEvents=64;
  static constexpr std::size_t MaximumPeers=64;
  const TransportStatisticsSnapshot& transport()const noexcept{return transport_;}
  bool operationsEnabled()const noexcept{return operationsEnabled_;}
  bool vivaldiEnabled()const noexcept{return vivaldiEnabled_;}
  std::span<const OperationEvent> events()const noexcept{return {events_.data(),count_};}
  std::uint64_t droppedEvents()const noexcept{return dropped_;}
  const VivaldiValue& localPosition()const noexcept{return local_;}
  const std::optional<VivaldiValue>& remotePosition()const noexcept{return remote_;}
  const std::optional<std::int64_t>& lastRttNanoseconds()const noexcept{return rtt_;}
  std::uint64_t coordinateObservations()const noexcept{return coordinateObservations_;}
  const std::optional<OwnedPingObservation>& pingObservation()const noexcept{return ping_;}
  bool pingObservationsLost()const noexcept{return pingLost_;}
  std::span<const VivaldiPeerObservation> peerPositions()const noexcept{return {peers_.data(),peerCount_};}
  std::uint64_t droppedPeerPositions()const noexcept{return droppedPeers_;}
  bool peerPositionsLost()const noexcept{return peersLost_;}
private:
  friend class OwnedPingTransport;
  explicit OwnedDhtStatisticsSnapshot(TransportStatisticsSnapshot value)noexcept:transport_(value){}
  TransportStatisticsSnapshot transport_;
  bool operationsEnabled_=false,vivaldiEnabled_=false;
  std::array<OperationEvent,MaximumEvents> events_{};std::size_t count_=0;std::uint64_t dropped_=0;
  VivaldiValue local_{};std::optional<VivaldiValue> remote_;std::optional<std::int64_t> rtt_;
  std::uint64_t coordinateObservations_=0;
  std::optional<OwnedPingObservation> ping_;bool pingLost_=false;
  std::array<VivaldiPeerObservation,MaximumPeers> peers_{};
  std::size_t peerCount_=0;std::uint64_t droppedPeers_=0;bool peersLost_=false;
};
} // namespace biglybtc::services::azureus_udp
