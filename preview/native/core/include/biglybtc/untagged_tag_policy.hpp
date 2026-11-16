// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <biglybtc/tag_runtime_registry.hpp>
#include <span>
namespace biglybtc::core::tags::untagged {
// Current manual-tag witnesses for one observed subject, not computed rule
// truth or event/effect authority. Unknown role is explicit, not ordinary.
struct Entry {std::optional<bool> isUntagged;runtime::Membership member=runtime::Membership::Unknown;};
enum class Error {Bounds};
std::expected<runtime::Truth,Error> otherManualTags(std::span<const Entry>,bool completeCensus=false)noexcept;
enum class Membership {Keep,Add,Remove};
// Reconcile an actual current persistent subject from current membership, not
// accumulated listener counts. Nonpersistent subjects get no inferred addition.
// The real owner must separately bind source, target, journal and Store CAS.
Membership project(runtime::Truth other,bool persistent,bool member)noexcept;
}
