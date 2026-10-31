// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "managed_payload_claim.hpp"
#include "../adapters/queue_positions.hpp"
#include "biglybtc/queue_policy.hpp"
#include "biglybtc/effective_rate_limits.hpp"
#include <functional>
#include <limits>
#include <vector>
#include <optional>
#include <utility>

namespace biglybtc::engine {
// Observations only. A copied binding cannot create a handle or source permit.
struct QueueBinding {
  core::TorrentHash hash;
  std::uint64_t admission=0;
  bool operator==(const QueueBinding&)const=default;
};
struct ExternalQueueRow {
  QueueBinding binding;
  core::QueueTorrentFacts facts;
  storage::LibraryPositionObservation position{};
  std::optional<storage::LibraryGroupTransition> transition;
  std::uint64_t transitionOrder=0;
  bool checked=false;
};
struct ExternalQueueSnapshot {
  std::uint64_t sequence=0;
  std::vector<ExternalQueueRow> rows;
  core::QueueContext context;
};
struct ExternalQueueDecision {QueueBinding binding;core::QueueDecision decision;};
struct RetainedPayloads {
  std::vector<detail::ManagedPayloadClaim> claims;
  std::vector<storage::OriginalTorrentIdentity> identities;
};
using SourceMutation=std::function<std::expected<void,storage::Error>()>;
using TargetPublication=std::function<std::expected<void,storage::Error>(
    const core::TorrentHash&,const SourceMutation&)>;
using PositionsPublication=std::function<std::expected<void,storage::Error>(const SourceMutation&)>;
// Original absent downloaded/uploaded is0. Invalid negative or overflow is
// unavailable; these display totals never certify a disk checkpoint.
inline std::optional<std::int64_t> cumulativePayload(std::optional<std::int64_t> saved,std::int64_t delta)noexcept{
  const auto prefix=saved.value_or(0);
  if(prefix<0||delta<0||delta>std::numeric_limits<std::int64_t>::max()-prefix){return {};}
  return prefix+delta;
}
// Only actual rate caps are accepted; Unavailable is never translated to0
// (LT's unlimited value). Connection/proxy fields are observations, not effects.
inline bool validExternalRateLimits(const core::SessionRateLimits& v)noexcept{
  return core::validSessionRateLimits(v)
      &&v.download.state!=core::RateLimitState::Unavailable
      &&v.upload.state!=core::RateLimitState::Unavailable;
}
}
