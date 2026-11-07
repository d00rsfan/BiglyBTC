// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "magnet_intake.hpp"
#include <stop_token>
#include <memory>
namespace biglybtc::search {class MetasearchMagnetAdmission;}
namespace biglybtc::engine {
// Pure inspection only: no destination, lease, persistent intent or network.
std::expected<PendingMagnet,storage::Error> inspectPublicMagnet(std::string_view,const NetworkPolicy&);

// Closed source-bound boundary. No caller can construct one from a revision,
// URL or boolean; only the actual retained-result admission owner is a friend.
class PendingMagnetCommitBoundary final {
private:
  friend class search::MetasearchMagnetAdmission;
  friend struct PendingMagnetCommitAccess;
  PendingMagnetCommitBoundary(std::shared_ptr<const storage::ProfileLease>,
      std::uint64_t,std::stop_token,std::stop_token,std::stop_token);
  std::shared_ptr<const storage::ProfileLease> lease_;
  std::uint64_t revision_;
  std::stop_token query_,receiver_,dispatch_;
};
enum class PendingMagnetCommitState {Rejected,Committed,Uncertain};
struct PendingMagnetCommitResult {
  PendingMagnetCommitState state=PendingMagnetCommitState::Rejected;
  std::optional<PendingMagnet> pending;
  std::optional<storage::Error> failure;
};
PendingMagnetCommitResult queueSourceBoundPendingMagnet(storage::ProfileLease&,
    const PendingMagnet&,const NetworkPolicy&,const PendingMagnetCommitBoundary&);

} // namespace biglybtc::engine
