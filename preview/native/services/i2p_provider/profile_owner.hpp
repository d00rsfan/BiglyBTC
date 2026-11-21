// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owner.hpp"
#include "coexist_queue.hpp"
#include "pending_magnet_source.hpp"
#include "../i2p_dht/actor.hpp"
#include "biglybtc/queue_policy.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace biglybtc::engine {class ProfileTagRates;}
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
    std::optional<std::int64_t> cumulativeReceived{},cumulativeSent{};
    bool queued=false,selectedComplete=false;
    bool completionPositionUnavailable=false;
    int downloadLimit=0,uploadLimit=0;
    int globalDownloadLimit=0,globalUploadLimit=0;
    std::uint32_t loopbackPeerClasses=0;
    std::uint32_t queueUnavailable=0;
  };
  using Retained=engine::RetainedPayloads;
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
  // SDK construction precedes payload preparation and requires no outer
  // source gate. Optional expected configuration only narrows the accepted
  // source;it is rechecked under the gate immediately before storage work.
  static std::expected<std::unique_ptr<OwnedProfile>,storage::Error> prepare(
      const storage::ProfileLease&,const core::TorrentHash&,const Retained& external={},
      const storage::TagManagerStartupMode* startup=nullptr,
      std::optional<std::string_view> expectedConfiguration=std::nullopt);
  // Explicit common tag owner,used synchronously on the I/O worker only.
  // The controller reference is never retained or invoked by the socket turn.
  static std::expected<std::unique_ptr<OwnedProfile>,storage::Error> prepareWithTagRates(
      const storage::ProfileLease&,const core::TorrentHash&,engine::ProfileTagRates&,
      const Retained& external={},const storage::TagManagerStartupMode* startup=nullptr,
      std::optional<std::string_view> expectedConfiguration=std::nullopt);
  // Pending-first pure bootstrap uses the SAME key1 session and disk sandbox.
  static std::expected<std::unique_ptr<OwnedProfile>,storage::Error> preparePending(
      const storage::ProfileLease&,const engine::PendingMagnet&,
      const storage::TagManagerStartupMode* startup=nullptr,
      std::optional<std::string_view> expectedConfiguration=std::nullopt);
  class TagSourceUpdate final {
  public:~TagSourceUpdate();
  private:friend class OwnedProfile;struct Impl;
    explicit TagSourceUpdate(std::unique_ptr<Impl>);std::unique_ptr<Impl> impl_;
  };
  // One synchronous service-owned transaction. No controller reference is kept.
  std::expected<void,storage::Error> preflightTagSourceUpdate(engine::ProfileTagRates&); // Host I/O,unlocked.
  std::expected<std::unique_ptr<TagSourceUpdate>,storage::Error> beginTagSourceUpdate(); // Private Qt.
  std::expected<void,storage::Error> joinTagSourceUpdate(TagSourceUpdate&); // Host I/O,no FS gate.
  std::expected<void,storage::Error> prepareTagSourceUpdate(TagSourceUpdate&,engine::ProfileTagRates&); // Host I/O,after CAS.
  bool finishTagSourceUpdate(std::unique_ptr<TagSourceUpdate>); // Private Qt;false closes.
  struct MagnetObservation {
    core::TorrentHash hash;std::uint64_t admission=0,generation=0;
    bool retained=false,metadataReady=false,cancelled=false;
    std::optional<storage::Error> failure;
  };
  class PreparedMagnet final {
  public:~PreparedMagnet();
  private:friend class OwnedProfile;struct Impl;explicit PreparedMagnet(std::unique_ptr<Impl>);std::unique_ptr<Impl> impl_;
  };
  std::expected<std::unique_ptr<PreparedMagnet>,storage::Error> prepareMagnet(const engine::PendingMagnet&); // Worker IO.
  bool configureContactExchange(std::shared_ptr<ContactExchange>); // Qt, before begin; owning mailbox only.
  bool configureDiscovery(const dht::Actor::Options&); // Same Qt thread, before begin.
  std::expected<void,storage::Error> startMagnet(std::unique_ptr<PreparedMagnet>); // Qt, no IO.
  bool cancelMagnet(const core::TorrentHash&,std::chrono::steady_clock::time_point); // Qt.
  std::vector<MagnetObservation> magnetObservations(); // Qt.
  // Actual complete metainfo only after matching removal, weak expiry and an
  // executor/alert fence. The worker imports via publishPendingMagnet afterward.
  std::expected<std::string,storage::Error> takeCompletedMetadata(const core::TorrentHash&); // Qt.
  std::expected<void,storage::Error> releaseMagnet(const core::TorrentHash&); // Worker IO, after retirement.
  std::expected<void,storage::Error> publishPendingMagnet(const core::TorrentHash&,
      const std::function<std::expected<void,storage::Error>()>&);
  ~OwnedProfile()override;
  // External Host worker only: filesystem/key/source preparation/publication.
  std::expected<std::unique_ptr<Prepared>,storage::Error> prepareTorrent(
      const core::TorrentHash&,const Retained&);
  std::expected<std::unique_ptr<Prepared>,storage::Error> prepareTorrentWithTagRates(
      const core::TorrentHash&,const Retained&,engine::ProfileTagRates&,
      std::optional<std::string_view> expectedConfiguration=std::nullopt);
  // Original construction IO worker only; FS-only, no Qt/SDK dispatch.
  // The Host serializes member insertion/removal; only retained state changes.
  std::expected<bool,storage::Error> rebaseCategory(const storage::ProfileLease&,const core::TorrentHash&);
  std::expected<void,storage::Error> checkpoint(const core::TorrentHash&);
  std::expected<void,storage::Error> publish(const core::TorrentHash&,
      const std::function<std::expected<void,storage::Error>()>&);
  std::expected<void,storage::Error> publishPositions(
      const std::function<std::expected<void,storage::Error>()>&);
  std::expected<std::unique_ptr<QueueState>,storage::Error> readQueueState();
  // Host-worker read/validation only; gate released before the Qt/SDK turn.
  std::expected<std::uint64_t,storage::Error> validateExternalQueueSource();
  // Same QObject thread only; no filesystem calls or caller callbacks.
  bool begin(const Options&,core::QueuePolicy,Rates);
  Retained retained()const;
  bool configureExternalQueue(bool);
  std::expected<engine::ExternalQueueSnapshot,storage::Error> externalQueueSnapshot();
  bool externalQueueCurrent(std::uint64_t,std::span<const engine::ExternalQueueDecision>)const;
  std::expected<bool,storage::Error> applyExternalQueue(std::uint64_t,
      std::span<const engine::ExternalQueueDecision>,std::uint32_t,core::QueueAction,
      std::optional<std::uint64_t> validatedSource={});
  std::expected<void,storage::Error> applyExternalQueuePositions(
      std::span<const std::pair<engine::QueueBinding,std::uint32_t>>);
  std::expected<void,storage::Error> applyExternalRateLimits(const core::SessionRateLimits&);
  bool applyQueueState(std::unique_ptr<QueueState>);
  std::vector<core::TorrentHash> completedForceStarts()const;
  void acknowledgeCompletion(const core::TorrentHash&);
  std::expected<void,storage::Error> add(std::unique_ptr<Prepared>);
  bool retire(const core::TorrentHash&,std::chrono::steady_clock::time_point);
  OwnedTorrent::RetirementObservation retirement(const core::TorrentHash&);
  std::expected<void,storage::Error> release(const core::TorrentHash&);
  std::vector<Observation> observations();
  // Explicit passive query on the owner thread only; no polling/IO/effect permit.
  std::vector<std::array<std::uint8_t,20>> peerIds(const core::TorrentHash&)const;
  Phase phase()const noexcept;
  bool sourceCurrent()const noexcept;
  void revokeSource()noexcept; // Atomic source revocation; no Qt or SDK wait.
  void close();
  void join(); // External lifetime only, after close.
private:
  static std::expected<std::unique_ptr<OwnedProfile>,storage::Error> prepareImpl(
      const storage::ProfileLease&,const core::TorrentHash&,const Retained&,
      const storage::TagManagerStartupMode*,std::optional<std::string_view>,engine::ProfileTagRates*);
  struct Impl;explicit OwnedProfile(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
  bool event(QEvent*)override;
};
}
