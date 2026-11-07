#include "torrent_intake.hpp"
#include "existing_subscription_target.hpp"
#include "durable_intake_receipt.hpp"
#include "verified_seeding.hpp"
#include "allocation_intent.hpp"
#include "verified_existing_scope.hpp"
#include "selected_existing_scope.hpp"
#include "biglybtc/existing_piece_goal.hpp"
#include "queue_positions.hpp"
#include "file_operations.hpp"
#include "linear_to_compact.hpp"
#include "original_torrent_identity_lt.hpp"
#include "encoded_document.hpp"
#include "file_io.hpp"
#include "file_operation_io.hpp"
#include "original_storage_defaults.hpp"
#include "original_filename_policy.hpp"
#include "torrent_state.hpp"
#include "biglybtc/original_file_links.hpp"

#include "biglybtc/state_cache.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <libtorrent/torrent_info.hpp>
#include <libtorrent/address.hpp>
#include <charconv>
#include <chrono>
#include <mutex>
#include <thread>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <exception>
#include <fcntl.h>
#include <limits>
#include <linux/fs.h>
#include <new>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/syscall.h>
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
constexpr const char* AcquisitionIntent = "BiglyBTC.subscription-acquire";
constexpr const char* AcquisitionIntentSaving = "BiglyBTC.subscription-acquire.saving";
constexpr const char* AcquisitionIntentBackup = "BiglyBTC.subscription-acquire.bak";
struct Failure { Error error; };
[[noreturn]] void fail(ErrorCode code, std::string message) { throw Failure{{code, std::move(message)}}; }
void require(bool value, ErrorCode code, const char* message) { if (!value) { fail(code, message); } }
template<class T> T take(std::expected<T, Error> value) {
  if (!value) { throw Failure{std::move(value.error())}; }
  return std::move(*value);
}
void take(std::expected<void, Error> value) {
  if (!value) { throw Failure{std::move(value.error())}; }
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
void noAcquisitionIntent(const ProfileLease& lease) {
  require(lease.valid(), ErrorCode::UnsafePath, "Torrent intake requires a held profile lease");
  for(const auto* leaf:{AcquisitionIntent,AcquisitionIntentSaving,AcquisitionIntentBackup}){absent(lease.directoryHandle(),leaf);}
}
void acquisitionOwnerCurrent(const ProfileLease& lease,std::string_view expected) {
  require(!expected.empty()&&expected.size()<=MaximumJournal,ErrorCode::Conflict,"Missing bounded acquisition owner intent");
  const int root=lease.directoryHandle();absent(root,AcquisitionIntentSaving);absent(root,AcquisitionIntentBackup);
  const auto owner=read(root,AcquisitionIntent,MaximumJournal);
  require(owner&&owner->bytes==expected,ErrorCode::Conflict,"Acquisition owner intent changed or is unavailable");
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
MetadataSubject metadataSubject(std::string_view metadata,std::optional<std::string_view> conversions={},bool decoderShowAll=false) {
  auto torrent = parse(std::string(metadata), MaximumMetadata);
  uniqueKeys(torrent.root());
  const auto* info = torrent.find("info");
  require(info && info->type == Node::Type::Dictionary && !info->find("hash-override"),
      ErrorCode::Unsupported, "Metadata-pending/hash-override torrents require their magnet intake");
  (void)take(originalTorrentInfoOrder(torrent)); // Exact raw Java UTF-16 tree order; qualified provider retains raw info.
  require(!decoderShowAll||conversions.has_value(),ErrorCode::InvalidFormat,"ShowAll requires an explicit accepted filename context");
  auto original = conversions?compat::originalLayout(torrent,100000,*conversions,decoderShowAll):compat::originalLayout(torrent);
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

#include "intake_file_destinations.inc"

struct IntakeFileOptions {
  std::string policy, stores;
  std::optional<std::int64_t> reorderMinimumMiB;
  core::TorrentIntakeLimits limits{};
  bool sequentialDownload=false;
  bool queued=false;
  bool paused=false;
  std::optional<core::TorrentIntakeTrackerTiers> trackers;
  std::optional<std::string> saveName;
  bool verifyExisting=false;
  std::optional<std::string> existingScope{};
  core::TorrentIntakeFileDestinations destinations{};
  std::optional<std::string> destinationSnapshot{};
  core::ExistingVerificationGoal goal=core::ExistingVerificationGoal::FullTorrent;
  std::optional<std::string> selectedScope{};
  core::TorrentAllocationMode allocation=core::TorrentAllocationMode::None;
};
const std::string& effectiveSaveName(const compat::OriginalLayout& layout,const IntakeFileOptions& options) {
  return options.saveName?*options.saveName:layout.name;
}
void validateFileOptions(const MetadataSubject& source,const IntakeFileOptions& options) {
  require(core::validTorrentAllocationMode(options.allocation),ErrorCode::InvalidFormat,"Invalid allocation mode");
  const bool allocating=options.allocation!=core::TorrentAllocationMode::None;
  require(!allocating||options.destinations.empty(),ErrorCode::Unsupported,"Allocation requires canonical file destinations");
  require(!allocating||(!options.verifyExisting&&!options.queued&&!options.paused&&options.goal==core::ExistingVerificationGoal::FullTorrent),ErrorCode::InvalidFormat,"Allocation requires stopped ordinary intent");
  require(!options.saveName||core::validTorrentIntakeSaveName(*options.saveName),ErrorCode::UnsafePath,"Save name must be one safe UTF8 filename component");
  require(!options.trackers||core::validTorrentIntakeTrackerTiers(*options.trackers),ErrorCode::InvalidFormat,"Invalid tracker tier override");
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
    require(!allocating||source.layout.files[n].padding||(!value.skipped&&kind=='L'),ErrorCode::Unsupported,"Allocation requires every real file selected in linear storage");
    require(value.skipped||(options.verifyExisting&&options.goal==core::ExistingVerificationGoal::SelectedFiles)||(kind=='L'||kind=='R'),ErrorCode::InvalidFormat,"Selected file cannot use compact storage");
    require(kind!='R'||!value.skipped||(options.verifyExisting&&options.goal==core::ExistingVerificationGoal::SelectedFiles),ErrorCode::InvalidFormat,"Skipped reordered file needs explicit X or existing linear data");
  }
  const bool selectedGoal=options.goal==core::ExistingVerificationGoal::SelectedFiles;
  require((options.goal==core::ExistingVerificationGoal::FullTorrent||selectedGoal)&&(!selectedGoal||options.verifyExisting),ErrorCode::InvalidFormat,"Selected goal requires explicit existing verification");
  if(options.verifyExisting&&!selectedGoal){
    require((effectiveSaveName(source.layout,options)!=source.layout.name||!options.destinations.empty())==options.existingScope.has_value(),ErrorCode::InvalidFormat,"Custom verified root requires its exact saved scope");
    require(!options.queued&&!options.paused,ErrorCode::InvalidFormat,"Verified seeding must begin stopped and unpaused");
    for(std::size_t n=0;n<values->size();++n){
      // Original padding is immutable virtual data, never a physical input.
      if(source.layout.files[n].padding){continue;}
      require(!(*values)[n].skipped&&options.stores[n]=='L',ErrorCode::Unsupported,"Verified seeding requires every real file selected in linear storage");
    }
  }
  require(!selectedGoal||!options.existingScope,ErrorCode::InvalidFormat,"Selected verification cannot consume Full scope authority");
  require(selectedGoal==options.selectedScope.has_value(),ErrorCode::InvalidFormat,"Selected verification requires its own saved scope");
  if(options.selectedScope){auto value=parse(*options.selectedScope);require(SelectedExistingScope::decode(value.root()).has_value(),ErrorCode::InvalidFormat,"Invalid selected scope encoding");}
  require(options.verifyExisting||!options.existingScope,ErrorCode::InvalidFormat,"Ordinary intake cannot acquire existing-root authority");
  require(core::validTorrentIntakeFileDestinations(options.destinations),ErrorCode::InvalidFormat,"Invalid file destination vector");
  require((!options.verifyExisting&&(!options.destinations.empty()||allocating))==options.destinationSnapshot.has_value(),ErrorCode::InvalidFormat,"Ordinary indexed targets require their transaction observation");
  if(options.destinationSnapshot){require(options.destinationSnapshot->size()==64&&std::ranges::all_of(*options.destinationSnapshot,[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}),ErrorCode::InvalidFormat,"Invalid destination snapshot digest");}
  for(const auto& value:options.destinations){require(value.index<source.layout.files.size()&&!source.layout.files[value.index].padding,ErrorCode::InvalidFormat,"File destination requires a real original index");}
  require(!options.reorderMinimumMiB||(*options.reorderMinimumMiB>=INT32_MIN&&*options.reorderMinimumMiB<=INT32_MAX),
      ErrorCode::InvalidFormat,"Invalid original reorder threshold");
}
std::optional<struct stat> inspectPayload(const fs::path& path) {
  try{return fileop::inspect(path);}catch(const fileop::Failure& failure){throw Failure{failure.error};}
}
void checkCompactAbsence(const compat::OriginalLayout& layout,const IntakeFileOptions& options,const fs::path& destination,std::optional<std::string_view> conversions={}) {
  const auto paths=physicalIntakePaths(layout,destination,effectiveSaveName(layout,options),options.destinations,conversions);
  for(std::size_t n=0;n<options.stores.size();++n) {
    if(layout.files[n].padding||(options.verifyExisting&&options.goal==core::ExistingVerificationGoal::SelectedFiles)||(options.stores[n]!='C'&&options.stores[n]!='X')) { continue; }
    require(!inspectPayload(paths[n]),ErrorCode::Conflict,"A skipped payload appeared after choosing absent compact storage");
  }
}
IntakeFileOptions fileOptions(const ProfileLease& lease,const MetadataSubject& source,
    const fs::path& destination,std::span<const compat::FilePolicy> values,core::TorrentIntakeLimits limits,bool sequentialDownload,bool queued,bool paused,std::optional<core::TorrentIntakeTrackerTiers> trackers,std::optional<std::string> saveName,bool verifyExisting,core::TorrentIntakeFileDestinations destinations,std::string_view conversions,core::ExistingVerificationGoal goal,core::TorrentAllocationMode allocation) {
  require(!saveName||core::validTorrentIntakeSaveName(*saveName),ErrorCode::UnsafePath,"Save name must be one safe UTF8 filename component");
  require(values.size()==source.layout.files.size(),ErrorCode::InvalidFormat,"Intake selection does not match metadata");
  const auto encoded=compat::encodeOriginalFilePolicy(values);
  require(encoded.has_value(),ErrorCode::Unsupported,encoded ? "" : encoded.error().message.c_str());
  auto empty=parse("de");const bool selectedGoal=verifyExisting&&goal==core::ExistingVerificationGoal::SelectedFiles;
  const auto* inherited=source.torrent.find("attributes");const auto defaults=take(resolveOriginalStorageTypes(lease,source.hash,source.layout,selectedGoal&&inherited?*inherited:empty.root()));
  require(selectedGoal||(defaults.markers.empty()&&!defaults.unsupportedConversion),ErrorCode::Conflict,"Orphan original storage controls already own this import");
  IntakeFileOptions result{*encoded,"",defaults.reorderMinimumMiB,limits,sequentialDownload,queued,paused,std::move(trackers),std::move(saveName),verifyExisting};result.goal=goal;result.allocation=allocation;result.destinations=std::move(destinations);const auto paths=physicalIntakePaths(source.layout,destination,effectiveSaveName(source.layout,result),result.destinations,conversions);result.stores.reserve(values.size());
  for(std::size_t n=0;n<values.size();++n) {
    const bool reorder=defaults.requested[n]==OriginalStorageKind::Reordered;
    if(selectedGoal&&!source.layout.files[n].padding){
      const auto kind=defaults.actual[n];result.stores+=kind==OriginalStorageKind::Linear?'L':kind==OriginalStorageKind::Compact?'C':kind==OriginalStorageKind::Reordered?'R':'X';continue;}
    if(!values[n].skipped) { result.stores+=reorder?'R':'L';continue; }
    if(source.layout.files[n].padding){result.stores+=reorder?'X':'C';continue;}
    const auto present=inspectPayload(paths[n]);
    result.stores+=present?'L':reorder?'X':'C'; // Never convert/reclaim existing data.
  }
  if(allocation!=core::TorrentAllocationMode::None){
    const auto flags=core::originalDownloadFlags(source.torrent.find("attributes"));
    require(flags&&((*flags&(0x200|0x4000))==0),ErrorCode::Unsupported,"Original flags disable stop after allocation");
    for(std::size_t n=0;n<paths.size();++n){if(source.layout.files[n].padding){continue;}const auto present=inspectPayload(paths[n]);require(!present||(present->st_size==source.layout.files[n].bytes&&present->st_nlink==1&&present->st_uid==::geteuid()),ErrorCode::Unsupported,"Allocation refuses unreviewed short or shared existing data");}
  }
  if(!verifyExisting&&(!result.destinations.empty()||allocation!=core::TorrentAllocationMode::None)){result.destinationSnapshot=intakeDestinationSnapshot(source,paths);}
  if(verifyExisting&&!selectedGoal&&(effectiveSaveName(source.layout,result)!=source.layout.name||!result.destinations.empty())){
    const auto scope=take(VerifiedExistingScope::capture(lease,source.torrent,source.layout,source.identity,destination,effectiveSaveName(source.layout,result),result.destinations.empty()?std::span<const fs::path>{}:std::span<const fs::path>{paths}));result.existingScope=scope.encoded();
  }
  if(selectedGoal){std::vector<OriginalStorageKind> kinds;for(const auto kind:result.stores){kinds.push_back(kind=='L'?OriginalStorageKind::Linear:kind=='C'?OriginalStorageKind::Compact:kind=='R'?OriginalStorageKind::Reordered:OriginalStorageKind::ReorderedCompact);}
    result.selectedScope=take(SelectedExistingScope::capture(lease,source.torrent,source.layout,source.identity,values,kinds,paths)).encoded();}
  validateFileOptions(source,result);checkCompactAbsence(source.layout,result,destination,conversions);return result;
}
#include "hybrid_alias_intake.inc"

// Editor projection only. Existing metadata outside its display bounds remains
// importable verbatim; never turn an unavailable projection into an empty list.
std::optional<core::TorrentIntakeTrackerFields> trackerFields(const core::Document& document) {
  core::TorrentIntakeTrackerFields result;
  if(const auto* value=document.find("announce")){
    if(value->type!=Node::Type::Bytes||value->bytes.size()>core::MaximumIntakeTrackerUrlBytes){return {};}
    result.announce=value->bytes;
  }
  if(const auto* list=document.find("announce-list")){
    if(list->type!=Node::Type::List||list->children.size()>core::MaximumIntakeTrackerTiers){return {};}
    std::size_t count=0,bytes=0;
    for(const auto& tier:list->children){
      if(tier.type!=Node::Type::List){return {};}result.tiers.emplace_back();
      for(const auto& url:tier.children){
        if(url.type!=Node::Type::Bytes||++count>core::MaximumIntakeTrackers||url.bytes.size()>core::MaximumIntakeTrackerUrlBytes
            ||url.bytes.size()>core::MaximumIntakeTrackerBytes-bytes){return {};}
        bytes+=url.bytes.size();result.tiers.back().emplace_back(url.bytes);
      }
    }
  }
  if(!core::boundedTorrentIntakeTrackers(result)){return {};}return result;
}
void literalTracker(std::string_view url) {
  const bool udp=url.starts_with("udp://");const auto prefix=url.find("://")+3;
  auto authority=url.substr(prefix);authority=authority.substr(0,authority.find_first_of("/?#"));
  if(const auto at=authority.find('@');at!=std::string_view::npos){
    require(at>0&&authority.find('@',at+1)==std::string_view::npos,ErrorCode::InvalidFormat,"Invalid tracker credentials");authority.remove_prefix(at+1);
  }
  std::string_view host,port;bool explicitPort=false;
  if(authority.starts_with('[')){
    const auto close=authority.find(']');require(close!=std::string_view::npos,ErrorCode::InvalidFormat,"Invalid tracker address");host=authority.substr(1,close-1);
    libtorrent::error_code error;const auto address=libtorrent::make_address(std::string(host),error);
    require(!error&&address.is_v6(),ErrorCode::InvalidFormat,"Invalid numeric tracker address");
    if(close+1<authority.size()){require(authority[close+1]==':',ErrorCode::InvalidFormat,"Invalid tracker authority");explicitPort=true;port=authority.substr(close+2);}
  }else{
    const auto colon=authority.find(':');host=authority.substr(0,colon);
    if(colon!=std::string_view::npos){explicitPort=true;port=authority.substr(colon+1);}
    require(!host.empty()&&host.size()<=253&&std::all_of(host.begin(),host.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-';}),ErrorCode::InvalidFormat,"Invalid literal tracker host");
    std::size_t start=0;
    while(start<host.size()){
      const auto end=host.find('.',start);const auto label=host.substr(start,end==std::string_view::npos?host.size()-start:end-start);
      require(!label.empty()&&label.size()<=63&&label.front()!='-'&&label.back()!='-',ErrorCode::InvalidFormat,"Invalid tracker hostname label");
      if(end==std::string_view::npos){break;}start=end+1;
    }
  }
  require(!host.empty()&&host.find('%')==std::string_view::npos,ErrorCode::InvalidFormat,"Invalid tracker host");
  require(!udp||explicitPort,ErrorCode::InvalidFormat,"UDP trackers require an explicit port");
  if(explicitPort){
    unsigned value=0;const auto parsed=std::from_chars(port.data(),port.data()+port.size(),value);
    require(!port.empty()&&parsed.ec==std::errc{}&&parsed.ptr==port.data()+port.size()&&value>=1&&value<=65535,ErrorCode::InvalidFormat,"Invalid tracker port");
  }
}
void applyTrackers(MetadataSubject& subject,const core::TorrentIntakeTrackerTiers& incoming,std::optional<std::string_view> conversions={},bool decoderShowAll=false) {
  require(core::validTorrentIntakeTrackerTiers(incoming),ErrorCode::InvalidFormat,"Invalid tracker tiers or literal URL");
  // Override rules need only the old primary, never a bounded interpretation
  // of old tier fields. A requested replacement/clear can remove those fields.
  std::string primary;
  if(const auto* old=subject.torrent.find("announce");old&&old->type==Node::Type::Bytes){primary=old->bytes;}
  // java.net.URL lowercases the protocol in toExternalForm; no hostname
  // resolution or tracker-list expansion belongs in this literal projection.
  const auto scheme=primary.find("://");
  if(scheme!=std::string::npos){for(std::size_t n=0;n<scheme;++n){auto& c=primary[n];if(c>='A'&&c<='Z'){c=static_cast<char>(c-'A'+'a');}}}
  core::TorrentIntakeTrackerTiers tiers;
  for(const auto& tier:incoming){if(tier.empty()){continue;}tiers.push_back(tier);for(const auto& url:tier){literalTracker(url);}}
  if(tiers.empty()){
    auto hex=subject.hash.hex();for(auto& c:hex){if(c>='a'&&c<='f'){c=static_cast<char>(c-'a'+'A');}}
    primary="dht://"+hex+".dht/announce";
  }else if(tiers.size()==1&&tiers[0].size()==1){primary=tiers[0][0];tiers.clear();}
  else{
    bool retained=false;for(const auto& tier:tiers){retained|=std::ranges::find(tier,primary)!=tier.end();}
    if(!retained){primary=tiers[0][0];}
  }
  const auto beforeInfo=take(originalTorrentInfoSubject(subject.torrent));const std::string info(beforeInfo);
  auto edited=withoutKey(subject.torrent,"announce-list");put(edited,"announce",b(primary));
  if(!tiers.empty()){
    std::string list="l";for(const auto& tier:tiers){list+='l';for(const auto& url:tier){list+=b(url);}list+='e';}put(edited,"announce-list",list+'e');
  }
  auto checked=metadataSubject(edited.serialize(),conversions,decoderShowAll);
  require(take(originalTorrentInfoSubject(checked.torrent))==info&&checked.identity.full==subject.identity.full
      &&checked.hash==subject.hash&&checked.identity.selected==subject.identity.selected,ErrorCode::Conflict,"Tracker edit changed torrent identity");
  subject.torrent=std::move(checked.torrent);
}
void applyIntakeNetwork(core::Document& attributes,TorrentIntakeNetwork network,bool alias) {
  if(network==TorrentIntakeNetwork::Public){if(!alias){put(attributes,"networks","l6:Publice");}return;}
  const bool mixed=network==TorrentIntakeNetwork::Mixed;
  require((network==TorrentIntakeNetwork::I2p||mixed)&&!alias,ErrorCode::Unsupported,"I2P or mixed intake cannot convert a saved alias");
  const auto* selected=attributes.find("networks");
  const bool inherited=selected!=nullptr;
  if(selected){
    bool publicNetwork=false,i2pNetwork=false;
    if(selected->type==Node::Type::List){for(const auto& value:selected->children){
      publicNetwork|=value.type==Node::Type::Bytes&&value.bytes=="Public";
      i2pNetwork|=value.type==Node::Type::Bytes&&value.bytes=="I2P";
    }}
    require(selected->type==Node::Type::List&&(mixed
        ?selected->children.size()==2&&publicNetwork&&i2pNetwork
        :selected->children.size()==1&&i2pNetwork),
        ErrorCode::Unsupported,"I2P or mixed intake refuses incompatible inherited networks");
  }
  const auto dht=[](std::string_view value){return value.size()==3
      &&(value[0]=='D'||value[0]=='d')&&(value[1]=='H'||value[1]=='h')&&(value[2]=='T'||value[2]=='t');};
  bool trackerDenied=false,dhtDenied=false;
  if(const auto* denied=attributes.find("peersourcesdenied")){
    require(denied->type==Node::Type::List,ErrorCode::InvalidFormat,"Malformed inherited denied peer sources");
    for(const auto& value:denied->children){require(value.type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Malformed inherited denied peer source");
      trackerDenied|=value.bytes=="Tracker";dhtDenied|=dht(value.bytes);}
  }
  bool tracker=true,discovery=false;
  if(const auto* sources=attributes.find("peersources")){
    require(sources->type==Node::Type::List,ErrorCode::InvalidFormat,"Malformed inherited peer sources");
    tracker=false;for(const auto& value:sources->children){require(value.type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Malformed inherited peer source");tracker|=value.bytes=="Tracker";discovery|=dht(value.bytes);}
  }else{put(attributes,"peersources","l7:Trackere");}
  require((tracker&&!trackerDenied)||(discovery&&!dhtDenied),ErrorCode::Unsupported,
      "I2P intake cannot override disabled or denied Tracker/DHT peer sources");
  // An explicit existing mixed list retains its original order/raw encoding.
  if(!mixed||!inherited){put(attributes,"networks",mixed?"l6:Public3:I2Pe":"l3:I2Pe");}
}
// Original SystemTime.getCurrentTimeUnique ordering, including wall rollback.
std::int64_t intakeAddedTime(){
  static std::mutex mutex;static std::int64_t previous=-1,unique=-1;
  std::lock_guard lock(mutex);
  auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  require(now>0,ErrorCode::Unsupported,"Original download added time is unavailable");
  if(previous==-1||now<previous){previous=now;unique=-1;}
  else if(now==previous){
    if(unique==-1){unique=now;}
    require(unique<std::numeric_limits<std::int64_t>::max(),ErrorCode::Unsupported,"Original added time exhausted");now=++unique;
  }else{previous=now;if(unique!=-1&&now<=unique){
    require(unique<std::numeric_limits<std::int64_t>::max(),ErrorCode::Unsupported,"Original added time exhausted");now=++unique;
  }else{unique=-1;}}
  return now;
}
#include "subscription_state_images.inc"

struct Plan {
  core::TorrentHash hash;
  std::string metadata, archive, destination, profile, state, inventory;
  std::optional<std::string> before, oldBackup;
  bool stopped, forceStart;
  mode_t inventoryMode;
  std::optional<IntakeFileOptions> options;
  std::optional<HybridAliasSource> alias;
  std::optional<std::string> filenameConversions;
  std::optional<bool> decoderShowAll; // absent accepted journals retain false, never current inferred permission.
  TorrentIntakeNetwork network=TorrentIntakeNetwork::Public;
  std::optional<std::int64_t> addedTime{}; // Frozen once; absent old journals remain byte compatible.
  bool subscriptionAcquisition=false;
  std::optional<std::int64_t> initialScrapeCache;
  bool automaticGeometry=false; // private actual-settings-derived save leaf
};
Plan plan(std::string metadata, std::string destination, std::string profile,
    std::optional<std::string> before, std::optional<std::string> oldBackup, bool stopped, mode_t mode, bool forceStart, std::optional<IntakeFileOptions> options = {}, std::optional<HybridAliasSource> alias = {},std::optional<std::string> filenameConversions={},std::optional<bool> decoderShowAll={},TorrentIntakeNetwork network=TorrentIntakeNetwork::Public,std::optional<std::int64_t> addedTime={},bool subscriptionAcquisition=false,std::optional<std::int64_t> initialScrapeCache={},bool automaticGeometry=false) {
  require(!addedTime||*addedTime>0,ErrorCode::InvalidFormat,"Invalid original added time");
  require(!initialScrapeCache||subscriptionAcquisition,ErrorCode::InvalidFormat,"Scrape preparation requires the private acquisition recipe");
  if(initialScrapeCache){require(*initialScrapeCache>=0&&(static_cast<std::uint64_t>(*initialScrapeCache)&~std::uint64_t{0x00ffffff00ffffff})==0,ErrorCode::InvalidFormat,"Invalid subscription scrape cache preparation");}
  require(!automaticGeometry||subscriptionAcquisition,ErrorCode::InvalidFormat,"Automatic geometry requires its private acquisition owner");
  if(subscriptionAcquisition){
    require(stopped&&!forceStart&&!alias&&network==TorrentIntakeNetwork::Public&&addedTime.has_value(),ErrorCode::Unsupported,"Acquisition requires frozen new stopped Public intake");
    require(options&&!options->verifyExisting&&options->allocation==core::TorrentAllocationMode::None&&!options->sequentialDownload&&!options->queued&&!options->paused&&!options->trackers&&(!options->saveName||automaticGeometry)&&options->destinations.empty()&&!options->limits.downloadBytesPerSecond&&!options->limits.uploadBytesPerSecond,ErrorCode::Unsupported,"Unsupported acquisition intake options");
  }
  require(network==TorrentIntakeNetwork::Public||network==TorrentIntakeNetwork::I2p||network==TorrentIntakeNetwork::Mixed,ErrorCode::InvalidFormat,"Invalid intake network context");
  require(network!=TorrentIntakeNetwork::I2p||(!alias
      &&(!options||(!options->verifyExisting&&options->allocation==core::TorrentAllocationMode::None))),
      ErrorCode::Unsupported,"I2P intake requires ordinary intent without verification or allocation");
  require(network!=TorrentIntakeNetwork::Mixed||(!alias
      &&(!options||(!options->verifyExisting&&options->allocation==core::TorrentAllocationMode::None))),
      ErrorCode::Unsupported,"Mixed intake requires ordinary intent without aliases, verification or allocation");
  require(!options||!options->queued||(!stopped&&!forceStart),ErrorCode::InvalidFormat,"Queued intake conflicts with stopped/forced intent");
  require(!options||!options->paused||(stopped&&!forceStart&&!options->queued),ErrorCode::InvalidFormat,"Paused intake requires stopped/unforced/nonqueued intent");
  require(!options||options->allocation==core::TorrentAllocationMode::None||(stopped&&!forceStart),ErrorCode::InvalidFormat,"Allocation intake must remain safely stopped and unforced");
  require(!options||!options->verifyExisting||(stopped&&!forceStart),ErrorCode::InvalidFormat,"Verified seeding requires stopped/unforced intake");
  auto subject=metadataSubject(metadata,filenameConversions?std::optional<std::string_view>{*filenameConversions}:std::nullopt,decoderShowAll.value_or(false));auto& torrent=subject.torrent;auto* original=&subject.layout;
  const auto hash=std::optional{subject.hash};const auto key=hash->bytes();
  const auto* digest=reinterpret_cast<const char*>(key.data());
  const auto& identity=subject.identity;
  if(subscriptionAcquisition){
    require(identity.full.v1&&!identity.full.v2,ErrorCode::Unsupported,"Acquisition requires pure v1 metadata, never hybrid or v2");
    std::vector<compat::FilePolicy> selected;for(const auto& file:subject.layout.files){selected.push_back({file.padding,0});}
    const auto policy=compat::encodeOriginalFilePolicy(selected);
    require(policy&&*policy==options->policy&&initialFileDestinations(subject).empty(),ErrorCode::Unsupported,"Acquisition requires storage-derived default file choices");
  }
  const auto knownAlias=[&](const core::TorrentHash& candidate){
    const auto value=candidate.bytes();
    return (value.size()==20&&((identity.full.v1&&std::equal(value.begin(),value.end(),identity.full.v1->begin()))
        ||(identity.full.v2&&std::equal(value.begin(),value.end(),identity.full.v2->begin()))))
        ||(value.size()==32&&identity.full.v2&&std::equal(value.begin(),value.end(),identity.full.v2->begin()));
  };
  if(options){validateFileOptions(subject,*options);checkCompactAbsence(*original,*options,destination,filenameConversions?std::optional<std::string_view>{*filenameConversions}:std::nullopt);
    if(!original->multifile&&!options->destinations.empty()){const auto target=projectedIntakeTarget(options->destinations.front(),filenameConversions?std::optional<std::string_view>{*filenameConversions}:std::nullopt);require(fs::path(destination)==target.parent_path()&&effectiveSaveName(*original,*options)==target.filename().string(),ErrorCode::InvalidFormat,"Single-file destination differs from its saved parent/name");}
  }
  auto archive=metadata;
  if(options&&options->trackers){applyTrackers(subject,*options->trackers,filenameConversions?std::optional<std::string_view>{*filenameConversions}:std::nullopt,decoderShowAll.value_or(false));archive=subject.torrent.serialize();}
  auto inventory = parse(before.value_or("d9:downloadslee"));
  const auto* downloads = inventory.find("downloads");
  require(downloads && downloads->type == Node::Type::List, ErrorCode::Unsupported, "Intake requires current list-form inventory");
  auto saved = core::readLibrary(inventory);
  require(saved && saved->issues.empty() && saved->downloads.size() < 100000, ErrorCode::InvalidFormat, "Invalid original download inventory");
  if(alias){validateHybridAlias(*alias,subject,inventory,destination,filenameConversions?std::optional<std::string_view>{*filenameConversions}:std::nullopt,decoderShowAll.value_or(false));}
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
  const auto archivePath = profile + "/torrents/" + hash->hex() + ".torrent";
  std::string entry = "d" + b("torrent_hash") + b(std::string_view(digest, 20)) + b("torrent") + b(archivePath)
      + b("save_dir") + b(destination) + b("save_file") + b(options?effectiveSaveName(*original,*options):original->name) + b("persistent") + "i1e"
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
    if(allSkipped){stopped=true;forceStart=false;options->queued=false;options->paused=false;put(selected,"state","i70e");put(selected,"forceStart","i0e");}
    if(options->verifyExisting){put(selected,VerifiedSeedKey,"i1e");}
    if(options->allocation!=core::TorrentAllocationMode::None){put(selected,AllocationModeKey,i(static_cast<int>(options->allocation)));put(selected,AllocationStateKey,"i1e");put(selected,AllocationSnapshotKey,b(*options->destinationSnapshot));put(selected,"allocated","i0e");}
    if(options->existingScope){put(selected,VerifiedExistingScopeKey,*options->existingScope);}
    if(options->goal==core::ExistingVerificationGoal::SelectedFiles){put(selected,VerifiedSeedGoalKey,"i1e");put(selected,VerifiedSelectedScopeKey,*options->selectedScope);}
    entry=selected.serialize();
  }
  std::string list = raw(inventory, *downloads);
  list.insert(list.size() - 1, entry);
  put(inventory, "downloads", std::move(list));
  if(options&&options->paused){
    const auto* pauses=inventory.find("pause_data");
    require(!pauses||pauses->type==Node::Type::List,ErrorCode::InvalidFormat,"Invalid original pause list");
    std::string value=pauses?raw(inventory,*pauses):"le";
    // Same original PS_GENERAL encoding as applyLibraryCommand(Pause). Copy
    // every existing pause row verbatim and publish with the new stopped row.
    value.insert(value.size()-1,"d4:hash"+b(std::string_view(digest,key.size()))+"5:forcei0e5:owneri1ee");
    put(inventory,"pause_data",std::move(value));
  }
  const auto flags=core::originalDownloadFlags(torrent.find("attributes"));
  require(flags.has_value(),ErrorCode::InvalidFormat,"Invalid original download flags");
  auto attributes = section(torrent.find("attributes"));
  if(initialScrapeCache){put(attributes,"scsrc","i1e");put(attributes,"scrapecache",i(*initialScrapeCache));}
  if(addedTime){auto parameters=section(attributes.find("parameters"));
    put(parameters,"stats.download.added.time",i(*addedTime));put(attributes,"parameters",parameters.serialize());}

  if(options&&options->allocation!=core::TorrentAllocationMode::None){put(attributes,"fas","i2e");}
  if(options&&original->files.size()==1&&!options->destinations.empty()
      &&fs::path(options->destinations.front().path).filename().string()!=fs::path(original->files[0].path).filename().string()){
    put(attributes,"displayname",b(fs::path(options->destinations.front().path).filename().string()));
  }else if(options&&options->saveName&&*options->saveName!=original->name
      &&(!original->multifile||original->files.size()!=1)){
    put(attributes,"displayname",b(*options->saveName));
  } // One-entry multifile root-only rename is not original manual file rename.
  if(options&&(options->sequentialDownload||options->verifyExisting)){put(attributes,"flags",i(*flags
      |(options->sequentialDownload?core::OriginalSequentialDownloadFlag:0)
      |(options->verifyExisting?std::int64_t{0x8}:0)));}
  std::string stores = "l";
  for (std::size_t n = 0; n < original->files.size(); ++n) { stores += options ? "1:"+std::string(1,options->stores[n]) : "1:L"; }
  if(options&&options->reorderMinimumMiB){put(attributes,"reordermb",i(*options->reorderMinimumMiB));}
  put(attributes, "storetypes", stores + 'e');
  applyIntakeNetwork(attributes,network,alias.has_value());
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
  std::string links="l";
  if(options&&!options->destinations.empty()){
    const auto paths=physicalIntakePaths(*original,destination,effectiveSaveName(*original,*options),options->destinations,filenameConversions?std::optional<std::string_view>{*filenameConversions}:std::nullopt);
    for(std::size_t n=0;n<paths.size();++n){if(original->files[n].padding){continue;}const auto base=fs::path(destination)/(original->multifile?fs::path(effectiveSaveName(*original,*options))/original->files[n].path:fs::path(effectiveSaveName(*original,*options)));if(base!=paths[n]){links+=b(std::to_string(n)+'\n'+base.string()+'\n'+paths[n].string());}}
  }
  put(attributes, "filelinks2", links+'e');
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
  if(alias){metadata=state;archive=state;} // Alternate archive also requires fresh verification; source stays untouched.
  auto verify = core::readLibrary(parse(after));
  require(verify && verify->issues.empty() && verify->downloads.size() == saved->downloads.size() + 1,
      ErrorCode::InvalidFormat, "New inventory failed its original reader");
  const auto pause=std::ranges::find_if(verify->paused,[&](const auto& value){return value.hash==*hash;});
  const bool paused=options&&options->paused;
  require((pause!=verify->paused.end())==paused&&verify->paused.size()==saved->paused.size()+(paused?1U:0U)
      &&(!paused||(!pause->forceStart&&pause->owner==1)),ErrorCode::InvalidFormat,"New paused intent failed its original reader");
  return {*hash, std::move(metadata), std::move(archive), std::move(destination), std::move(profile), std::move(state), std::move(after),
      std::move(before), std::move(oldBackup), stopped, forceStart, static_cast<mode_t>(mode & 0777),std::move(options),std::move(alias),std::move(filenameConversions),decoderShowAll,network,addedTime,subscriptionAcquisition,initialScrapeCache,automaticGeometry};
}
std::string journalBytes(const Plan& p) {
  std::string optionalOptions;
  if(p.addedTime){optionalOptions+=b("addedTime")+i(*p.addedTime);}
  if(p.subscriptionAcquisition){optionalOptions+=b("subscriptionAcquisition")+"d"+(p.initialScrapeCache?b("scrapeCache")+i(*p.initialScrapeCache):std::string{})+(p.automaticGeometry?b("automaticGeometry")+i(1):std::string{})+"e";}
  if(p.options) {
    std::string value="d"+b("policy")+b(p.options->policy)+b("stores")+b(p.options->stores);
    if(p.options->reorderMinimumMiB){value+=b("reordermb")+i(*p.options->reorderMinimumMiB);}
    // Omit zero choices so old default journals remain byte-compatible.
    if(p.options->limits.downloadBytesPerSecond){value+=b("maxdl")+i(p.options->limits.downloadBytesPerSecond);}
    if(p.options->limits.uploadBytesPerSecond){value+=b("maxul")+i(p.options->limits.uploadBytesPerSecond);}
    if(p.options->sequentialDownload){value+=b("sequential")+"i1e";}
    if(p.options->queued){value+=b("queued")+"i1e";}
    if(p.options->paused){value+=b("paused")+"i1e";}
    if(p.options->allocation!=core::TorrentAllocationMode::None){value+=b("allocation")+i(static_cast<int>(p.options->allocation));}
    if(p.options->verifyExisting){value+=b("verifiedSeed")+"i1e";}
    if(p.options->saveName){value+=b("saveName")+b(*p.options->saveName);}
    if(p.options->existingScope){value+=b("existingScope")+*p.options->existingScope;}
    if(!p.options->destinations.empty()){value+=b("fileDestinations")+encodeFileDestinations(p.options->destinations);}
    if(p.options->destinationSnapshot){value+=b("destinationSnapshot")+b(*p.options->destinationSnapshot);}
    if(p.options->goal==core::ExistingVerificationGoal::SelectedFiles){value+=b("existingGoal")+"i1e"+b("selectedScope")+*p.options->selectedScope;}
    if(p.options->trackers){
      value+=b("trackers")+'l';for(const auto& tier:*p.options->trackers){value+='l';for(const auto& url:tier){value+=b(url);}value+='e';}value+='e';
    }
    optionalOptions+=b("fileOptions")+value+'e';
  }
  if(p.network==TorrentIntakeNetwork::I2p){optionalOptions+=b("network")+b("I2P");}
  else if(p.network==TorrentIntakeNetwork::Mixed){optionalOptions+=b("network")+b("Public+I2P");}
  if(p.filenameConversions){optionalOptions+=b("filenameConversions")+b(*p.filenameConversions);}
  if(p.decoderShowAll){optionalOptions+=b("decoderShowAll")+i(*p.decoderShowAll?1:0);}
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
void checkSaveNameScope(const ProfileLease&,const Plan&);
Plan readJournal(std::string bytes, const std::string& profile,const ProfileLease& lease,bool allowSubscriptionAcquisition=false,bool targetOnlySettings=false) {
  auto document = parse(std::move(bytes), MaximumJournal);
  uniqueKeys(document.root());
  const auto* force = document.find("forceStart");
  const auto* addedNode=document.find("addedTime");std::optional<std::int64_t> addedTime;
  if(addedNode){addedTime=integer(addedNode);require(*addedTime>0,ErrorCode::InvalidFormat,"Invalid accepted original added time");}
  const auto* optionsNode=document.find("fileOptions");
  const auto* acquisition=document.find("subscriptionAcquisition");std::optional<std::int64_t> initialScrapeCache;bool automaticGeometry=false;
  if(acquisition){
    require(allowSubscriptionAcquisition,ErrorCode::Conflict,"Subscription intake requires its private owned recovery");
    require(acquisition->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid acquisition intake journal");
    const auto* scrape=acquisition->find("scrapeCache");
    const auto* automatic=acquisition->find("automaticGeometry");
    require(acquisition->children.size()==(scrape?2U:0U)+(automatic?2U:0U),ErrorCode::InvalidFormat,"Unknown acquisition preparation field");
    if(automatic){require(integer(automatic)==1,ErrorCode::InvalidFormat,"Invalid automatic geometry mode");automaticGeometry=true;}
    if(scrape){initialScrapeCache=integer(scrape);}
  }
  const auto* networkNode=document.find("network");
  const auto networkText=networkNode?text(networkNode):std::string{};
  require(!networkNode||networkText=="I2P"||networkText=="Public+I2P",ErrorCode::InvalidFormat,"Invalid accepted intake network context");
  const auto network=!networkNode?TorrentIntakeNetwork::Public:networkText=="I2P"?TorrentIntakeNetwork::I2p:TorrentIntakeNetwork::Mixed;
  const auto* aliasNode=document.find("hybridAlias");
  const auto* conversionsNode=document.find("filenameConversions");
  const auto* decoderNode=document.find("decoderShowAll");std::optional<bool> decoderShowAll;
  if(decoderNode){const auto value=integer(decoderNode);require((value==0||value==1)&&conversionsNode,ErrorCode::InvalidFormat,"Invalid accepted decoder context");decoderShowAll=value==1;require(targetOnlySettings||*decoderShowAll==take(readOriginalDecoderShowAll(lease)),ErrorCode::Conflict,"Decoder registration setting changed since intake acceptance");}
  std::optional<std::string> conversions;if(conversionsNode){conversions=text(conversionsNode);const auto current=targetOnlySettings?*conversions:take(readOriginalFilenameConversions(lease));require(*conversions==current,ErrorCode::Conflict,"Filename conversion policy changed since intake acceptance");}
  require(document.root().children.size()==20+(addedNode?2U:0U)+(force?2U:0U)+(optionsNode?2U:0U)+(aliasNode?2U:0U)+(conversionsNode?2U:0U)+(decoderNode?2U:0U)+(networkNode?2U:0U)+(acquisition?2U:0U)
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
    const auto* paused=optionsNode->find("paused");const auto pausedValue=paused?integer(paused):0;
    require(pausedValue==0||pausedValue==1,ErrorCode::InvalidFormat,"Invalid paused intake intent");
    const auto* allocation=optionsNode->find("allocation");
    require(!allocation||(integer(allocation)==5||integer(allocation)==6),ErrorCode::InvalidFormat,"Invalid allocation journal intent");
    const auto* verified=optionsNode->find("verifiedSeed");
    require(!verified||integer(verified)==1,ErrorCode::InvalidFormat,"Invalid verified-seeding journal intent");
    const auto* saveName=optionsNode->find("saveName");
    const auto* existing=optionsNode->find("existingScope");
    const auto* destinations=optionsNode->find("fileDestinations");
    const auto* destinationSnapshot=optionsNode->find("destinationSnapshot");
    const auto* goal=optionsNode->find("existingGoal"),*selectedScope=optionsNode->find("selectedScope");
    require(!goal||(integer(goal)==1&&verified),ErrorCode::InvalidFormat,"Invalid selected-existing journal goal");
    require(!destinations||conversions.has_value(),ErrorCode::InvalidFormat,"Indexed destination journal requires its saved filename policy");
    const auto* trackers=optionsNode->find("trackers");
    std::optional<core::TorrentIntakeTrackerTiers> override;
    if(trackers){override=core::decodeTorrentIntakeTrackerTiers(*trackers);require(override.has_value(),ErrorCode::InvalidFormat,"Invalid tracker override journal");}
    require(optionsNode->children.size()==4+(reorder?2U:0U)+(download?2U:0U)+(upload?2U:0U)+(sequential?2U:0U)+(queued?2U:0U)+(paused?2U:0U)+(trackers?2U:0U)+(saveName?2U:0U)+(verified?2U:0U)+(existing?2U:0U)+(destinations?2U:0U)+(destinationSnapshot?2U:0U)+(goal?2U:0U)+(selectedScope?2U:0U)+(allocation?2U:0U),ErrorCode::InvalidFormat,"Unexpected intake options fields");
    options=IntakeFileOptions{text(optionsNode->find("policy")),text(optionsNode->find("stores")),
        reorder?std::optional{integer(reorder)}:std::nullopt,
        {download?integer(download):0,upload?integer(upload):0},sequentialValue==1,queuedValue==1,pausedValue==1,std::move(override),saveName?std::optional{text(saveName)}:std::nullopt,verified!=nullptr};
    if(existing){options->existingScope=take(VerifiedExistingScope::decode(*existing)).encoded();}
    if(destinations){options->destinations=decodeFileDestinations(*destinations);}
    if(destinationSnapshot){options->destinationSnapshot=text(destinationSnapshot);}
    options->goal=goal?core::ExistingVerificationGoal::SelectedFiles:core::ExistingVerificationGoal::FullTorrent;
    options->allocation=allocation?static_cast<core::TorrentAllocationMode>(integer(allocation)):core::TorrentAllocationMode::None;
    if(selectedScope){options->selectedScope=take(SelectedExistingScope::decode(*selectedScope)).encoded();}
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
  auto result=plan(text(document.find("metadata")), std::move(destination), profile,
      before ? std::optional{std::move(old)} : std::nullopt,
      backup ? std::optional{std::move(bak)} : std::nullopt, stopped == 1, static_cast<mode_t>(mode), forceStart == 1,std::move(options),std::move(alias),std::move(conversions),decoderShowAll,network,addedTime,acquisition!=nullptr,initialScrapeCache,automaticGeometry);
  if(!result.filenameConversions){
    const auto current=take(readOriginalFilenameContext(lease));const auto original=metadataSubject(result.metadata);const auto projected=metadataSubject(result.metadata,current.conversions,current.decoderShowAll);
    require(original.layout.name==projected.layout.name&&original.layout.files.size()==projected.layout.files.size()
        &&std::ranges::equal(original.layout.files,projected.layout.files,[](const auto& a,const auto& b){return a.path==b.path;}),
        ErrorCode::Unsupported,"Legacy accepted intake names require explicit review before filename conversion");
  }
  if(!targetOnlySettings){checkSaveNameScope(lease,result);}return result;
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
bool intersectsSaveName(const fs::path& a,const fs::path& b) {
  const auto within=[](const fs::path& path,const fs::path& root){const auto relative=path.lexically_relative(root);return !relative.empty()&&*relative.begin()!="..";};
  return within(a,b)||within(b,a);
}
void checkSaveNameScope(const ProfileLease& lease,const Plan& p) {
  const auto currentContext=take(readOriginalFilenameContext(lease));
  if(p.filenameConversions){require(currentContext.conversions==*p.filenameConversions,ErrorCode::Conflict,"Filename conversion policy changed since intake acceptance");}
  if(p.decoderShowAll){require(currentContext.decoderShowAll==*p.decoderShowAll,ErrorCode::Conflict,"Decoder registration setting changed since intake acceptance");}
  if(!p.options){return;}
  const auto subject=metadataSubject(p.metadata,p.filenameConversions?std::optional<std::string_view>{*p.filenameConversions}:std::nullopt,p.decoderShowAll.value_or(false));const auto& leaf=effectiveSaveName(subject.layout,*p.options);
  const bool renamed=leaf!=subject.layout.name,linked=!p.options->destinations.empty();
  const bool selectedGoal=p.options->goal==core::ExistingVerificationGoal::SelectedFiles;
  const bool allocating=p.options->allocation!=core::TorrentAllocationMode::None;
  if(!renamed&&!linked&&!selectedGoal&&!allocating&&!p.automaticGeometry){return;}
  const auto policy=p.filenameConversions?std::optional<std::string_view>{*p.filenameConversions}:std::nullopt;
  const auto paths=physicalIntakePaths(subject.layout,p.destination,leaf,p.options->destinations,policy);
  if(p.options->destinationSnapshot){require(intakeDestinationSnapshot(subject,paths)==*p.options->destinationSnapshot,ErrorCode::Conflict,"Approved indexed destination observations changed");}
  std::set<std::string,std::less<>> reviewed;
  if(renamed||p.automaticGeometry){reviewed.insert((fs::path(p.destination)/leaf).string());}
  if(linked||selectedGoal||allocating){for(const auto& path:paths){if(!path.empty()){reviewed.insert(path.string());}}}
  const auto intersectsReviewed=[&](const fs::path& path){
    for(auto ancestor=path;!ancestor.empty();ancestor=ancestor.parent_path()){
      if(reviewed.contains(ancestor.string())){return true;}if(ancestor==ancestor.root_path()){break;}
    }
    const auto prefix=path.string()+(path==path.root_path()?"":"/");const auto found=reviewed.lower_bound(prefix);
    return found!=reviewed.end()&&found->starts_with(prefix);
  };
  for(const auto& value:reviewed){const fs::path chosen(value);pathShape(chosen);
    require(!intersectsSaveName(chosen,fs::path(p.profile)),ErrorCode::UnsafePath,"Chosen payload cannot overlap the held profile");
    for(const auto& component:chosen.relative_path()){require(component!=".git"&&!component.native().starts_with(".BiglyBTC"),ErrorCode::UnsafePath,"Chosen payload overlaps a reserved path");}
  }
  try{
    if(selectedGoal){
      auto value=parse(*p.options->selectedScope);const auto scope=take(SelectedExistingScope::decode(value.root()));
      auto row=parse("de");put(row,"file_priorities_c",p.options->policy);const auto policies=compat::originalFilePolicy(row.root(),subject.layout.files.size());require(policies.has_value(),ErrorCode::InvalidFormat,"Invalid selected scope policy");
      std::vector<OriginalStorageKind> kinds;for(const auto kind:p.options->stores){kinds.push_back(kind=='L'?OriginalStorageKind::Linear:kind=='C'?OriginalStorageKind::Compact:kind=='R'?OriginalStorageKind::Reordered:OriginalStorageKind::ReorderedCompact);}
      const auto checked=scope.validate(lease,subject.torrent,subject.layout,subject.identity,*policies,kinds,paths);if(!checked){throw Failure{checked.error()};}
    }else if(p.options->existingScope){const auto encoded=parse(*p.options->existingScope);const auto scope=take(VerifiedExistingScope::decode(encoded.root()));
      const auto checked=scope.validate(lease,subject.torrent,subject.layout,subject.identity,p.destination,leaf,linked?std::span<const fs::path>{paths}:std::span<const fs::path>{});if(!checked){throw Failure{checked.error()};}
    }else{
      if(renamed&&!p.automaticGeometry&&(subject.layout.multifile||!linked)){const auto chosen=fs::path(p.destination)/leaf;auto parent=fileop::directory(chosen.parent_path(),false,true);if(parent.get()>=0){require(!named(parent.get(),chosen.filename().native()),ErrorCode::Conflict,"Chosen save name already exists; no payload reuse or overwrite was approved");}}
      // Explicit indexed links, including creator initial linkage, may name
      // existing ordinary data just as the canonical map can. Intake never
      // creates, truncates, repairs or moves it; normal owner checks still apply.
      if(linked){std::set<std::pair<std::uint64_t,std::uint64_t>> inodes;
        for(std::size_t n=0;n<paths.size();++n){if(paths[n].empty()){continue;}const auto actual=inspectPayload(paths[n]);if(!actual){continue;}
          require(S_ISREG(actual->st_mode)&&actual->st_uid==::geteuid()&&actual->st_nlink==1&&actual->st_size>=0&&actual->st_size<=subject.layout.files[n].bytes
              &&inodes.emplace(actual->st_dev,actual->st_ino).second,ErrorCode::UnsafePath,"Explicit file target is unsafe, oversized or aliases another leaf");
        }
      }
    }
  }catch(const fileop::Failure& error){throw Failure{error.error};}
  const auto inventory=take([&]()->std::expected<core::LibrarySnapshot,Error>{auto source=core::readLibrary(parse(p.before.value_or("d9:downloadslee")));if(!source||!source->issues.empty()){return std::unexpected(Error{ErrorCode::InvalidFormat,"Invalid existing save-name inventory"});}return std::move(*source);}());
  if(inventory.downloads.empty()){return;}
  auto store=take(TorrentStateStore::open(lease));
  auto active=childDirectory(lease.directoryHandle(),"active",false);
  require(active.get()>=0,ErrorCode::UnsafePath,"Existing save mappings have no authenticated state directory");
  std::optional<EncodedDocument> cache;std::map<std::string,const Node*,std::less<>> cached;
  if(const auto input=read(active.get(),"cache.dat",MaximumInventory)){
    auto decoded=EncodedDocument::parse(input->bytes,compatibleConfigLimits());
    require(decoded&&decoded->compressed(),ErrorCode::InvalidFormat,"Resolve invalid original cache before choosing a save name");
    cache.emplace(std::move(*decoded));
    const auto projection=core::readStateCache(cache->document());require(projection&&projection->issues.empty(),ErrorCode::InvalidFormat,"Invalid original mapping cache");
    if(const auto* entries=cache->document().find("state")){
      require(entries->type==Node::Type::List,ErrorCode::InvalidFormat,"Invalid original mapping cache list");
      for(const auto& entry:entries->children){const auto* hash=entry.find("hash");if(!hash){continue;}const auto key=core::TorrentHash::parse(hash->bytes);require(key.has_value(),ErrorCode::InvalidFormat,"Invalid cached mapping identity");cached.insert_or_assign(key->hex(),&entry);}
    }
  }
  const auto& conversion=currentContext.conversions;
  std::size_t files=0;
  for(const auto& saved:inventory.downloads){
    require(saved.hash.has_value(),ErrorCode::Unsupported,"Cannot prove independence from a saved mapping without its hash");
    const auto state=take(store.load(lease,*saved.hash));const Node* attributes=state.document().find("attributes");
    const Node* properties=state.document().find("azureus_private_properties");
    if(const auto found=cached.find(saved.hash->hex());found!=cached.end()){
      if(const auto* value=found->second->find("attributes")){attributes=value;}
      if(const auto* value=found->second->find("azpp")){properties=value;}
    }
    std::string metadata="d4:info"+std::string(take(originalTorrentInfoSubject(state.document())));
    if(const auto* encoding=state.document().find("encoding")){metadata+=b("encoding")+raw(state.document(),*encoding);}
    if(properties){const auto encoded=core::encodeValue(*properties,compatibleConfigLimits());require(encoded.has_value(),ErrorCode::InvalidFormat,"Invalid original identity properties");metadata+=b("azureus_private_properties")+*encoded;}
    const auto other=metadataSubject(metadata+'e',conversion,currentContext.decoderShowAll);const auto match=validateOriginalTorrentKey(other.identity,*saved.hash,true);require(match.has_value(),ErrorCode::Conflict,"Other saved mapping differs from its authenticated torrent identity");
    require(other.layout.files.size()<=100000-files,ErrorCode::Unsupported,"Saved mapping claims exceed the save-name validation budget");files+=other.layout.files.size();
    const auto resolved=take(resolveOriginalSaveRoot(saved,other.layout));const auto root=fs::path(resolved.saveDirectory)/ *resolved.saveFile;
    require(!intersectsReviewed(root),ErrorCode::Conflict,"Chosen save name overlaps another saved payload root");
    require(!attributes||attributes->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid authoritative saved mapping attributes");
    std::vector<std::string> bases;for(const auto& file:other.layout.files){bases.push_back((fs::path(resolved.saveDirectory)/(other.layout.multifile?fs::path(*resolved.saveFile)/file.path:fs::path(*resolved.saveFile))).native());}
    const auto links=compat::resolveOriginalFileLinks(attributes?attributes->find("filelinks2"):nullptr,bases);
    require(links.has_value(),ErrorCode::InvalidFormat,"Invalid original saved file links");
    for(const auto& link:links->byIndex){if(!link){continue;}const fs::path target(link->target);pathShape(target);require(!intersectsReviewed(target),ErrorCode::Conflict,"Chosen save name overlaps another saved linked payload");}
    if(attributes){if(const auto* alternate=attributes->find("dnd_al")){require(alternate->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Invalid alternate saved DND root");if(!alternate->bytes.empty()){const fs::path target(alternate->bytes);pathShape(target);require(!intersectsReviewed(target),ErrorCode::Conflict,"Chosen save name overlaps another saved DND root");}}}
  }
}
void inventoryCurrent(int root, const Plan& p) {
  const auto current = contents(read(root, "downloads.config", MaximumInventory));
  require(current == p.before || current == std::optional{p.inventory}, ErrorCode::Conflict, "Inventory changed outside pending intake");
  absent(root, "downloads.config.saving");
  const auto backup = contents(read(root, "downloads.config.bak", MaximumInventory));
  require(backup == p.oldBackup || (p.before && backup == p.before), ErrorCode::Conflict, "Inventory backup changed outside pending intake");
}
void notify(void (*observer)(TorrentIntakeStage), TorrentIntakeStage value) { if (observer) { observer(value); } }
void apply(ProfileLease& lease, const Plan& p, std::string_view journal, void (*observer)(TorrentIntakeStage),bool ownedAcquisition=false,std::string_view ownerIntent={}) {
  const auto guard=[&]{if(ownedAcquisition){acquisitionOwnerCurrent(lease,ownerIntent);}else{noAcquisitionIntent(lease);}};guard();
  const int root = lease.directoryHandle();
  require(profilePath(lease) == p.profile, ErrorCode::Conflict, "Profile moved during intake");
  inventoryCurrent(root, p);
  checkSaveNameScope(lease,p);
  if(p.alias){hybridSourceCurrent(root,*p.alias,p.filenameConversions?std::optional<std::string_view>{*p.filenameConversions}:std::nullopt,p.decoderShowAll.value_or(false));}
  if(p.options){const auto source=metadataSubject(p.metadata,p.filenameConversions?std::optional<std::string_view>{*p.filenameConversions}:std::nullopt,p.decoderShowAll.value_or(false));checkCompactAbsence(source.layout,*p.options,p.destination,p.filenameConversions?std::optional<std::string_view>{*p.filenameConversions}:std::nullopt);}
  auto active = childDirectory(root, "active", true);
  auto archive = childDirectory(root, "torrents", true);
  const auto stateName = p.hash.hex() + ".dat";
  const auto archiveName = p.hash.hex() + ".torrent";
  for (const auto& suffix : {".bak", ".saving", ".bak.saving"}) { absent(active.get(), stateName + suffix); }
  guard();publishNew(active.get(), stateName, p.state);
  notify(observer, TorrentIntakeStage::StatePublished);guard();
  checkSaveNameScope(lease,p);
  publishNew(archive.get(), archiveName, p.archive);
  notify(observer, TorrentIntakeStage::ArchivePublished);guard();
  childIdentity(root, "active", active.get()); childIdentity(root, "torrents", archive.get());
  const auto state = read(active.get(), stateName, MaximumInventory);
  const auto source = read(archive.get(), archiveName, MaximumMetadata);
  require(state && state->bytes == p.state && source && source->bytes == p.archive,
      ErrorCode::Conflict, "Published intake artifacts changed before inventory admission");
  require(profilePath(lease) == p.profile, ErrorCode::Conflict, "Profile moved during intake");
  inventoryCurrent(root, p);
  checkSaveNameScope(lease,p);
  if(p.alias){hybridSourceCurrent(root,*p.alias,p.filenameConversions?std::optional<std::string_view>{*p.filenameConversions}:std::nullopt,p.decoderShowAll.value_or(false));}
  if(p.options){const auto source=metadataSubject(p.metadata,p.filenameConversions?std::optional<std::string_view>{*p.filenameConversions}:std::nullopt,p.decoderShowAll.value_or(false));checkCompactAbsence(source.layout,*p.options,p.destination,p.filenameConversions?std::optional<std::string_view>{*p.filenameConversions}:std::nullopt);}
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
    notify(observer, TorrentIntakeStage::InventoryPublished);guard();
  }
  checkSaveNameScope(lease,p);
  require(contents(read(root, "downloads.config", MaximumInventory)) == std::optional{p.inventory},
      ErrorCode::Conflict, "Published inventory changed before intake acknowledgement");
  guard();removeExact(active.get(), stateName + ".import", p.state);
  removeExact(archive.get(), archiveName + ".import", p.archive);
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
core::TorrentHash commitIntakePlan(ProfileLease& lease,Plan& p,void(*observer)(TorrentIntakeStage),std::stop_token stop,bool ownedAcquisition=false,std::string_view ownerIntent={}) {
  if(ownedAcquisition){acquisitionOwnerCurrent(lease,ownerIntent);}else{noAcquisitionIntent(lease);require(!p.subscriptionAcquisition,ErrorCode::Conflict,"Marked acquisition intake requires its private owner");}
  const int root=lease.directoryHandle();
  checkSaveNameScope(lease,p);
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
    if(p.alias){hybridSourceCurrent(root,*p.alias,p.filenameConversions?std::optional<std::string_view>{*p.filenameConversions}:std::nullopt,p.decoderShowAll.value_or(false));}
    const auto journal = journalBytes(p);
    require(journal.size() <= MaximumJournal, ErrorCode::InvalidFormat, "Intake journal exceeds its bounded limit");
    require(!stop.stop_requested(),ErrorCode::Cancelled,"Torrent import cancelled before commit");
    auto fd = stage(root, JournalSaving, journal, 0600);
    verifyStage(root, JournalSaving, fd);
    inventoryCurrent(root, p);
    move(root, JournalSaving, Journal, RENAME_NOREPLACE);
    notify(observer, TorrentIntakeStage::JournalSynced);
    apply(lease, p, journal, observer,ownedAcquisition,ownerIntent);
    return p.hash;
}
std::string existingDictionary(const std::map<std::string,std::string>& values){std::string out="d";for(const auto& [key,value]:values){out+=b(key)+value;}return out+'e';}
std::string existingStampBytes(const struct stat& stamp){
  return existingDictionary({{"dev",b(std::to_string(stamp.st_dev))},{"ino",b(std::to_string(stamp.st_ino))},{"mode",b(std::to_string(stamp.st_mode))},{"uid",b(std::to_string(stamp.st_uid))},{"gid",b(std::to_string(stamp.st_gid))},{"links",b(std::to_string(stamp.st_nlink))},{"size",b(std::to_string(stamp.st_size))},{"mtsec",b(std::to_string(stamp.st_mtim.tv_sec))},{"mtns",b(std::to_string(stamp.st_mtim.tv_nsec))},{"ctsec",b(std::to_string(stamp.st_ctim.tv_sec))},{"ctns",b(std::to_string(stamp.st_ctim.tv_nsec))}});
}
struct stat existingStampValue(const Node& node){
  require(node.type==Node::Type::Dictionary&&node.children.size()==22,ErrorCode::InvalidFormat,"Unknown existing-target stamp field");uniqueKeys(node);struct stat stamp{};
  const auto number=[&](auto& destination,std::string_view key){const auto* value=node.find(key);require(value&&value->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Missing existing-target stamp");const auto result=std::from_chars(value->bytes.data(),value->bytes.data()+value->bytes.size(),destination);require(result.ec==std::errc{}&&result.ptr==value->bytes.data()+value->bytes.size(),ErrorCode::InvalidFormat,"Malformed existing-target stamp");};
  number(stamp.st_dev,"dev");number(stamp.st_ino,"ino");number(stamp.st_mode,"mode");number(stamp.st_uid,"uid");number(stamp.st_gid,"gid");number(stamp.st_nlink,"links");number(stamp.st_size,"size");number(stamp.st_mtim.tv_sec,"mtsec");number(stamp.st_mtim.tv_nsec,"mtns");number(stamp.st_ctim.tv_sec,"ctsec");number(stamp.st_ctim.tv_nsec,"ctns");
  require(stamp.st_mtim.tv_nsec>=0&&stamp.st_mtim.tv_nsec<1000000000&&stamp.st_ctim.tv_nsec>=0&&stamp.st_ctim.tv_nsec<1000000000,ErrorCode::InvalidFormat,"Invalid existing-target timestamp");return stamp;
}
void automaticOwnerRecipe(std::string_view owner,std::string_view recipe){
  const auto intent=parse(std::string(owner),MaximumJournal);uniqueKeys(intent.root());
  require(text(intent.find("schema"))=="BiglyBTC.Subscription.Acquisition.V3"&&text(intent.find("plan"))==recipe,ErrorCode::Conflict,"Automatic effect requires the exact durably published closed recipe");
}
#include "subscription_automatic_intake.inc"
} // namespace

struct HistoryPostimages {std::string inventory,state,archive;};

static std::expected<core::TorrentHash, Error> importImpl(ProfileLease& lease,
    const fs::path& metadata, const fs::path& destination, bool stopped,
    void (*observer)(TorrentIntakeStage), bool forceStart,TorrentIntakeNetwork network=TorrentIntakeNetwork::Public,HistoryPostimages* historyExpected=nullptr,
    std::optional<std::span<const compat::FilePolicy>> policies = {},const File* expected = nullptr, std::stop_token stop = {},
    core::TorrentIntakeLimits limits = {},bool sequentialDownload = false,bool queued = false,bool paused = false,std::optional<core::TorrentIntakeTrackerTiers> trackers = {},std::optional<std::string> saveName = {},bool verifyExisting = false,const std::string* expectedConversions=nullptr,std::optional<core::TorrentIntakeFileDestinations> fileDestinations={},core::ExistingVerificationGoal goal=core::ExistingVerificationGoal::FullTorrent,const bool* expectedDecoderShowAll=nullptr,core::TorrentAllocationMode allocation=core::TorrentAllocationMode::None,std::optional<Plan>* preparedPlan=nullptr,std::optional<std::int64_t> initialScrapeCache={}) {
  try {
    noAcquisitionIntent(lease);
    require(network==TorrentIntakeNetwork::Public||network==TorrentIntakeNetwork::I2p||network==TorrentIntakeNetwork::Mixed,ErrorCode::InvalidFormat,"Invalid intake network context");
    require(network!=TorrentIntakeNetwork::I2p||(!verifyExisting&&allocation==core::TorrentAllocationMode::None),
        ErrorCode::Unsupported,"I2P intake requires ordinary intent without verification or allocation");
    require(network!=TorrentIntakeNetwork::Mixed||(!verifyExisting&&allocation==core::TorrentAllocationMode::None),
        ErrorCode::Unsupported,"Mixed intake requires ordinary intent without verification or allocation");
    require(core::validTorrentAllocationMode(allocation)&& (allocation==core::TorrentAllocationMode::None||(stopped&&!forceStart&&!queued&&!paused&&!verifyExisting&&policies.has_value())),ErrorCode::InvalidFormat,"Allocation requires explicit selected stopped/unforced intent");
    require((goal==core::ExistingVerificationGoal::FullTorrent||goal==core::ExistingVerificationGoal::SelectedFiles)&&(verifyExisting||goal==core::ExistingVerificationGoal::FullTorrent),ErrorCode::InvalidFormat,"Invalid existing verification goal");
    require(!stop.stop_requested(),ErrorCode::Cancelled,"Torrent import cancelled before commit");
    require(!saveName||core::validTorrentIntakeSaveName(*saveName),ErrorCode::UnsafePath,"Save name must be one safe UTF8 filename component");
    require(!trackers||core::validTorrentIntakeTrackerTiers(*trackers),ErrorCode::InvalidFormat,"Invalid tracker override");
    require(core::validTorrentIntakeLimits(limits),ErrorCode::InvalidFormat,"Per-torrent limits must be between zero and INT32_MAX bytes/s");
    require(!queued||(!stopped&&!forceStart),ErrorCode::InvalidFormat,"Queued intake conflicts with stopped/forced intent");
    require(!verifyExisting||(stopped&&!forceStart&&!queued&&!paused),ErrorCode::InvalidFormat,"Verified seeding requires stopped/unforced/unpaused intake");
    require(!paused||(stopped&&!forceStart&&!queued),ErrorCode::InvalidFormat,"Paused intake requires stopped/unforced/nonqueued intent");
    const auto profile = profilePath(lease);
    const int root = lease.directoryHandle();
    for (const auto* name : {Journal, JournalSaving, InventorySaving, BackupSaving, "downloads.config.saving"}) { absent(root, name); }
    pathShape(metadata); destinationPath(destination);
    auto parent = directory(metadata.parent_path());
    auto source = read(parent.get(), metadata.filename().native(), MaximumMetadata);
    require(source.has_value(), ErrorCode::Missing, "Torrent metadata file is missing");
    require(!expected||(unchanged(expected->info,source->info)&&expected->bytes==source->bytes),
        ErrorCode::Conflict,"Torrent metadata changed after file preview; load it again");
    const auto context=take(readOriginalFilenameContext(lease));const auto& conversions=context.conversions;
    require(!expectedDecoderShowAll||*expectedDecoderShowAll==context.decoderShowAll,ErrorCode::Conflict,"Decoder registry changed after file preview; load it again");
    require(!expectedConversions||*expectedConversions==conversions,ErrorCode::Conflict,"Filename conversion policy changed after file preview; load it again");
    const auto subject=metadataSubject(source->bytes,conversions,context.decoderShowAll);
    auto targets=fileDestinations?std::move(*fileDestinations):initialFileDestinations(subject);
    require(core::validTorrentIntakeFileDestinations(targets),ErrorCode::InvalidFormat,"Invalid bounded indexed destinations");
    fs::path effectiveDestination=destination;
    if(!subject.layout.multifile&&!targets.empty()){
      require(targets.size()==1&&targets[0].index==0,ErrorCode::InvalidFormat,"Single-file target must name original index zero");
      const auto target=projectedIntakeTarget(targets[0],conversions);
      require(!saveName||*saveName==target.filename().string(),ErrorCode::InvalidFormat,"Single-file target conflicts with Save as name");
      effectiveDestination=target.parent_path();saveName=target.filename().string();destinationPath(effectiveDestination);
    }
    std::vector<compat::FilePolicy> defaultPolicies;
    if(!policies&&!targets.empty()){for(const auto& file:subject.layout.files){defaultPolicies.push_back({file.padding,0});}policies=std::span<const compat::FilePolicy>{defaultPolicies};}
    std::optional<IntakeFileOptions> options;
    if(policies){options=fileOptions(lease,subject,effectiveDestination,*policies,limits,sequentialDownload,queued,paused,std::move(trackers),std::move(saveName),verifyExisting,std::move(targets),conversions,goal,allocation);}
    const auto before = read(root, "downloads.config", MaximumInventory);
    const auto backup = read(root, "downloads.config.bak", MaximumInventory);
    require(before || !backup, ErrorCode::Conflict, "Resolve missing inventory primary before intake");
    auto p = plan(std::move(source->bytes), effectiveDestination.native(), profile, contents(before), contents(backup),
        stopped, before ? before->info.st_mode : 0600, forceStart,std::move(options),{},conversions,context.decoderShowAll,network,intakeAddedTime(),preparedPlan!=nullptr,initialScrapeCache);
    if(preparedPlan){preparedPlan->emplace(std::move(p));return preparedPlan->value().hash;}
    auto imported=commitIntakePlan(lease,p,observer,stop);
    if(historyExpected){*historyExpected={std::move(p.inventory),std::move(p.state),std::move(p.archive)};}
    return imported;
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::exception&) { return std::unexpected(Error{ErrorCode::Io, "Torrent intake failed; inspect pending journal before retry"}); }
}

struct TorrentIntakeMetadata::Data {
  fs::path path;File input;compat::OriginalLayout layout;core::TorrentHash hash;std::optional<core::TorrentIntakeTrackerFields> trackers;std::string filenameConversions;core::TorrentIntakeFileDestinations initialDestinations;bool decoderShowAll=false;
};
TorrentIntakeMetadata::TorrentIntakeMetadata(std::shared_ptr<const Data> data):data_(std::move(data)){}
const core::TorrentHash& TorrentIntakeMetadata::hash()const noexcept{return data_->hash;}
const compat::OriginalLayout& TorrentIntakeMetadata::layout()const noexcept{return data_->layout;}
const fs::path& TorrentIntakeMetadata::source()const noexcept{return data_->path;}
const std::optional<core::TorrentIntakeTrackerFields>& TorrentIntakeMetadata::trackers()const noexcept{return data_->trackers;}
const core::TorrentIntakeFileDestinations& TorrentIntakeMetadata::initialDestinations()const noexcept{return data_->initialDestinations;}
const std::string& TorrentIntakeMetadata::filenameConversions()const noexcept{return data_->filenameConversions;}
bool TorrentIntakeMetadata::decoderShowAll()const noexcept{return data_->decoderShowAll;}
std::expected<TorrentIntakeMetadata,Error> inspectTorrentIntake(const fs::path& path,std::stop_token stop,std::string conversions,bool decoderShowAll) {
  try {
    require(!stop.stop_requested(),ErrorCode::Cancelled,"Torrent preview cancelled");pathShape(path);
    auto parent=directory(path.parent_path());auto input=read(parent.get(),path.filename().native(),MaximumMetadata);
    require(input.has_value(),ErrorCode::Missing,"Torrent metadata file is missing");
    auto subject=metadataSubject(input->bytes,conversions,decoderShowAll);
    auto initial=initialFileDestinations(subject);
    require(!stop.stop_requested(),ErrorCode::Cancelled,"Torrent preview cancelled");
    return TorrentIntakeMetadata(std::make_shared<const TorrentIntakeMetadata::Data>(
        TorrentIntakeMetadata::Data{path,std::move(*input),std::move(subject.layout),subject.hash,trackerFields(subject.torrent),std::move(conversions),std::move(initial),decoderShowAll}));
  } catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot inspect torrent metadata"});}
}
struct PreparedStoppedPublicIntake::Data {
  std::shared_ptr<const ProfileLease> lease;
  std::thread::id worker=std::this_thread::get_id();
  Plan plan;std::string journal,frozen,associationState,queued;std::optional<FixedSubscriptionDownloadPreparation> preparation;std::optional<AutomaticIntakeContext> automatic;Images view;
  fs::path source;std::optional<File> input;bool fresh=false;
  std::expected<State,Error> inspectBound(ProfileLease&,std::string_view,bool strictSourceSettings)const;
  Data(std::shared_ptr<const ProfileLease> held,Plan value,std::optional<FixedSubscriptionDownloadPreparation> fixed={},std::optional<AutomaticIntakeContext> actualAutomatic={})
      :lease(std::move(held)),plan(std::move(value)),journal(journalBytes(plan)),frozen(journal),associationState(plan.state),preparation(std::move(fixed)),automatic(std::move(actualAutomatic)) {
    require(plan.automaticGeometry==automatic.has_value(),ErrorCode::Conflict,"Automatic intake requires its closed actual-settings context");
    if(preparation){
      require(search::detail::subscriptionRawScrapeCache(preparation->rawCounts)==plan.initialScrapeCache,ErrorCode::Conflict,"Legacy packed cache differs from actual raw subscription counts");
      auto images=fixedSubscriptionStateImages(plan.state,*preparation,true);require(images.prepared==plan.state,ErrorCode::Conflict,"Fresh S0 cache preparation changed");associationState=std::move(images.associated);frozen=fixedFreshRecipe(journal,*preparation);
    }
    refreshFrozen();
    view={plan.hash,plan.before?std::optional<std::string_view>{*plan.before}:std::nullopt,
          plan.oldBackup?std::optional<std::string_view>{*plan.oldBackup}:std::nullopt,
          plan.inventory,plan.state,plan.archive,associationState};
  }
  void refreshFrozen(){
    if(automatic){
      require(preparation.has_value()&&plan.options&&plan.destination==automatic->geometry.destinationDirectory&&effectiveSaveName(metadataSubject(plan.metadata,*plan.filenameConversions,plan.decoderShowAll.value_or(false)).layout,*plan.options)==automatic->geometry.saveName,ErrorCode::Conflict,"Automatic geometry differs from actual saved plan");
      queued=automaticAdmissionInventory(plan,automatic->geometry.queued,automatic->geometry.paused);
      frozen=existingDictionary({{"schema",b("BiglyBTC.StoppedPublicSubscriptionIntake.V3")},{"journal",b(journal)},{"preparation",fixedPreparationBytes(*preparation)},{"automatic",b(automatic->encoded())}});
    }
  }
};
PreparedStoppedPublicIntake::PreparedStoppedPublicIntake(std::unique_ptr<Data> data)noexcept:data_(std::move(data)){}
PreparedStoppedPublicIntake::PreparedStoppedPublicIntake(PreparedStoppedPublicIntake&&)noexcept=default;
PreparedStoppedPublicIntake& PreparedStoppedPublicIntake::operator=(PreparedStoppedPublicIntake&&)noexcept=default;
PreparedStoppedPublicIntake::~PreparedStoppedPublicIntake()=default;
std::string_view PreparedStoppedPublicIntake::frozenJournal()const noexcept{return data_?std::string_view(data_->frozen):std::string_view{};}
const FixedSubscriptionDownloadPreparation* PreparedStoppedPublicIntake::fixedPreparation()const noexcept{return data_&&data_->preparation?&*data_->preparation:nullptr;}
const PreparedStoppedPublicIntake::Images& PreparedStoppedPublicIntake::images()const noexcept{return data_->view;}
std::expected<PreparedStoppedPublicIntake,Error> PreparedStoppedPublicIntake::prepare(ProfileLease& lease,
    const TorrentIntakeMetadata& metadata,const fs::path& destination,
    std::optional<std::int64_t> initialScrapeCache,std::stop_token stop,std::optional<FixedSubscriptionDownloadPreparation> preparation) {
  try {
    require(bool(metadata.data_),ErrorCode::Conflict,"Missing sealed acquisition metainfo");
    require(metadata.data_->initialDestinations.empty(),ErrorCode::Unsupported,"Acquisition refuses inherited file-destination overrides");
    std::vector<compat::FilePolicy> policies;
    for(const auto& file:metadata.data_->layout.files){policies.push_back({file.padding,0});}
    std::optional<Plan> prepared;
    auto result=importImpl(lease,metadata.data_->path,destination,true,nullptr,false,TorrentIntakeNetwork::Public,nullptr,
        std::span<const compat::FilePolicy>{policies},&metadata.data_->input,stop,{},false,false,false,{},{},false,
        &metadata.data_->filenameConversions,{},core::ExistingVerificationGoal::FullTorrent,
        &metadata.data_->decoderShowAll,core::TorrentAllocationMode::None,&prepared,initialScrapeCache);
    if(!result){return std::unexpected(result.error());}
    require(prepared.has_value(),ErrorCode::Conflict,"Private acquisition plan was not prepared");
    auto data=std::make_unique<Data>(take(lease.retain()),std::move(*prepared),std::move(preparation));
    require(data->frozen.size()<=MaximumJournal,ErrorCode::Unsupported,"Frozen acquisition intake plan exceeds bounds");
    data->source=metadata.data_->path;data->input=metadata.data_->input;data->fresh=true;
    return PreparedStoppedPublicIntake(std::move(data));
  }catch(const Failure& e){return std::unexpected(e.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot freeze stopped acquisition intake"});}
}
std::expected<PreparedStoppedPublicIntake,Error> PreparedStoppedPublicIntake::prepareAutomatic(ProfileLease& lease,
    const TorrentIntakeMetadata& metadata,FixedSubscriptionDownloadPreparation preparation,std::stop_token stop){
  try{
    require(metadata.data_&&!stop.stop_requested(),ErrorCode::Cancelled,"Automatic intake requires current sealed metainfo");
    auto access=take(lease.sourceAccess());noAcquisitionIntent(lease);const auto profile=profilePath(lease);
    for(const auto* name:{Journal,JournalSaving,InventorySaving,BackupSaving,"downloads.config.saving"}){absent(lease.directoryHandle(),name);}
    require(metadata.data_->initialDestinations.empty(),ErrorCode::Unsupported,"Automatic intake refuses inherited file destinations");
    const auto filename=take(readOriginalFilenameContext(lease));require(filename.conversions==metadata.filenameConversions()&&filename.decoderShowAll==metadata.decoderShowAll(),ErrorCode::Conflict,"Automatic filename policy changed");
    auto parent=directory(metadata.data_->path.parent_path());const auto source=read(parent.get(),metadata.data_->path.filename().native(),MaximumMetadata);
    require(source&&source->bytes==metadata.data_->input.bytes&&unchanged(source->info,metadata.data_->input.info),ErrorCode::Conflict,"Automatic metainfo changed");
    auto subject=metadataSubject(source->bytes,filename.conversions,filename.decoderShowAll);
    auto automatic=AutomaticIntakeContext::capture(lease,subject,source->bytes,filename.conversions);
    std::vector<compat::FilePolicy> selected;for(const auto& file:subject.layout.files){selected.push_back({file.padding,0});}
    auto options=fileOptions(lease,subject,automatic.geometry.destinationDirectory,selected,{},false,false,false,{},automatic.geometry.saveName,false,{},filename.conversions,core::ExistingVerificationGoal::FullTorrent,core::TorrentAllocationMode::None);
    const auto before=read(lease.directoryHandle(),"downloads.config",MaximumInventory),backup=read(lease.directoryHandle(),"downloads.config.bak",MaximumInventory);require(before||!backup,ErrorCode::Conflict,"Automatic intake refuses backup-only inventory");
    auto value=plan(source->bytes,automatic.geometry.destinationDirectory,profile,contents(before),contents(backup),true,before?before->info.st_mode:0600,false,std::move(options),{},filename.conversions,filename.decoderShowAll,TorrentIntakeNetwork::Public,intakeAddedTime(),true,search::detail::subscriptionRawScrapeCache(preparation.rawCounts),true);
    checkSaveNameScope(lease,value);automatic.verify(lease,true);
    auto data=std::make_unique<Data>(take(lease.retain()),std::move(value),std::move(preparation),std::move(automatic));
    require(data->frozen.size()<=MaximumJournal,ErrorCode::Unsupported,"Automatic frozen plan exceeds bound");data->source=metadata.data_->path;data->input=metadata.data_->input;data->fresh=true;return PreparedStoppedPublicIntake(std::move(data));
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot prepare actual automatic intake"});}
}
const search::detail::SubscriptionAutomaticPolicyValues* PreparedStoppedPublicIntake::automaticPolicy()const noexcept{return data_&&data_->automatic?&data_->automatic->policy:nullptr;}
const search::detail::SubscriptionAutomaticGeometry* PreparedStoppedPublicIntake::automaticGeometry()const noexcept{return data_&&data_->automatic?&data_->automatic->geometry:nullptr;}
std::string_view PreparedStoppedPublicIntake::queuedInventory()const noexcept{return data_&&data_->automatic?std::string_view(data_->queued):std::string_view{};}
std::optional<std::string_view> PreparedStoppedPublicIntake::automaticInventoryBackup()const noexcept{if(!data_||!data_->automatic){return {};}if(data_->automatic->queueBound){return data_->automatic->inventoryBackup?std::optional<std::string_view>{data_->automatic->inventoryBackup->bytes}:std::nullopt;}return data_->plan.before?std::optional<std::string_view>{*data_->plan.before}:data_->plan.oldBackup?std::optional<std::string_view>{*data_->plan.oldBackup}:std::nullopt;}
fs::path PreparedStoppedPublicIntake::automaticInventoryPath()const{return data_&&data_->automatic?fs::path(data_->plan.profile)/"downloads.config":fs::path{};}
std::expected<void,Error> PreparedStoppedPublicIntake::prepareAutomaticArtifacts(ProfileLease& lease,std::string_view owner){
  try{
    require(data_&&data_->automatic&&take(inspect(lease,owner))==State::Committed,ErrorCode::Conflict,"Automatic artifact preparation requires actual committed intake");
    auto& context=*data_->automatic;context.stageOwned(lease,owner);
    if(!context.copyParentScope){context.copyParentScope=automaticDirectoryStamp(context.copyPath.parent_path());}
    if(!context.queueBound){context.inventoryBackup=read(lease.directoryHandle(),"downloads.config.bak",MaximumInventory);require(contents(context.inventoryBackup)==(data_->plan.before?data_->plan.before:data_->plan.oldBackup),ErrorCode::Conflict,"Automatic queue backup differs from intake proof");context.queueBound=true;}
    data_->refreshFrozen();require(data_->frozen.size()<=MaximumJournal,ErrorCode::Unsupported,"Automatic created-inode recipe exceeds bound");return {};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Automatic artifact creation requires review"});}
}
std::expected<bool,Error> PreparedStoppedPublicIntake::automaticArtifactPresent(ProfileLease& lease,std::string_view owner)const{
  try{require(data_&&data_->automatic&&take(data_->inspectBound(lease,owner,false))==State::Committed,ErrorCode::Conflict,"Automatic artifact needs actual intake proof");return data_->automatic->artifactPresent(lease);}catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Cannot observe automatic artifact"});}
}
std::expected<void,Error> PreparedStoppedPublicIntake::publishAutomaticArtifact(ProfileLease& lease,std::string_view owner){
  try{require(data_&&data_->automatic&&take(inspect(lease,owner))==State::Committed,ErrorCode::Conflict,"Automatic artifact needs strict intake proof");automaticOwnerRecipe(owner,data_->frozen);data_->automatic->publish(lease,owner);return {};}catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Automatic artifact needs review"});}
}
std::expected<void,Error> PreparedStoppedPublicIntake::rollbackAutomaticArtifact(ProfileLease& lease,std::string_view owner){
  try{require(data_&&data_->automatic&&take(data_->inspectBound(lease,owner,false))==State::Committed,ErrorCode::Conflict,"Automatic artifact rollback needs actual intake proof");automaticOwnerRecipe(owner,data_->frozen);data_->automatic->rollback(lease,owner);return {};}catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Automatic artifact rollback needs review"});}
}
std::expected<void,Error> PreparedStoppedPublicIntake::publishAutomaticQueue(ProfileLease& lease,std::string_view owner,void(*observer)(SubscriptionDownloadStateStage)){
  try{require(data_&&data_->automatic&&data_->automatic->queueBound&&take(inspect(lease,owner))==State::Committed&&take(automaticArtifactPresent(lease,owner)),ErrorCode::Conflict,"Automatic queue requires complete strict artifact/intake proof");
    automaticOwnerRecipe(owner,data_->frozen);const auto& p=data_->plan;publishFixedSubscriptionState(lease,lease.directoryHandle(),"downloads.config",data_->automatic->inventoryBackup,{p.inventory,p.inventory,data_->queued},2,owner,observer);require(take(inspect(lease,owner))==State::Committed,ErrorCode::Conflict,"Queued intake proof changed");return {};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Automatic queue publication needs review"});}
}
std::expected<void,Error> PreparedStoppedPublicIntake::rollbackAutomaticQueue(ProfileLease& lease,std::string_view owner){
  try{require(data_&&data_->automatic&&take(data_->inspectBound(lease,owner,false))==State::Committed,ErrorCode::Conflict,"Automatic queue rollback requires actual intake proof");
    automaticOwnerRecipe(owner,data_->frozen);if(data_->automatic->queueBound){const auto& p=data_->plan;publishFixedSubscriptionState(lease,lease.directoryHandle(),"downloads.config",data_->automatic->inventoryBackup,{p.inventory,p.inventory,data_->queued},0,owner);}return {};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Automatic queue rollback needs review"});}
}
std::expected<PreparedStoppedPublicIntake,Error> PreparedStoppedPublicIntake::restore(ProfileLease& lease,std::string journal) {
  try {
    require(!journal.empty()&&journal.size()<=MaximumJournal,ErrorCode::InvalidFormat,"Frozen acquisition plan exceeds bounds");
    std::string ordinary=journal;std::optional<FixedSubscriptionDownloadPreparation> preparation;std::optional<std::string> automaticBytes;
    const auto wrapper=parse(journal,MaximumJournal);uniqueKeys(wrapper.root());if(const auto* schema=wrapper.find("schema")){
      require(schema->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Unknown frozen subscription intake schema");
      const bool automatic=schema->bytes=="BiglyBTC.StoppedPublicSubscriptionIntake.V3";
      require((automatic||schema->bytes=="BiglyBTC.StoppedPublicSubscriptionIntake.V2")&&wrapper.root().children.size()==(automatic?8U:6U),ErrorCode::InvalidFormat,"Unknown frozen subscription intake schema");
      if(automatic){automaticBytes=text(wrapper.find("automatic"));}
      const auto* inner=wrapper.find("journal"),*fixed=wrapper.find("preparation");require(inner&&inner->type==Node::Type::Bytes&&fixed,ErrorCode::InvalidFormat,"Frozen subscription intake fields missing");ordinary=std::string(inner->bytes);preparation=fixedPreparationValue(*fixed);
    }
    auto value=readJournal(ordinary,profilePath(lease),lease,true,automaticBytes.has_value());
    require(value.subscriptionAcquisition&&journalBytes(value)==ordinary,ErrorCode::InvalidFormat,"Only the canonical marked acquisition plan can be restored");
    std::optional<AutomaticIntakeContext> automatic;
    if(automaticBytes){const auto subject=metadataSubject(value.metadata,value.filenameConversions?std::optional<std::string_view>{*value.filenameConversions}:std::nullopt,value.decoderShowAll.value_or(false));automatic=AutomaticIntakeContext::restore(lease,subject,value.archive,value.filenameConversions.value_or(std::string(compat::OriginalDefaultFilenameConversions)),*automaticBytes);}
    auto data=std::make_unique<Data>(take(lease.retain()),std::move(value),std::move(preparation),std::move(automatic));
    require(data->frozen==journal,ErrorCode::Conflict,"Frozen acquisition plan changed during restore");
    return PreparedStoppedPublicIntake(std::move(data));
  }catch(const Failure& e){return std::unexpected(e.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot restore stopped acquisition intake"});}
}
std::expected<PreparedStoppedPublicIntake::State,Error> PreparedStoppedPublicIntake::inspect(ProfileLease& lease,std::string_view owner)const{
  if(!data_){return std::unexpected(Error{ErrorCode::Conflict,"Prepared intake was consumed"});}return data_->inspectBound(lease,owner,true);
}
std::expected<PreparedStoppedPublicIntake::State,Error> PreparedStoppedPublicIntake::Data::inspectBound(
    ProfileLease& lease,std::string_view ownerIntent,bool strictSourceSettings)const {
  try {
    require(worker==std::this_thread::get_id()&&this->lease->sharesOwnership(lease),
        ErrorCode::Conflict,"Missing,foreign-worker or foreign-OFD acquisition intake plan");
    const auto& p=this->plan;const int root=lease.directoryHandle();
    require(profilePath(lease)==p.profile,ErrorCode::Conflict,"Acquisition profile moved");
    acquisitionOwnerCurrent(lease,ownerIntent);
    const auto journal=read(root,Journal,MaximumJournal),journalSaving=read(root,JournalSaving,MaximumJournal);
    require((!journal||journal->bytes==this->journal)&&(!journalSaving||journalSaving->bytes==this->journal),
        ErrorCode::Conflict,"Another intake journal conflicts with the acquisition");
    const bool pending=journal.has_value()||journalSaving.has_value();
    if(this->automatic){
      const auto primary=contents(read(root,"downloads.config",MaximumInventory));const auto staging=contents(read(root,"downloads.config.saving",MaximumInventory));
      require(primary==p.before||primary==std::optional{p.inventory}||primary==std::optional{this->queued},ErrorCode::Conflict,"Automatic inventory has unknown bytes");
      require(!staging||*staging==p.inventory||*staging==this->queued,ErrorCode::Conflict,"Automatic inventory has foreign staging");
      const auto backup=contents(read(root,"downloads.config.bak",MaximumInventory));require(backup==p.oldBackup||(p.before&&backup==p.before),ErrorCode::Conflict,"Automatic inventory backup changed");
      this->automatic->verify(lease,strictSourceSettings);
    }else{inventoryCurrent(root,p);}
    if(this->automatic){
      const auto& context=*this->automatic;const auto backup=read(root,"downloads.config.bak",MaximumInventory);
      if(context.queueBound){require(bool(backup)==bool(context.inventoryBackup)&&(!backup||(backup->bytes==context.inventoryBackup->bytes&&unchanged(backup->info,context.inventoryBackup->info))),ErrorCode::Conflict,"Frozen automatic inventory backup changed");}
      else{require(contents(read(root,"downloads.config",MaximumInventory))!=std::optional{this->queued}||this->queued==p.inventory,ErrorCode::Conflict,"Queued inventory appeared before its closed witness");absent(root,"downloads.config.saving");}
    }
    if(strictSourceSettings){checkSaveNameScope(lease,p);}cacheAbsent(root,p.hash);
    auto active=childDirectory(root,"active",false),archive=childDirectory(root,"torrents",false);
    if(active.get()>=0){
      require(read(active.get(),"cache.dat",MaximumInventory).has_value()||!read(active.get(),"cache.dat.bak",MaximumInventory),
          ErrorCode::Conflict,"Missing state-cache primary has a recovery candidate");
      absent(active.get(),"cache.dat.saving");
    }
    const auto stateName=p.hash.hex()+".dat",archiveName=p.hash.hex()+".torrent";
    std::optional<File> state,source,stateStage,archiveStage,callbackStage;
    if(active.get()>=0){
      for(const auto* suffix:{".bak",".bak.saving",".saving.bak"}){absent(active.get(),stateName+suffix);}
      if(this->preparation){callbackStage=read(active.get(),stateName+".saving",MaximumInventory);}else{absent(active.get(),stateName+".saving");}
      state=read(active.get(),stateName,MaximumInventory);stateStage=read(active.get(),stateName+".import",MaximumInventory);
    }
    if(archive.get()>=0){
      for(const auto* suffix:{".bak",".saving",".bak.saving"}){absent(archive.get(),archiveName+suffix);}
      source=read(archive.get(),archiveName,MaximumMetadata);archiveStage=read(archive.get(),archiveName+".import",MaximumMetadata);
    }
    require((!state||state->bytes==p.state||(this->preparation&&state->bytes==this->associationState))&&(!source||source->bytes==p.archive)
        &&(!stateStage||stateStage->bytes==p.state)&&(!archiveStage||archiveStage->bytes==p.archive),
        ErrorCode::Conflict,"Acquisition intake artifacts conflict with the frozen plan");
    const auto inventory=read(root,"downloads.config",MaximumInventory),backup=read(root,"downloads.config.bak",MaximumInventory);
    const auto inventoryStage=read(root,InventorySaving,MaximumInventory),backupStage=read(root,BackupSaving,MaximumInventory);
    require((!inventoryStage||inventoryStage->bytes==p.inventory)
        &&(!backupStage||(p.before&&backupStage->bytes==*p.before)),ErrorCode::Conflict,"Acquisition intake staging bytes changed");
    const bool committedInventory=contents(inventory)==std::optional{p.inventory}||(this->automatic&&contents(inventory)==std::optional{this->queued});
    if(committedInventory){
      require(state&&source&&contents(backup)==(p.before?p.before:p.oldBackup),ErrorCode::Conflict,"Inventory postimage lacks the matching state/archive/backup");
    }
    require(!source||state.has_value(),ErrorCode::Conflict,"Archive exists before its expected intake state");
    if(callbackStage|| (state&&state->bytes!=p.state)){
      require(this->preparation&&!pending&&state&&source&&committedInventory,ErrorCode::Conflict,"Callback state appeared before complete stopped intake");
      require(!callbackStage||callbackStage->bytes==p.state||callbackStage->bytes==this->associationState,ErrorCode::Conflict,"Unknown subscription callback state stage");
    }
    if(pending){return State::Pending;}
    require(!inventoryStage&&!backupStage&&!stateStage&&!archiveStage,ErrorCode::Conflict,"Acquisition staging remains without its intake journal");
    if(committedInventory&&state&&source){
      require(contents(backup)==(p.before?p.before:p.oldBackup),ErrorCode::Conflict,"Committed intake backup differs from the frozen preimage");
      return State::Committed;
    }
    require(contents(inventory)==p.before&&contents(backup)==p.oldBackup&&!state&&!source,
        ErrorCode::Conflict,"Acquisition has neither exact before nor all three committed postimages");
    return State::Before;
  }catch(const Failure& e){return std::unexpected(e.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot inspect stopped acquisition intake"});}
}
std::expected<core::TorrentHash,Error> PreparedStoppedPublicIntake::commit(ProfileLease& lease,
    std::string_view ownerIntent,std::stop_token stop,void(*observer)(TorrentIntakeStage),IntakeHistoryOutcome* history) {
  if(history){*history={};}
  try {
    const auto state=take(inspect(lease,ownerIntent));
    if(state==State::Committed){return data_->plan.hash;}
    if(state==State::Before){
      require(!stop.stop_requested(),ErrorCode::Cancelled,"Acquisition cancelled before intake commit");
      if(data_->input){
        auto directoryHandle=directory(data_->source.parent_path());
        const auto current=read(directoryHandle.get(),data_->source.filename().native(),MaximumMetadata);
        require(current&&unchanged(current->info,data_->input->info)&&current->bytes==data_->input->bytes,
            ErrorCode::Conflict,"Owned acquisition metainfo changed after freezing");
      }
      std::optional<ProfileSourceAccess> historySource;
      const bool freshHistory=history&&data_->fresh;
      if(freshHistory){
        require(!observer,ErrorCode::Unsupported,"Observed acquisition intake cannot issue auxiliary history");
        historySource.emplace(take(lease.sourceAccess(true)));
      }
      acquisitionOwnerCurrent(lease,ownerIntent);
      const auto imported=commitIntakePlan(lease,data_->plan,observer,stop,true,ownerIntent);
      data_->fresh=false;data_->input.reset();
      require(take(inspect(lease,ownerIntent))==State::Committed,ErrorCode::Conflict,"New acquisition intake lacks its exact committed postimages");
      if(freshHistory){
        auto issued=DurableIntakeReceipt::issue(lease,imported,data_->plan.inventory,data_->plan.state,data_->plan.archive);
        if(issued){history->receipt.emplace(std::move(*issued));}else{history->warning=std::move(issued.error());}
      }
      return imported;
    }
    const int root=lease.directoryHandle();
    const auto journal=read(root,Journal,MaximumJournal);
    if(!journal){
      const auto staged=read(root,JournalSaving,MaximumJournal);
      require(staged&&staged->bytes==data_->journal,ErrorCode::Conflict,"Owned acquisition intake journal disappeared");
      acquisitionOwnerCurrent(lease,ownerIntent);move(root,JournalSaving,Journal,RENAME_NOREPLACE);
    }
    acquisitionOwnerCurrent(lease,ownerIntent);apply(lease,data_->plan,data_->journal,observer,true,ownerIntent);
    require(take(inspect(lease,ownerIntent))==State::Committed,ErrorCode::Conflict,"Acquisition intake is not fully committed");
    return data_->plan.hash;
  }catch(const Failure& e){return std::unexpected(e.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Stopped acquisition intake requires explicit recovery"});}
}

std::expected<PreparedStoppedPublicIntake::AssociationStateObservation,Error> PreparedStoppedPublicIntake::inspectAssociationState(ProfileLease& lease,std::string_view owner)const{
  try{
    require(data_&&take(data_->inspectBound(lease,owner,false))==State::Committed,ErrorCode::Conflict,"Association callback observation requires complete stopped intake");auto active=childDirectory(lease.directoryHandle(),"active",false);require(active.get()>=0,ErrorCode::Conflict,"Callback state directory missing");
    const auto& p=data_->plan;const auto observed=observeFixedSubscriptionState(active.get(),p.hash.hex()+".dat",{}, {p.state,p.state,data_->associationState});
    const auto matches=[](std::uint8_t bits){return static_cast<std::uint8_t>(((bits&3U)?1U:0U)|((bits&4U)?2U:0U));};
    return AssociationStateObservation{matches(observed.primaryMatches),observed.savingMatches?std::optional<std::uint8_t>{matches(*observed.savingMatches)}:std::nullopt};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Cannot inspect fixed subscription callback state"});}
}
std::expected<void,Error> PreparedStoppedPublicIntake::publishAssociationState(ProfileLease& lease,std::string_view owner,void(*observer)(SubscriptionDownloadStateStage)){
  try{
    auto access=take(lease.sourceAccess(true));require(take(inspect(lease,owner))==State::Committed,ErrorCode::Conflict,"Callback forward publication needs strict stopped-intake proof");const auto phase=take(inspectAssociationState(lease,owner));require(!phase.savingMatches||phase.savingIs(AssociationStatePhase::Associated),ErrorCode::Conflict,"Callback forward publication crossed rollback staging");auto active=childDirectory(lease.directoryHandle(),"active",false);const auto& p=data_->plan;
    publishFixedSubscriptionState(lease,active.get(),p.hash.hex()+".dat",{}, {p.state,p.state,data_->associationState},2,owner,observer);
    require(take(inspect(lease,owner))==State::Committed,ErrorCode::Conflict,"Stopped intake proof changed after callback");return {};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Subscription callback state requires review"});}
}
std::expected<void,Error> PreparedStoppedPublicIntake::rollbackAssociationState(ProfileLease& lease,std::string_view owner){
  try{
    auto access=take(lease.sourceAccess(true));require(data_&&take(data_->inspectBound(lease,owner,false))==State::Committed,ErrorCode::Conflict,"Callback rollback needs actual complete stopped intake");auto active=childDirectory(lease.directoryHandle(),"active",false);const auto& p=data_->plan;
    publishFixedSubscriptionState(lease,active.get(),p.hash.hex()+".dat",{}, {p.state,p.state,data_->associationState},0,owner);
    require(take(data_->inspectBound(lease,owner,false))==State::Committed,ErrorCode::Conflict,"Stopped intake proof changed after callback rollback");return {};
  }catch(const Failure& error){return std::unexpected(error.error);}catch(...){return std::unexpected(Error{ErrorCode::Conflict,"Subscription callback rollback requires review"});}
}

// Private existing-target helpers and sealed Data live in the primary TU,
// alongside PreparedStoppedPublicIntake::Data, not in a multiply includable file.
namespace {
constexpr auto ExistingRootPending=std::array{Journal,JournalSaving,InventorySaving,BackupSaving,"BiglyBTC.file-operation","BiglyBTC.file-operation.BiglyBTC.fileop-saving",
  ".BiglyBTC.linear-to-compact",".BiglyBTC.linear-to-compact.saving","BiglyBTC.category-delete.config","BiglyBTC.category-delete.config.bak","BiglyBTC.category-delete.config.saving",
  ".BiglyBTC.category-delete",".BiglyBTC.category-delete.bak",".BiglyBTC.category-delete.saving",".subscriptions-native-write",".subscriptions-native-write.bak",".subscriptions-native-write.saving","downloads.config.saving","downloads.config.saving.bak"};
constexpr auto ExistingSettings=std::array{"biglybt.config","biglybt.config.bak","azureus.config","azureus.config.bak"};
constexpr auto ExistingSettingsPending=std::array{"biglybt.config.saving","biglybt.config.saving.bak","azureus.config.saving","azureus.config.saving.bak"};
constexpr auto ExistingStatePending=std::array{".saving",".saving.bak",".bak.saving",".import"};
constexpr auto ExistingCachePending=std::array{"cache.dat.saving","cache.dat.saving.bak","cache.dat.bak.saving"};
void existingSameLayout(const compat::OriginalLayout& actual,const compat::OriginalLayout& expected){
  require(actual.name==expected.name&&actual.multifile==expected.multifile&&actual.files.size()==expected.files.size(),ErrorCode::Conflict,"Existing target filename geometry differs");
  for(std::size_t n=0;n<actual.files.size();++n){require(actual.files[n].path==expected.files[n].path&&actual.files[n].bytes==expected.files[n].bytes&&actual.files[n].padding==expected.files[n].padding,ErrorCode::Conflict,"Existing target file geometry differs");}
}
void existingReadonlyOwner(const ProfileLease& lease,std::string_view expected){
  require(!expected.empty()&&expected.size()<=MaximumJournal,ErrorCode::Conflict,"Missing existing-target owner candidate");const int root=lease.directoryHandle();absent(root,AcquisitionIntentBackup);
  const auto primary=read(root,AcquisitionIntent,MaximumJournal),saving=read(root,AcquisitionIntentSaving,MaximumJournal);
  require((saving&&saving->bytes==expected)||(!saving&&primary&&primary->bytes==expected),ErrorCode::Conflict,"Existing-target selected owner candidate changed");
}

}
struct ExistingStoppedPublicSubscriptionTarget::Data {
  static constexpr std::size_t DocumentBytes=16*1024*1024,TotalBytes=64*1024*1024,MaximumRows=4096;
  struct Directory {fs::path path;Descriptor held{-1};struct stat stamp{};};
  std::shared_ptr<const ProfileLease> lease;
  std::uint64_t session=0;
  std::thread::id worker=std::this_thread::get_id();
  fs::path profile;
  std::vector<Directory> directories;
  std::vector<std::string> knownHashes;
  std::map<fs::path,std::optional<File>> inputs;
  std::size_t bytes=0;
  std::string rawInfo,attributes;
  Plan independence{};
  fs::path statePath,archivePath;
  std::optional<FixedSubscriptionDownloadPreparation> preparation;
  SubscriptionStateImages states;std::string recipe;std::optional<AutomaticIntakeContext> automatic;
  Images view{};
  std::set<fs::path> expectedPaths()const{
    std::set<fs::path> out;for(const auto* name:ExistingRootPending){out.insert(profile/name);}for(const auto* name:ExistingSettings){out.insert(profile/name);}for(const auto* name:ExistingSettingsPending){out.insert(profile/name);}
    out.insert(profile/"downloads.config");out.insert(profile/"downloads.config.bak");
    for(const auto& hash:knownHashes){const auto state=profile/"active"/(hash+".dat");out.insert(state);out.insert(fs::path(state.native()+".bak"));for(const auto* suffix:ExistingStatePending){out.insert(fs::path(state.native()+suffix));}}
    out.insert(profile/"active"/view.hash.hex());out.insert(archivePath);out.insert(fs::path(archivePath.native()+".bak"));for(const auto* suffix:ExistingStatePending){out.insert(fs::path(archivePath.native()+suffix));}
    for(const auto* name:{"cache.dat","cache.dat.bak"}){out.insert(profile/"active"/name);}for(const auto* name:ExistingCachePending){out.insert(profile/"active"/name);}return out;
  }
  void closedPaths()const{std::set<fs::path> actual;for(const auto& [path,value]:inputs){static_cast<void>(value);actual.insert(path);}require(actual==expectedPaths(),ErrorCode::InvalidFormat,"Existing-target recipe omitted or added a candidate path");}
  void bindViews(){
    const auto viewFile=[](const std::optional<File>& value)->std::optional<std::string_view>{return value?std::optional<std::string_view>{value->bytes}:std::nullopt;};
    const auto& inventory=inputs.at(profile/"downloads.config"),&archive=inputs.at(archivePath),&state=inputs.at(statePath);require(inventory&&archive&&state,ErrorCode::InvalidFormat,"Existing-target primary images missing");attributes=subscriptionStateAttributes(state->bytes);
    view.rawInfo=rawInfo;view.inventory=inventory->bytes;view.archive=archive->bytes;view.state=state->bytes;view.attributes=attributes;view.inventoryBackup=viewFile(inputs.at(profile/"downloads.config.bak"));view.archiveBackup=viewFile(inputs.at(fs::path(archivePath.native()+".bak")));view.stateBackup=viewFile(inputs.at(fs::path(statePath.native()+".bak")));
    view.statePrepared=preparation?std::string_view(states.prepared):view.state;view.stateAfterAssociation=preparation?std::string_view(states.associated):view.state;
  }
  std::string encodeRecipe()const{
    require(preparation.has_value(),ErrorCode::Conflict,"Existing-target fixed preparation is absent");std::map<std::string,std::string> files,scopes;
    for(const auto& [path,value]:inputs){files[path.native()]=value?existingDictionary({{"bytes",b(value->bytes)},{"stamp",existingStampBytes(value->info)}}):"i0e";}
    for(const auto& observed:directories){scopes[observed.path.native()]=existingStampBytes(observed.stamp);}
    const auto ordinary=existingDictionary({{"schema",b("BiglyBTC.ExistingStoppedSubscriptionTarget.V1")},{"profile",b(profile.native())},{"destination",b(view.destination.native())},{"metadata",b(independence.metadata)},{"conversions",b(independence.filenameConversions.value_or(std::string(compat::OriginalDefaultFilenameConversions)))},{"showAll",i(independence.decoderShowAll.value_or(false)?1:0)},{"preparation",fixedPreparationBytes(*preparation)},{"files",existingDictionary(files)},{"directories",existingDictionary(scopes)}});
    return automatic?existingDictionary({{"schema",b("BiglyBTC.ExistingStoppedSubscriptionTarget.V2")},{"target",b(ordinary)},{"automatic",b(automatic->encoded())}}):ordinary;
  }
  void scope(const fs::path& path){
    if(std::ranges::any_of(directories,[&](const auto& value){return value.path==path;})){return;}
    auto fd=directory(path);struct stat value{};
    require(::fstat(fd.get(),&value)==0&&S_ISDIR(value.st_mode)&&value.st_uid==::geteuid(),ErrorCode::UnsafePath,"Existing target directory is not owned");
    directories.push_back({path,std::move(fd),value});
  }
  const std::optional<File>& observe(const fs::path& path,std::size_t maximum=DocumentBytes){
    if(const auto found=inputs.find(path);found!=inputs.end()){return found->second;}
    require(inputs.size()<32768,ErrorCode::Unsupported,"Existing target candidate count exceeds bound");
    auto parent=directory(path.parent_path());auto value=read(parent.get(),path.filename().native(),maximum);
    if(value){require(value->bytes.size()<=TotalBytes-bytes,ErrorCode::Unsupported,"Existing target observations exceed total bound");bytes+=value->bytes.size();}
    return inputs.emplace(path,std::move(value)).first->second;
  }
  void missing(const fs::path& path){require(!observe(path),ErrorCode::Conflict,"Existing target has a pending or unsupported candidate");}
  void checkDirectories()const{
    for(const auto& observed:directories){auto now=directory(observed.path);struct stat held{},named{};
      require(::fstat(observed.held.get(),&held)==0&&::fstat(now.get(),&named)==0&&identity(held,observed.stamp)&&identity(named,observed.stamp)
          &&held.st_uid==observed.stamp.st_uid&&named.st_uid==observed.stamp.st_uid&&held.st_mode==observed.stamp.st_mode&&named.st_mode==observed.stamp.st_mode&&held.st_gid==observed.stamp.st_gid&&named.st_gid==observed.stamp.st_gid,
          ErrorCode::Conflict,"Existing target directory scope changed");}
  }
  bool sourceSetting(const fs::path& path)const{
    if(path.parent_path()!=profile){return false;}const auto leaf=path.filename().native();return leaf=="biglybt.config"||leaf=="biglybt.config.bak"||leaf=="biglybt.config.saving"||leaf=="biglybt.config.saving.bak"||leaf=="azureus.config"||leaf=="azureus.config.bak"||leaf=="azureus.config.saving"||leaf=="azureus.config.saving.bak";
  }
  void checkInputs(bool strictSettings,bool fixedState)const{
    for(const auto& [path,before]:inputs){
      if((!strictSettings&&sourceSetting(path))||(fixedState&&(path==statePath||path==fs::path(statePath.native()+".saving")))){continue;}auto parent=directory(path.parent_path());auto now=read(parent.get(),path.filename().native(),std::max(DocumentBytes,before?before->bytes.size():std::size_t{}));
      require(bool(now)==bool(before)&&(!before||(now->bytes==before->bytes&&unchanged(now->info,before->info))),ErrorCode::Conflict,"Existing target candidate changed");}
  }
  void verify(const ProfileLease& caller,std::uint64_t currentSession,std::string_view owner,bool strictSettings=true,bool fixedState=false)const{
    require(lease&&lease->sharesOwnership(caller)&&worker==std::this_thread::get_id()&&session&&session==currentSession,ErrorCode::Conflict,"Existing target worker/session/SAMEOFD changed");
    require(profilePath(caller)==profile,ErrorCode::Conflict,"Existing target profile path changed");
    if(owner.empty()){noAcquisitionIntent(caller);}else{acquisitionOwnerCurrent(caller,owner);}
    if(automatic){automatic->verify(caller,strictSettings);}
    checkDirectories();checkInputs(strictSettings,fixedState);if(strictSettings){checkSaveNameScope(caller,independence);}checkInputs(strictSettings,fixedState);checkDirectories();
    if(owner.empty()){noAcquisitionIntent(caller);}else{acquisitionOwnerCurrent(caller,owner);}
  }
};

#include "existing_subscription_target.inc"

std::expected<core::TorrentHash,Error> importTorrent(ProfileLease& lease,const fs::path& source,
    const fs::path& destination,bool stopped,void(*observer)(TorrentIntakeStage),bool forceStart,TorrentIntakeNetwork network,IntakeHistoryOutcome* history) {
  if(history){*history={};}
  std::optional<ProfileSourceAccess> historySource;
  if(history){
    if(observer){return std::unexpected(Error{ErrorCode::Unsupported,"Observed intake cannot issue history under a nested callback boundary"});}
    auto held=lease.sourceAccess(true);if(!held){return std::unexpected(held.error());}historySource.emplace(std::move(*held));
  }
  HistoryPostimages expected;
  auto imported=importImpl(lease,source,destination,stopped,observer,forceStart,network,history?&expected:nullptr);
  if(imported&&history){auto issued=DurableIntakeReceipt::issue(lease,*imported,expected.inventory,expected.state,expected.archive);if(issued){history->receipt.emplace(std::move(*issued));}else{history->warning=std::move(issued.error());}}
  return imported;
}
std::expected<core::TorrentHash,Error> importTorrentWithFileOptions(ProfileLease& lease,
    const TorrentIntakeMetadata& metadata,const fs::path& destination,bool stopped,
    std::span<const compat::FilePolicy> policies,void(*observer)(TorrentIntakeStage),bool forceStart,std::stop_token stop,core::TorrentIntakeLimits limits,bool sequentialDownload,bool queued,bool paused,std::optional<core::TorrentIntakeTrackerTiers> trackers,std::optional<std::string> saveName,bool verifyExisting,std::optional<core::TorrentIntakeFileDestinations> fileDestinations,core::ExistingVerificationGoal goal,core::TorrentAllocationMode allocation,TorrentIntakeNetwork network,IntakeHistoryOutcome* history) {
  if(history){*history={};}
  std::optional<ProfileSourceAccess> historySource;
  if(history){
    if(observer){return std::unexpected(Error{ErrorCode::Unsupported,"Observed intake cannot issue history under a nested callback boundary"});}
    auto held=lease.sourceAccess(true);if(!held){return std::unexpected(held.error());}historySource.emplace(std::move(*held));
  }
  HistoryPostimages expected;
  auto imported=importImpl(lease,metadata.data_->path,destination,stopped,observer,forceStart,network,history?&expected:nullptr,policies,&metadata.data_->input,stop,limits,sequentialDownload,queued,paused,std::move(trackers),std::move(saveName),verifyExisting,&metadata.data_->filenameConversions,std::move(fileDestinations),goal,&metadata.data_->decoderShowAll,allocation);
  if(imported&&history){auto issued=DurableIntakeReceipt::issue(lease,*imported,expected.inventory,expected.state,expected.archive);if(issued){history->receipt.emplace(std::move(*issued));}else{history->warning=std::move(issued.error());}}
  return imported;
}

std::expected<std::string,Error> rewriteTorrentIntakeTrackers(std::string_view metadata,
    const std::optional<core::TorrentIntakeTrackerTiers>& trackers) {
  try {
    auto subject=metadataSubject(metadata);if(!trackers){return std::string(metadata);}
    applyTrackers(subject,*trackers);return subject.torrent.serialize();
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(const std::exception&){return std::unexpected(Error{ErrorCode::Io,"Cannot project tracker tiers"});}
}

std::expected<core::TorrentHash,Error> importHybridAlias(ProfileLease& lease,const core::TorrentHash& from,
    OriginalSelectedHash target,std::span<const OriginalTorrentIdentity> retained,
    void(*observer)(TorrentIntakeStage),std::stop_token stop,IntakeHistoryOutcome* history) {
  if(history){*history={};}
  std::optional<ProfileSourceAccess> historySource;
  if(history){
    if(observer){return std::unexpected(Error{ErrorCode::Unsupported,"Observed intake cannot issue history under a nested callback boundary"});}
    auto held=lease.sourceAccess(true);if(!held){return std::unexpected(held.error());}historySource.emplace(std::move(*held));
  }
  try {
    noAcquisitionIntent(lease);
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
    const auto context=take(readOriginalFilenameContext(lease));const auto& conversions=context.conversions;
    auto original=metadataSubject(hybridSourceDocument(proof).serialize(),conversions,context.decoderShowAll);
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
        before->info.st_mode,false,IntakeFileOptions{*encoded,std::move(stores),{}, {},false,false,false,{},{}},std::move(proof),conversions,context.decoderShowAll,TorrentIntakeNetwork::Public,intakeAddedTime());
    auto imported=commitIntakePlan(lease,planValue,observer,stop);
    if(history){auto issued=DurableIntakeReceipt::issue(lease,imported,planValue.inventory,planValue.state,planValue.archive);if(issued){history->receipt.emplace(std::move(*issued));}else{history->warning=std::move(issued.error());}}
    return imported;
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(const std::exception&){return std::unexpected(Error{ErrorCode::Io,"Hybrid companion publication failed; inspect pending intake"});}
}

bool hasPendingTorrentIntake(const ProfileLease& lease) noexcept {
  try{
    if(!lease.valid()){return true;}
    for(const auto* leaf:{Journal,JournalSaving,InventorySaving,BackupSaving,AcquisitionIntent,AcquisitionIntentSaving,AcquisitionIntentBackup}){if(named(lease.directoryHandle(),leaf)){return true;}}
    return false;
  }catch(...){return true;}
}
std::expected<std::optional<core::TorrentHash>, Error> recoverTorrentIntake(
    ProfileLease& lease, void (*observer)(TorrentIntakeStage)) {
  try {
    noAcquisitionIntent(lease);
    const auto profile = profilePath(lease);
    const int root = lease.directoryHandle();
    auto journal = read(root, Journal, MaximumJournal);
    if (!journal) {
      journal = read(root, JournalSaving, MaximumJournal);
      if (!journal) { return std::nullopt; }
      (void)readJournal(journal->bytes, profile, lease); // Validate before first recovery mutation.
      move(root, JournalSaving, Journal, RENAME_NOREPLACE);
    }
    auto p = readJournal(journal->bytes, profile, lease);
    apply(lease, p, journal->bytes, observer);
    return p.hash;
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::exception&) { return std::unexpected(Error{ErrorCode::Io, "Torrent intake recovery failed; journal retained"}); }
}
#include "verified_existing_scope.inc"
#include "selected_existing_scope.inc"
#include "verified_seeding.inc"
#include "allocation_intent.inc"
} // namespace biglybtc::storage
