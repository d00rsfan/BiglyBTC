// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/tag_constraints.hpp"
#include "library_model.hpp"
#include "managed_download.hpp"
#include "coexist_queue.hpp"
#include "owned_i2p_service.hpp"
#include "torrent_state.hpp"
#include "manual_tag_store.hpp"
#include "internal_tag_namespace.hpp"
#include <biglybtc/download_state_names.hpp>
#include <array>
#include <algorithm>

namespace biglybtc::app {
// Exact existing SessionBridge UI convention: canonical uppercase 40/64 hex
// characters, decoded to raw20/32 before TorrentHash::parse. No normalisation,
// profile/source access or provider authority is created by this value decoder.
core::tags::constraints::Result<core::TorrentHash> constraintFactHash(const QString&);
// Passive source stamps, never command, source-permit or membership authority.
// session is the existing Host profile lifetime; Target.generation is its
// library revision. Provider generation is a distinct engine/group lifetime.
struct ConstraintFactBinding {
  std::uint64_t session=0;
  ui::Target target;
  std::uint64_t providerAdmission=0,providerGeneration=0;
  bool operator==(const ConstraintFactBinding&)const=default;
};
struct ConstraintFactStamp {
  ConstraintFactBinding binding;
  std::uint64_t sequence=0; // Existing worker's monotonically accepted turn.
  bool operator==(const ConstraintFactStamp&)const=default;
};
struct ConstraintOriginalState {
  ConstraintFactStamp stamp;
  core::Document document; // Owns the exact captured backing bytes.
};
// Worker-only read of an ALREADY loaded value; no filesystem/lease acquisition.
// A failed hash/stamp association creates no snapshot or provider permission.
core::tags::constraints::Result<ConstraintOriginalState> captureConstraintOriginalState(
    const ConstraintFactStamp&,const storage::TorrentStateFile&);

class TagConstraintWorker;
// Only the actual enabled normal worker can create this current-membership
// cohort. It is not a claim that a dependency's expression is true, nor an
// effect grant. Safe ordinary top-level roots do not recursively re-evaluate
// dependencies in Original checkStuff(); new-only roots are deliberately absent.
class ConstraintMembershipCohort final {
public:
  ConstraintMembershipCohort()=default;
  bool valid()const noexcept{return data_&&data_->source.valid()&&data_->stamp.sequence!=0;}
  const ConstraintFactStamp& stamp()const noexcept{static const ConstraintFactStamp empty;return data_?data_->stamp:empty;}
  const storage::ManualTagSnapshot& source()const noexcept{static const storage::ManualTagSnapshot empty;return data_?data_->source:empty;}
  bool knows(std::int32_t id)const noexcept{return data_&&std::binary_search(data_->known.begin(),data_->known.end(),id);}
  bool ordinaryRoot(std::int32_t id)const noexcept{return data_&&std::binary_search(data_->roots.begin(),data_->roots.end(),id);}
  bool sameScope(const ConstraintMembershipCohort& other)const noexcept{return valid()&&data_==other.data_;}
private:
  friend class TagConstraintWorker;
  struct Data {ConstraintFactStamp stamp;storage::ManualTagSnapshot source;std::vector<std::int32_t> known,roots;};
  std::shared_ptr<const Data> data_;
};

// Issued only by the normal worker from its already captured primary
// inventory/full library. This is source scope,not a provider or effect grant.
class ConstraintNamedMetricCensus final {
public:
 bool current(const ConstraintFactStamp&,const storage::ManualTagStore&,
   const storage::ProfileLease&,const storage::ManualTagSnapshot&)const;
 bool containsPrimaryV1(const core::TorrentHash&)const;
 std::optional<std::int32_t> queuePosition(const core::TorrentHash&)const;
private:
 friend class TagConstraintWorker;
 struct Data {std::uint64_t session=0,sequence=0,libraryRevision=0,sourceRevision=0;
   storage::ManualTagSnapshot source;std::vector<std::string> primaryV1;
   std::vector<std::pair<std::string,std::int32_t>> positions;};
 std::shared_ptr<const Data> data_;
};

struct ConstraintCategorySource {
  ConstraintFactStamp stamp;
  core::tags::runtime::Type type; // Source-bound owning metadata, not a grant.
};
struct ConstraintPeerSetSource {
 ConstraintFactStamp stamp;
 core::tags::runtime::Type type;
 core::tags::constraints::Strings present;
 bool observationComplete=false;
};
struct ConstraintInternalTagSource {ConstraintFactStamp stamp;InternalTagSnapshot source;};
struct ConstraintFactInputs {
  std::optional<ConstraintCategorySource> categories;
  std::optional<ConstraintPeerSetSource> peerSets;
  std::optional<ConstraintInternalTagSource> internalTags;
  std::optional<std::string> stockTagLocale; // Normal owner startup source, never caller authority.
  ConstraintFactStamp stamp;
  ui::Presentation library;
  std::optional<ui::Details> details;
  std::optional<engine::ManagedDownloadStatus> managed;
  std::optional<OwnedI2pService::TorrentObservation> i2p;
  std::optional<engine::ExternalQueueSnapshot> queue;
  std::optional<ConstraintOriginalState> original;
};
class ConstraintFactSnapshot final {
public:
  const ConstraintFactStamp& stamp()const noexcept{return stamp_;}
  const core::tags::constraints::Facts& facts()const noexcept{return facts_;}
  bool hasTagScope()const noexcept{return tagSnapshot_.valid();}
  bool hasConstraintMembershipCohort()const noexcept{return tagCohort_.valid();}
private:
  ConstraintFactStamp stamp_;
  std::optional<core::tags::runtime::Type> categoryTagType_,peerSetTagType_,internalTagType_;
  std::optional<InternalTagSnapshot> internalTagScope_;
  std::optional<std::string> stockTagLocale_;
  std::optional<core::tags::DownloadStateNames> downloadTagNames_;
  std::optional<std::array<core::tags::runtime::Membership,15>> downloadTagMembers_;
  core::tags::constraints::Facts facts_;
  ConstraintMembershipCohort tagCohort_;
  storage::ManualTagMembershipPlan tagPlan_;
  storage::ManualTagSnapshot tagSnapshot_; // Owning preimage, never a lease/grant.
  friend core::tags::constraints::Result<ConstraintFactSnapshot> captureConstraintFacts(
      const ConstraintFactStamp&,const ConstraintFactInputs&);
  friend core::tags::constraints::Result<ConstraintFactSnapshot> captureConstraintTagMembership(
      ConstraintFactSnapshot,const ConstraintFactStamp&,const storage::ManualTagStore&,
      const storage::ProfileLease&,const storage::ManualTagSnapshot&,
      std::int32_t,ui::Presentation,std::int64_t,const storage::ManualTagMembershipPlan*,const ConstraintMembershipCohort*);
  friend core::tags::constraints::Result<ConstraintFactSnapshot> captureConstraintNamedTagMetrics(
      ConstraintFactSnapshot,const ConstraintFactStamp&,const storage::ManualTagStore&,
      const storage::ProfileLease&,const storage::ManualTagSnapshot&,
      std::span<const core::tags::constraints::NamedTagRequest>,std::int64_t,
      const ConstraintNamedMetricCensus*);
  friend core::tags::constraints::Result<core::tags::constraints::Value> evaluateCurrentConstraint(
      const core::tags::constraints::Expression&,const ConstraintFactStamp&,
      const ConstraintFactSnapshot&);
  friend core::tags::constraints::Result<core::tags::constraints::Value> evaluateCurrentConstraint(
      const core::tags::constraints::Expression&,const ConstraintFactStamp&,
      const ConstraintFactSnapshot&,const storage::ManualTagStore&,
      const storage::ProfileLease&,const storage::ManualTagSnapshot&);
};
// All supplied optional records must be captured in the SAME existing worker
// turn. Every explicit identity/generation is checked. Missing records remain
// missing facts. No translated state parsing, live getters, timers or effects.
core::tags::constraints::Result<ConstraintFactSnapshot> captureConstraintFacts(
    const ConstraintFactStamp& current,const ConstraintFactInputs&);
// Re-check after queued delivery/other work, immediately before evaluation.
// A stale snapshot returns Unavailable, preserving existing membership.
core::tags::constraints::Result<core::tags::constraints::Value> evaluateCurrentConstraint(
    const core::tags::constraints::Expression&,const ConstraintFactStamp& current,
    const ConstraintFactSnapshot&);
// Source-bound immutable registry from actual manual tags. Same target/turn/
// preimage scopes can reuse metadata; selected-own age is always derived anew.
// Unknown registry
// absence is not false. Selected-own tag_age uses original aligned o/p.a.
// wallMilliseconds must be sampled by the same serialized worker turn.
core::tags::constraints::Result<ConstraintFactSnapshot> captureConstraintTagMembership(
    ConstraintFactSnapshot,const ConstraintFactStamp&,const storage::ManualTagStore&,
      const storage::ProfileLease&,const storage::ManualTagSnapshot&,
    std::int32_t selectedTag,ui::Presentation currentLibrary,std::int64_t wallMilliseconds,
    const storage::ManualTagMembershipPlan* plan=nullptr,const ConstraintMembershipCohort* cohort=nullptr);
// Ordinary current source only. Virtual new-only plans have no census rights.
core::tags::constraints::Result<ConstraintFactSnapshot> captureConstraintNamedTagMetrics(
    ConstraintFactSnapshot,const ConstraintFactStamp&,const storage::ManualTagStore&,
    const storage::ProfileLease&,const storage::ManualTagSnapshot&,
    std::span<const core::tags::constraints::NamedTagRequest>,std::int64_t wallMilliseconds,
    const ConstraintNamedMetricCensus* census=nullptr);
// A snapshot containing tag facts cannot use the old overload: the actual
// current store generation and physical captured source must be supplied.
core::tags::constraints::Result<core::tags::constraints::Value> evaluateCurrentConstraint(
    const core::tags::constraints::Expression&,const ConstraintFactStamp&,
    const ConstraintFactSnapshot&,const storage::ManualTagStore&,
    const storage::ProfileLease&,const storage::ManualTagSnapshot& currentTags);
} // namespace biglybtc::app
