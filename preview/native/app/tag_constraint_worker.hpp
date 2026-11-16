// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tag_runtime_facts.hpp"
#include "manual_tag_store.hpp"
#include "persistent_session.hpp"
#include <stop_token>
#include <thread>

namespace biglybtc::app {
class SessionBridge;
// Owning values collected by the EXISTING serialized Host worker, with no FS
// gate held across provider calls. These stamps are observations, not grants.
struct ConstraintPeerRequest {
 ConstraintFactStamp stamp;
 std::uint64_t sourceRevision=0;
 QByteArray definitionsDigest;
};
struct ConstraintPeerObservation {
 ConstraintPeerRequest request;
 std::optional<engine::PeerSetEndpoints> value;
 std::chrono::steady_clock::time_point sampled;
};
struct ConstraintWorkerFrame {
  std::uint64_t session=0,sequence=0,sourceRevision=0;
  bool refreshSettings=false; // Cadence hint only; values are read under FS gate.
  ui::Presentation library;
  std::vector<engine::ManagedDownloadStatus> managed;
  std::vector<OwnedI2pService::TorrentObservation> i2p;
  std::optional<engine::ExternalQueueSnapshot> queue;
  std::optional<engine::AccountingIdentity> peerEngineIdentity;
  std::optional<InternalTagSnapshot> internalNamespace;
  std::vector<ConstraintPeerObservation> peerObservations; // <=32, owning two-phase replies.
};
struct ConstraintWorkerUpdate {
  storage::ManualTagSnapshot snapshot;
  bool changed=false;
  unsigned evaluated=0;
  std::optional<std::int32_t> tag;
  std::optional<core::TorrentHash> hash;
  std::optional<core::tags::constraints::Problem> kept;
  std::optional<ConstraintPeerRequest> peerRequest; // At most one SDK query after step releases its FS gate.
};
// Worker-only optional rule executor. Host activates it ONLY after normal
// startup; opening the offline preview must never construct/drive this service.
// It shares the existing retained ManualTagStore (no second writer/allocator).
// A step visits <=32 pairs, makes <=1 synchronous durable commit, and checks
// cancellation before publication. All provider calls happen before step().
// No callback, Qt delivery, SDK call, timer or pool exists in this service.
class TagConstraintWorker final {
public:
  static std::expected<std::unique_ptr<TagConstraintWorker>,storage::Error> activate(
      const storage::ProfileLease&,std::uint64_t profileSession);
  // The real Host shares its one startup observation with history/state tags.
  static std::expected<std::unique_ptr<TagConstraintWorker>,storage::Error> activate(
      const storage::ProfileLease&,std::uint64_t profileSession,const storage::TagManagerStartupMode&);
  ~TagConstraintWorker();
  TagConstraintWorker(const TagConstraintWorker&)=delete;
  TagConstraintWorker& operator=(const TagConstraintWorker&)=delete;
  std::expected<ConstraintWorkerUpdate,storage::Error> step(
      storage::ManualTagStore&,const storage::ManualTagSnapshot&,
      const ConstraintWorkerFrame&,std::stop_token={});
  // Close is terminal. No source grant or automatic retry after uncertainty.
  void close() noexcept;
private:
  friend class SessionBridge;
  // Genuine durable intake hook only. No public/UI setter for newness.
  std::expected<bool,storage::Error> recordIntake(storage::ManualTagStore&,
      const storage::ManualTagSnapshot&,const core::TorrentHash&,std::uint64_t event);
  // Private genuine producers: changed durable settings or explicit user sync.
  enum class TrackerPropertyEvent {Changed,Sync};
  std::expected<bool,storage::Error> recordTrackerPropertyEvent(storage::ManualTagStore&,
      const storage::ManualTagSnapshot&,std::int32_t,ui::Presentation,TrackerPropertyEvent,std::stop_token={});
  std::expected<bool,storage::Error> recordUntaggedPropertyChange(storage::ManualTagStore&,
      const storage::ManualTagSnapshot& before,const storage::ManualTagSnapshot& after,ui::Presentation);
  struct Impl;
  explicit TagConstraintWorker(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::app
