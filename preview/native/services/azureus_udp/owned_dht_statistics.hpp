// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "transport_statistics.hpp"
#include "vivaldi_position.hpp"
#include <array>
#include <optional>
#include <span>
namespace biglybtc::services::azureus_udp {
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
  const TransportStatisticsSnapshot& transport()const noexcept{return transport_;}
  bool operationsEnabled()const noexcept{return operationsEnabled_;}
  bool vivaldiEnabled()const noexcept{return vivaldiEnabled_;}
  std::span<const OperationEvent> events()const noexcept{return {events_.data(),count_};}
  std::uint64_t droppedEvents()const noexcept{return dropped_;}
  const VivaldiValue& localPosition()const noexcept{return local_;}
  const std::optional<VivaldiValue>& remotePosition()const noexcept{return remote_;}
  const std::optional<std::int64_t>& lastRttNanoseconds()const noexcept{return rtt_;}
  std::uint64_t coordinateObservations()const noexcept{return coordinateObservations_;}
private:
  friend class OwnedPingTransport;
  explicit OwnedDhtStatisticsSnapshot(TransportStatisticsSnapshot value)noexcept:transport_(value){}
  TransportStatisticsSnapshot transport_;
  bool operationsEnabled_=false,vivaldiEnabled_=false;
  std::array<OperationEvent,MaximumEvents> events_{};std::size_t count_=0;std::uint64_t dropped_=0;
  VivaldiValue local_{};std::optional<VivaldiValue> remote_;std::optional<std::int64_t> rtt_;
  std::uint64_t coordinateObservations_=0;
};
} // namespace biglybtc::services::azureus_udp
