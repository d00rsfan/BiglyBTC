// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owned_transport.hpp"
#include <atomic>
namespace libtorrent {struct session_handle;}
namespace biglybtc::services::shared_udp {
using Clock=std::chrono::steady_clock;
using Binding=azureus_udp::Peer;
// Qt owner of a source/image-qualified attachment to the existing SDK session.
// Never constructs a session, binds a socket or grants profile/network rights.
class Provider final:public QObject {
public:
  static std::expected<std::unique_ptr<Provider>,azureus_udp::Failure> attach(
      const libtorrent::session_handle&,quint16 port);
  ~Provider()override;
  std::vector<Binding> bindings()const;
  std::expected<void,azureus_udp::Failure> refresh();
  // Receiver executes on this Qt thread, never on the SDK executor. False
  // reports application backpressure; that datagram is dropped, as with UDP.
  void receive(std::function<bool(Binding,azureus_udp::Peer,std::string)>);
  std::expected<azureus_udp::SharedChannel,azureus_udp::Failure> channel(
      Binding local,azureus_udp::Peer advertised,std::function<bool()> heldPermit);
  std::uint64_t dropped()const noexcept;
  void close()noexcept;
private:
  struct Impl;
  explicit Provider(std::unique_ptr<Impl>);
  void drain();
  std::unique_ptr<Impl> impl_;
};
}
