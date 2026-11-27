// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_archive_restore.hpp"
#include "download_archive_payload.hpp"
#include "download_archive_tags.hpp"
#include "encoded_document.hpp"
#include "file_operation_digest.hpp"
#include "file_operation_io.hpp"
#include "original_identity_digest.hpp"
#include "profile_transaction_pending.hpp"
#include "state_directory_listing.hpp"
#include "../platform/utf8_path.hpp"
#include "biglybtc/saved_download_tags.hpp"
#include "biglybtc/library.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <new>

namespace biglybtc::storage {
namespace {
using namespace detail;
using Node = core::Node;
namespace fs = std::filesystem;
using namespace archiveJournal;
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

struct MetainfoRemoval {
  fs::path leaf;
  std::string directoryId;
  std::optional<Asset> primary;
  std::optional<Asset> backup;
  std::optional<fs::path> configuredDirectory{};
};

struct PayloadAsset {
  fs::path path;
  std::string stamp;
};
struct PayloadRemoval {
  fs::path root;
  bool recycle;
  std::vector<PayloadAsset> files;
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
  std::optional<MetainfoRemoval> metainfo{};
  std::optional<PayloadRemoval> payload{};
};

std::string payloadStamp(const StateStamp& stamp) {
  return fileop::digest(stampId(stamp)+':'+std::to_string(stamp.size)+':'+std::to_string(stamp.links)
      +':'+std::to_string(stamp.owner)+':'+std::to_string(stamp.group)+':'+std::to_string(stamp.mode)
      +':'+std::to_string(stamp.modified.tv_sec)+':'+std::to_string(stamp.modified.tv_nsec)
      +':'+std::to_string(stamp.changed.tv_sec)+':'+std::to_string(stamp.changed.tv_nsec));
}

bool within(const fs::path& parent,const fs::path& path) {
  auto current=path.begin();
  for(const auto& part:parent){
    if(current==path.end()||!sameFilesystemComponent(part,*current)){return false;}
    ++current;
  }
  return true;
}

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
  if (plan.metainfo) {
    const auto& removal=*plan.metainfo;
    out += "8:metainfod";
    if (removal.backup) { out += "6:backup" + asset(*removal.backup); }
    if (removal.configuredDirectory) {
      out += "9:directory" + bytes(platform::pathToUtf8(*removal.configuredDirectory));
    }
    out += "2:id" + bytes(removal.directoryId) + "4:leaf" + bytes(platform::pathToUtf8(removal.leaf));
    if (removal.primary) { out += "7:primary" + asset(*removal.primary); }
    out += 'e';
  }
  if (plan.payload) {
    const auto& removal=*plan.payload;
    out += "7:payloadd5:filesl";
    for(const auto& file:removal.files){
      out += "d4:path"+bytes(platform::pathToUtf8(file.path))+"5:stamp"+bytes(file.stamp)+'e';
    }
    out += "e7:recyclei"+std::to_string(removal.recycle)+"e4:root"+bytes(platform::pathToUtf8(removal.root))+'e';
    return out+"7:versioni7ee";
  }
  if (plan.metainfo) { return out+(plan.metainfo->configuredDirectory?"7:versioni6ee":"7:versioni5ee"); }
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

fs::path metainfoBackup(const fs::path& leaf) {
  return platform::pathFromUtf8(platform::pathToUtf8(leaf) + ".bak");
}

void validateMetainfoLeaf(const fs::path& leaf) {
  require(leaf==leaf.filename()&&leaf.extension()==".torrent"&&leaf.native().size()<=251
      &&leaf.native().find('\0')==fs::path::string_type::npos&&leaf.native().find('\n')==fs::path::string_type::npos,
      ErrorCode::UnsafePath,
      "Archived metainfo must name an original .torrent file");
}

void validateMetainfo(const Asset& asset, const fs::path& leaf, const core::TorrentHash& hash) {
  require(asset.path==leaf&&asset.data.has_value(),ErrorCode::InvalidFormat,"Invalid archived metainfo intent");
  const auto metadata=EncodedDocument::parse(*asset.data,compatibleConfigLimits());
  require(metadata.has_value(),ErrorCode::InvalidFormat,"Archived metainfo is unreadable");
  take(validateOriginalTorrentKey(take(digestOriginalTorrentIdentity(metadata->document())),hash));
}

Plan decode(const std::string& encoded) {
  const auto document = core::Document::parse(encoded, compatibleConfigLimits());
  require(document.has_value(), ErrorCode::InvalidFormat, "Archive restore journal is incomplete or unreadable");
  const auto& root = document->root();
  const auto* version = root.find("version");
  require(version && version->type == Node::Type::Integer && version->integer >= 1 && version->integer <= 7, ErrorCode::Unsupported,
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
  const auto operation = version->integer >= 4 ? Operation::Discard
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
  if (version->integer==5||version->integer==6||root.find("metainfo")) {
    const auto* metainfo=root.find("metainfo");
    require(version->integer>=5&&metainfo&&metainfo->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,
        "Missing archived metainfo intent");
    MetainfoRemoval removal{platform::pathFromUtf8(value(*metainfo,"leaf")),std::string(value(*metainfo,"id")),{}, {}};
    validateMetainfoLeaf(removal.leaf);
    if (version->integer==6||(version->integer==7&&metainfo->find("directory"))) {
      removal.configuredDirectory=platform::pathFromUtf8(value(*metainfo,"directory"));
      fileop::pathShape(*removal.configuredDirectory);
    }
    require(!removal.directoryId.empty(),ErrorCode::InvalidFormat,"Archived metainfo has no directory identity");
    if (const auto* primary=metainfo->find("primary")) {
      removal.primary=readAsset(*primary);
      validateMetainfo(*removal.primary,removal.leaf,*hash);
    }
    if (const auto* backup=metainfo->find("backup")) {
      removal.backup=readAsset(*backup);
      validateMetainfo(*removal.backup,metainfoBackup(removal.leaf),*hash);
    }
    plan.metainfo=std::move(removal);
  }
  if(version->integer==7){
    const auto* payload=root.find("payload");
    require(payload&&payload->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Missing archived payload intent");
    const auto* recycle=payload->find("recycle");
    const auto* entries=payload->find("files");
    require(recycle&&recycle->type==Node::Type::Integer&&(recycle->integer==0||recycle->integer==1)
        &&entries&&entries->type==Node::Type::List&&!entries->children.empty()&&entries->children.size()<=100000,
        ErrorCode::InvalidFormat,"Invalid archived payload journal geometry");
    PayloadRemoval removal{platform::pathFromUtf8(value(*payload,"root")),recycle->integer==1,{}};
    fileop::pathShape(removal.root);
    std::set<fs::path> paths;
    for(const auto& entry:entries->children){
      auto path=platform::pathFromUtf8(value(entry,"path"));
      fileop::pathShape(path);
      const auto stamp=value(entry,"stamp");
      require(within(removal.root,path)&&paths.insert(path).second&&stamp.size()==64,
          ErrorCode::UnsafePath,"Invalid archived payload journal file");
      removal.files.push_back({std::move(path),std::string(stamp)});
    }
    plan.payload=std::move(removal);
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

bool metainfoInside(const fs::path& path, const fs::path& root) {
  if (!root.is_absolute()) { return false; }
  return std::ranges::mismatch(path,root,sameFilesystemComponent).in2==root.end();
}

bool sameMetainfoPath(const fs::path& left, const fs::path& right) {
  const FilesystemPathLess less;
  if (!less(left,right)&&!less(right,left)) { return true; }
  std::error_code error;
  if (sameFilesystemComponent(left.filename(),right.filename())
      &&fs::equivalent(left.parent_path(),right.parent_path(),error)) { return true; }
  return fs::equivalent(left,right,error);
}

std::optional<fs::path> configuredMetainfoDirectory(const ProfileLease& lease) {
  const auto root=lease.directoryHandle();
  require(!take(stateStampAt(root,"biglybt.config.saving")),ErrorCode::Conflict,
      "Resolve the pending profile configuration before removing external metainfo");
  const auto source=capture(root,"biglybt.config");
  if (!source.bytes) { return {}; }
  const auto profile=core::Document::parse(*source.bytes,compatibleConfigLimits());
  require(profile&&profile->root().type==Node::Type::Dictionary,ErrorCode::InvalidFormat,
      "Cannot read the saved metainfo directory setting");
  const auto* directory=profile->find("General_sDefaultTorrent_Directory");
  if (!directory) { return {}; }
  require(directory->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Invalid saved metainfo directory");
  if (directory->bytes.empty()) { return {}; }
  auto path=platform::pathFromUtf8(directory->bytes);
  fileop::pathShape(path);
  std::error_code error;
  auto canonical=fs::canonical(path,error);
  require(!error,ErrorCode::UnsafePath,"Cannot resolve the saved metainfo directory");
  return canonical;
}

StateDescriptor openConfiguredMetainfoDirectory(const fs::path& path,std::string_view expectedId={}) {
  auto directory=take(openStatePathDirectory(path));
  const auto stamp=take(stateStamp(directory.get()));
  require(stamp.directory&&stamp.owned&&(expectedId.empty()||stampId(stamp)==expectedId),
      ErrorCode::Conflict,"Saved metainfo directory identity changed");
  return directory;
}

void protectMetainfoPayload(const fs::path& path, std::string_view saveDirectory, std::string_view saveFile={}) {
  if (saveDirectory.empty()) { return; }
  auto root=platform::pathFromUtf8(saveDirectory);
  if (!saveFile.empty()) { root/=platform::pathFromUtf8(saveFile); }
  root=root.lexically_normal();
  const auto backup=metainfoBackup(path);
  std::error_code error;
  const auto resolved=root.is_absolute()?fs::weakly_canonical(root,error):fs::path{};
  require(!metainfoInside(path,root)&&!metainfoInside(backup,root)
      &&(error||(!metainfoInside(path,resolved)&&!metainfoInside(backup,resolved))),ErrorCode::Conflict,
      "The archived metainfo file overlaps a saved payload");
}

void verifyMetainfoReferences(StateHandle root, const fs::path& path, const core::Document& catalog) {
  const auto backup=metainfoBackup(path);
  const auto matches=[&](const fs::path& candidate, const fs::path& target) {
    if (!candidate.is_absolute()) { return sameFilesystemComponent(candidate.filename(),target.filename()); }
    return sameMetainfoPath(candidate,target);
  };
  const auto protectReference=[&](std::string_view raw) {
    if (raw.empty()) { return; }
    const auto candidate=platform::pathFromUtf8(raw).lexically_normal();
    require(!matches(candidate,path)&&!matches(candidate,backup),ErrorCode::Conflict,
        "Another download references the selected archived metainfo");
  };
  const auto active=capture(root,Inventory);
  require(!take(stateStampAt(root,"downloads.config.saving")),ErrorCode::Conflict,"The library inventory is being saved");
  if (active.bytes) {
    const auto document=core::Document::parse(*active.bytes,compatibleConfigLimits());
    require(document.has_value(),ErrorCode::InvalidFormat,"The library inventory is unreadable");
    const auto library=core::readLibrary(*document);
    require(library&&library->issues.empty(),ErrorCode::InvalidFormat,"The library inventory is incomplete");
    for (const auto& row:library->downloads) {
      protectReference(row.torrentFile);
      protectMetainfoPayload(path,row.saveDirectory,row.saveFile.value_or(""));
    }
  }
  const auto* stubs=catalog.find("stubs");
  require(stubs&&stubs->type==Node::Type::List,ErrorCode::InvalidFormat,"The archive inventory is incomplete");
  for (const auto& archiveRow:stubs->children) {
    const auto* inventory=archiveRow.find("gm");
    if (!inventory) { continue; }
    const auto read=[&](const char* name) -> std::string_view {
      const auto* field=inventory->find(name);
      return field&&field->type==Node::Type::Bytes?field->bytes:std::string_view{};
    };
    protectReference(read("torrent"));
    protectMetainfoPayload(path,read("save_dir"),read("save_file"));
  }
}

void verifyMetainfoFiles(StateHandle directory, const MetainfoRemoval& removal, bool catalogPublished) {
  const auto verify=[&](const fs::path& leaf, const std::optional<Asset>& expected) {
    const auto current=capture(directory,leaf);
    if (!current.stamp&&catalogPublished) { return; }
    if (expected) { verifyFile(current,*expected,true); }
    else { require(!current.stamp,ErrorCode::Conflict,"An unreviewed metainfo file appeared"); }
  };
  verify(removal.leaf,removal.primary);
  verify(metainfoBackup(removal.leaf),removal.backup);
}

void verifyPayload(const ProfileLease& lease,const Plan& plan,bool catalogPublished,const ArchivePayloadRecycler& recycle) {
  const auto& payload=*plan.payload;
  require(!payload.recycle||bool(recycle),ErrorCode::Unsupported,"The operating system trash is unavailable");
  const auto profile=platform::pathFromUtf8(fileop::profilePath(lease));
  require(!within(profile,payload.root)&&!within(payload.root,profile),ErrorCode::UnsafePath,
      "Archived payload overlaps the saved profile");
  ArchivePayloadSelection named{payload.root,{}};
  for(const auto& file:payload.files){
    const auto stamp=fileop::inspect(file.path);
    if(!stamp&&catalogPublished){continue;}
    require(stamp&&stamp->links==1&&payloadStamp(*stamp)==file.stamp,ErrorCode::Conflict,
        "An archived payload file changed before removal");
    named.files.push_back({file.path,*stamp});
  }
  take(verifyArchivePayloadReferences(lease,plan.hash,named));
}

void applyPayload(const PayloadRemoval& payload,const ArchivePayloadRecycler& recycle) {
  for(const auto& file:payload.files){
    const auto stamp=fileop::inspect(file.path);
    if(!stamp){continue;}
    require(stamp->links==1&&payloadStamp(*stamp)==file.stamp,ErrorCode::Conflict,
        "An archived payload file changed before removal");
    if(payload.recycle){
      take(recycle(file.path));
      require(!fileop::inspect(file.path),ErrorCode::Conflict,"The archived payload path still contains data");
    }else{
      fileop::erase(file.path,*stamp);
    }
  }
}

void applyDiscard(const ProfileLease& lease, StateHandle root, StateHandle archive, const Plan& plan,
    DownloadArchiveRestoreObserver observer,const ArchivePayloadRecycler& recycle) {
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
  const auto metainfoDirectory=[&](bool published) {
    const auto& removal=*plan.metainfo;
    auto path=platform::pathFromUtf8(fileop::profilePath(lease))/"torrents";
    if (removal.configuredDirectory) {
      const auto current=configuredMetainfoDirectory(lease);
      require(current&&sameMetainfoPath(*current,*removal.configuredDirectory),ErrorCode::Conflict,
          "Saved metainfo directory setting changed during archive removal");
      path=*removal.configuredDirectory;
    }
    auto directory=removal.configuredDirectory?openConfiguredMetainfoDirectory(path,removal.directoryId)
        :openDirectory(root,"torrents",removal.directoryId);
    verifyMetainfoFiles(directory.get(),removal,published);
    const auto after=core::Document::parse(plan.catalog.after,compatibleConfigLimits());
    require(after.has_value(),ErrorCode::InvalidFormat,"The archive inventory is unreadable");
    verifyMetainfoReferences(root,path/removal.leaf,*after);
    return directory;
  };
  if (plan.metainfo) {
    static_cast<void>(metainfoDirectory(catalogPublished));
  }
  if(plan.payload){verifyPayload(lease,plan,catalogPublished,recycle);}

  publish(root, DownloadArchiveFile, CatalogTemp, plan.catalog.after, std::move(catalog));
  if (observer) { observer(DownloadArchiveRestoreStage::CatalogPublished); }
  const auto namedArchive = take(stateStampAt(root, "dlarchive"));
  require(namedArchive && stampId(*namedArchive) == plan.archiveId
      && capture(root, DownloadArchiveFile).bytes == plan.catalog.after, ErrorCode::Conflict,
      "Archive catalog changed before removing archived state");
  if(plan.payload){
    verifyPayload(lease,plan,true,recycle);
    applyPayload(*plan.payload,recycle);
  }
  if (plan.metainfo) {
    const auto directory=metainfoDirectory(true);
    if (plan.metainfo->primary) { retireArchiveFile(directory.get(),*plan.metainfo->primary); }
    if (plan.metainfo->backup) { retireArchiveFile(directory.get(),*plan.metainfo->backup); }
  }
  retireArchiveFile(archive, plan.state);
  if (plan.sourceBackup) { retireArchiveFile(archive, *plan.sourceBackup); }
  retireTree(archive, hash, plan.auxiliary);
}

void apply(const ProfileLease& lease, const Plan& plan, DownloadArchiveRestoreObserver observer,
    const ArchivePayloadRecycler& recycle) {
  const auto root = lease.directoryHandle();
  require(stampId(take(stateStamp(root))) == plan.profileId, ErrorCode::Conflict, "Archive restore journal belongs to another profile");
  const auto archive = openDirectory(root, "dlarchive", plan.archiveId);
  if (plan.operation == Operation::Discard) {
    applyDiscard(lease, root, archive.get(), plan, observer,recycle);
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

std::optional<MetainfoRemoval> prepareMetainfoRemoval(const ProfileLease& lease,
    const DownloadArchiveRestoreSource& archive, const std::string& catalogAfter) {
  const auto& inventory=archive.inventoryRow;
  const auto read=[&](const char* name) -> std::string_view {
    const auto* field=inventory.find(name);
    return field&&field->type==Node::Type::Bytes?field->bytes:std::string_view{};
  };
  const auto raw=read("torrent");
  if (raw.empty()) { return {}; }
  const auto profile=platform::pathFromUtf8(fileop::profilePath(lease));
  const auto path=platform::pathFromUtf8(raw);
  fileop::pathShape(path);
  const auto hash=archive.identity.originalKey;
  const auto state=profile/"active"/(hash.hex()+".dat");
  if (sameMetainfoPath(path,state)||sameMetainfoPath(path,metainfoBackup(state))) { return {}; }
  std::optional<fs::path> external;
  if (!sameMetainfoPath(path.parent_path(),profile/"torrents")) {
    external=configuredMetainfoDirectory(lease);
    require(external&&sameMetainfoPath(path.parent_path(),*external),ErrorCode::Unsupported,
        "The saved .torrent file is outside the configured metainfo directories");
  }
  validateMetainfoLeaf(path.filename());
  protectMetainfoPayload(path,read("save_dir"),read("save_file"));
  const auto catalog=core::Document::parse(catalogAfter,compatibleConfigLimits());
  require(catalog.has_value(),ErrorCode::InvalidFormat,"The archive inventory is unreadable");
  const auto root=lease.directoryHandle();
  verifyMetainfoReferences(root,path,*catalog);
  if (!external&&!take(stateStampAt(root,"torrents"))) { return {}; }
  const auto directory=external?openConfiguredMetainfoDirectory(*external):openDirectory(root,"torrents");
  const auto captureAsset=[&](const fs::path& leaf) -> std::optional<Asset> {
    auto current=capture(directory.get(),leaf);
    if (!current.stamp) { return {}; }
    Asset asset{leaf,stampId(*current.stamp),std::move(current.bytes)};
    validateMetainfo(asset,leaf,hash);
    return asset;
  };
  auto result=MetainfoRemoval{path.filename(),stampId(take(stateStamp(directory.get()))),
      captureAsset(path.filename()),captureAsset(metainfoBackup(path.filename())),std::move(external)};
  return result;
}

Plan makeDiscardPlan(const ProfileLease& lease, DownloadArchiveDiscardEdits edits, bool deleteMetainfo,
    bool deletePayload,const ArchivePayloadRecycler& recycle,std::stop_token stop) {
  auto& source = edits.source;
  const auto hash = source.identity.originalKey;
  auto metainfo=deleteMetainfo?prepareMetainfoRemoval(lease,source,edits.archiveAfter):std::nullopt;
  Plan plan{hash, stampId(take(stateStamp(lease.directoryHandle()))), {}, stampId(source.archiveDirectoryStamp), {},
      {fileop::digest(source.configuration.document().originalBytes()), std::move(edits.archiveAfter)},
      {hash.hex() + ".dat", stampId(*source.state.stamp), {}}, {}, Operation::Discard, {}, {}, {}, {}};
  plan.metainfo=std::move(metainfo);
  if(deletePayload){
    auto selection=take(selectArchivePayload(lease,source,stop));
    take(verifyArchivePayloadReferences(lease,hash,selection,stop));
    if(!selection.files.empty()){
      const auto recycleEnabled=take(payloadRecyclePreference(lease,stop));
      require(!recycleEnabled||bool(recycle),ErrorCode::Unsupported,"The operating system trash is unavailable");
      PayloadRemoval removal{std::move(selection.root),recycleEnabled,{}};
      for(auto& file:selection.files){removal.files.push_back({std::move(file.path),payloadStamp(file.stamp)});}
      plan.payload=std::move(removal);
    }
  }
  plan.state.data=std::move(source.state.bytes);
  if (edits.backup.stamp) {
    plan.sourceBackup = Asset{hash.hex() + ".dat.bak", stampId(*edits.backup.stamp), std::move(edits.backup.bytes)};
  }
  for (auto& entry : source.auxiliary) {
    plan.auxiliary.push_back({std::move(entry.relativePath), stampId(entry.stamp),
        entry.file ? std::move(entry.file->bytes) : std::nullopt});
  }
  return plan;
}

void finish(const ProfileLease& lease, const Plan& plan, const std::string& encoded, DownloadArchiveRestoreObserver observer,
    const ArchivePayloadRecycler& recycle = {}) {
  const auto root = lease.directoryHandle();
  const auto journal = capture(root, Journal);
  require(journal.bytes == encoded, ErrorCode::Conflict, "Archive restore journal changed");
  apply(lease, plan, observer,recycle);
  const auto current = capture(root, Journal);
  require(journal.matches(current) && current.stamp, ErrorCode::Conflict, "Archive restore journal changed after publication");
  take(retireStateFile(root, Journal, *current.stamp));
  take(syncStateDirectory(root));
}

void commit(const ProfileLease& lease, const Plan& plan, std::stop_token stop, DownloadArchiveRestoreObserver observer,
    const ArchivePayloadRecycler& recycle = {}) {
  const auto encoded = encode(plan);
  require(encoded.size() <= compatibleConfigLimits().inputBytes, ErrorCode::Unsupported, "Archive intent exceeds its byte limit");
  static_cast<void>(decode(encoded));
  require(!stop.stop_requested(), ErrorCode::Cancelled, "Archive operation cancelled");
  publish(lease.directoryHandle(), Journal, JournalTemp, encoded, {});
  if (observer) { observer(DownloadArchiveRestoreStage::JournalSynced); }
  finish(lease, plan, encoded, observer,recycle);
}
} // namespace

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
    std::stop_token stop, DownloadArchiveRestoreObserver observer, bool deleteMetainfo,
    bool deletePayload, const ArchivePayloadRecycler& recycle, std::string_view confirmedRow) try {
  auto access = take(lease.sourceAccess(true, stop));
  require(!hasPendingArchiveRestore(lease), ErrorCode::Conflict, "Recover the interrupted archive operation first");
  auto edits = take(detail::prepareDownloadArchiveDiscard(lease, hash, stop));
  if (deletePayload && !confirmedRow.empty()) {
    require(archiveRowRevision(edits.source.configuration, edits.source.rowIndex) == confirmedRow,
        ErrorCode::Conflict, "The selected archive entry changed since it was displayed");
  }
  const auto plan = makeDiscardPlan(lease, std::move(edits), deleteMetainfo, deletePayload, recycle, stop);
  commit(lease, plan, stop, observer,recycle);
  return {};
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const fileop::Failure& failure) {
  return std::unexpected(failure.error);
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io, "Removing the archived download did not finish"});
}

std::expected<bool, Error> recoverArchiveRestore(const ProfileLease& lease,const ArchivePayloadRecycler& recycle) try {
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
  finish(lease, plan, encoded, nullptr,recycle);
  return true;
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const fileop::Failure& failure) {
  return std::unexpected(failure.error);
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io, "Archive restoration recovery did not finish"});
}

} // namespace biglybtc::storage
