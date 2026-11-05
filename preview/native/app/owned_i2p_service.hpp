// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "../engine/coexist_queue.hpp"
#include "library_commands.hpp"
#include "biglybtc/library.hpp"
#include "biglybtc/download_action.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace biglybtc::engine {struct PendingMagnet;struct PendingMagnetStatus;}
namespace biglybtc::app {
struct I2pDiscoverySeed {
  std::array<std::uint8_t,20> nodeId{};
  std::array<std::uint8_t,32> destinationHash{};
  std::uint16_t queryPort=0;std::vector<std::uint8_t> destination;
};
struct I2pDiscoveryOptions {
  std::array<std::uint8_t,20> nodeId{};std::uint16_t queryPort=0;
  std::vector<I2pDiscoverySeed> seeds;
  // Explicit node policy request; actual source-held grants remain mandatory.
  bool inbound=false,inboundSleeping=false,inboundSuspended=false,routingMaintenance=false;
};
struct I2pServiceOptions {
  std::uint16_t routerPort=7654;
  std::vector<std::pair<std::string,std::string>> sessionOptions;
  std::uint16_t peerPort=6881;
  // Coexistence is an explicit Host mode, not new network/source authority.
  // It requires the union controller before the first possible admission.
  bool publicCoexistence=false,externalQueue=false;
  std::optional<I2pDiscoveryOptions> discovery; // Explicit routing hints; no peer/source authority.
};
// All calls and destruction belong to the external Host worker that called
// start(). Blocking Qt/provider observations and joins must never run on GUI.
class OwnedI2pService final {
public:
  enum class Phase {Prepared,Starting,Active,Withdrawn,Retiring};
  struct Observation {
    Phase phase=Phase::Prepared;
    std::array<std::uint8_t,32> publicIdentity{};
    std::uint64_t admission=0,generation=0;
    std::int64_t verifiedBytes=0,payloadDownloaded=-1,payloadUploaded=-1;
    int downloadRate=0,uploadRate=0,peers=0,seeds=0;
    std::optional<std::int64_t> cumulativeReceived{},cumulativeSent{};
    bool checked=false,seeding=false,current=false,joined=false,queued=false,selectedComplete=false;
    bool completionPositionUnavailable=false;
    int downloadLimit=0,uploadLimit=0;
    int globalDownloadLimit=0,globalUploadLimit=0;
    std::uint32_t loopbackPeerClasses=0;
    std::uint32_t queueUnavailable=0;
    std::uint64_t trackerAnnounces=0,trackerReplies=0,trackerErrors=0;
    std::string failure;
  };
  static bool available()noexcept;
  static std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> start(
      const storage::ProfileLease&,const core::TorrentHash&,I2pServiceOptions={},
      const engine::RetainedPayloads& external={});
  static std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> startPending(
      const storage::ProfileLease&,const engine::PendingMagnet&,I2pServiceOptions={});
  std::expected<void,storage::Error> addMagnet(const engine::PendingMagnet&);
  std::expected<void,storage::Error> cancelMagnet(const core::TorrentHash&);
  std::vector<engine::PendingMagnetStatus> pendingMagnets()const;
  std::expected<std::string,storage::Error> takeCompletedMetadata(const core::TorrentHash&);
  std::expected<void,storage::Error> releaseMagnet(const core::TorrentHash&);
  std::expected<void,storage::Error> publishPendingMagnet(const core::TorrentHash&,
      const std::function<std::expected<void,storage::Error>()>&);
  ~OwnedI2pService();
  OwnedI2pService(const OwnedI2pService&)=delete;
  OwnedI2pService& operator=(const OwnedI2pService&)=delete;
  struct TorrentObservation {core::TorrentHash hash;Observation value;};
  Observation observation()const;
  std::vector<TorrentObservation> observations()const;
  // Optional passive peer view, at most4 real handshake IDs; caller worker only.
  std::vector<std::array<std::uint8_t,20>> peerIds(const core::TorrentHash&)const;
  std::expected<void,storage::Error> add(const core::TorrentHash&,const engine::RetainedPayloads& external={});
  std::expected<void,storage::Error> stop(const core::TorrentHash&);
  std::expected<void,storage::Error> command(const core::TorrentHash&,core::DownloadAction,const engine::RetainedPayloads& external={});
  std::expected<void,storage::Error> move(const core::TorrentHash&,core::DownloadAction,
      std::span<const storage::LibraryCompletionPool>);
  // Internal Host worker publication; no UI/user observer may be called here.
  std::expected<void,storage::Error> publish(const core::TorrentHash&,
      const std::function<std::expected<void,storage::Error>()>&);
  std::expected<void,storage::Error> publishPositions(const engine::SourceMutation&);
  std::expected<void,storage::Error> configureExternalQueue(bool);
  std::expected<engine::ExternalQueueSnapshot,storage::Error> externalQueueSnapshot();
  std::expected<bool,storage::Error> applyExternalQueue(std::uint64_t,
      std::span<const engine::ExternalQueueDecision>,std::uint32_t,core::QueueAction);
  std::expected<void,storage::Error> applyExternalQueuePositions(
      std::span<const std::pair<engine::QueueBinding,std::uint32_t>>);
  std::expected<void,storage::Error> applyExternalRateLimits(const core::SessionRateLimits&);
  engine::RetainedPayloads retainedPayloads()const;
  // Five-second actual retirement, external join, then exact saved checkpoint.
  // Failure retains recheck state; callers must not change saved intent.
  // Provider/source joins and checkpoint IO may block on this Host worker.
  bool sourceCurrent()const noexcept; // No Qt/SDK/I/O.
  void revoke()noexcept; // Atomic source-only withdrawal; no Qt/SDK dispatch.
  std::optional<storage::Error> contactCacheDiagnostic()const; // Existing external Host worker only.
  std::expected<void,storage::Error> close();
private:
  struct Impl;explicit OwnedI2pService(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}
