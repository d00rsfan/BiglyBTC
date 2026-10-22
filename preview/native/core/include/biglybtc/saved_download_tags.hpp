#pragma once
#include "biglybtc/bencode.hpp"
#include "biglybtc/library.hpp"

#include <limits>
#include <optional>

namespace biglybtc::core {
enum class SavedTagError { Format, Root, ManualType, Limit, Memory };
enum class SavedTagIssueCode { InvalidId, DuplicateId, Type, InvalidMember, DuplicateMember };
inline constexpr std::size_t NoSavedTagIndex = std::numeric_limits<std::size_t>::max();
struct SavedTagIssue {
  SavedTagIssueCode code;
  std::size_t tagIndex, memberIndex = NoSavedTagIndex;
};
struct SavedTagMember {
  std::size_t sourceIndex = 0;
  std::optional<std::string> rawId;
  // A normal uppercase Base32 identity as emitted by getTaggableID(). This is
  // a declared saved hash, NOT evidence that a live DownloadManager resolved it.
  std::optional<TorrentHash> hash;
  bool duplicate = false;
};
struct SavedDownloadTag {
  std::size_t sourceIndex = 0;
  std::string idKey;
  std::optional<std::int32_t> id;
  std::optional<std::int64_t> uid;
  // Exact n bytes. Missing/wrong type stays unknown, never an invented name.
  // tag.* may be a localization key; no MessageText initialization is performed.
  std::optional<std::string> rawName;
  bool nameMayRequireLocalization = false, duplicateId = false;
  // TagBase reads record c/vis as a full Long: exactly1 means visible.
  // Missing or malformed input falls back to getVisibleDefault()==true.
  std::optional<std::int64_t> rawVisibility;
  bool visible = true, visibilityDefaulted = true, visibilityMalformed = false;
  bool membersPresent = false, membersMalformed = false;
  std::vector<SavedTagMember> members;
  std::vector<TorrentHash> uniqueHashes;
};
struct SavedDownloadTags {
  std::shared_ptr<const Document> source;
  bool manualTypePresent = false;
  std::vector<SavedDownloadTag> tags;
  std::vector<SavedTagIssue> issues;
};
struct SavedTagLimits {
  Limits document{};
  std::size_t tags = 4096, members = 65536, fieldBytes = 4096;
  std::size_t textBytes = 1024 * 1024, issues = 4096;
};
// Fixed persisted manual-download type3 only. Raw input is parsed into a fresh
// owned document under normal4MiB/100k/depth64 limits; every quota is lower-only.
// Preflight selected row/text/member caps precedes projection allocation.
// Unknown types/fields remain in source; no state-file reads, rules, live joins,
// automatic tags, localization, managers, rates, activation or writeback.
std::expected<SavedDownloadTags, SavedTagError> readSavedDownloadTags(
    std::string_view encoded, SavedTagLimits limits = {});
} // namespace biglybtc::core
