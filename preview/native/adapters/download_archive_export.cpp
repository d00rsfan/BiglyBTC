// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_archive_export.hpp"
#include "original_filename_policy.hpp"
#include "original_identity_digest.hpp"
#include "original_storage_defaults.hpp"
#include "torrent_state.hpp"
#include "../platform/utf8_path.hpp"
#include "biglybtc/original_file_links.hpp"
#include "biglybtc/queue_observations.hpp"
#include "biglybtc/torrent_intake_options.hpp"

#include <algorithm>
#include <new>
#include <set>

namespace biglybtc::storage::detail {
namespace {
using Node = core::Node;
namespace fs = std::filesystem;

void require(bool condition, ErrorCode code, const char* message) {
  if (!condition) { throw Error{code, message}; }
}

template<class T>
T take(std::expected<T, Error> result) {
  if (!result) { throw std::move(result.error()); }
  return std::move(*result);
}

std::string bytes(std::string_view value) {
  return std::to_string(value.size()) + ':' + std::string(value);
}

std::string integer(std::int64_t value) {
  return 'i' + std::to_string(value) + 'e';
}

std::optional<std::string_view> text(const Node& map, const char* key) {
  const auto* node = map.find(key);
  if (!node) { return {}; }
  require(node->type == Node::Type::Bytes && node->bytes.find('\0') == std::string_view::npos,
      ErrorCode::InvalidFormat, "Invalid archived download text field");
  return node->bytes;
}

std::int64_t number(const Node* map, const char* key) {
  const auto* node = map ? map->find(key) : nullptr;
  if (!node) { return 0; }
  require(node->type == Node::Type::Integer, ErrorCode::InvalidFormat, "Invalid archived download numeric field");
  return node->integer;
}

const Node& field(const Node& map, const char* key, Node::Type type) {
  const auto* node = map.find(key);
  require(node && node->type == type, ErrorCode::InvalidFormat, "Archived download field is absent or invalid");
  return *node;
}

void safePath(const fs::path& path) {
  require(path.is_absolute() && path.has_filename() && path == path.lexically_normal(), ErrorCode::UnsafePath,
      "Archived save location or file link is not an absolute normalized file path");
}

std::string fileRecord(const fs::path& location, const fs::path& file, std::int64_t length) {
  const auto relative = file.lexically_relative(location);
  if (!relative.empty() && relative != "." && *relative.begin() != "..") {
    return "d3:len" + integer(length) + "3:rel" + bytes(platform::pathToUtf8(relative)) + 'e';
  }
  return "d4:file" + bytes(platform::pathToUtf8(file)) + "3:len" + integer(length) + 'e';
}

bool containsPath(const fs::path& directory, const fs::path& path) {
  auto component = path.begin();
  for (const auto& part : directory) {
    if (component == path.end() || !sameFilesystemComponent(part, *component)) { return false; }
    ++component;
  }
  return true;
}

std::string archivedFiles(const Node& attributes, const core::Document& torrent, const compat::OriginalLayout& layout,
    const fs::path& location, const fs::path& profile, std::span<const compat::FilePolicy> policies, std::stop_token stop) {
  const auto& resume = field(torrent.root(), "resume", Node::Type::Dictionary);
  const auto& data = field(resume, "data", Node::Type::Dictionary);
  const auto& pieces = field(data, "resume data", Node::Type::Bytes);
  const auto pieceCount = layout.bytes / layout.pieceLength + (layout.bytes % layout.pieceLength != 0 ? 1 : 0);
  require(number(&data, "valid") == 1 && static_cast<std::uint64_t>(pieceCount) == pieces.bytes.size(),
      ErrorCode::Conflict, "Archive requires a verified, complete stopped download");

  std::vector<std::string> positions;
  positions.reserve(layout.files.size());
  for (const auto& file : layout.files) {
    positions.push_back(platform::pathToUtf8(layout.multifile ? location / platform::pathFromUtf8(file.path) : location));
  }
  const auto links = compat::resolveOriginalFileLinks(attributes.find("filelinks2"), positions);
  require(links.has_value(), ErrorCode::UnsafePath, "Archived file links cannot be resolved safely");
  std::string list = "l";
  bool hasWantedFile = false;
  const auto active = profile / "active";
  const auto archive = profile / "dlarchive";
  for (std::size_t index = 0; index < layout.files.size(); ++index) {
    require(!stop.stop_requested(), ErrorCode::Cancelled, "Archive preparation cancelled");
    const auto& file = layout.files[index];
    const auto target = platform::pathFromUtf8(links->byIndex[index] ? links->byIndex[index]->target : positions[index]);
    safePath(target);
    require(!containsPath(active, target) && !containsPath(archive, target), ErrorCode::UnsafePath,
        "Archived payload path overlaps torrent state files");
    const bool wanted = !policies[index].skipped;
    hasWantedFile |= wanted && !file.padding;
    if (wanted && !file.padding && file.bytes != 0) {
      const auto first = static_cast<std::size_t>(file.offset / layout.pieceLength);
      const auto last = static_cast<std::size_t>((file.offset + file.bytes - 1) / layout.pieceLength);
      require(last < pieces.bytes.size(), ErrorCode::InvalidFormat, "Archive file falls outside original piece geometry");
      const auto states = pieces.bytes.substr(first, last - first + 1);
      require(std::ranges::all_of(states, [](char value) { return value == 1; }), ErrorCode::Conflict,
          "Archive requires all selected files to be verified");
    }
    list += fileRecord(location, target, wanted ? file.bytes : -file.bytes);
  }
  require(hasWantedFile, ErrorCode::Conflict, "The download has no selected content to archive");
  return list + 'e';
}
} // namespace

std::expected<DownloadArchiveExportRecord, Error> prepareArchiveExportRecord(const ProfileLease& lease,
    const core::TorrentHash& hash, std::int64_t archivedMillis, std::span<const std::string> manualTags,
    std::stop_token stop) try {
  require(!stop.stop_requested(), ErrorCode::Cancelled, "Archive preparation cancelled");
  auto access = take(lease.sourceAccess(false, stop));
  require(archivedMillis > 0 && manualTags.size() <= 100000, ErrorCode::InvalidFormat, "Invalid download archive metadata");
  const auto config = take(ConfigFile::load(lease, "downloads.config"));
  require(config.recoverySource() == RecoverySource::Primary, ErrorCode::Conflict, "Resolve library recovery before archiving");
  const auto& inventory = config.document();
  const auto saved = core::readLibrary(inventory);
  require(saved.has_value(), ErrorCode::InvalidFormat, "Invalid original library inventory");
  const auto matches = [&](const core::SavedDownload& candidate) { return candidate.hash == hash; };
  const auto selected = std::ranges::find_if(saved->downloads, matches);
  require(selected != saved->downloads.end(), ErrorCode::Missing, "Selected torrent is absent from the library");
  require(std::ranges::count_if(saved->downloads, matches) == 1 && selected->persistent
      && selected->startup == core::StartupState::Stopped && !selected->forceStart, ErrorCode::Conflict,
      "Archive requires one stopped persistent download");
  const auto& originalRow = saved->legacyMap ? inventory.root().children.at(selected->sourceIndex * 2 + 1)
      : inventory.find("downloads")->children.at(selected->sourceIndex);
  auto stateStore = take(TorrentStateStore::open(lease));
  const auto state = take(stateStore.loadAttributes(lease, hash));
  require(!state.recoveredBackup(), ErrorCode::Conflict, "Resolve original torrent state recovery before archiving");
  const auto& document = state.document();
  const auto identity = take(digestOriginalTorrentIdentity(document));
  const auto validated = validateOriginalTorrentKey(identity, hash);
  if (!validated) { return std::unexpected(validated.error()); }
  const auto& attributes = field(document.root(), "attributes", Node::Type::Dictionary);
  const auto flags = core::originalDownloadFlags(&attributes);
  // Download.FLAG_LOW_NOISE and Download.FLAG_METADATA_DOWNLOAD.
  constexpr auto archiveExcludedFlags = 0x10 | 0x200;
  require(flags && (*flags & archiveExcludedFlags) == 0, ErrorCode::Conflict, "This saved download cannot be archived");
  const auto layout = take(readOriginalLayout(lease, document));
  const auto mapping = take(resolveOriginalSaveRoot(*selected, layout));
  const auto location = platform::pathFromUtf8(mapping.saveDirectory) / platform::pathFromUtf8(*mapping.saveFile);
  safePath(location);
  const auto types = take(resolveOriginalStorageTypes(lease, hash, layout, attributes));
  const auto policies = take(readOriginalFilePolicies(originalRow, types.requested));
  const auto profile = take(stateDirectoryPath(lease.directoryHandle()));
  auto files = archivedFiles(attributes, document, layout, location, profile, policies.restored, stop);

  const auto rowBytes = std::string_view(inventory.originalBytes()).substr(originalRow.begin, originalRow.end - originalRow.begin);
  auto archivedRow = core::Document::parse(std::string(rowBytes), compatibleConfigLimits());
  require(archivedRow && archivedRow->eraseValue("position").has_value(), ErrorCode::InvalidFormat,
      "Cannot encode original archived global-manager row");
  auto name = text(attributes, "displayname").value_or(layout.name);
  if (name.empty()) { name = layout.name; }
  auto completeTime = number(&attributes, "complt");
  if (!completeTime) { completeTime = number(attributes.find("parameters"), "stats.download.completed.time"); }
  const auto ratio = core::sourceQueueShareRatio(selected->downloaded.value_or(0), selected->uploaded.value_or(0),
      selected->discarded.value_or(0), selected->hashFailed.value_or(0));
  const auto rawHash = hash.bytes();
  auto stub = "d2:dt" + integer(archivedMillis) + "5:files" + std::move(files)
      + "2:gm" + archivedRow->serialize() + "4:hash" + bytes({reinterpret_cast<const char*>(rawHash.data()), rawHash.size()})
      + "1:l" + bytes(platform::pathToUtf8(location)) + "4:name" + bytes(name) + "1:s" + integer(layout.bytes);
  if (completeTime > 0) { stub += "2:dc" + integer(completeTime); }
  if (ratio >= 0) { stub += "2:sr" + integer(ratio); }
  if (const auto category = text(attributes, "category"); category && !category->empty()) { stub += "3:cat" + bytes(*category); }
  if (const auto tracker = text(document.root(), "announce"); tracker && !tracker->empty() && !tracker->starts_with("dht:")) {
    stub += "2:tr" + bytes(*tracker);
  }
  const std::set<std::string_view> tags(manualTags.begin(), manualTags.end());
  if (!tags.empty()) {
    stub += "1:tl";
    for (const auto tag : tags) { stub += bytes(tag); }
    stub += 'e';
  }
  stub += 'e';
  require(core::Document::parse(stub, compatibleConfigLimits()).has_value(), ErrorCode::InvalidFormat,
      "Archived download record exceeds original format limits");
  return DownloadArchiveExportRecord{identity, selected->sourceIndex, std::move(stub), document.serialize()};
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::bad_alloc&) {
  return std::unexpected(Error{ErrorCode::Io, "Insufficient memory to prepare a download archive"});
} catch (const fs::filesystem_error&) {
  return std::unexpected(Error{ErrorCode::UnsafePath, "Invalid archived download file path"});
}

} // namespace biglybtc::storage::detail
