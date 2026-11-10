// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owned_transport.hpp"
#include "biglybtc/shared_udp_witness.hpp"
#include <atomic>
namespace libtorrent {struct session_handle;}
namespace biglybtc::services::shared_udp {
using Clock=std::chrono::steady_clock;
using Binding=azureus_udp::Peer;
struct Listener {Binding local,observedExternal;};
struct ProbePeer {azureus_udp::Peer endpoint;std::int8_t version=55;};
struct AddressObservation {azureus_udp::Peer external;std::vector<ProbePeer> candidates;};
using AddressResult=std::expected<AddressObservation,azureus_udp::Failure>;
// Qt owner of a source/image-qualified attachment to the existing SDK session.
// Never constructs a session, binds a socket or grants profile/network rights.
class Provider final:public QObject {
public:
  static std::expected<std::unique_ptr<Provider>,azureus_udp::Failure> attach(
      const libtorrent::session_handle&,quint16 port);
  ~Provider()override;
  std::vector<Binding> bindings()const;
  std::vector<Listener> listeners()const;
  std::shared_ptr<const core::SharedUdpWitness> witness()const;
  std::expected<void,azureus_udp::Failure> refresh();
  // Receiver executes on this Qt thread, never on the SDK executor. False
  // reports application backpressure; that datagram is dropped, as with UDP.
  void receive(std::function<bool(Binding,azureus_udp::Peer,std::string)>);
  std::expected<azureus_udp::SharedChannel,azureus_udp::Failure> channel(
      Binding local,azureus_udp::Peer advertised,std::function<bool()> heldPermit);
  // NODE probes through this same socket. Two correlated responses from
  // different IPs must agree before publishing an external-address observation.
  std::expected<void,azureus_udp::Failure> probeAddress(Binding,std::int32_t network,
      std::vector<ProbePeer>,bool ownedLoopback,
      std::function<bool(Clock::time_point,std::move_only_function<void()>)> withPermit,
      std::function<bool()> heldPermit,std::function<void(AddressResult)>);
  std::uint64_t dropped()const noexcept;
  void close()noexcept;
private:
  struct Impl;struct Probe;
  explicit Provider(std::unique_ptr<Impl>);
  void drain();
  void publishListeners();
  void pumpProbe();
  void probeReply(std::uint64_t,ProbePeer,azureus_udp::DetailedFindNodeOutcome);
  void finishProbe(AddressResult);
  std::unique_ptr<Impl> impl_;
};
}
