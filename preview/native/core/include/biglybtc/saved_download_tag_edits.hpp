// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/saved_download_tags.hpp"
#include "tag_rate_limits.hpp"
#include <variant>
#include <array>

namespace biglybtc::core {
enum class SavedTagEditError {
  Format, Root, ManualType, Limit, Memory, AmbiguousId, MissingTag,
  MalformedRecord, MalformedConfiguration, MalformedMembership,
  InvalidName, InvalidMember, InvalidCounter, IdExhausted, InvalidConstraint, UnsupportedGroupSettings
};
// Passive raw-source policy only, not a group registry or membership grant.
// Ungrouped rows are independent. Grouped effects require exact ordinary
// affected-group settings; unknown/exclusive/colour/assign-root refuse.
bool ordinarySavedTagGroup(const SavedDownloadTags&,const Node& record)noexcept;
bool ordinarySavedTagGroup(const SavedDownloadTags&,std::optional<std::string_view> group)noexcept;
// Known supported membership configuration only. This value check never grants
// effects; the real Store repeats it from fresh source before a changed save.
// Audited display-only TagBase attributes, with original persisted node types.
// This never loads an image/file or enables any tag action/property.
bool savedTagDisplayMetadata(std::string_view key,const Node& value)noexcept;
// Known integer lifecycle configuration is data;active effects still need their owner.
bool savedTagLifecycleProperty(std::string_view key,const Node& value)noexcept;
// Exact bounded ASCII property list. Value-only; no event or effect authority.
bool supportedSavedTrackerTagNames(const Node& value)noexcept;
bool ordinarySavedTagMembershipEffects(const SavedDownloadTags&,const Node& record)noexcept;
// Value policy only;the private actual lifecycle owner must handle every action.
bool lifecycleSavedTagMembershipEffects(const SavedDownloadTags&,const Node& record)noexcept;
// Value schema only;actual source/SDK preparation is required by private writers.
bool rateReadyLifecycleSavedTagMembershipEffects(const SavedDownloadTags&,const Node& record)noexcept;
struct SavedTagCreate {
  std::string name;
  // Absent: exact original reload counter=max(0,id+1), then increment.
  // Present: caller's retained counter for consecutive creates in one session.
  // This is value-only allocator state, never a live tag/manager capability.
  std::optional<std::int32_t> counter;
};
struct SavedTagRename { std::int32_t id; std::string name; };
struct SavedTagRemove { std::int32_t id; };
// Original setter: null erases c/gr; empty is a distinct saved string.
// Exact typed property setter: null removes pp.trackers; [] remains explicit.
// Saves configuration only; actual property event/executor is separate.
// Null removes the property; only exact Long1 reads true in the original.
struct SavedTagSetUntagged {std::int32_t id;std::optional<bool> enabled;};
struct SavedTagSetTrackerNames {std::int32_t id;std::optional<std::vector<std::string>> names;};
struct SavedTagSetGroup { std::int32_t id; std::optional<std::string> group; };
using SavedTagRgb=std::array<std::int32_t,3>;
// cb8 manual-download type default. Effective inheritance is not inferred here.
inline constexpr SavedTagRgb ManualTagDefaultRgb{0,140,66};
// Missing => null/default. Bounded ASCII Java int/split forms are decoded;
// malformed, duplicate or unqualified Unicode numeric input is preserved/refused.
std::expected<std::optional<SavedTagRgb>,SavedTagEditError> readSavedTagColour(const Node& record)noexcept;
struct SavedTagSetColour { std::int32_t id; std::optional<SavedTagRgb> colour; };

struct SavedTagVisibility { std::int32_t id; bool visible; };
struct SavedTagSetAssignmentAction {std::int32_t id;std::uint32_t action;bool enabled;};
// Exact original setter no-op on effective saved value;positive SDK subset only.
struct SavedTagSetRateLimit {std::int32_t id;bool upload;std::int32_t bytesPerSecond;};
struct SavedTagAddMember {
  std::int32_t id;
  TorrentHash hash;
  // Exact original p[index]["a"] signed seconds. No clock is read here.
  std::int64_t addedTimeSeconds;
};
struct SavedTagRemoveMember { std::int32_t id; TorrentHash hash; };
// One original o/p update for a user-selected group; order is retained.
struct SavedTagMemberBatch {std::int32_t id;std::vector<TorrentHash> hashes;bool add=true;std::int64_t addedTimeSeconds=0;};
// Only known property slots0/1 and enabled are edited. Unknown tail entries,
// other configuration, raw members and their per-member properties survive.
// Text is configuration, not a compiled expression or membership authority.
struct SavedTagSetConstraint {
  std::int32_t id;
  std::string expression, options;
  bool enabled=true;
};
using SavedTagEdit = std::variant<SavedTagCreate, SavedTagRename, SavedTagRemove,
    SavedTagVisibility, SavedTagAddMember, SavedTagRemoveMember, SavedTagSetConstraint, SavedTagSetGroup, SavedTagSetColour, SavedTagMemberBatch, SavedTagSetTrackerNames, SavedTagSetUntagged, SavedTagSetAssignmentAction, SavedTagSetRateLimit>;
struct SavedTagEditResult {
  SavedDownloadTags value;
  bool changed = false;
  std::optional<std::int32_t> createdId, allocationCounter;
  std::optional<std::int64_t> createdUid;
};
// Parse fresh bounded input, edit persisted manual-download type3 only, then
// reparse the whole result under the same lower-only quotas. All unedited keys,
// subtrees, ordering and raw member IDs remain byte-exact. No file or manager
// operations, resolver, live membership, rules/actions, clock or publication.
// Canonical nonnegative int32 IDs required; aliases/overflow/duplicate IDs
// refuse. Names must be bounded UTF-8 (empty/duplicate names remain allowed).
// Member edits preserve o/p index alignment; ambiguous properties/duplicates
// refuse, while unrelated unknown raw member IDs/properties remain untouched.
std::expected<SavedTagEditResult, SavedTagEditError> editSavedDownloadTags(
    std::string_view pristineBytes, const SavedTagEdit&, SavedTagLimits limits = {});
} // namespace biglybtc::core
