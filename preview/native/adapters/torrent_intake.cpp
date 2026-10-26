#include "torrent_intake.hpp"
#include "original_torrent_identity_lt.hpp"
#include "encoded_document.hpp"
#include "file_io.hpp"
#include "file_operation_io.hpp"
#include "original_storage_defaults.hpp"
#include "biglybtc/original_file_links.hpp"

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
struct MetadataSubject {
  core::Document torrent;
  compat::OriginalLayout layout;
  core::TorrentHash hash;
  int pieces;
  OriginalTorrentIdentity identity;
};
// Shared by old direct import and the new preview. CF02 identity caller patches
// belong in this single subject validation function, not in file-option logic.
MetadataSubject metadataSubject(std::string_view metadata) {
  auto torrent = parse(std::string(metadata), MaximumMetadata);
  uniqueKeys(torrent.root());
  const auto* info = torrent.find("info");
  require(info && info->type == Node::Type::Dictionary && !info->find("hash-override"),
      ErrorCode::Unsupported, "Metadata-pending/hash-override torrents require their magnet intake");
  (void)take(originalTorrentInfoOrder(torrent)); // Exact raw Java UTF-16 tree order; qualified provider retains raw info.
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
  auto identity=take(originalTorrentIdentityFromLibtorrent(torrent,engine));
  const auto hash=identity.originalKey;
  return {std::move(torrent),std::move(*original),hash,engine.num_pieces(),std::move(identity)};
}

struct IntakeFileOptions {
  std::string policy, stores;
  std::optional<std::int64_t> reorderMinimumMiB;
  core::TorrentIntakeLimits limits{};
  bool sequentialDownload=false;
  bool queued=false;
};
void validateFileOptions(const MetadataSubject& source,const IntakeFileOptions& options) {
  require(core::validTorrentIntakeLimits(options.limits),ErrorCode::InvalidFormat,"Per-torrent limits must be between zero and INT32_MAX bytes/s");
  auto entry=parse("de");put(entry,"file_priorities_c",options.policy);
  const auto values=compat::originalFilePolicy(entry.root(),source.layout.files.size());
  require(values && values->size()==options.stores.size(),ErrorCode::InvalidFormat,"Intake options differ from original file indices");
  const auto encoded=compat::encodeOriginalFilePolicy(*values);
  require(encoded && *encoded==options.policy,ErrorCode::InvalidFormat,"Noncanonical intake file policy");
  for(std::size_t n=0;n<values->size();++n) {
    const auto& value=(*values)[n];const auto kind=options.stores[n];
    require(kind=='L'||kind=='C'||kind=='R'||kind=='X',ErrorCode::InvalidFormat,"Invalid intake storage kind");
    require(!source.layout.files[n].padding||(value.skipped&&value.priority==0),ErrorCode::Unsupported,"Padding cannot be selected");
    require(value.skipped||(kind=='L'||kind=='R'),ErrorCode::InvalidFormat,"Selected file cannot use compact storage");
    require(kind!='R'||!value.skipped,ErrorCode::InvalidFormat,"Skipped reordered file needs explicit X or existing linear data");
  }
  require(!options.reorderMinimumMiB||(*options.reorderMinimumMiB>=INT32_MIN&&*options.reorderMinimumMiB<=INT32_MAX),
      ErrorCode::InvalidFormat,"Invalid original reorder threshold");
}
std::optional<struct stat> inspectPayload(const fs::path& path) {
  try{return fileop::inspect(path);}catch(const fileop::Failure& failure){throw Failure{failure.error};}
}
void checkCompactAbsence(const compat::OriginalLayout& layout,const IntakeFileOptions& options,const fs::path& destination) {
  for(std::size_t n=0;n<options.stores.size();++n) {
    if(layout.files[n].padding||(options.stores[n]!='C'&&options.stores[n]!='X')) { continue; }
    const auto path=destination/(layout.multifile?fs::path(layout.name)/layout.files[n].path:fs::path(layout.name));
    require(!inspectPayload(path),ErrorCode::Conflict,"A skipped payload appeared after choosing absent compact storage");
  }
}
IntakeFileOptions fileOptions(const ProfileLease& lease,const MetadataSubject& source,
    const fs::path& destination,std::span<const compat::FilePolicy> values,core::TorrentIntakeLimits limits,bool sequentialDownload,bool queued) {
  require(values.size()==source.layout.files.size(),ErrorCode::InvalidFormat,"Intake selection does not match metadata");
  const auto encoded=compat::encodeOriginalFilePolicy(values);
  require(encoded.has_value(),ErrorCode::Unsupported,encoded ? "" : encoded.error().message.c_str());
  auto empty=parse("de");const auto defaults=take(resolveOriginalStorageTypes(lease,source.hash,source.layout,empty.root()));
  require(defaults.markers.empty()&&!defaults.unsupportedConversion,ErrorCode::Conflict,"Orphan original storage controls already own this import");
  IntakeFileOptions result{*encoded,"",defaults.reorderMinimumMiB,limits,sequentialDownload,queued};result.stores.reserve(values.size());
  for(std::size_t n=0;n<values.size();++n) {
    const bool reorder=defaults.requested[n]==OriginalStorageKind::Reordered;
    if(!values[n].skipped) { result.stores+=reorder?'R':'L';continue; }
    if(source.layout.files[n].padding){result.stores+=reorder?'X':'C';continue;}
    const auto path=destination/(source.layout.multifile?fs::path(source.layout.name)/source.layout.files[n].path:fs::path(source.layout.name));
    const auto present=inspectPayload(path);
    result.stores+=present?'L':reorder?'X':'C'; // Never convert/reclaim existing data.
  }
  validateFileOptions(source,result);checkCompactAbsence(source.layout,result,destination);return result;
}
#include "hybrid_alias_intake.inc"
struct Plan {
  core::TorrentHash hash;
  std::string metadata, destination, profile, state, inventory;
  std::optional<std::string> before, oldBackup;
  bool stopped, forceStart;
  mode_t inventoryMode;
  std::optional<IntakeFileOptions> options;
  std::optional<HybridAliasSource> alias;
};
Plan plan(std::string metadata, std::string destination, std::string profile,
    std::optional<std::string> before, std::optional<std::string> oldBackup, bool stopped, mode_t mode, bool forceStart, std::optional<IntakeFileOptions> options = {}, std::optional<HybridAliasSource> alias = {}) {
  require(!options||!options->queued||(!stopped&&!forceStart),ErrorCode::InvalidFormat,"Queued intake conflicts with stopped/forced intent");
  auto subject=metadataSubject(metadata);auto& torrent=subject.torrent;auto* original=&subject.layout;
  const auto hash=std::optional{subject.hash};const auto key=hash->bytes();
  const auto* digest=reinterpret_cast<const char*>(key.data());
  const auto& identity=subject.identity;
  const auto knownAlias=[&](const core::TorrentHash& candidate){
    const auto value=candidate.bytes();
    return (value.size()==20&&((identity.full.v1&&std::equal(value.begin(),value.end(),identity.full.v1->begin()))
        ||(identity.full.v2&&std::equal(value.begin(),value.end(),identity.full.v2->begin()))))
        ||(value.size()==32&&identity.full.v2&&std::equal(value.begin(),value.end(),identity.full.v2->begin()));
  };
  if(options){validateFileOptions(subject,*options);checkCompactAbsence(*original,*options,destination);}
  auto inventory = parse(before.value_or("d9:downloadslee"));
  const auto* downloads = inventory.find("downloads");
  require(downloads && downloads->type == Node::Type::List, ErrorCode::Unsupported, "Intake requires current list-form inventory");
  auto saved = core::readLibrary(inventory);
  require(saved && saved->issues.empty() && saved->downloads.size() < 100000, ErrorCode::InvalidFormat, "Invalid original download inventory");
  if(alias){validateHybridAlias(*alias,subject,inventory,destination);}
  std::set<std::string> identities;
  std::int64_t position = 0;
  for (const auto& entry : saved->downloads) {
    require(entry.hash && identities.insert(entry.hash->hex()).second, ErrorCode::Conflict, "Inventory has missing or duplicate torrent identity");
    require(!knownAlias(*entry.hash)||(alias&&*entry.hash==alias->hash), ErrorCode::Conflict,
        "Torrent or a conflicting full-hash alias is already in the saved inventory");
    if (entry.position) { position = std::max(position, *entry.position); }
  }
  for (const auto& paused : saved->paused) {
    require(!knownAlias(paused.hash)||(alias&&paused.hash==alias->hash), ErrorCode::Conflict,
        "An existing pause record already owns the imported/full-hash alias");
  }
  require(position < std::numeric_limits<std::int64_t>::max(), ErrorCode::InvalidFormat, "Inventory position overflow");
  const auto archive = profile + "/torrents/" + hash->hex() + ".torrent";
  std::string entry = "d" + b("torrent_hash") + b(std::string_view(digest, 20)) + b("torrent") + b(archive)
      + b("save_dir") + b(destination) + b("save_file") + b(original->name) + b("persistent") + "i1e"
      + b("state") + i(stopped && !forceStart ? 70 : options&&options->queued ? 75 : 0) + b("position") + i(position + 1)
      + b("forceStart") + i(forceStart ? 1 : 0)
      + b("downloaded") + "i0e" + b("uploaded") + "i0e" + b("completedbytes") + "i0e"
      + b("maxdl") + "i0e" + b("maxul") + "i0e" + b("file_priorities") + 'l';
  for (std::size_t n = 0; n < original->files.size(); ++n) { entry += "i-1e"; }
  entry += "ee";
  if(options) {
    auto selected=parse(std::move(entry));put(selected,"file_priorities_c",options->policy);
    if(options->limits.downloadBytesPerSecond){put(selected,"maxdl",i(options->limits.downloadBytesPerSecond));}
    if(options->limits.uploadBytesPerSecond){put(selected,"maxul",i(options->limits.uploadBytesPerSecond));}
    const auto policies=compat::originalFilePolicy(selected.root(),original->files.size());
    require(policies.has_value(),ErrorCode::InvalidFormat,"Published intake policy invalid");
    const bool allSkipped=std::ranges::all_of(*policies,[](const auto& value){return value.skipped;});
    if(allSkipped){stopped=true;forceStart=false;options->queued=false;put(selected,"state","i70e");put(selected,"forceStart","i0e");}
    entry=selected.serialize();
  }
  std::string list = raw(inventory, *downloads);
  list.insert(list.size() - 1, entry);
  put(inventory, "downloads", std::move(list));
  const auto flags=core::originalDownloadFlags(torrent.find("attributes"));
  require(flags.has_value(),ErrorCode::InvalidFormat,"Invalid original download flags");
  auto attributes = section(torrent.find("attributes"));
  if(options&&options->sequentialDownload){put(attributes,"flags",i(*flags|core::OriginalSequentialDownloadFlag));}
  std::string stores = "l";
  for (std::size_t n = 0; n < original->files.size(); ++n) { stores += options ? "1:"+std::string(1,options->stores[n]) : "1:L"; }
  if(options&&options->reorderMinimumMiB){put(attributes,"reordermb",i(*options->reorderMinimumMiB));}
  put(attributes, "storetypes", stores + 'e');
  if(!alias){put(attributes, "networks", "l6:Publice");}
  put(attributes, "resumecomplete", "i1e");
  put(attributes, "filedownloaded", "de");
  if(options){
    auto selected=parse("de");put(selected,"file_priorities_c",options->policy);
    const auto policies=compat::originalFilePolicy(selected.root(),original->files.size());
    require(policies.has_value(),ErrorCode::InvalidFormat,"Invalid prepared file choices");
    std::int64_t skipped=0;bool hasSkipped=false;std::string counts="l";
    for(std::size_t n=0;n<policies->size();++n){
      if((*policies)[n].skipped){skipped+=original->files[n].bytes;hasSkipped=true;}
      counts+="i0e";
    }
    auto completion=parse("de");put(completion,"downloaded",counts+'e');
    put(attributes,"filedownloaded",completion.serialize());
    put(attributes,"skipfss",i(skipped));put(attributes,"skipbdl","i0e");
    auto parameters=section(attributes.find("parameters"));
    put(parameters,"dndflags",i((hasSkipped?1:0)|(skipped==original->bytes?2:0)));
    put(attributes,"parameters",parameters.serialize());
  }
  put(attributes, "filelinks2", "le");
  put(attributes, "BiglyBTC.completion", "de");
  put(torrent, "attributes", attributes.serialize());
  auto resume = section(torrent.find("resume"));
  auto data = section(resume.find("data"));
  put(data, "valid", "i0e"); put(data, "blocks", "de");
  put(data, "resume data", b(std::string(static_cast<std::size_t>(subject.pieces), '\2')));
  put(resume, "data", data.serialize()); put(torrent, "resume", resume.serialize());
  auto after = inventory.serialize();
  auto state = torrent.serialize();
  require(after.size() <= MaximumInventory && state.size() <= MaximumInventory, ErrorCode::InvalidFormat, "Intake outputs exceed limits");
  if(alias){metadata=state;} // Alternate archive also requires fresh verification; source stays untouched.
  auto verify = core::readLibrary(parse(after));
  require(verify && verify->issues.empty() && verify->downloads.size() == saved->downloads.size() + 1,
      ErrorCode::InvalidFormat, "New inventory failed its original reader");
  return {*hash, std::move(metadata), std::move(destination), std::move(profile), std::move(state), std::move(after),
      std::move(before), std::move(oldBackup), stopped, forceStart, static_cast<mode_t>(mode & 0777),std::move(options),std::move(alias)};
}
std::string journalBytes(const Plan& p) {
  std::string optionalOptions;
  if(p.options) {
    std::string value="d"+b("policy")+b(p.options->policy)+b("stores")+b(p.options->stores);
    if(p.options->reorderMinimumMiB){value+=b("reordermb")+i(*p.options->reorderMinimumMiB);}
    // Omit zero choices so old default journals remain byte-compatible.
    if(p.options->limits.downloadBytesPerSecond){value+=b("maxdl")+i(p.options->limits.downloadBytesPerSecond);}
    if(p.options->limits.uploadBytesPerSecond){value+=b("maxul")+i(p.options->limits.uploadBytesPerSecond);}
    if(p.options->sequentialDownload){value+=b("sequential")+"i1e";}
    if(p.options->queued){value+=b("queued")+"i1e";}
    optionalOptions=b("fileOptions")+value+'e';
  }
  if(p.alias){
    const auto& a=*p.alias;
    optionalOptions+=b("hybridAlias")+"d"+b("hash")+b(hashBytes(a.hash))+b("state")+b(a.state)
        +b("cache_present")+i(a.cache?1:0)+b("cache")+b(a.cache.value_or(""))
        +b("sdev")+i(a.stateDevice)+b("sino")+i(a.stateInode)+b("cdev")+i(a.cacheDevice)+b("cino")+i(a.cacheInode)+'e';
  }
  return "d" + b("version") + "i1e" + b("metadata") + b(p.metadata) + b("destination") + b(p.destination)
      + b("profile") + b(p.profile) + b("stopped") + i(p.stopped ? 1 : 0) + b("mode") + i(p.inventoryMode)
      + b("before_present") + i(p.before ? 1 : 0) + b("before") + b(p.before.value_or(""))
      + b("backup_present") + i(p.oldBackup ? 1 : 0) + b("backup") + b(p.oldBackup.value_or(""))
      + b("forceStart") + i(p.forceStart ? 1 : 0) + optionalOptions + 'e';
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
  const auto* optionsNode=document.find("fileOptions");
  const auto* aliasNode=document.find("hybridAlias");
  require(document.root().children.size()==20+(force?2U:0U)+(optionsNode?2U:0U)+(aliasNode?2U:0U)
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
  std::optional<IntakeFileOptions> options;
  if(optionsNode){
    require(optionsNode->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid intake options journal");
    const auto* reorder=optionsNode->find("reordermb");
    const auto* download=optionsNode->find("maxdl");const auto* upload=optionsNode->find("maxul");
    const auto* sequential=optionsNode->find("sequential");const auto sequentialValue=sequential?integer(sequential):0;
    require(sequentialValue==0||sequentialValue==1,ErrorCode::InvalidFormat,"Invalid sequential intake intent");
    const auto* queued=optionsNode->find("queued");const auto queuedValue=queued?integer(queued):0;
    require(queuedValue==0||queuedValue==1,ErrorCode::InvalidFormat,"Invalid queued intake intent");
    require(optionsNode->children.size()==4+(reorder?2U:0U)+(download?2U:0U)+(upload?2U:0U)+(sequential?2U:0U)+(queued?2U:0U),ErrorCode::InvalidFormat,"Unexpected intake options fields");
    options=IntakeFileOptions{text(optionsNode->find("policy")),text(optionsNode->find("stores")),
        reorder?std::optional{integer(reorder)}:std::nullopt,
        {download?integer(download):0,upload?integer(upload):0},sequentialValue==1,queuedValue==1};
  }
  std::optional<HybridAliasSource> alias;
  if(aliasNode){
    require(aliasNode->type==Node::Type::Dictionary&&aliasNode->children.size()==16,ErrorCode::InvalidFormat,"Invalid hybrid alias journal");
    const auto hash=core::TorrentHash::parse(text(aliasNode->find("hash")));
    const auto cached=integer(aliasNode->find("cache_present"));
    const auto cache=text(aliasNode->find("cache"));
    require(hash&&hash->bytes().size()==20&&(cached==0||cached==1)&&(cached||cache.empty()),ErrorCode::InvalidFormat,"Invalid hybrid source identity/cache");
    const auto sdev=integer(aliasNode->find("sdev")),sino=integer(aliasNode->find("sino"));
    const auto cdev=integer(aliasNode->find("cdev")),cino=integer(aliasNode->find("cino"));
    require(sdev>=0&&sino>0&&cdev>=0&&cino>=0&&(cached?(cino>0):(cino==0&&cdev==0)),ErrorCode::InvalidFormat,"Invalid hybrid source inode");
    alias=HybridAliasSource{*hash,text(aliasNode->find("state")),cached?std::optional{cache}:std::nullopt,
        static_cast<std::uint64_t>(sdev),static_cast<std::uint64_t>(sino),static_cast<std::uint64_t>(cdev),static_cast<std::uint64_t>(cino)};
  }
  return plan(text(document.find("metadata")), std::move(destination), profile,
      before ? std::optional{std::move(old)} : std::nullopt,
      backup ? std::optional{std::move(bak)} : std::nullopt, stopped == 1, static_cast<mode_t>(mode), forceStart == 1,std::move(options),std::move(alias));
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
  if(p.alias){hybridSourceCurrent(root,*p.alias);}
  if(p.options){const auto source=metadataSubject(p.metadata);checkCompactAbsence(source.layout,*p.options,p.destination);}
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
  if(p.alias){hybridSourceCurrent(root,*p.alias);}
  if(p.options){const auto source=metadataSubject(p.metadata);checkCompactAbsence(source.layout,*p.options,p.destination);}
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
core::TorrentHash commitIntakePlan(ProfileLease& lease,Plan& p,void(*observer)(TorrentIntakeStage),std::stop_token stop) {
  const int root=lease.directoryHandle();
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
    if(p.alias){hybridSourceCurrent(root,*p.alias);}
    const auto journal = journalBytes(p);
    require(journal.size() <= MaximumJournal, ErrorCode::InvalidFormat, "Intake journal exceeds its bounded limit");
    require(!stop.stop_requested(),ErrorCode::Cancelled,"Torrent import cancelled before commit");
    auto fd = stage(root, JournalSaving, journal, 0600);
    verifyStage(root, JournalSaving, fd);
    inventoryCurrent(root, p);
    move(root, JournalSaving, Journal, RENAME_NOREPLACE);
    notify(observer, TorrentIntakeStage::JournalSynced);
    apply(lease, p, journal, observer);
    return p.hash;
}
} // namespace

static std::expected<core::TorrentHash, Error> importImpl(ProfileLease& lease,
    const fs::path& metadata, const fs::path& destination, bool stopped,
    void (*observer)(TorrentIntakeStage), bool forceStart,
    std::optional<std::span<const compat::FilePolicy>> policies = {},const File* expected = nullptr, std::stop_token stop = {},
    core::TorrentIntakeLimits limits = {},bool sequentialDownload = false,bool queued = false) {
  try {
    require(!stop.stop_requested(),ErrorCode::Cancelled,"Torrent import cancelled before commit");
    require(core::validTorrentIntakeLimits(limits),ErrorCode::InvalidFormat,"Per-torrent limits must be between zero and INT32_MAX bytes/s");
    require(!queued||(!stopped&&!forceStart),ErrorCode::InvalidFormat,"Queued intake conflicts with stopped/forced intent");
    const auto profile = profilePath(lease);
    const int root = lease.directoryHandle();
    for (const auto* name : {Journal, JournalSaving, InventorySaving, BackupSaving, "downloads.config.saving"}) { absent(root, name); }
    pathShape(metadata); destinationPath(destination);
    auto parent = directory(metadata.parent_path());
    auto source = read(parent.get(), metadata.filename().native(), MaximumMetadata);
    require(source.has_value(), ErrorCode::Missing, "Torrent metadata file is missing");
    require(!expected||(unchanged(expected->info,source->info)&&expected->bytes==source->bytes),
        ErrorCode::Conflict,"Torrent metadata changed after file preview; load it again");
    std::optional<IntakeFileOptions> options;
    if(policies){const auto subject=metadataSubject(source->bytes);options=fileOptions(lease,subject,destination,*policies,limits,sequentialDownload,queued);}
    const auto before = read(root, "downloads.config", MaximumInventory);
    const auto backup = read(root, "downloads.config.bak", MaximumInventory);
    require(before || !backup, ErrorCode::Conflict, "Resolve missing inventory primary before intake");
    auto p = plan(std::move(source->bytes), destination.native(), profile, contents(before), contents(backup),
        stopped, before ? before->info.st_mode : 0600, forceStart,std::move(options));
    return commitIntakePlan(lease,p,observer,stop);
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::exception&) { return std::unexpected(Error{ErrorCode::Io, "Torrent intake failed; inspect pending journal before retry"}); }
}

struct TorrentIntakeMetadata::Data {
  fs::path path;File input;compat::OriginalLayout layout;core::TorrentHash hash;
};
TorrentIntakeMetadata::TorrentIntakeMetadata(std::shared_ptr<const Data> data):data_(std::move(data)){}
const core::TorrentHash& TorrentIntakeMetadata::hash()const noexcept{return data_->hash;}
const compat::OriginalLayout& TorrentIntakeMetadata::layout()const noexcept{return data_->layout;}
const fs::path& TorrentIntakeMetadata::source()const noexcept{return data_->path;}
std::expected<TorrentIntakeMetadata,Error> inspectTorrentIntake(const fs::path& path,std::stop_token stop) {
  try {
    require(!stop.stop_requested(),ErrorCode::Cancelled,"Torrent preview cancelled");pathShape(path);
    auto parent=directory(path.parent_path());auto input=read(parent.get(),path.filename().native(),MaximumMetadata);
    require(input.has_value(),ErrorCode::Missing,"Torrent metadata file is missing");
    auto subject=metadataSubject(input->bytes);
    require(!stop.stop_requested(),ErrorCode::Cancelled,"Torrent preview cancelled");
    return TorrentIntakeMetadata(std::make_shared<const TorrentIntakeMetadata::Data>(
        TorrentIntakeMetadata::Data{path,std::move(*input),std::move(subject.layout),subject.hash}));
  } catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot inspect torrent metadata"});}
}
std::expected<core::TorrentHash,Error> importTorrent(ProfileLease& lease,const fs::path& source,
    const fs::path& destination,bool stopped,void(*observer)(TorrentIntakeStage),bool forceStart) {
  return importImpl(lease,source,destination,stopped,observer,forceStart);
}
std::expected<core::TorrentHash,Error> importTorrentWithFileOptions(ProfileLease& lease,
    const TorrentIntakeMetadata& metadata,const fs::path& destination,bool stopped,
    std::span<const compat::FilePolicy> policies,void(*observer)(TorrentIntakeStage),bool forceStart,std::stop_token stop,core::TorrentIntakeLimits limits,bool sequentialDownload,bool queued) {
  return importImpl(lease,metadata.data_->path,destination,stopped,observer,forceStart,policies,&metadata.data_->input,stop,limits,sequentialDownload,queued);
}

std::expected<core::TorrentHash,Error> importHybridAlias(ProfileLease& lease,const core::TorrentHash& from,
    OriginalSelectedHash target,std::span<const OriginalTorrentIdentity> retained,
    void(*observer)(TorrentIntakeStage),std::stop_token stop) {
  try {
    require(!stop.stop_requested(),ErrorCode::Cancelled,"Hybrid alias cancelled before commit");
    require(target==OriginalSelectedHash::V1||target==OriginalSelectedHash::V2,ErrorCode::InvalidFormat,"Invalid hybrid target version");
    require(from.bytes().size()==20&&retained.size()<=100000,ErrorCode::InvalidFormat,"Bounded original hybrid source required");
    const auto profile=profilePath(lease);const int root=lease.directoryHandle();
    for(const auto* leaf:{Journal,JournalSaving,InventorySaving,BackupSaving,"downloads.config.saving",
        "BiglyBTC.file-operation","BiglyBTC.file-operation.saving"}){absent(root,leaf);}
    auto active=childDirectory(root,"active",false);
    require(active.get()>=0,ErrorCode::Missing,"Original hybrid state directory unavailable");
    for(const auto* suffix:{".dat.saving",".dat.bak.saving"}){absent(active.get(),from.hex()+suffix);}
    for(const auto* leaf:{"cache.dat.saving","cache.dat.bak.saving"}){absent(active.get(),leaf);}
    const auto state=read(active.get(),from.hex()+".dat",MaximumMetadata);
    require(state.has_value(),ErrorCode::Missing,"Original hybrid source state unavailable");
    const auto cached=read(active.get(),"cache.dat",MaximumInventory);
    if(!cached){absent(active.get(),"cache.dat.bak");} // Do not bypass original resilient-cache authority.
    const auto bounded=[](auto value){return static_cast<std::uint64_t>(value)<=static_cast<std::uint64_t>(INT64_MAX);};
    require(bounded(state->info.st_dev)&&bounded(state->info.st_ino)
        &&(!cached||(bounded(cached->info.st_dev)&&bounded(cached->info.st_ino))),ErrorCode::Unsupported,"Hybrid inode exceeds journal range");
    HybridAliasSource proof{from,state->bytes,contents(cached),state->info.st_dev,state->info.st_ino,
        cached?static_cast<std::uint64_t>(cached->info.st_dev):0,cached?static_cast<std::uint64_t>(cached->info.st_ino):0};
    auto original=metadataSubject(hybridSourceDocument(proof).serialize());
    require(original.identity.full.v1&&original.identity.full.v2&&original.identity.originalKey==from
        &&original.identity.selected!=target,ErrorCode::Conflict,"Select the other version of the actual hybrid source");
    for(const auto& entry:retained){
      require(take(compareOriginalTorrentIdentities(original.identity,entry))==OriginalIdentityRelation::Distinct,
          ErrorCode::Conflict,"Retire all shared hybrid identities before companion publication");
    }
    auto properties=section(original.torrent.find("azureus_private_properties"));
    if(target==OriginalSelectedHash::V1){properties=withoutKey(properties,"hybrid_hash_v2");}
    else{put(properties,"hybrid_hash_v2","i1e");}
    put(original.torrent,"azureus_private_properties",properties.serialize());
    auto selected=withoutKey(parse(original.torrent.serialize(),MaximumMetadata),"torrent filename");
    const auto before=read(root,"downloads.config",MaximumInventory),backup=read(root,"downloads.config.bak",MaximumInventory);
    require(before.has_value(),ErrorCode::Missing,"Existing hybrid inventory primary required");
    const auto inventory=parse(before->bytes);const auto library=core::readLibrary(inventory);
    require(library&&library->issues.empty(),ErrorCode::InvalidFormat,"Invalid hybrid source inventory");
    const core::SavedDownload* old=nullptr;
    for(const auto& row:library->downloads){if(row.hash&&*row.hash==from){require(!old,ErrorCode::Conflict,"Duplicate source key");old=&row;}}
    require(old,ErrorCode::Conflict,"Original hybrid source is not in this library");
    destinationPath(old->saveDirectory);
    std::vector<compat::FilePolicy> policy;std::string stores;
    for(const auto& file:original.layout.files){policy.push_back({file.padding,0});stores+='L';}
    const auto encoded=compat::encodeOriginalFilePolicy(policy);
    require(encoded.has_value(),ErrorCode::Unsupported,"Cannot encode explicit alias selection");
    auto planValue=plan(selected.serialize(),old->saveDirectory,profile,contents(before),contents(backup),true,
        before->info.st_mode,false,IntakeFileOptions{*encoded,std::move(stores),{}},std::move(proof));
    return commitIntakePlan(lease,planValue,observer,stop);
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(const std::exception&){return std::unexpected(Error{ErrorCode::Io,"Hybrid companion publication failed; inspect pending intake"});}
}

bool hasPendingTorrentIntake(const ProfileLease& lease) noexcept {
  try{
    if(!lease.valid()){return true;}
    for(const auto* leaf:{Journal,JournalSaving,InventorySaving,BackupSaving}){if(named(lease.directoryHandle(),leaf)){return true;}}
    return false;
  }catch(...){return true;}
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
