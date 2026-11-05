// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tag_runtime_facts.hpp"
#include "manual_tag_store.hpp"
#include <stop_token>
#include <thread>

namespace biglybtc::app {
// Owning values collected by the EXISTING serialized Host worker, with no FS
// gate held across provider calls. These stamps are observations, not grants.
struct ConstraintWorkerFrame {
  std::uint64_t session=0,sequence=0,sourceRevision=0;
  bool refreshSettings=false; // Cadence hint only; values are read under FS gate.
  ui::Presentation library;
  std::vector<engine::ManagedDownloadStatus> managed;
  std::vector<OwnedI2pService::TorrentObservation> i2p;
  std::optional<engine::ExternalQueueSnapshot> queue;
};
struct ConstraintWorkerUpdate {
  storage::ManualTagSnapshot snapshot;
  bool changed=false;
  unsigned evaluated=0;
  std::optional<std::int32_t> tag;
  std::optional<core::TorrentHash> hash;
  std::optional<core::tags::constraints::Problem> kept;
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
  ~TagConstraintWorker();
  TagConstraintWorker(const TagConstraintWorker&)=delete;
  TagConstraintWorker& operator=(const TagConstraintWorker&)=delete;
  std::expected<ConstraintWorkerUpdate,storage::Error> step(
      storage::ManualTagStore&,const storage::ManualTagSnapshot&,
      const ConstraintWorkerFrame&,std::stop_token={});
  // Close is terminal. No source grant or automatic retry after uncertainty.
  void close() noexcept;
private:
  struct Impl;
  explicit TagConstraintWorker(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::app
