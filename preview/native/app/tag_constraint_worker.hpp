// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tag_runtime_facts.hpp"
#include "manual_tag_store.hpp"
#include "tag_assignment_owner.hpp"
#include "persistent_session.hpp"
#include <set>
#include <stop_token>
#include <thread>
#include <functional>

namespace biglybtc::app {
class SessionBridge;
// Owning values collected by the EXISTING serialized Host worker, with no FS
// gate held across provider calls. These stamps are observations, not grants.
struct ConstraintPeerRequest {
 enum class Scope { Selected, SelectedI2p, Complete };
 ConstraintFactStamp stamp;
 std::uint64_t sourceRevision=0;
 QByteArray definitionsDigest;
 Scope scope=Scope::Selected;
};
struct ConstraintPeerObservation {
 ConstraintPeerRequest request;
 std::optional<engine::PeerSetEndpoints> value;
 std::chrono::steady_clock::time_point sampled;
 std::optional<std::size_t> privateI2pCount{};
};
struct ConstraintPeerCensusObservation {
 ConstraintPeerRequest request;
 std::optional<engine::PeerSetCensus> value;
 std::chrono::steady_clock::time_point sampled;
};
enum class ConstraintAssignmentKind {Constraint,Tracker,TrackerBirth,Untagged};
struct ConstraintAssignmentRequest {
 ConstraintFactStamp stamp;
 std::uint64_t sourceRevision=0;
 std::int32_t tag=0;
 bool add=false;
 ConstraintAssignmentKind kind=ConstraintAssignmentKind::Constraint;
 std::vector<std::int32_t> rateTags{}; // bounded exact positive cohort for this pending operation.
};
struct ConstraintAssignmentObservation {
 ConstraintAssignmentRequest request;
 std::optional<storage::TagAssignmentObservation> value;
 bool retry=false; // Actual transient Busy;no publication or unbounded retry.
 std::optional<storage::PreparedTagRateSource> rates{};
};
struct ConstraintWorkerFrame {
  std::uint64_t session=0,sequence=0,sourceRevision=0;
  bool refreshSettings=false; // Cadence hint only; values are read under FS gate.
  ui::Presentation library;
  std::vector<engine::ManagedDownloadStatus> managed;
  std::set<QString> retiredRecheckTags;
  QString movingTagSubject;
  std::set<QString> retiredMoveTagSubjects;
  QString deletingTagSubject;
  std::set<QString> retiredDeletingTagSubjects;
  std::vector<OwnedI2pService::TorrentObservation> i2p;
  std::optional<engine::ExternalQueueSnapshot> queue;
  std::optional<engine::AccountingIdentity> peerEngineIdentity;
  std::optional<InternalTagSnapshot> internalNamespace;
  std::vector<ConstraintPeerObservation> peerObservations; // <=32, owning two-phase replies.
  std::optional<ConstraintPeerCensusObservation> peerCensus;
  std::optional<ConstraintAssignmentObservation> assignmentObservation;
  using PrivateRateValidation=std::function<std::expected<void,storage::Error>(const storage::ProfileSourceAccess&)>;
  std::function<std::expected<storage::ManualTagCommit,storage::Error>(const core::TorrentHash&,
      std::uint64_t,std::uint64_t,std::span<const storage::ManualTagMembershipEdit>,const PrivateRateValidation&)> privateRateWrite;
  using TagWrite=std::function<std::expected<storage::ManualTagCommit,storage::Error>()>;
  std::function<std::expected<storage::ManualTagCommit,storage::Error>(const TagWrite&)> publishTagChange;
  storage::TagAssignmentOwner* assignmentOwner=nullptr; // Actual normal Host only;never a source grant.
};
struct ConstraintWorkerUpdate {
  storage::ManualTagSnapshot snapshot;
  bool changed=false;
  unsigned evaluated=0;
  std::optional<std::int32_t> tag;
  std::optional<core::TorrentHash> hash;
  std::optional<core::tags::constraints::Problem> kept;
  std::optional<ConstraintPeerRequest> peerRequest; // At most one SDK query after step releases its FS gate.
  std::optional<ConstraintAssignmentRequest> assignmentRequest;
  std::optional<storage::TagAssignmentReceipt> assignmentReceipt; // Only a changed committed assignment.
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
  // Called after a durable definition change by this profile's category owner.
  void recordCategoryDefinitionChange(const core::SavedCategories& before,
      const core::SavedCategories& after, std::string_view name, bool added) noexcept;
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
