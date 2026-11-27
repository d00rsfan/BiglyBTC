// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_archive_tags.hpp"
#include "biglybtc/saved_download_tag_edits.hpp"
#include "biglybtc/stock_tag_names.hpp"
#include "state_file_io.hpp"

#include <algorithm>
#include <new>
#include <set>

namespace biglybtc::storage::detail {
namespace {
constexpr char TagConfiguration[] = "tag.config";

void require(bool condition, ErrorCode code, const char* message) {
  if (!condition) { throw Error{code, message}; }
}

void requireAbsent(const ProfileLease& lease, const char* leaf) {
  const auto candidate = stateStampAt(lease.directoryHandle(), leaf);
  if (!candidate) { throw candidate.error(); }
  require(!*candidate, ErrorCode::Conflict, "Resolve the original tag configuration before restoring archive tags");
}

// Only used with text from the generated original locale table.
std::string utf8(std::u16string_view text) {
  std::string output;
  for (std::size_t index = 0; index < text.size(); ++index) {
    std::uint32_t code = text[index];
    if (code >= 0xd800 && code <= 0xdbff && index + 1 < text.size()) {
      code = 0x10000 + ((code - 0xd800) << 10) + (static_cast<std::uint32_t>(text[++index]) - 0xdc00);
    }
    if (code < 0x80) {
      output += static_cast<char>(code);
    } else if (code < 0x800) {
      output += static_cast<char>(0xc0 | (code >> 6));
      output += static_cast<char>(0x80 | (code & 0x3f));
    } else {
      if (code >= 0x10000) {
        output += static_cast<char>(0xf0 | (code >> 18));
        output += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
      } else {
        output += static_cast<char>(0xe0 | (code >> 12));
      }
      output += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
      output += static_cast<char>(0x80 | (code & 0x3f));
    }
  }
  return output;
}

std::string displayName(const core::SavedDownloadTag& tag, const std::optional<std::string>& locale) {
  if (!tag.rawName) { return {}; }
  if (tag.nameMayRequireLocalization && std::ranges::all_of(*tag.rawName, [](unsigned char ch) { return ch < 0x80; })) {
    const std::u16string key(tag.rawName->begin(), tag.rawName->end());
    if (const auto display = core::tags::stockTagName(locale.value_or(""), key)) { return utf8(*display); }
  }
  return *tag.rawName;
}

bool automaticAdded(const core::Node& record) {
  const auto* settings = record.find("c");
  if (!settings) { return false; }
  const auto* tracker = settings->find("pp.trackers");
  const auto* untagged = settings->find("pp.untagged");
  const auto* constraint = settings->find("pp.constraint");
  const auto* enabled = settings->find("pp.enabled.constraint");
  if ((tracker && tracker->type == core::Node::Type::List && !tracker->children.empty())
      || (untagged && untagged->type == core::Node::Type::Integer && untagged->integer == 1)) { return true; }
  if (!constraint || constraint->type != core::Node::Type::List || constraint->children.empty()
      || (enabled && enabled->type == core::Node::Type::Integer && enabled->integer != 1)) { return false; }
  const auto& expression = constraint->children.front();
  if (expression.type != core::Node::Type::Bytes || std::ranges::all_of(expression.bytes,
      [](char ch) { return static_cast<unsigned char>(ch) <= 0x20; })) { return false; }
  if (constraint->children.size() > 1) {
    const auto& options = constraint->children[1];
    if (options.type == core::Node::Type::Bytes && !options.bytes.contains("am=1;")
        && (options.bytes.contains("am=2;") || options.bytes.contains("am=3;"))) { return false; }
  }
  return true;
}
} // namespace

std::expected<std::optional<ArchiveTagRestore>, Error> prepareArchiveTagRestore(const ProfileLease& lease,
    const TagManagerStartupMode& mode, const core::TorrentHash& hash, std::span<const std::string> names,
    std::int64_t addedSeconds, std::stop_token stop) try {
  require(mode.matches(lease), ErrorCode::Conflict, "Archive tag startup settings belong to another profile");
  if (!mode.enabled() || names.empty()) { return std::nullopt; }
  require(names.size() <= 4096 && addedSeconds > 0, ErrorCode::InvalidFormat, "Invalid archived manual tag list");
  auto access = lease.sourceAccess(false, stop);
  if (!access) { return std::unexpected(access.error()); }
  requireAbsent(lease, "tag.config.saving");
  auto config = ConfigFile::load(lease, TagConfiguration, core::Limits{});
  if (!config && config.error().code != ErrorCode::Missing) { return std::unexpected(config.error()); }
  if (!config) {
    requireAbsent(lease, "tag.config");
    requireAbsent(lease, "tag.config.bak");
  }
  require(!config || config->recoverySource() == RecoverySource::Primary, ErrorCode::Conflict,
      "Resolve manual tag recovery before restoring archive tags");
  const auto original = config ? config->document().originalBytes() : std::string("de");
  auto current = original;
  const std::set<std::string_view> distinct(names.begin(), names.end());
  const auto& locale = mode.stockTagLocale();
  std::optional<std::int32_t> allocationCounter;
  for (const auto name : distinct) {
    require(!stop.stop_requested(), ErrorCode::Cancelled, "Archive tag restoration cancelled");
    auto parsed = core::readSavedDownloadTags(current);
    require(parsed && parsed->issues.empty(), ErrorCode::InvalidFormat, "Original manual tag index is incomplete");
    const auto found = std::ranges::find_if(parsed->tags, [&](const auto& tag) { return tag.rawName && displayName(tag, locale) == name; });
    std::optional<core::SavedTagEdit> edit;
    if (found == parsed->tags.end()) {
      const auto created = core::editSavedDownloadTags(current, core::SavedTagCreate{std::string(name), allocationCounter});
      require(created && created->createdId && created->allocationCounter, ErrorCode::InvalidFormat,
          "Cannot create the original archived tag");
      allocationCounter = created->allocationCounter;
      current = created->value.source->originalBytes();
      edit = core::SavedTagAddMember{*created->createdId, hash, addedSeconds};
    } else {
      require(found->id.has_value(), ErrorCode::InvalidFormat, "Original manual tag has no identity");
      const auto& record = parsed->source->find("3")->children.at(found->sourceIndex * 2 + 1);
      if (automaticAdded(record) || !core::ordinarySavedTagMembershipEffects(*parsed, record)
          || std::ranges::find(found->uniqueHashes, hash) != found->uniqueHashes.end()) { continue; }
      edit = core::SavedTagAddMember{*found->id, hash, addedSeconds};
    }
    auto updated = core::editSavedDownloadTags(current, *edit);
    require(updated.has_value(), ErrorCode::InvalidFormat, "Cannot restore an original archived tag");
    current = updated->value.source->originalBytes();
  }
  if (current == original) { return std::nullopt; }
  return ArchiveTagRestore{config ? std::optional<ConfigFile>{std::move(*config)} : std::nullopt, std::move(current)};
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::bad_alloc&) {
  return std::unexpected(Error{ErrorCode::Io, "Insufficient memory to restore archived tags"});
}

} // namespace biglybtc::storage::detail
