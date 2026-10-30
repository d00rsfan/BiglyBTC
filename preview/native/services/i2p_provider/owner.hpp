// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bridge.hpp"
#include <libtorrent/torrent_status.hpp>
#include <QObject>
#include <memory>
#include <cstddef>
#include <array>
#include <cstdint>
#include <string>

namespace biglybtc::services::i2p::provider {
// One dedicated provider session/admission. No default Host, Public/mixed
// network, system SAM, automatic transient identity or alternate transport.
class OwnedTorrent final:public QObject {
  Q_OBJECT
public:
  enum class Phase {Prepared,Starting,Active,Withdrawn};
  struct Options {i2cp::InitialChannel::Options router;i2cp::SessionOptions session;Bridge::Ports ports;};
  // Diagnostic values only. No URL, address, tracker reason, capability or key.
  struct ErrorObservation {int value=0;std::string category,message;};
  struct TrackerHashObservation {bool updating=false,startSent=false;unsigned fails=0;ErrorObservation error;};
  struct TrackerEndpointObservation {bool enabled=false;std::array<TrackerHashObservation,2> hashes{};};
  struct TrackerObservation {std::size_t endpointCount=0,endpointsShown=0;std::array<TrackerEndpointObservation,4> endpoints{};};
  struct AlertObservation {int type=0;ErrorObservation error;};
  struct AlertDiagnostics {
    std::uint64_t drained=0,uninspected=0,announces=0,trackerErrors=0,trackerReplies=0,i2pErrors=0,fileErrors=0,torrentErrors=0;
    std::size_t currentShown=0,currentOmitted=0;std::array<AlertObservation,16> current{};
  };
  struct Diagnostics {
    bool statusAvailable=false,incomplete=false,paused=false,announcingToTrackers=false;int torrentState=-1;std::uint64_t flags=0;
    ErrorObservation error;std::size_t trackerCount=0,trackersShown=0;std::array<TrackerObservation,4> trackers{};AlertDiagnostics alerts;
  };
  struct Observation {Phase phase=Phase::Prepared;Hash publicIdentity{};std::uint64_t admission=0,generation=0;bool seeding=false;std::int64_t verifiedBytes=0;std::uint16_t bridgePort=0;std::uint64_t rejectedClients=0;std::size_t bridgeSockets=0;Diagnostics diagnostics{};};
  // External preparation/lifetime caller only. Uses genuine original profile
  // lock, key, full metadata and payload mappings; never a caller DTO grant.
  // Requires the separately root-built/approved optional PRIVATE SDK image.
  static std::expected<std::unique_ptr<OwnedTorrent>,storage::Error> prepare(
      const storage::ProfileLease&,const core::TorrentHash&,std::string pluginDirectory="azneti2phelper");
  ~OwnedTorrent()override;
  bool begin(const Options&); // Same QObject thread, explicit once.
  void close(); // Withdraw all sockets/provider operations, no filesystem wait.
  void join(); // External lifetime after close; actual provider + source joins.
  Observation observation()const;
signals:
  void changed(); // Queued, no private key/capability/URL in public observations.
private:
  struct Impl;explicit OwnedTorrent(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
  bool event(QEvent*)override;
};
}
