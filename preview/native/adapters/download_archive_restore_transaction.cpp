// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_archive_restore.hpp"
#include "download_archive_tags.hpp"
#include "encoded_document.hpp"
#include "file_operation_digest.hpp"
#include "original_identity_digest.hpp"
#include "profile_transaction_pending.hpp"
#include "state_directory_listing.hpp"
#include "../platform/utf8_path.hpp"
#include "biglybtc/saved_download_tags.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <new>

namespace biglybtc::storage {
namespace {
using namespace detail;
using Node = core::Node;
namespace fs = std::filesystem;
constexpr char Journal[] = "BiglyBTC.archive-restore";
constexpr char JournalTemp[] = "BiglyBTC.archive-restore.saving";
constexpr char CatalogTemp[] = "BiglyBTC.archive-restore.catalog";
constexpr char InventoryTemp[] = "BiglyBTC.archive-restore.inventory";
constexpr char TagsTemp[] = "BiglyBTC.archive-restore.tags";
constexpr char StateTemp[] = "BiglyBTC.archive-restore.state";
constexpr char FileTemp[] = "BiglyBTC.archive-restore.file";
constexpr char CacheTemp[] = "BiglyBTC.archive-restore.cache";
constexpr char CacheBackupTemp[] = "BiglyBTC.archive-restore.cache-backup";
constexpr char Inventory[] = "downloads.config";
constexpr char TagsConfiguration[] = "tag.config";
constexpr std::size_t MaximumAssets = 1024;
constexpr std::size_t MaximumDepth = 24;

void require(bool condition, ErrorCode code, const char* message) {
  if (!condition) { throw Error{code, message}; }
}

template<class T>
T take(std::expected<T, Error> result) {
  if (!result) { throw std::move(result.error()); }
  return std::move(*result);
}

void take(std::expected<void, Error> result) {
  if (!result) { throw std::move(result.error()); }
}

std::string bytes(std::string_view value) {
  return std::to_string(value.size()) + ':' + std::string(value);
}

std::string stampId(const StateStamp& stamp) {
  return std::to_string(stamp.device) + ':' + std::to_string(stamp.inode);
}

std::string_view value(const Node& row, const char* key) {
  const auto* field = row.find(key);
  require(field && field->type == Node::Type::Bytes, ErrorCode::InvalidFormat, "Incomplete archive restore journal");
  return field->bytes;
}

struct Image {
  std::string beforeDigest;
  std::string after;
};

struct Asset {
  fs::path path;
  std::string sourceId;
  std::optional<std::string> data;
};

enum class Operation { Restore, Export, Discard };

struct Plan {
  core::TorrentHash hash;
  std::string profileId;
  std::string activeId;
  std::string archiveId;
  Image inventory;
  Image catalog;
  Asset state;
  std::vector<Asset> auxiliary;
  Operation operation = Operation::Restore;
  std::string activeStateDigest;
  std::optional<Asset> sourceBackup;
  std::map<std::string, Image> cache;
  std::optional<Image> tags;
};

std::string encode(const Plan& plan) {
  const auto image = [](const Image& entry) {
    return "d5:after" + bytes(entry.after) + "6:before" + bytes(entry.beforeDigest) + 'e';
  };
  const auto asset = [](const Asset& entry) {
    return "d2:id" + bytes(entry.sourceId) + "4:path" + bytes(platform::pathToUtf8(entry.path))
        + (entry.data ? "5:value" + bytes(*entry.data) : std::string{}) + 'e';
  };
  const auto hash = plan.hash.bytes();
  auto out = std::string("d");
  if (plan.operation != Operation::Discard) { out += "6:active" + bytes(plan.activeId); }
  out += "7:archive" + bytes(plan.archiveId) + "9:auxiliaryl";
  for (const auto& entry : plan.auxiliary) { out += asset(entry); }
  out += 'e';
  if (plan.sourceBackup) { out += "6:backup" + asset(*plan.sourceBackup); }
  if (plan.operation == Operation::Export) {
    out += "5:cached";
    for (const auto& [leaf, files] : plan.cache) { out += bytes(leaf) + image(files); }
    out += 'e';
  }
  out += "7:catalog" + image(plan.catalog) + "4:hash"
      + bytes({reinterpret_cast<const char*>(hash.data()), hash.size()});
  if (plan.operation != Operation::Discard) { out += "9:inventory" + image(plan.inventory); }
  out += "7:profile" + bytes(plan.profileId) + "5:state" + asset(plan.state);
  if (plan.operation == Operation::Export) { out += "11:stateDigest" + bytes(plan.activeStateDigest); }
  if (plan.tags) { out += "4:tags" + image(*plan.tags); }
  if (plan.operation == Operation::Discard) { return out + "7:versioni4ee"; }
  out += plan.tags ? "7:versioni3ee" : plan.operation == Operation::Export ? "7:versioni2ee" : "7:versioni1ee";
  return out;
}

Asset readAsset(const Node& entry) {
  auto path = platform::pathFromUtf8(value(entry, "path"));
  require(path == path.lexically_normal() && !path.has_root_path(), ErrorCode::UnsafePath,
      "Invalid relative archive state path");
  std::size_t depth = 0;
  for (const auto& leaf : path) {
    require(!leaf.empty() && leaf != "." && leaf != ".." && leaf != FileTemp
        && platform::pathToUtf8(leaf).find('\0') == std::string::npos && ++depth <= MaximumDepth,
        ErrorCode::UnsafePath, "Unsafe archive state filename in restore journal");
  }
  Asset asset{std::move(path), std::string(value(entry, "id")), {}};
  if (entry.find("value")) { asset.data = value(entry, "value"); }
  require(!asset.sourceId.empty(), ErrorCode::InvalidFormat, "Archive state has no source identity");
  return asset;
}

Plan decode(const std::string& encoded) {
  const auto document = core::Document::parse(encoded, compatibleConfigLimits());
  require(document.has_value(), ErrorCode::InvalidFormat, "Archive restore journal is incomplete or unreadable");
  const auto& root = document->root();
  const auto* version = root.find("version");
  require(version && version->type == Node::Type::Integer && version->integer >= 1 && version->integer <= 4, ErrorCode::Unsupported,
      "Unknown archive restore journal version");
  const auto hash = core::TorrentHash::parse(value(root, "hash"));
  require(hash && hash->bytes().size() == 20, ErrorCode::InvalidFormat, "Invalid restore journal torrent identity");
  const auto image = [](const Node& map, const char* key) {
    const auto* entry = map.find(key);
    require(entry != nullptr, ErrorCode::InvalidFormat, "Missing archive restore configuration image");
    return Image{std::string(value(*entry, "before")), std::string(value(*entry, "after"))};
  };
  const auto* tree = root.find("auxiliary");
  const auto* state = root.find("state");
  require(tree && tree->type == Node::Type::List && tree->children.size() <= MaximumAssets && state,
      ErrorCode::InvalidFormat, "Invalid archive restore state manifest");
  const auto operation = version->integer == 4 ? Operation::Discard
      : version->integer == 2 ? Operation::Export : Operation::Restore;
  const bool discard = operation == Operation::Discard;
  Plan plan{*hash, std::string(value(root, "profile")), discard ? std::string{} : std::string(value(root, "active")),
      std::string(value(root, "archive")), discard ? Image{} : image(root, "inventory"), image(root, "catalog"),
      readAsset(*state), {}, operation, {}, {}, {}, {}};
  require(plan.state.data && plan.state.path == hash->hex() + ".dat", ErrorCode::InvalidFormat,
      "Archive restore journal state has the wrong filename");
  if (operation != Operation::Restore) {
    if (const auto* backup = root.find("backup")) {
      plan.sourceBackup = readAsset(*backup);
      require(plan.sourceBackup->data && plan.sourceBackup->path == hash->hex() + ".dat.bak", ErrorCode::InvalidFormat,
          "Archive journal has an invalid source backup");
    }
  }
  if (operation == Operation::Export) {
    plan.activeStateDigest = value(root, "stateDigest");
    require(plan.activeStateDigest.size() == 64, ErrorCode::InvalidFormat, "Archive journal has no primary-state preimage digest");
    const auto* cache = root.find("cache");
    require(cache && cache->type == Node::Type::Dictionary, ErrorCode::InvalidFormat, "Invalid archive journal cache edits");
    for (const auto* leaf : {"cache.dat", "cache.dat.bak"}) {
      if (cache->find(leaf)) { plan.cache.emplace(leaf, image(*cache, leaf)); }
    }
  }
  if (version->integer == 3) {
    plan.tags = image(root, "tags");
    const auto savedTags = core::readSavedDownloadTags(plan.tags->after);
    require(savedTags && savedTags->issues.empty(), ErrorCode::InvalidFormat, "Invalid archived tag postimage");
  }
  std::map<fs::path, bool, FilesystemPathLess> paths;
  for (const auto& entry : tree->children) {
    auto asset = readAsset(entry);
    if (plan.auxiliary.empty()) {
      require(asset.path.empty() && !asset.data, ErrorCode::InvalidFormat, "Missing archive auxiliary root");
    } else {
      const auto parent = paths.find(asset.path.parent_path());
      require(!asset.path.empty() && parent != paths.end() && parent->second, ErrorCode::InvalidFormat,
          "Missing parent for archived auxiliary path");
    }
    require(paths.emplace(asset.path, !asset.data).second, ErrorCode::InvalidFormat, "Duplicate archived auxiliary path");
    plan.auxiliary.push_back(std::move(asset));
  }
  const auto stateDocument = EncodedDocument::parse(*plan.state.data, compatibleConfigLimits());
  require(stateDocument.has_value(), ErrorCode::InvalidFormat, "Invalid archived state in restore journal");
  take(validateOriginalTorrentKey(take(digestOriginalTorrentIdentity(stateDocument->document())), *hash));
  if (!discard) {
    const auto inventory = core::Document::parse(plan.inventory.after, compatibleConfigLimits());
    require(inventory.has_value(), ErrorCode::InvalidFormat, "Invalid library inventory in archive restore journal");
    const auto library = core::readLibrary(*inventory);
    require(library && std::ranges::count_if(library->downloads, [&](const auto& row) { return row.hash == hash; }) == (operation == Operation::Restore ? 1 : 0),
        ErrorCode::InvalidFormat, "Archive journal library image has the wrong selected torrent count");
  }
  const auto catalog = core::Document::parse(plan.catalog.after, compatibleConfigLimits());
  const auto* stubs = catalog ? catalog->find("stubs") : nullptr;
  require(stubs && stubs->type == Node::Type::List && std::ranges::count_if(stubs->children, [&](const Node& row) {
    const auto* key = row.find("hash");
    return key && key->type == Node::Type::Bytes && core::TorrentHash::parse(key->bytes) == hash;
  }) == (operation == Operation::Export ? 1 : 0), ErrorCode::InvalidFormat, "Archive journal catalog image has the wrong selected torrent count");
  require(encode(plan) == encoded, ErrorCode::InvalidFormat, "Noncanonical archive restore journal");
  return plan;
}

ProfileFileSnapshot capture(StateHandle directory, const fs::path& leaf) {
  return take(captureProfileFile(directory, platform::pathToUtf8(leaf), compatibleConfigLimits().inputBytes));
}

std::string digest(const std::optional<std::string>& data) {
  return data ? fileop::digest(*data) : std::string{};
}

void verifyImage(const ProfileFileSnapshot& current, const Image& image) {
  require(current.bytes == image.after || digest(current.bytes) == image.beforeDigest, ErrorCode::Conflict,
      "Original archive or library changed during restoration");
}

void publish(StateHandle directory, const fs::path& leaf, const fs::path& temporary, const std::string& data,
    ProfileFileSnapshot current) {
  auto pending = capture(directory, temporary);
  require(!pending.bytes || *pending.bytes == data, ErrorCode::Conflict, "Archive restore staging file changed");
  if (current.bytes == data) {
    if (pending.stamp) { take(retireStateFile(directory, temporary, *pending.stamp)); }
  } else {
    StateDescriptor descriptor;
    if (!pending.stamp) {
      const auto mode = current.stamp ? stateWriteMode(*current.stamp) : 0600;
      descriptor = take(stageStateFile(directory, temporary, data, mode));
      pending.stamp = take(stateStamp(descriptor.get()));
    }
    pending.releaseWitness();
    const auto* old = current.stamp ? &*current.stamp : nullptr;
    take(publishStateFile(directory, temporary, leaf, descriptor.get(), old != nullptr, &*pending.stamp, old));
  }
  take(syncStateDirectory(directory));
}

StateDescriptor openDirectory(StateHandle directory, const fs::path& leaf, std::string_view expectedId = {}) {
  const auto named = take(stateStampAt(directory, leaf));
  auto held = take(openDirectoryAt(directory, leaf));
  const auto stamp = take(stateStamp(held.get()));
  require(named && sameStateDirectory(*named, stamp) && stamp.owned
      && (expectedId.empty() || stampId(stamp) == expectedId), ErrorCode::Conflict,
      "Archive restore directory changed");
  return held;
}

void verifyFile(const ProfileFileSnapshot& snapshot, const Asset& expected, bool source) {
  require(snapshot.bytes == expected.data && snapshot.stamp && (!source || stampId(*snapshot.stamp) == expected.sourceId),
      ErrorCode::Conflict, "Archived torrent state or its copy has changed");
}

void verifySourceState(const ProfileFileSnapshot& snapshot, const Plan& plan) {
  if (plan.operation != Operation::Export) {
    verifyFile(snapshot, plan.state, true);
    return;
  }
  require(snapshot.stamp && stampId(*snapshot.stamp) == plan.state.sourceId && digest(snapshot.bytes) == plan.activeStateDigest,
      ErrorCode::Conflict, "Original active state changed before archiving");
}

using DirectoryMap = std::map<fs::path, StateDescriptor, FilesystemPathLess>;

StateHandle parentOf(StateHandle root, const DirectoryMap& directories, const fs::path& path) {
  if (path.empty()) { return root; }
  auto parent = directories.find(path.parent_path());
  return parent == directories.end() ? InvalidStateHandle : parent->second.get();
}

fs::path nameOf(const fs::path& rootName, const fs::path& path) {
  return path.empty() ? rootName : path.filename();
}

DirectoryMap scanTree(StateHandle root, const fs::path& rootName, const std::vector<Asset>& tree, bool source,
    bool complete) {
  DirectoryMap directories;
  std::map<fs::path, std::vector<fs::path>, FilesystemPathLess> expectedNames;
  for (const auto& asset : tree) {
    if (!asset.path.empty()) { expectedNames[asset.path.parent_path()].push_back(asset.path.filename()); }
    const auto parent = parentOf(root, directories, asset.path);
    if (parent == InvalidStateHandle) { continue; }
    const auto leaf = nameOf(rootName, asset.path);
    const auto stamp = take(stateStampAt(parent, leaf));
    require(!complete || stamp.has_value(), ErrorCode::Conflict, "Archive restore auxiliary state is incomplete");
    if (!stamp) { continue; }
    if (asset.data) {
      verifyFile(capture(parent, leaf), asset, source);
    } else {
      directories.emplace(asset.path, openDirectory(parent, leaf, source ? asset.sourceId : std::string_view{}));
    }
  }
  for (const auto& [path, descriptor] : directories) {
    const auto names = take(listStateDirectoryNames(descriptor.get(), MaximumAssets + 3));
    const auto& expected = expectedNames[path];
    for (const auto& leaf : names) {
      if (leaf == "." || leaf == "..") { continue; }
      const bool staging = !source && !complete && leaf == FileTemp;
      const auto matches = [&](const auto& name) { return sameFilesystemComponent(leaf, name); };
      require(staging || std::ranges::any_of(expected, matches), ErrorCode::Conflict,
          "Unrelated file found in archived auxiliary state");
    }
  }
  if (tree.empty()) {
    require(!take(stateStampAt(root, rootName)), ErrorCode::Conflict, "An unexpected archive auxiliary directory exists");
  }
  return directories;
}

void copyTree(StateHandle active, const fs::path& hash, const std::vector<Asset>& tree) {
  auto directories = scanTree(active, hash, tree, false, false);
  for (const auto& entry : tree) {
    const auto parent = parentOf(active, directories, entry.path);
    const auto leaf = nameOf(hash, entry.path);
    if (entry.data) {
      auto current = capture(parent, leaf);
      if (current.bytes) { verifyFile(current, entry, false); }
      publish(parent, leaf, FileTemp, *entry.data, std::move(current));
    } else if (!directories.contains(entry.path)) {
      directories.emplace(entry.path, take(createDirectoryAt(parent, leaf)));
      take(syncStateDirectory(parent));
    }
  }
  static_cast<void>(scanTree(active, hash, tree, false, true));
}

void retireTree(StateHandle archive, const fs::path& hash, const std::vector<Asset>& tree) {
  auto directories = scanTree(archive, hash, tree, true, false);
  for (auto it = tree.rbegin(); it != tree.rend(); ++it) {
    const auto parent = parentOf(archive, directories, it->path);
    if (parent == InvalidStateHandle) { continue; }
    const auto leaf = nameOf(hash, it->path);
    if (it->data) {
      const auto current = capture(parent, leaf);
      if (current.stamp) {
        verifyFile(current, *it, true);
        take(retireStateFile(parent, leaf, *current.stamp));
      }
    } else if (auto owned = directories.extract(it->path); !owned.empty()) {
      const auto stamp = take(stateStamp(owned.mapped().get()));
      owned.mapped() = {};
      take(retireEmptyDirectoryAt(parent, leaf, stamp));
    }
    take(syncStateDirectory(parent));
  }
}

void retireArchiveFile(StateHandle source, const Asset& asset) {
  const auto current = capture(source, asset.path);
  if (!current.stamp) { return; }
  verifyFile(current, asset, true);
  take(retireStateFile(source, asset.path, *current.stamp));
  take(syncStateDirectory(source));
}

void applyDiscard(StateHandle root, StateHandle archive, const Plan& plan, DownloadArchiveRestoreObserver observer) {
  const auto hash = plan.hash.hex();
  require(!take(stateStampAt(root, "dlarchive.config.saving")) && !take(stateStampAt(archive, hash + ".dat.saving")),
      ErrorCode::Conflict, "Original archived state is being saved");
  auto catalog = capture(root, DownloadArchiveFile);
  verifyImage(catalog, plan.catalog);
  const auto catalogPublished = catalog.bytes == plan.catalog.after;
  const auto primary = capture(archive, plan.state.path);
  if (!catalogPublished || primary.stamp) { verifyFile(primary, plan.state, true); }
  const auto backup = capture(archive, hash + ".dat.bak");
  if (plan.sourceBackup && (!catalogPublished || backup.stamp)) { verifyFile(backup, *plan.sourceBackup, true); }
  require(plan.sourceBackup || !backup.stamp, ErrorCode::Conflict, "Archived entry gained an unplanned state backup");
  static_cast<void>(scanTree(archive, hash, plan.auxiliary, true, !catalogPublished));

  publish(root, DownloadArchiveFile, CatalogTemp, plan.catalog.after, std::move(catalog));
  if (observer) { observer(DownloadArchiveRestoreStage::CatalogPublished); }
  const auto namedArchive = take(stateStampAt(root, "dlarchive"));
  require(namedArchive && stampId(*namedArchive) == plan.archiveId
      && capture(root, DownloadArchiveFile).bytes == plan.catalog.after, ErrorCode::Conflict,
      "Archive catalog changed before removing archived state");
  retireArchiveFile(archive, plan.state);
  if (plan.sourceBackup) { retireArchiveFile(archive, *plan.sourceBackup); }
  retireTree(archive, hash, plan.auxiliary);
}

void apply(const ProfileLease& lease, const Plan& plan, DownloadArchiveRestoreObserver observer) {
  const auto root = lease.directoryHandle();
  require(stampId(take(stateStamp(root))) == plan.profileId, ErrorCode::Conflict, "Archive restore journal belongs to another profile");
  const auto archive = openDirectory(root, "dlarchive", plan.archiveId);
  if (plan.operation == Operation::Discard) {
    applyDiscard(root, archive.get(), plan, observer);
    return;
  }
  const auto active = openDirectory(root, "active", plan.activeId);
  const bool exporting = plan.operation == Operation::Export;
  const auto source = exporting ? active.get() : archive.get();
  const auto destination = exporting ? archive.get() : active.get();
  const auto hash = plan.hash.hex();
  for (const auto* leaf : {"downloads.config.saving", "dlarchive.config.saving"}) {
    require(!take(stateStampAt(root, leaf)), ErrorCode::Conflict, "Original configuration recovery appeared during archive restore");
  }
  for (const auto& leaf : {hash + ".dat.saving", std::string("cache.dat.saving"), std::string("cache.dat.bak.saving")}) {
    require(!take(stateStampAt(active.get(), leaf)), ErrorCode::Conflict, "Original state recovery appeared during archive restore");
  }
  for (const auto& suffix : {".dat.bak", ".dat.saving"}) {
    require(!take(stateStampAt(destination, hash + suffix)), ErrorCode::Conflict, "Archive transaction destination has unresolved state");
  }
  auto inventory = capture(root, Inventory);
  auto catalog = capture(root, DownloadArchiveFile);
  verifyImage(inventory, plan.inventory);
  verifyImage(catalog, plan.catalog);
  const bool inventoryPublished = inventory.bytes == plan.inventory.after;
  const bool catalogPublished = catalog.bytes == plan.catalog.after;
  const bool firstPublished = exporting ? catalogPublished : inventoryPublished;
  const bool lastPublished = exporting ? inventoryPublished : catalogPublished;
  require(!lastPublished || firstPublished, ErrorCode::Conflict, "Archive transaction metadata appeared out of order");
  if (plan.tags) {
    require(!take(stateStampAt(root, "tag.config.saving")), ErrorCode::Conflict, "Original tag configuration recovery appeared");
    const auto tags = capture(root, TagsConfiguration);
    verifyImage(tags, *plan.tags);
    require(!lastPublished || tags.bytes == plan.tags->after, ErrorCode::Conflict, "Archived torrent disappeared before tag restoration");
  }
  if (exporting) {
    const auto oldBackup = capture(source, hash + ".dat.bak");
    if (plan.sourceBackup && (!lastPublished || oldBackup.stamp)) { verifyFile(oldBackup, *plan.sourceBackup, true); }
    require(plan.sourceBackup || !oldBackup.stamp, ErrorCode::Conflict, "Original source gained an unplanned backup");
    for (const auto& [leaf, image] : plan.cache) { verifyImage(capture(active.get(), leaf), image); }
  }
  const auto original = capture(source, plan.state.path);
  if (!lastPublished || original.stamp) { verifySourceState(original, plan); }
  static_cast<void>(scanTree(source, hash, plan.auxiliary, true, !lastPublished));
  auto copy = capture(destination, plan.state.path);
  if (copy.stamp || firstPublished) { verifyFile(copy, plan.state, false); }
  static_cast<void>(scanTree(destination, hash, plan.auxiliary, false, firstPublished));
  publish(destination, plan.state.path, StateTemp, *plan.state.data, std::move(copy));
  if (!firstPublished) { copyTree(destination, hash, plan.auxiliary); }
  if (observer) { observer(DownloadArchiveRestoreStage::StatePublished); }
  const auto writeInventory = [&] {
    auto current = capture(root, Inventory);
    verifyImage(current, plan.inventory);
    publish(root, Inventory, InventoryTemp, plan.inventory.after, std::move(current));
    if (observer) { observer(DownloadArchiveRestoreStage::InventoryPublished); }
  };
  const auto writeCatalog = [&] {
    auto current = capture(root, DownloadArchiveFile);
    verifyImage(current, plan.catalog);
    publish(root, DownloadArchiveFile, CatalogTemp, plan.catalog.after, std::move(current));
    if (observer) { observer(DownloadArchiveRestoreStage::CatalogPublished); }
  };
  if (exporting) {
    writeCatalog();
    writeInventory();
    for (const auto& [leaf, image] : plan.cache) {
      auto current = capture(active.get(), leaf);
      verifyImage(current, image);
      publish(active.get(), leaf, leaf == "cache.dat" ? CacheTemp : CacheBackupTemp, image.after, std::move(current));
    }
  } else {
    writeInventory();
    if (plan.tags) {
      auto current = capture(root, TagsConfiguration);
      verifyImage(current, *plan.tags);
      publish(root, TagsConfiguration, TagsTemp, plan.tags->after, std::move(current));
      if (observer) { observer(DownloadArchiveRestoreStage::TagsPublished); }
    }
    writeCatalog();
  }
  verifyFile(capture(destination, plan.state.path), plan.state, false);
  const auto inventoryAfter = capture(root, Inventory);
  const auto catalogAfter = capture(root, DownloadArchiveFile);
  require(inventoryAfter.bytes == plan.inventory.after && catalogAfter.bytes == plan.catalog.after, ErrorCode::Conflict,
      "Restored archive or library changed before retiring archived state");
  if (plan.tags) {
    require(capture(root, TagsConfiguration).bytes == plan.tags->after, ErrorCode::Conflict,
        "Restored manual tags changed before retiring archived state");
  }
  static_cast<void>(scanTree(destination, hash, plan.auxiliary, false, true));
  const auto retired = capture(source, plan.state.path);
  if (retired.stamp) {
    verifySourceState(retired, plan);
    take(retireStateFile(source, plan.state.path, *retired.stamp));
    take(syncStateDirectory(source));
  }
  if (plan.sourceBackup) { retireArchiveFile(source, *plan.sourceBackup); }
  retireTree(source, hash, plan.auxiliary);
}

Plan makePlan(const ProfileLease& lease, DownloadArchiveRestoreEdits edits) {
  auto& source = edits.source;
  const auto hash = source.identity.originalKey;
  Plan plan{hash, stampId(take(stateStamp(lease.directoryHandle()))), stampId(edits.activeDirectoryStamp),
      stampId(source.archiveDirectoryStamp),
      {edits.inventory ? fileop::digest(edits.inventory->document().originalBytes()) : std::string{}, std::move(edits.inventoryAfter)},
      {fileop::digest(source.configuration.document().originalBytes()), std::move(edits.archiveAfter)},
      {hash.hex() + ".dat", stampId(*source.state.stamp), std::move(source.state.bytes)}, {}, Operation::Restore, {}, {}, {}, {}};
  for (auto& entry : source.auxiliary) {
    plan.auxiliary.push_back({std::move(entry.relativePath), stampId(entry.stamp),
        entry.file ? std::move(entry.file->bytes) : std::nullopt});
  }
  return plan;
}

Plan makeExportPlan(const ProfileLease& lease, DownloadArchiveExportEdits edits, StateStamp archive) {
  const auto hash = edits.record.identity.originalKey;
  Plan plan{hash, stampId(take(stateStamp(lease.directoryHandle()))), stampId(edits.activeDirectoryStamp), stampId(archive),
      {fileop::digest(edits.inventory.document().originalBytes()), std::move(edits.inventoryAfter)},
      {edits.catalog ? fileop::digest(edits.catalog->document().originalBytes()) : std::string{}, std::move(edits.catalogAfter)},
      {hash.hex() + ".dat", stampId(*edits.activeState.stamp), std::move(edits.record.state)}, {}, Operation::Export,
      digest(edits.activeState.bytes), {}, {}, {}};
  if (edits.activeBackup.stamp) {
    plan.sourceBackup = Asset{hash.hex() + ".dat.bak", stampId(*edits.activeBackup.stamp), std::move(edits.activeBackup.bytes)};
  }
  for (auto& edit : edits.caches) {
    plan.cache.emplace(std::move(edit.leaf), Image{digest(edit.source.bytes), std::move(edit.after)});
  }
  for (auto& entry : edits.auxiliary) {
    plan.auxiliary.push_back({std::move(entry.relativePath), stampId(entry.stamp),
        entry.file ? std::move(entry.file->bytes) : std::nullopt});
  }
  return plan;
}

Plan makeDiscardPlan(const ProfileLease& lease, DownloadArchiveDiscardEdits edits) {
  auto& source = edits.source;
  const auto hash = source.identity.originalKey;
  Plan plan{hash, stampId(take(stateStamp(lease.directoryHandle()))), {}, stampId(source.archiveDirectoryStamp), {},
      {fileop::digest(source.configuration.document().originalBytes()), std::move(edits.archiveAfter)},
      {hash.hex() + ".dat", stampId(*source.state.stamp), std::move(source.state.bytes)}, {}, Operation::Discard, {}, {}, {}, {}};
  if (edits.backup.stamp) {
    plan.sourceBackup = Asset{hash.hex() + ".dat.bak", stampId(*edits.backup.stamp), std::move(edits.backup.bytes)};
  }
  for (auto& entry : source.auxiliary) {
    plan.auxiliary.push_back({std::move(entry.relativePath), stampId(entry.stamp),
        entry.file ? std::move(entry.file->bytes) : std::nullopt});
  }
  return plan;
}

void finish(const ProfileLease& lease, const Plan& plan, const std::string& encoded, DownloadArchiveRestoreObserver observer) {
  const auto root = lease.directoryHandle();
  const auto journal = capture(root, Journal);
  require(journal.bytes == encoded, ErrorCode::Conflict, "Archive restore journal changed");
  apply(lease, plan, observer);
  const auto current = capture(root, Journal);
  require(journal.matches(current) && current.stamp, ErrorCode::Conflict, "Archive restore journal changed after publication");
  take(retireStateFile(root, Journal, *current.stamp));
  take(syncStateDirectory(root));
}

void commit(const ProfileLease& lease, const Plan& plan, std::stop_token stop, DownloadArchiveRestoreObserver observer) {
  const auto encoded = encode(plan);
  require(encoded.size() <= compatibleConfigLimits().inputBytes, ErrorCode::Unsupported, "Archive intent exceeds its byte limit");
  static_cast<void>(decode(encoded));
  require(!stop.stop_requested(), ErrorCode::Cancelled, "Archive operation cancelled");
  publish(lease.directoryHandle(), Journal, JournalTemp, encoded, {});
  if (observer) { observer(DownloadArchiveRestoreStage::JournalSynced); }
  finish(lease, plan, encoded, observer);
}
} // namespace

bool hasPendingArchiveRestore(const ProfileLease& lease) noexcept {
  try {
    if (!lease.valid()) { return true; }
    for (const auto* leaf : {Journal, JournalTemp, CatalogTemp, InventoryTemp, TagsTemp}) {
      const auto current = detail::stateStampAt(lease.directoryHandle(), leaf);
      if (!current || *current) { return true; }
    }
    return false;
  } catch (...) { return true; }
}

std::expected<void, Error> restoreArchivedDownload(const ProfileLease& lease, const core::TorrentHash& hash,
    std::stop_token stop, DownloadArchiveRestoreObserver observer, const TagManagerStartupMode* tagMode,
    bool markRestored) try {
  auto access = take(lease.sourceAccess(true, stop));
  require(!hasPendingArchiveRestore(lease), ErrorCode::Conflict, "Recover the interrupted archive restoration first");
  auto edits = take(detail::prepareDownloadArchiveRestore(lease, hash, stop));
  std::optional<ArchiveTagRestore> tagEdit;
  if (tagMode) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    tagEdit = take(prepareArchiveTagRestore(lease, *tagMode, hash, edits.source.manualTags, seconds, markRestored, stop));
  }
  auto plan = makePlan(lease, std::move(edits));
  if (tagEdit) {
    plan.tags = Image{tagEdit->original ? fileop::digest(tagEdit->original->document().originalBytes()) : std::string{},
        std::move(tagEdit->after)};
  }
  commit(lease, plan, stop, observer);
  return {};
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const fileop::Failure& failure) {
  return std::unexpected(failure.error);
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io, "Archive restoration did not finish"});
}

std::expected<void, Error> archiveStoppedDownload(const ProfileLease& lease, const core::TorrentHash& hash,
    std::int64_t archivedMillis, std::span<const std::string> manualTags, std::stop_token stop,
    DownloadArchiveRestoreObserver observer) try {
  auto access = take(lease.sourceAccess(true, stop));
  require(!hasPendingArchiveRestore(lease), ErrorCode::Conflict, "Recover the interrupted archive operation first");
  auto edits = take(detail::prepareDownloadArchiveExport(lease, hash, archivedMillis, manualTags, stop));
  require(!stop.stop_requested(), ErrorCode::Cancelled, "Archive operation cancelled");
  if (!edits.archiveDirectoryStamp) {
    edits.archiveDirectory = take(createStateDirectory(lease.directoryHandle(), "dlarchive"));
    edits.archiveDirectoryStamp = take(stateStamp(edits.archiveDirectory.get()));
  }
  const auto archiveStamp = *edits.archiveDirectoryStamp;
  const auto plan = makeExportPlan(lease, std::move(edits), archiveStamp);
  commit(lease, plan, stop, observer);
  return {};
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const fileop::Failure& failure) {
  return std::unexpected(failure.error);
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io, "Archiving the download did not finish"});
}

std::expected<void, Error> removeArchivedDownload(const ProfileLease& lease, const core::TorrentHash& hash,
    std::stop_token stop, DownloadArchiveRestoreObserver observer) try {
  auto access = take(lease.sourceAccess(true, stop));
  require(!hasPendingArchiveRestore(lease), ErrorCode::Conflict, "Recover the interrupted archive operation first");
  const auto plan = makeDiscardPlan(lease, take(detail::prepareDownloadArchiveDiscard(lease, hash, stop)));
  commit(lease, plan, stop, observer);
  return {};
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const fileop::Failure& failure) {
  return std::unexpected(failure.error);
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io, "Removing the archived download did not finish"});
}

std::expected<bool, Error> recoverArchiveRestore(const ProfileLease& lease) try {
  if (!hasPendingArchiveRestore(lease)) { return false; }
  auto access = take(lease.sourceAccess(true));
  take(requireNoPendingSubscriptionAcquisition(lease));
  require(!hasPendingTorrentIntake(lease) && !hasPendingCategoryDeletion(lease), ErrorCode::Conflict,
      "An unrelated profile recovery conflicts with archive restoration");
  const auto root = lease.directoryHandle();
  auto journal = capture(root, Journal);
  const auto temporary = capture(root, JournalTemp);
  require(journal.bytes || temporary.bytes, ErrorCode::Conflict, "No complete archive restore journal is available");
  const auto encoded = journal.bytes.value_or(temporary.bytes.value_or(std::string{}));
  const auto plan = decode(encoded);
  require(stampId(take(stateStamp(root))) == plan.profileId, ErrorCode::Conflict, "Archive restore journal belongs to another profile");
  publish(root, Journal, JournalTemp, encoded, std::move(journal));
  finish(lease, plan, encoded, nullptr);
  return true;
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const fileop::Failure& failure) {
  return std::unexpected(failure.error);
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io, "Archive restoration recovery did not finish"});
}

} // namespace biglybtc::storage
