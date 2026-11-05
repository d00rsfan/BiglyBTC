// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/tag_constraints.hpp"
#include "library_model.hpp"
#include "managed_download.hpp"
#include "coexist_queue.hpp"
#include "owned_i2p_service.hpp"
#include "torrent_state.hpp"

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

struct ConstraintFactInputs {
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
private:
  ConstraintFactStamp stamp_;
  core::tags::constraints::Facts facts_;
  friend core::tags::constraints::Result<ConstraintFactSnapshot> captureConstraintFacts(
      const ConstraintFactStamp&,const ConstraintFactInputs&);
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
} // namespace biglybtc::app
