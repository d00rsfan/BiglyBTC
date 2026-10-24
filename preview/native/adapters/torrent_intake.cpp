#include "torrent_intake.hpp"
#include "encoded_document.hpp"
#include "file_io.hpp"

#include "biglybtc/state_cache.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <libtorrent/torrent_info.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <exception>
#include <fcntl.h>
#include <limits>
#include <linux/fs.h>
#include <new>
#include <set>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace biglybtc::storage {
namespace {
namespace fs = std::filesystem;
using detail::Descriptor;
using Node = core::Node;
constexpr std::size_t MaximumMetadata = 32 * 1024 * 1024;
constexpr std::size_t MaximumInventory = 64 * 1024 * 1024;
constexpr std::size_t MaximumJournal = 192 * 1024 * 1024;
constexpr const char* Journal = "BiglyBTC.torrent-import";
constexpr const char* JournalSaving = "BiglyBTC.torrent-import.saving";
constexpr const char* InventorySaving = "BiglyBTC.torrent-import.inventory";
constexpr const char* BackupSaving = "BiglyBTC.torrent-import.backup";
struct Failure { Error error; };
[[noreturn]] void fail(ErrorCode code, std::string message) { throw Failure{{code, std::move(message)}}; }
void require(bool value, ErrorCode code, const char* message) { if (!value) { fail(code, message); } }
template<class T> T take(std::expected<T, Error> value) {
  if (!value) { throw Failure{std::move(value.error())}; }
  return std::move(*value);
}
void sync(int fd) { require(::fsync(fd) == 0, ErrorCode::Io, "Cannot synchronize intake transaction"); }
bool identity(const struct stat& a, const struct stat& b) {
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
bool unchanged(const struct stat& a, const struct stat& b) {
  return identity(a, b) && a.st_size == b.st_size && a.st_mode == b.st_mode && a.st_nlink == b.st_nlink
      && a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec
      && a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
std::optional<struct stat> named(int directory, const std::string& name) {
  struct stat result{};
  if (::fstatat(directory, name.c_str(), &result, AT_SYMLINK_NOFOLLOW) == 0) { return result; }
  require(errno == ENOENT, ErrorCode::Io, "Cannot inspect intake pathname");
  return std::nullopt;
}
void regular(const struct stat& value) {
  require(S_ISREG(value.st_mode) && value.st_uid == ::geteuid() && value.st_nlink == 1,
      ErrorCode::UnsafePath, "Intake file must be an owned regular file with one link");
}
struct File { std::string bytes; struct stat info{}; };
std::optional<File> read(int directory, const std::string& name, std::size_t maximum) {
  const auto before = named(directory, name);
  if (!before) { return std::nullopt; }
  regular(*before);
  Descriptor fd(::openat(directory, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  require(fd.get() >= 0, ErrorCode::UnsafePath, "Cannot open intake file safely");
  struct stat opened{};
  require(::fstat(fd.get(), &opened) == 0 && unchanged(*before, opened), ErrorCode::Conflict, "Intake file changed before reading");
  regular(opened);
  require(opened.st_size >= 0 && static_cast<std::uint64_t>(opened.st_size) <= maximum,
      ErrorCode::InvalidFormat, "Intake file exceeds its bounded input size");
  File result{std::string(static_cast<std::size_t>(opened.st_size), '\0'), opened};
  std::size_t offset = 0;
  while (offset < result.bytes.size()) {
    const auto count = ::read(fd.get(), result.bytes.data() + offset, result.bytes.size() - offset);
    if (count < 0 && errno == EINTR) { continue; }
    require(count > 0, ErrorCode::Io, "Intake file read was incomplete");
    offset += static_cast<std::size_t>(count);
  }
  struct stat after{};
  const auto path = named(directory, name);
  require(::fstat(fd.get(), &after) == 0 && path && unchanged(opened, after) && unchanged(after, *path),
      ErrorCode::Conflict, "Intake file changed while reading");
  return result;
}
void absent(int directory, const std::string& name) {
  require(!named(directory, name), ErrorCode::Conflict, "An intake target or recovery pathname already exists");
}
void pathShape(const fs::path& path) {
  const auto& text = path.native();
  require(path.is_absolute() && path.lexically_normal() == path && text.size() <= 4096
      && text.find('\0') == std::string::npos, ErrorCode::UnsafePath, "Intake path must be absolute and normalized");
  for (const auto& part : path.relative_path()) {
    require(part != "." && part != ".." && !part.empty(), ErrorCode::UnsafePath, "Unsafe intake path component");
  }
}
Descriptor directory(const fs::path& path) {
  pathShape(path);
  Descriptor current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  require(current.get() >= 0, ErrorCode::Io, "Cannot open filesystem root");
  for (const auto& part : path.relative_path()) {
    Descriptor next(::openat(current.get(), part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    require(next.get() >= 0, ErrorCode::UnsafePath, "Intake path has an inaccessible or symbolic directory");
    current = std::move(next);
  }
  return current;
}
void destinationPath(const fs::path& path) {
  pathShape(path);
  Descriptor current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  require(current.get() >= 0, ErrorCode::Io, "Cannot inspect destination root");
  // Missing suffixes are only recorded; intake never creates payload directories.
  for (const auto& part : path.relative_path()) {
    Descriptor next(::openat(current.get(), part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (next.get() < 0 && errno == ENOENT) { return; }
    require(next.get() >= 0, ErrorCode::UnsafePath, "Destination contains a file, symlink or inaccessible directory");
    current = std::move(next);
  }
}
std::string profilePath(const ProfileLease& lease) {
  require(lease.valid(), ErrorCode::UnsafePath, "Torrent intake requires a held profile lease");
  std::array<char, 4097> buffer{};
  const std::string link = "/proc/self/fd/" + std::to_string(lease.directoryHandle());
  const auto count = ::readlink(link.c_str(), buffer.data(), buffer.size());
  require(count > 0 && static_cast<std::size_t>(count) < buffer.size(), ErrorCode::UnsafePath, "Cannot resolve held profile identity");
  const std::string path(buffer.data(), static_cast<std::size_t>(count));
  auto opened = directory(path);
  struct stat held{}, actual{};
  require(::fstat(lease.directoryHandle(), &held) == 0 && ::fstat(opened.get(), &actual) == 0
      && identity(held, actual) && held.st_uid == ::geteuid(), ErrorCode::UnsafePath, "Held profile path changed or is not owned");
  return path;
}
Descriptor childDirectory(int root, const char* name, bool create) {
  auto value = named(root, name);
  if (!value && create) {
    require(::mkdirat(root, name, 0700) == 0, ErrorCode::Io, "Cannot create owned intake directory");
    sync(root);
    value = named(root, name);
  }
  if (!value) { return Descriptor(-1); }
  require(S_ISDIR(value->st_mode) && value->st_uid == ::geteuid(), ErrorCode::UnsafePath, "Intake directory is not an owned nonsymlink directory");
  Descriptor fd(::openat(root, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  struct stat opened{};
  require(fd.get() >= 0 && ::fstat(fd.get(), &opened) == 0 && identity(*value, opened),
      ErrorCode::Conflict, "Intake directory changed while opening");
  return fd;
}
void childIdentity(int root, const char* name, int fd) {
  const auto value = named(root, name);
  struct stat opened{};
  require(value && S_ISDIR(value->st_mode) && ::fstat(fd, &opened) == 0 && identity(*value, opened),
      ErrorCode::Conflict, "Intake directory was replaced");
}
std::string b(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string i(std::int64_t value) { return 'i' + std::to_string(value) + 'e'; }
core::Document parse(std::string bytes, std::size_t maximum = MaximumInventory) {
  auto limits = compatibleConfigLimits(); limits.inputBytes = maximum; limits.stringBytes = maximum;
  auto result = core::Document::parse(std::move(bytes), limits);
  require(result && result->root().type == Node::Type::Dictionary, ErrorCode::InvalidFormat, "Intake document is not a complete bounded dictionary");
  return std::move(*result);
}
void put(core::Document& document, std::string_view key, std::string encoded) {
  require(document.replaceValue(key, std::move(encoded)).has_value(), ErrorCode::InvalidFormat, "Intake structured edit exceeds its limits");
}
core::Document section(const Node* node) {
  if (!node) { return parse("de"); }
  require(node->type == Node::Type::Dictionary, ErrorCode::InvalidFormat, "Imported state section is not a dictionary");
  auto encoded = core::encodeValue(*node, compatibleConfigLimits());
  require(encoded.has_value(), ErrorCode::InvalidFormat, "Cannot encode imported state section");
  return parse(std::move(*encoded));
}
void uniqueKeys(const Node& node, std::size_t depth = 0, bool sorted = false) {
  require(depth <= 128, ErrorCode::InvalidFormat, "Imported metadata exceeds depth limit");
  if (node.type == Node::Type::Dictionary) {
    std::set<std::string_view> keys;
    std::string_view previous;
    for (std::size_t n = 0; n < node.children.size(); n += 2) {
      const auto key = node.children[n].bytes;
      require(keys.insert(key).second && (!sorted || n == 0 || previous < key),
          ErrorCode::InvalidFormat, "Imported metadata has duplicate/noncanonical info keys");
      previous = key;
      uniqueKeys(node.children[n + 1], depth + 1, sorted);
    }
  } else if (node.type == Node::Type::List) {
    for (const auto& child : node.children) { uniqueKeys(child, depth + 1, sorted); }
  }
}
std::string raw(const core::Document& document, const Node& value) {
  return document.originalBytes().substr(value.begin, value.end - value.begin);
}
struct Plan {
  core::TorrentHash hash;
  std::string metadata, destination, profile, state, inventory;
  std::optional<std::string> before, oldBackup;
  bool stopped, forceStart;
  mode_t inventoryMode;
};
Plan plan(std::string metadata, std::string destination, std::string profile,
    std::optional<std::string> before, std::optional<std::string> oldBackup, bool stopped, mode_t mode, bool forceStart) {
  auto torrent = parse(metadata, MaximumMetadata);
  uniqueKeys(torrent.root());
  const auto* info = torrent.find("info");
  require(info && info->type == Node::Type::Dictionary && !info->find("hash-override"),
      ErrorCode::Unsupported, "Metadata-pending/hash-override torrents require their magnet intake");
  uniqueKeys(*info, 0, true);
  auto original = compat::originalLayout(torrent);
  require(original.has_value(), ErrorCode::InvalidFormat, "Unsafe or unsupported original file layout");
  libtorrent::load_torrent_limits limits;
  limits.max_buffer_size = static_cast<int>(MaximumMetadata);
  limits.max_pieces = 1000000;
  limits.max_decode_depth = 128;
  limits.max_decode_tokens = 2000000;
  auto engine = [&] {
    try { return libtorrent::torrent_info(libtorrent::span<const char>(metadata.data(), metadata.size()), limits, libtorrent::from_span); }
    catch (const std::bad_alloc&) { throw; }
    catch (const std::exception&) { fail(ErrorCode::InvalidFormat, "BitTorrent metadata failed bounded engine validation"); }
  }();
  require(engine.is_valid() && engine.num_files() > 0 && engine.num_files() <= 100000
      && engine.num_pieces() <= 1000000, ErrorCode::InvalidFormat, "Invalid or over-limit BitTorrent metadata");
  const auto hashes = engine.info_hashes();
  require(hashes.has_v1() || hashes.has_v2(), ErrorCode::InvalidFormat, "Metadata has no actual info hash");
  // Original getHash uses v1 for ordinary hybrid and the first 20 v2 bytes for
  // pure v2. Native state readers already accept that original short identity.
  const char* digest = hashes.has_v1() ? hashes.v1.data() : hashes.v2.data();
  auto hash = core::TorrentHash::parse(std::string_view(digest, 20));
  require(hash.has_value(), ErrorCode::InvalidFormat, "Invalid imported torrent identity");
  if (const auto* privateProperties = torrent.find("azureus_private_properties")) {
    require(privateProperties->type == Node::Type::Dictionary && !privateProperties->find("hybrid_hash_v2"),
        ErrorCode::Unsupported, "Alternate hybrid identity needs its qualified adapter");
  }
  auto inventory = parse(before.value_or("d9:downloadslee"));
  const auto* downloads = inventory.find("downloads");
  require(downloads && downloads->type == Node::Type::List, ErrorCode::Unsupported, "Intake requires current list-form inventory");
  auto saved = core::readLibrary(inventory);
  require(saved && saved->issues.empty() && saved->downloads.size() < 100000, ErrorCode::InvalidFormat, "Invalid original download inventory");
  std::set<std::string> identities;
  std::int64_t position = 0;
  for (const auto& entry : saved->downloads) {
    require(entry.hash && identities.insert(entry.hash->hex()).second, ErrorCode::Conflict, "Inventory has missing or duplicate torrent identity");
    require(*entry.hash != *hash, ErrorCode::Conflict, "Torrent is already in the saved inventory");
    if (entry.position) { position = std::max(position, *entry.position); }
  }
  for (const auto& paused : saved->paused) {
    require(paused.hash != *hash, ErrorCode::Conflict, "An existing pause record already owns the imported identity");
  }
  require(position < std::numeric_limits<std::int64_t>::max(), ErrorCode::InvalidFormat, "Inventory position overflow");
  const auto archive = profile + "/torrents/" + hash->hex() + ".torrent";
  std::string entry = "d" + b("torrent_hash") + b(std::string_view(digest, 20)) + b("torrent") + b(archive)
      + b("save_dir") + b(destination) + b("save_file") + b(original->name) + b("persistent") + "i1e"
      + b("state") + i(stopped && !forceStart ? 70 : 0) + b("position") + i(position + 1)
      + b("forceStart") + i(forceStart ? 1 : 0)
      + b("downloaded") + "i0e" + b("uploaded") + "i0e" + b("completedbytes") + "i0e"
      + b("maxdl") + "i0e" + b("maxul") + "i0e" + b("file_priorities") + 'l';
  for (std::size_t n = 0; n < original->files.size(); ++n) { entry += "i-1e"; }
  entry += "ee";
  std::string list = raw(inventory, *downloads);
  list.insert(list.size() - 1, entry);
  put(inventory, "downloads", std::move(list));
  auto attributes = section(torrent.find("attributes"));
  std::string stores = "l";
  for (std::size_t n = 0; n < original->files.size(); ++n) { stores += "1:L"; }
  put(attributes, "storetypes", stores + 'e');
  put(attributes, "networks", "l6:Publice");
  put(attributes, "resumecomplete", "i1e");
  put(attributes, "filedownloaded", "de");
  put(attributes, "filelinks2", "le");
  put(attributes, "BiglyBTC.completion", "de");
  put(torrent, "attributes", attributes.serialize());
  auto resume = section(torrent.find("resume"));
  auto data = section(resume.find("data"));
  put(data, "valid", "i0e"); put(data, "blocks", "de");
  put(data, "resume data", b(std::string(static_cast<std::size_t>(engine.num_pieces()), '\2')));
  put(resume, "data", data.serialize()); put(torrent, "resume", resume.serialize());
  auto after = inventory.serialize();
  auto state = torrent.serialize();
  require(after.size() <= MaximumInventory && state.size() <= MaximumInventory, ErrorCode::InvalidFormat, "Intake outputs exceed limits");
  auto verify = core::readLibrary(parse(after));
  require(verify && verify->issues.empty() && verify->downloads.size() == saved->downloads.size() + 1,
      ErrorCode::InvalidFormat, "New inventory failed its original reader");
  return {*hash, std::move(metadata), std::move(destination), std::move(profile), std::move(state), std::move(after),
      std::move(before), std::move(oldBackup), stopped, forceStart, static_cast<mode_t>(mode & 0777)};
}
std::string journalBytes(const Plan& p) {
  return "d" + b("version") + "i1e" + b("metadata") + b(p.metadata) + b("destination") + b(p.destination)
      + b("profile") + b(p.profile) + b("stopped") + i(p.stopped ? 1 : 0) + b("mode") + i(p.inventoryMode)
      + b("before_present") + i(p.before ? 1 : 0) + b("before") + b(p.before.value_or(""))
      + b("backup_present") + i(p.oldBackup ? 1 : 0) + b("backup") + b(p.oldBackup.value_or(""))
      + b("forceStart") + i(p.forceStart ? 1 : 0) + 'e';
}
std::string text(const Node* value) {
  require(value && value->type == Node::Type::Bytes, ErrorCode::InvalidFormat, "Invalid intake journal string");
  return std::string(value->bytes);
}
std::int64_t integer(const Node* value) {
  require(value && value->type == Node::Type::Integer, ErrorCode::InvalidFormat, "Invalid intake journal integer");
  return value->integer;
}
Plan readJournal(std::string bytes, const std::string& profile) {
  auto document = parse(std::move(bytes), MaximumJournal);
  uniqueKeys(document.root());
  const auto* force = document.find("forceStart");
  require(((document.root().children.size() == 20 && !force) || (document.root().children.size() == 22 && force))
      && integer(document.find("version")) == 1,
      ErrorCode::Unsupported, "Unknown intake journal version or fields");
  const auto before = integer(document.find("before_present"));
  const auto backup = integer(document.find("backup_present"));
  const auto stopped = integer(document.find("stopped"));
  const auto mode = integer(document.find("mode"));
  const auto forceStart = force ? integer(force) : 0;
  require((before == 0 || before == 1) && (backup == 0 || backup == 1) && (stopped == 0 || stopped == 1)
      && (forceStart == 0 || forceStart == 1) && mode >= 0 && mode <= 0777 && text(document.find("profile")) == profile,
      ErrorCode::Conflict, "Intake journal identity/flags differ from this profile");
  auto destination = text(document.find("destination"));
  destinationPath(destination);
  auto old = text(document.find("before"));
  auto bak = text(document.find("backup"));
  require((before || old.empty()) && (backup || bak.empty()), ErrorCode::InvalidFormat, "Invalid absent journal input");
  return plan(text(document.find("metadata")), std::move(destination), profile,
      before ? std::optional{std::move(old)} : std::nullopt,
      backup ? std::optional{std::move(bak)} : std::nullopt, stopped == 1, static_cast<mode_t>(mode), forceStart == 1);
}
struct Stage { Descriptor fd; struct stat info{}; };
Stage stage(int root, const std::string& name, std::string_view data, mode_t mode) {
  const auto previous = read(root, name, MaximumJournal);
  Descriptor fd(-1);
  if (previous) {
    require(previous->bytes == data, ErrorCode::Conflict, "Pending intake bytes conflict; retained unchanged");
    fd = Descriptor(::openat(root, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    struct stat opened{};
    require(fd.get() >= 0 && ::fstat(fd.get(), &opened) == 0 && unchanged(previous->info, opened),
        ErrorCode::Conflict, "Pending intake identity changed");
  } else {
    fd = take(detail::writeExclusiveOwned(root, name, data, mode));
    require(::fchmod(fd.get(), mode) == 0, ErrorCode::Io, "Cannot set intake file mode");
    sync(fd.get());
    sync(root);
  }
  struct stat opened{};
  require(::fstat(fd.get(), &opened) == 0, ErrorCode::Io, "Cannot identify staged intake file");
  regular(opened);
  return {std::move(fd), opened};
}
void verifyStage(int directory, const std::string& name, const Stage& stage) {
  const auto path = named(directory, name);
  struct stat opened{};
  require(path && ::fstat(stage.fd.get(), &opened) == 0 && unchanged(stage.info, opened) && unchanged(*path, opened),
      ErrorCode::Conflict, "Pending intake file was replaced or edited");
  regular(opened);
}
void move(int directory, const std::string& source, const std::string& target, unsigned flags) {
  if (::renameat2(directory, source.c_str(), directory, target.c_str(), flags) == 0) { sync(directory); return; }
  if (errno == EEXIST) { fail(ErrorCode::Conflict, "New intake target appeared; retained unchanged"); }
  if (errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP) { fail(ErrorCode::Unsupported, "Atomic no-replace intake publication is unavailable"); }
  fail(ErrorCode::Io, "Cannot publish intake file; journal retained");
}
void publishNew(int directory, const std::string& name, std::string_view bytes) {
  if (const auto existing = read(directory, name, MaximumInventory)) {
    require(existing->bytes == bytes, ErrorCode::Conflict, "Intake target conflicts with pending transaction");
    return;
  }
  const auto temporary = name + ".import";
  auto fd = stage(directory, temporary, bytes, 0600);
  verifyStage(directory, temporary, fd);
  move(directory, temporary, name, RENAME_NOREPLACE);
}
std::optional<std::string> contents(const std::optional<File>& file) {
  return file ? std::optional{file->bytes} : std::nullopt;
}
void removeExact(int directory, const std::string& name, std::string_view bytes) {
  const auto current = read(directory, name, MaximumJournal);
  if (!current) { return; }
  require(current->bytes == bytes, ErrorCode::Conflict, "Intake cleanup found changed bytes; retained unchanged");
  const auto latest = named(directory, name);
  require(latest && unchanged(current->info, *latest), ErrorCode::Conflict, "Intake cleanup identity changed");
  require(::unlinkat(directory, name.c_str(), 0) == 0, ErrorCode::Io, "Cannot clean completed intake transaction");
  sync(directory);
}
void inventoryCurrent(int root, const Plan& p) {
  const auto current = contents(read(root, "downloads.config", MaximumInventory));
  require(current == p.before || current == std::optional{p.inventory}, ErrorCode::Conflict, "Inventory changed outside pending intake");
  absent(root, "downloads.config.saving");
  const auto backup = contents(read(root, "downloads.config.bak", MaximumInventory));
  require(backup == p.oldBackup || (p.before && backup == p.before), ErrorCode::Conflict, "Inventory backup changed outside pending intake");
}
void notify(void (*observer)(TorrentIntakeStage), TorrentIntakeStage value) { if (observer) { observer(value); } }
void apply(ProfileLease& lease, const Plan& p, std::string_view journal, void (*observer)(TorrentIntakeStage)) {
  const int root = lease.directoryHandle();
  require(profilePath(lease) == p.profile, ErrorCode::Conflict, "Profile moved during intake");
  inventoryCurrent(root, p);
  auto active = childDirectory(root, "active", true);
  auto archive = childDirectory(root, "torrents", true);
  const auto stateName = p.hash.hex() + ".dat";
  const auto archiveName = p.hash.hex() + ".torrent";
  for (const auto& suffix : {".bak", ".saving", ".bak.saving"}) { absent(active.get(), stateName + suffix); }
  publishNew(active.get(), stateName, p.state);
  notify(observer, TorrentIntakeStage::StatePublished);
  publishNew(archive.get(), archiveName, p.metadata);
  notify(observer, TorrentIntakeStage::ArchivePublished);
  childIdentity(root, "active", active.get()); childIdentity(root, "torrents", archive.get());
  const auto state = read(active.get(), stateName, MaximumInventory);
  const auto source = read(archive.get(), archiveName, MaximumMetadata);
  require(state && state->bytes == p.state && source && source->bytes == p.metadata,
      ErrorCode::Conflict, "Published intake artifacts changed before inventory admission");
  require(profilePath(lease) == p.profile, ErrorCode::Conflict, "Profile moved during intake");
  inventoryCurrent(root, p);
  auto current = read(root, "downloads.config", MaximumInventory);
  if (contents(current) != std::optional{p.inventory}) {
    if (p.before) {
      auto fd = stage(root, BackupSaving, *p.before, p.inventoryMode);
      verifyStage(root, BackupSaving, fd);
      inventoryCurrent(root, p);
      move(root, BackupSaving, "downloads.config.bak", 0);
    }
    auto fd = stage(root, InventorySaving, p.inventory, p.inventoryMode);
    verifyStage(root, InventorySaving, fd);
    inventoryCurrent(root, p);
    // Cooperative lease + exact preimage check; Linux has no primary inode CAS.
    move(root, InventorySaving, "downloads.config", p.before ? 0U : RENAME_NOREPLACE);
    notify(observer, TorrentIntakeStage::InventoryPublished);
  }
  require(contents(read(root, "downloads.config", MaximumInventory)) == std::optional{p.inventory},
      ErrorCode::Conflict, "Published inventory changed before intake acknowledgement");
  removeExact(active.get(), stateName + ".import", p.state);
  removeExact(archive.get(), archiveName + ".import", p.metadata);
  removeExact(root, InventorySaving, p.inventory);
  if (p.before) { removeExact(root, BackupSaving, *p.before); }
  removeExact(root, JournalSaving, journal);
  removeExact(root, Journal, journal);
}
void cacheAbsent(int root, const core::TorrentHash& hash) {
  auto active = childDirectory(root, "active", false);
  if (active.get() < 0) { return; }
  const auto input = read(active.get(), "cache.dat", MaximumInventory);
  if (!input) { return; }
  auto document = EncodedDocument::parse(input->bytes, compatibleConfigLimits());
  require(document && document->compressed(), ErrorCode::InvalidFormat, "Resolve invalid original state cache before intake");
  const auto cache = core::readStateCache(document->document());
  require(cache && cache->issues.empty() && !cache->torrents.contains(hash.hex()),
      ErrorCode::Conflict, "Existing cached identity conflicts with new intake");
}
} // namespace

std::expected<core::TorrentHash, Error> importTorrent(ProfileLease& lease,
    const fs::path& metadata, const fs::path& destination, bool stopped,
    void (*observer)(TorrentIntakeStage), bool forceStart) {
  try {
    const auto profile = profilePath(lease);
    const int root = lease.directoryHandle();
    for (const auto* name : {Journal, JournalSaving, InventorySaving, BackupSaving, "downloads.config.saving"}) { absent(root, name); }
    pathShape(metadata); destinationPath(destination);
    auto parent = directory(metadata.parent_path());
    auto source = read(parent.get(), metadata.filename().native(), MaximumMetadata);
    require(source.has_value(), ErrorCode::Missing, "Torrent metadata file is missing");
    const auto before = read(root, "downloads.config", MaximumInventory);
    const auto backup = read(root, "downloads.config.bak", MaximumInventory);
    require(before || !backup, ErrorCode::Conflict, "Resolve missing inventory primary before intake");
    auto p = plan(std::move(source->bytes), destination.native(), profile, contents(before), contents(backup),
        stopped, before ? before->info.st_mode : 0600, forceStart);
    auto active = childDirectory(root, "active", false);
    auto archive = childDirectory(root, "torrents", false);
    if (active.get() >= 0) {
      for (const auto& suffix : {".dat", ".dat.bak", ".dat.saving", ".dat.bak.saving", ".dat.import"}) {
        absent(active.get(), p.hash.hex() + suffix);
      }
    }
    if (archive.get() >= 0) {
      for (const auto& suffix : {".torrent", ".torrent.import", ".torrent.bak", ".torrent.saving"}) {
        absent(archive.get(), p.hash.hex() + suffix);
      }
    }
    cacheAbsent(root, p.hash);
    const auto journal = journalBytes(p);
    require(journal.size() <= MaximumJournal, ErrorCode::InvalidFormat, "Intake journal exceeds its bounded limit");
    auto fd = stage(root, JournalSaving, journal, 0600);
    verifyStage(root, JournalSaving, fd);
    inventoryCurrent(root, p);
    move(root, JournalSaving, Journal, RENAME_NOREPLACE);
    notify(observer, TorrentIntakeStage::JournalSynced);
    apply(lease, p, journal, observer);
    return p.hash;
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::exception&) { return std::unexpected(Error{ErrorCode::Io, "Torrent intake failed; inspect pending journal before retry"}); }
}

std::expected<std::optional<core::TorrentHash>, Error> recoverTorrentIntake(
    ProfileLease& lease, void (*observer)(TorrentIntakeStage)) {
  try {
    const auto profile = profilePath(lease);
    const int root = lease.directoryHandle();
    auto journal = read(root, Journal, MaximumJournal);
    if (!journal) {
      journal = read(root, JournalSaving, MaximumJournal);
      if (!journal) { return std::nullopt; }
      (void)readJournal(journal->bytes, profile); // Validate before first recovery mutation.
      move(root, JournalSaving, Journal, RENAME_NOREPLACE);
    }
    auto p = readJournal(journal->bytes, profile);
    apply(lease, p, journal->bytes, observer);
    return p.hash;
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::exception&) { return std::unexpected(Error{ErrorCode::Io, "Torrent intake recovery failed; journal retained"}); }
}
} // namespace biglybtc::storage
