// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_archive_restore.hpp"
#include "encoded_document.hpp"
#include "original_identity_digest.hpp"
#include "profile_transaction_pending.hpp"
#include "state_directory_listing.hpp"
#include "../platform/utf8_path.hpp"
#include "biglybtc/state_cache.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <new>
#include <set>

namespace biglybtc::storage::detail {
namespace {
constexpr std::size_t MaximumAuxiliaryEntries = 1024;
constexpr std::size_t MaximumAuxiliaryBytes = 64 * 1024 * 1024;
constexpr std::size_t MaximumAuxiliaryDepth = 24;

void require(bool condition, ErrorCode code, const char* message) {
  if (!condition) { throw Error{code, message}; }
}

template<class T>
T take(std::expected<T, Error> value) {
  if (!value) { throw std::move(value.error()); }
  return std::move(*value);
}

void checkCancellation(std::stop_token stop) {
  require(!stop.stop_requested(), ErrorCode::Cancelled, "Archive restore preparation cancelled");
}

std::string_view raw(const core::Document& document, const core::Node& value) {
  return std::string_view(document.originalBytes()).substr(value.begin, value.end - value.begin);
}

core::Document readInventoryRow(const core::Document& catalog, const core::Node& stub,
    const core::TorrentHash& hash) {
  const auto* row = stub.find("gm");
  require(row && row->type == core::Node::Type::Dictionary, ErrorCode::InvalidFormat,
      "Archive entry has no original saved download");
  const auto encoded = raw(catalog, *row);
  const auto library = core::Document::parse("d9:downloadsl" + std::string(encoded) + "ee", compatibleConfigLimits());
  require(library.has_value(), ErrorCode::InvalidFormat, "Invalid original archived download row");
  const auto saved = core::readLibrary(*library);
  require(saved && saved->issues.empty() && saved->downloads.size() == 1, ErrorCode::InvalidFormat,
      "Invalid original archived download details");
  const auto& download = saved->downloads.front();
  require(download.hash == hash && download.persistent, ErrorCode::Conflict,
      "Archived download identity or persistence differs from its saved row");
  auto result = core::Document::parse(std::string(encoded), compatibleConfigLimits());
  require(result.has_value(), ErrorCode::InvalidFormat, "Invalid archived download dictionary");
  const auto* location = stub.find("l");
  if (location) {
    require(location->type == core::Node::Type::Bytes && location->bytes.find('\0') == std::string_view::npos,
        ErrorCode::InvalidFormat, "Invalid original archive save location");
    if (!location->bytes.empty()) {
      const auto path = platform::pathFromUtf8(location->bytes);
      require(path.is_absolute() && path.has_filename() && path == path.lexically_normal(),
          ErrorCode::UnsafePath, "Archive save location is not a normalized absolute path on this system");
      for (const auto& [key, component] : {std::pair{"save_dir", path.parent_path()}, {"save_file", path.filename()}}) {
        const auto bytes = platform::pathToUtf8(component);
        require(result->replaceValue(key, std::to_string(bytes.size()) + ':' + bytes).has_value(),
            ErrorCode::InvalidFormat, "Cannot retain the original archive save location");
      }
      if (download.legacySavePath) {
        require(result->eraseValue("path").has_value(), ErrorCode::InvalidFormat,
            "Cannot replace the original legacy archive save location");
      }
    }
  }
  return std::move(*result);
}

bool aliases(const OriginalTorrentIdentity& identity, const core::TorrentHash& candidate) {
  const auto bytes = candidate.bytes();
  if (candidate == identity.originalKey) { return true; }
  if (identity.full.v1 && std::ranges::equal(bytes, *identity.full.v1)) { return true; }
  return identity.full.v2 && std::ranges::equal(bytes, std::span(*identity.full.v2).first(bytes.size()));
}

void requireAbsent(StateHandle directory, const std::filesystem::path& leaf, const char* message) {
  require(!take(stateStampAt(directory, leaf)), ErrorCode::Conflict, message);
}

std::optional<ConfigFile> readInventory(const ProfileLease& lease) {
  const auto directory = lease.directoryHandle();
  requireAbsent(directory, "downloads.config.saving", "Resolve the pending library save before restoring an archive");
  auto file = ConfigFile::load(lease, "downloads.config");
  if (!file) {
    if (file.error().code != ErrorCode::Missing) { throw file.error(); }
    for (const auto* name : {"downloads.config", "downloads.config.bak"}) {
      requireAbsent(directory, name, "Original library recovery is incomplete");
    }
    return {};
  }
  require(file->recoverySource() == RecoverySource::Primary, ErrorCode::Conflict,
      "Resolve library recovery before restoring an archive");
  return std::move(*file);
}

void checkCache(StateHandle active, const OriginalTorrentIdentity& identity) {
  for (const auto* name : {"cache.dat.saving", "cache.dat.bak.saving"}) {
    requireAbsent(active, name, "Resolve the pending cache save before restoring an archive");
  }
  for (const auto* name : {"cache.dat", "cache.dat.bak"}) {
    auto bytes = take(readStateFile(active, name, compatibleConfigLimits().inputBytes));
    if (!bytes) { continue; }
    const auto document = EncodedDocument::parse(std::move(*bytes), compatibleConfigLimits());
    require(document.has_value(), ErrorCode::InvalidFormat, "Original state cache is unreadable");
    const auto entries = core::readStateCache(document->document());
    require(entries && entries->issues.empty(), ErrorCode::InvalidFormat, "Original state cache is incomplete");
    const auto conflict = [&](const auto& value) { return aliases(identity, value.second.hash); };
    require(std::ranges::none_of(entries->torrents, conflict), ErrorCode::Conflict,
        "The active state cache already contains the archived torrent or its alias");
  }
}

std::string addInventoryRow(core::Document inventory, core::Document row, const OriginalTorrentIdentity& identity) {
  const auto saved = core::readLibrary(inventory);
  require(saved && saved->downloads.size() < 100000, ErrorCode::InvalidFormat, "Invalid original library inventory");
  for (const auto& issue : saved->issues) {
    require(!issue.pauseEntry && saved->legacyMap, ErrorCode::InvalidFormat, "Invalid library or pause record");
    const auto& node = inventory.root().children.at(issue.sourceIndex * 2 + 1);
    require(!node.find("torrent") && !node.find("torrent_hash") && !node.find("save_dir") && !node.find("path"),
        ErrorCode::InvalidFormat, "Invalid legacy library record");
  }
  std::int64_t lastPosition = 0;
  std::set<std::string> identities;
  for (const auto& value : saved->downloads) {
    require(value.hash && identities.insert(value.hash->hex()).second && !aliases(identity, *value.hash),
        ErrorCode::Conflict, "The library contains a duplicate, missing, or conflicting torrent identity");
    lastPosition = std::max(lastPosition, value.position.value_or(0));
  }
  for (const auto& pause : saved->paused) {
    require(!aliases(identity, pause.hash), ErrorCode::Conflict, "The library already has a pause for the archived torrent");
  }
  require(lastPosition < std::numeric_limits<std::int32_t>::max(), ErrorCode::InvalidFormat,
      "No representable library position is available for the restored torrent");
  require(row.replaceValue("position", "i" + std::to_string(lastPosition + 1) + 'e').has_value(),
      ErrorCode::InvalidFormat, "Cannot save restored torrent position");
  if (saved->legacyMap) {
    const auto key = identity.originalKey.hex();
    require(!inventory.find(key) && inventory.replaceValue(key, row.serialize()).has_value(),
        ErrorCode::Conflict, "Cannot add the restored entry to the legacy library map");
  } else {
    auto downloads = std::string(raw(inventory, *inventory.find("downloads")));
    downloads.insert(downloads.size() - 1, row.serialize());
    require(inventory.replaceValue("downloads", std::move(downloads)).has_value(), ErrorCode::InvalidFormat,
        "Restored library inventory exceeds its limit");
  }
  return inventory.serialize();
}

std::string removeArchiveRow(core::Document catalog, std::size_t rowIndex) {
  const auto& stubs = *catalog.find("stubs");
  const auto& row = stubs.children.at(rowIndex);
  auto entries = std::string(raw(catalog, stubs));
  entries.erase(row.begin - stubs.begin, row.end - row.begin);
  require(catalog.replaceValue("stubs", std::move(entries)).has_value(), ErrorCode::InvalidFormat,
      "Cannot produce the updated original archive catalog");
  return catalog.serialize();
}

std::string addArchiveRow(core::Document catalog, std::string_view entry) {
  const auto* stubs = catalog.find("stubs");
  require(!stubs || (stubs->type == core::Node::Type::List && stubs->children.size() < 100000), ErrorCode::InvalidFormat,
      "Original download archive has no space for the selected row");
  auto entries = stubs ? std::string(raw(catalog, *stubs)) : std::string("le");
  entries.insert(entries.size() - 1, entry);
  require(catalog.replaceValue("stubs", std::move(entries)).has_value(), ErrorCode::InvalidFormat,
      "Cannot add the download to the original archive catalog");
  return catalog.serialize();
}

std::string removeInventoryRow(core::Document inventory, const core::TorrentHash& hash, std::size_t index) {
  const auto saved = core::readLibrary(inventory);
  require(saved && std::ranges::none_of(saved->paused, [&](const auto& pause) { return pause.hash == hash; }),
      ErrorCode::Conflict, "Resume or stop the original pause before archiving this download");
  const auto matches = [&](const auto& row) { return row.sourceIndex == index && row.hash == hash; };
  const auto selected = std::ranges::find_if(saved->downloads, matches);
  require(selected != saved->downloads.end(), ErrorCode::Conflict, "Original library selection changed before archiving");
  if (saved->legacyMap) {
    const auto removed = inventory.eraseValue(*selected->legacyKey);
    require(removed && *removed, ErrorCode::InvalidFormat, "Cannot retire the original legacy library row");
  } else {
    const auto& downloads = *inventory.find("downloads");
    const auto& row = downloads.children.at(index);
    auto entries = std::string(raw(inventory, downloads));
    entries.erase(row.begin - downloads.begin, row.end - row.begin);
    require(inventory.replaceValue("downloads", std::move(entries)).has_value(), ErrorCode::InvalidFormat,
        "Cannot retire the original library row");
  }
  return inventory.serialize();
}

std::optional<ArchiveExportCacheEdit> removeCacheRow(StateHandle active, const char* leaf,
    const OriginalTorrentIdentity& identity) {
  auto source = take(captureProfileFile(active, leaf, compatibleConfigLimits().inputBytes));
  if (!source.bytes) { return {}; }
  auto cache = EncodedDocument::parse(*source.bytes, compatibleConfigLimits());
  require(cache.has_value(), ErrorCode::InvalidFormat, "Original state cache is unreadable");
  const auto* entries = cache->document().find("state");
  if (!entries) { return {}; }
  require(entries->type == core::Node::Type::List && entries->children.size() <= 100000, ErrorCode::InvalidFormat,
      "Original state cache has an invalid entry list");
  std::optional<std::size_t> selected;
  for (std::size_t index = 0; index < entries->children.size(); ++index) {
    const auto* value = entries->children[index].find("hash");
    require(value && value->type == core::Node::Type::Bytes, ErrorCode::InvalidFormat, "Original cache entry has no torrent identity");
    const auto key = core::TorrentHash::parse(value->bytes);
    require(key.has_value(), ErrorCode::InvalidFormat, "Invalid original state cache identity");
    if (!aliases(identity, *key)) { continue; }
    require(*key == identity.originalKey && !selected, ErrorCode::Conflict, "State cache contains an ambiguous archived torrent key");
    selected = index;
  }
  if (!selected) { return {}; }
  const auto& removed = entries->children[*selected];
  auto replacement = std::string(raw(cache->document(), *entries));
  replacement.erase(removed.begin - entries->begin, removed.end - removed.begin);
  require(cache->replaceValue("state", std::move(replacement)).has_value(), ErrorCode::InvalidFormat,
      "Cannot remove the archived torrent from the original state cache");
  auto after = cache->serialize();
  require(after.has_value(), ErrorCode::Io, "Cannot encode the updated original state cache");
  return ArchiveExportCacheEdit{leaf, std::move(source), std::move(*after)};
}

class AuxiliaryTree final {
public:
  explicit AuxiliaryTree(std::stop_token stop) : stop_(stop) {}

  std::vector<ArchivedStateEntry> capture(StateHandle parent, const std::filesystem::path& root) {
    const auto original = take(stateStampAt(parent, root));
    if (original) {
      require(original->directory, ErrorCode::UnsafePath, "Archived auxiliary root is not a directory");
      visit(parent, root, {}, *original, 0);
    }
    return std::move(entries_);
  }

private:
  void visit(StateHandle parent, const std::filesystem::path& leaf, const std::filesystem::path& relative,
      const StateStamp& original, std::size_t depth) {
    checkCancellation(stop_);
    require(entries_.size() < MaximumAuxiliaryEntries && depth <= MaximumAuxiliaryDepth, ErrorCode::Unsupported,
        "Archived auxiliary state exceeds its entry or nesting limit");
    require(original.owned && (original.directory || original.regular), ErrorCode::UnsafePath,
        "Archived auxiliary state is not an owned regular file or directory");
    if (original.regular) {
      auto file = take(captureProfileFile(parent, platform::pathToUtf8(leaf), bytesLeft_));
      require(file.stamp && sameStateSnapshot(original, *file.stamp) && file.bytes, ErrorCode::Conflict,
          "Archived auxiliary file changed during capture");
      bytesLeft_ -= file.bytes->size();
      entries_.push_back({relative, original, {}, std::move(file)});
      return;
    }
    auto directory = take(openDirectoryAt(parent, leaf));
    const auto held = directory.get();
    require(sameStateSnapshot(original, take(stateStamp(held))), ErrorCode::Conflict,
        "Archived auxiliary directory changed before enumeration");
    entries_.push_back({relative, original, std::move(directory), {}});
    auto names = take(listStateDirectoryNames(held, MaximumAuxiliaryEntries + 2, stop_));
    std::ranges::sort(names);
    for (const auto& name : names) {
      if (name == "." || name == "..") { continue; }
      const auto child = take(stateStampAt(held, name));
      require(child.has_value(), ErrorCode::Conflict, "Archived auxiliary entry changed during enumeration");
      visit(held, name, relative / name, *child, depth + 1);
    }
    const auto latest = take(stateStampAt(parent, leaf));
    require(latest && sameStateSnapshot(original, *latest) && sameStateSnapshot(original, take(stateStamp(held))),
        ErrorCode::Conflict, "Archived auxiliary directory changed during enumeration");
  }

  std::stop_token stop_;
  std::size_t bytesLeft_ = MaximumAuxiliaryBytes;
  std::vector<ArchivedStateEntry> entries_;
};
} // namespace

std::expected<DownloadArchiveRestoreSource, Error> readDownloadArchiveRestoreSource(
    const ProfileLease& lease, const core::TorrentHash& hash, std::stop_token stop) try {
  auto access = take(lease.sourceAccess(false, stop));
  auto archive = take(readDownloadArchive(lease, stop));
  checkCancellation(stop);
  require(archive.source.has_value(), ErrorCode::Missing, "The archive is empty");
  require(archive.source->recoverySource() == RecoverySource::Primary, ErrorCode::Conflict,
      "Resolve archive recovery before restoring a download");
  const auto matching = [&](const core::ArchivedDownload& stub) { return stub.hash == hash; };
  const auto found = std::ranges::find_if(archive.stubs, matching);
  require(found != archive.stubs.end(), ErrorCode::Missing, "The selected download is no longer in the archive");
  require(std::find_if(std::next(found), archive.stubs.end(), matching) == archive.stubs.end(), ErrorCode::Conflict,
      "More than one archive entry has the selected identity");
  const auto index = static_cast<std::size_t>(found - archive.stubs.begin());
  const auto& catalog = archive.source->document();
  auto inventoryRow = readInventoryRow(catalog, catalog.find("stubs")->children[index], hash);

  const auto root = lease.directoryHandle();
  const auto stamp = take(stateStampAt(root, "dlarchive"));
  require(stamp && stamp->directory && stamp->owned, ErrorCode::UnsafePath,
      "The original archive state directory is missing or unsafe");
  auto directory = take(openDirectoryAt(root, "dlarchive"));
  const auto held = directory.get();
  require(sameStateSnapshot(*stamp, take(stateStamp(held))), ErrorCode::Conflict,
      "The archive state directory changed before opening");
  const auto name = hash.hex();
  auto state = take(captureProfileFile(held, name + ".dat", compatibleConfigLimits().inputBytes));
  require(state.bytes.has_value(), ErrorCode::Missing, "The archived torrent state is missing");
  const auto document = EncodedDocument::parse(*state.bytes, compatibleConfigLimits());
  require(document.has_value(), ErrorCode::InvalidFormat, "The archived torrent state is unreadable");
  const auto identity = take(digestOriginalTorrentIdentity(document->document()));
  const auto selected = validateOriginalTorrentKey(identity, hash);
  if (!selected) { return std::unexpected(selected.error()); }
  auto auxiliary = AuxiliaryTree(stop).capture(held, name);
  const auto currentStamp = take(stateStampAt(root, "dlarchive"));
  const auto currentState = take(stateStampAt(held, name + ".dat"));
  require(currentStamp && sameStateSnapshot(*stamp, *currentStamp) && sameStateSnapshot(*stamp, take(stateStamp(held)))
      && currentState && sameStateSnapshot(*state.stamp, *currentState) && state.stillHeld(), ErrorCode::Conflict,
      "The archive state changed during restore preparation");
  checkCancellation(stop);
  return DownloadArchiveRestoreSource{identity, std::move(*archive.source), index, std::move(inventoryRow),
      std::move(directory), *stamp, std::move(state), std::move(auxiliary)};
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::bad_alloc&) {
  return std::unexpected(Error{ErrorCode::Io, "Insufficient memory to prepare archive restoration"});
} catch (const std::filesystem::filesystem_error&) {
  return std::unexpected(Error{ErrorCode::UnsafePath, "Invalid archive state or save path"});
}

std::expected<DownloadArchiveRestoreEdits, Error> prepareDownloadArchiveRestore(
    const ProfileLease& lease, const core::TorrentHash& hash, std::stop_token stop) try {
  auto access = take(lease.sourceAccess(false, stop));
  checkCancellation(stop);
  const auto acquisition = requireNoPendingSubscriptionAcquisition(lease);
  if (!acquisition) { return std::unexpected(acquisition.error()); }
  require(!hasPendingTorrentIntake(lease) && !hasPendingCategoryDeletion(lease), ErrorCode::Conflict,
      "Finish the pending profile operation before restoring a download");
  const auto root = lease.directoryHandle();
  requireAbsent(root, "dlarchive.config.saving", "Resolve the pending archive save before restoring a download");
  auto source = take(readDownloadArchiveRestoreSource(lease, hash, stop));
  auto inventory = readInventory(lease);
  auto active = take(openDirectoryAt(root, "active"));
  const auto stamp = take(stateStamp(active.get()));
  require(stamp.owned && stamp.directory, ErrorCode::UnsafePath, "Active state directory is not owned by the profile");
  const auto name = hash.hex();
  for (const auto* suffix : {"", ".dat", ".dat.bak", ".dat.saving"}) {
    requireAbsent(active.get(), name + suffix, "Active state already exists for the archived torrent");
  }
  checkCache(active.get(), source.identity);
  const auto empty = core::Document::parse("d9:downloadslee", compatibleConfigLimits()).value();
  auto inventoryAfter = addInventoryRow(inventory ? inventory->document() : empty, source.inventoryRow, source.identity);
  auto archiveAfter = removeArchiveRow(source.configuration.document(), source.rowIndex);
  const auto current = take(stateStampAt(root, "active"));
  require(current && sameStateSnapshot(stamp, *current), ErrorCode::Conflict,
      "Active state directory changed during restore preparation");
  checkCancellation(stop);
  return DownloadArchiveRestoreEdits{std::move(source), std::move(active), stamp, std::move(inventory),
      std::move(inventoryAfter), std::move(archiveAfter)};
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::bad_alloc&) {
  return std::unexpected(Error{ErrorCode::Io, "Insufficient memory to prepare archive restoration"});
}

std::expected<DownloadArchiveExportEdits, Error> prepareDownloadArchiveExport(const ProfileLease& lease,
    const core::TorrentHash& hash, std::int64_t archivedMillis, std::span<const std::string> manualTags,
    std::stop_token stop) try {
  auto access = take(lease.sourceAccess(false, stop));
  checkCancellation(stop);
  const auto acquisition = requireNoPendingSubscriptionAcquisition(lease);
  if (!acquisition) { return std::unexpected(acquisition.error()); }
  require(!hasPendingTorrentIntake(lease) && !hasPendingCategoryDeletion(lease), ErrorCode::Conflict,
      "Finish the pending profile operation before archiving this download");
  const auto root = lease.directoryHandle();
  requireAbsent(root, "dlarchive.config.saving", "Resolve archive configuration recovery before archiving");
  auto inventory = readInventory(lease);
  require(inventory.has_value(), ErrorCode::Missing, "No original library is available");
  auto catalog = take(readDownloadArchive(lease, stop));
  require(!catalog.source || catalog.source->recoverySource() == RecoverySource::Primary, ErrorCode::Conflict,
      "Resolve the original archive catalog recovery before archiving");
  auto record = take(prepareArchiveExportRecord(lease, hash, archivedMillis, manualTags, stop));
  require(std::ranges::none_of(catalog.stubs, [&](const auto& row) { return row.hash && aliases(record.identity, *row.hash); }),
      ErrorCode::Conflict, "Archive catalog already contains the torrent or its alias");
  auto active = take(openDirectoryAt(root, "active"));
  const auto activeStamp = take(stateStamp(active.get()));
  require(activeStamp.owned && activeStamp.directory, ErrorCode::UnsafePath, "Active state directory is not owned by the profile");
  const auto name = hash.hex();
  for (const auto& leaf : {name + ".dat.saving", std::string("cache.dat.saving"), std::string("cache.dat.bak.saving")}) {
    requireAbsent(active.get(), leaf, "Resolve pending active state writes before archiving");
  }
  auto activeState = take(captureProfileFile(active.get(), name + ".dat", compatibleConfigLimits().inputBytes));
  require(activeState.stamp && activeState.bytes, ErrorCode::Missing, "The selected original torrent state is absent");
  auto backup = take(captureProfileFile(active.get(), name + ".dat.bak", compatibleConfigLimits().inputBytes));
  std::vector<ArchiveExportCacheEdit> cacheEdits;
  for (const auto* leaf : {"cache.dat", "cache.dat.bak"}) {
    if (auto edit = removeCacheRow(active.get(), leaf, record.identity)) { cacheEdits.push_back(std::move(*edit)); }
  }
  auto auxiliary = AuxiliaryTree(stop).capture(active.get(), name);
  StateDescriptor archive;
  const auto archiveStamp = take(stateStampAt(root, "dlarchive"));
  if (archiveStamp) {
    archive = take(openDirectoryAt(root, "dlarchive"));
    require(archiveStamp->owned && sameStateSnapshot(*archiveStamp, take(stateStamp(archive.get()))), ErrorCode::Conflict,
        "Archive state directory changed or is unsafe");
    for (const auto* suffix : {"", ".dat", ".dat.bak", ".dat.saving"}) {
      requireAbsent(archive.get(), name + suffix, "Archive already contains state for the selected torrent");
    }
  }
  const auto empty = core::Document::parse("d5:stubslee", compatibleConfigLimits()).value();
  auto inventoryAfter = removeInventoryRow(inventory->document(), hash, record.inventoryIndex);
  auto catalogAfter = addArchiveRow(catalog.source ? catalog.source->document() : empty, record.stub);
  checkCancellation(stop);
  return DownloadArchiveExportEdits{std::move(record), std::move(*inventory), std::move(catalog.source),
      std::move(inventoryAfter), std::move(catalogAfter), std::move(active), activeStamp, std::move(archive), archiveStamp,
      std::move(activeState), std::move(backup), std::move(cacheEdits), std::move(auxiliary)};
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::bad_alloc&) {
  return std::unexpected(Error{ErrorCode::Io, "Insufficient memory to prepare download archiving"});
}

} // namespace biglybtc::storage::detail
