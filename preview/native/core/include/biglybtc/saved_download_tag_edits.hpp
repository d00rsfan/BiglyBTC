// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/saved_download_tags.hpp"
#include <variant>

namespace biglybtc::core {
enum class SavedTagEditError {
  Format, Root, ManualType, Limit, Memory, AmbiguousId, MissingTag,
  MalformedRecord, MalformedConfiguration, MalformedMembership,
  InvalidName, InvalidMember, InvalidCounter, IdExhausted
};
struct SavedTagCreate {
  std::string name;
  // Absent: exact original reload counter=max(0,id+1), then increment.
  // Present: caller's retained counter for consecutive creates in one session.
  // This is value-only allocator state, never a live tag/manager capability.
  std::optional<std::int32_t> counter;
};
struct SavedTagRename { std::int32_t id; std::string name; };
struct SavedTagRemove { std::int32_t id; };
struct SavedTagVisibility { std::int32_t id; bool visible; };
struct SavedTagAddMember {
  std::int32_t id;
  TorrentHash hash;
  // Exact original p[index]["a"] signed seconds. No clock is read here.
  std::int64_t addedTimeSeconds;
};
struct SavedTagRemoveMember { std::int32_t id; TorrentHash hash; };
using SavedTagEdit = std::variant<SavedTagCreate, SavedTagRename, SavedTagRemove,
    SavedTagVisibility, SavedTagAddMember, SavedTagRemoveMember>;
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
