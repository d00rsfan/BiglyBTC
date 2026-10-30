// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owner.hpp"
#include "biglybtc/queue_policy.hpp"
#include <functional>
#include <span>
#include <vector>

namespace biglybtc::services::i2p::provider {
// One original pure identity, I2CP connection, Streaming manager, bridge and
// measured provider session. Each member has its own storage/counter lifetime.
class OwnedProfile final:public QObject {
public:
  using Phase=OwnedTorrent::Phase;
  using Options=OwnedTorrent::Options;
  struct Rates {int download=0,upload=0,seedingUpload=0;bool seedingOnly=false;};
  struct Observation {
    core::TorrentHash hash;
    OwnedTorrent::Observation torrent;
    bool queued=false,selectedComplete=false;
    bool completionPositionUnavailable=false;
    int downloadLimit=0,uploadLimit=0;
    int globalDownloadLimit=0,globalUploadLimit=0;
    std::uint32_t loopbackPeerClasses=0;
    std::uint32_t queueUnavailable=0;
  };
  struct Retained {
    std::vector<engine::detail::ManagedPayloadClaim> claims;
    std::vector<storage::OriginalTorrentIdentity> identities;
  };
  class Prepared final {
  public:
    ~Prepared();
  private:
    friend class OwnedProfile;
    struct Impl;explicit Prepared(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
  };
  class QueueState final {
  public:
    ~QueueState();
  private:
    friend class OwnedProfile;
    struct Impl;explicit QueueState(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
  };
  static std::expected<std::unique_ptr<OwnedProfile>,storage::Error> prepare(
      const storage::ProfileLease&,const core::TorrentHash&);
  ~OwnedProfile()override;
  // External Host worker only: filesystem/key/source preparation/publication.
  std::expected<std::unique_ptr<Prepared>,storage::Error> prepareTorrent(
      const core::TorrentHash&,const Retained&);
  std::expected<void,storage::Error> checkpoint(const core::TorrentHash&);
  std::expected<void,storage::Error> publish(const core::TorrentHash&,
      const std::function<std::expected<void,storage::Error>()>&);
  std::expected<void,storage::Error> publishPositions(
      const std::function<std::expected<void,storage::Error>()>&);
  std::expected<std::unique_ptr<QueueState>,storage::Error> readQueueState();
  // Same QObject thread only; no filesystem calls or caller callbacks.
  bool begin(const Options&,core::QueuePolicy,Rates);
  Retained retained()const;
  bool applyQueueState(std::unique_ptr<QueueState>);
  std::vector<core::TorrentHash> completedForceStarts()const;
  void acknowledgeCompletion(const core::TorrentHash&);
  std::expected<void,storage::Error> add(std::unique_ptr<Prepared>);
  bool retire(const core::TorrentHash&,std::chrono::steady_clock::time_point);
  OwnedTorrent::RetirementObservation retirement(const core::TorrentHash&);
  std::expected<void,storage::Error> release(const core::TorrentHash&);
  std::vector<Observation> observations();
  Phase phase()const noexcept;
  void close();
  void join(); // External lifetime only, after close.
private:
  struct Impl;explicit OwnedProfile(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
  bool event(QEvent*)override;
};
}
