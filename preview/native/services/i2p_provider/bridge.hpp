// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile_source.hpp"
#include "channel.hpp"
#include <QObject>
#include <memory>
#include <functional>

namespace biglybtc::services::i2p::provider {
class OwnedTorrent;
// Explicit optional private SAM-shaped endpoint, never a system SAM service.
// All calls/destruction are on the same QObject thread as InitialChannel.
class Bridge final:public QObject {
  Q_OBJECT
public:
  struct Ports {std::uint16_t peer=6881,localPeer=6881;};
  enum class Phase {Idle,Preparing,Ready,Withdrawn};
  struct Observation {Phase phase=Phase::Idle;std::uint64_t generation=0,admission=0;Hash publicIdentity{};std::uint16_t port=0;std::size_t sockets=0,streams=0,knownPeers=0;std::uint64_t rejectedClients=0;};
  Bridge(std::shared_ptr<ProfileSource>,i2cp::InitialChannel&,QObject* parent=nullptr);
  ~Bridge()override;
  bool begin(Ports,std::uint64_t admission);
  void close(); // Sockets/stream authority withdrawn before external I/O join.
  void joinSource(); // Lifetime caller only, after close; may wait for OS I/O.
  Observation observation()const noexcept; // No capability/key/private config.
signals:
  void changed(); // Queued, coalesced, deletion/restart safe.
private:
  friend class OwnedTorrent;
  friend class OwnedProfile;
  struct Impl;std::unique_ptr<Impl> impl_;
  // Private ephemeral capability is transferred only into the exact measured
  // provider hook. Not exposed by a public config/read/observation interface.
  bool installProvider(std::function<bool(void*)>);
  bool event(QEvent*)override;
};
}
