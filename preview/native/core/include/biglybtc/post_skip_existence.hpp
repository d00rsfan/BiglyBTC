// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/compact_file_requirement.hpp"
#include <optional>

namespace biglybtc::compat {
enum class PostSkipExistenceError { Shape,Geometry,Order,Resume,Limit,Memory };
struct PostSkipExistencePlan {
  std::vector<std::uint8_t> interesting,existenceProbes,allocationRequests,compactRemovals;
  bool operator==(const PostSkipExistencePlan&)const=default;
};
// Current complete post-action facts,not raw saved priority hints. Kinds are
// Original L=1,C=2,R=3,X=4. Resume validity is deliberately not an input:the
// Source predicate tests any nonzero saved flag for noncompact targets.
// Pure intentions only;no allocation/delete/source freshness or IO authority.
// Caller authenticates all current facts and complete geometry before effects.
std::expected<PostSkipExistencePlan,PostSkipExistenceError> originalPostSkipExistence(
    std::span<const OriginalFilePieceSpan>,std::span<const std::int32_t> kinds,
    std::span<const std::uint8_t> changed,std::span<const std::uint8_t> exists,
    bool skipped,bool constructed,
    std::optional<std::span<const std::uint8_t>> resume=std::nullopt);
}
