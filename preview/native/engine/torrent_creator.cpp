#include "state_directory_listing.hpp"
#include "state_file_io.hpp"
#include "../platform/utf8_path.hpp"
#ifdef _WIN32
#include "../platform/windows_relative_file.hpp"
#else
#include "../platform/posix_profile_files.hpp"
#endif
#include "torrent_creator.hpp"
#include "original_torrent_identity_lt.hpp"
#include "original_layout_projection.hpp"

#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/version.hpp>
#include <openssl/evp.h>
#include <openssl/provider.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <map>
#include <set>
#include <span>
#include <new>
#include <string_view>
#include "../platform/system_entropy.hpp"
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <utility>

static_assert(LIBTORRENT_VERSION_NUM == 20012, "creator qualified against libtorrent 2.0.12");

namespace biglybtc::engine {
namespace {
namespace fs = std::filesystem;
namespace lt = libtorrent;
namespace files = storage::detail;
using Descriptor = files::StateDescriptor;
using Handle = files::StateHandle;
using Stamp = files::StateStamp;
using platform::pathFromUtf8;
using platform::pathToUtf8;
constexpr std::size_t MaximumPathBytes = 4096;
constexpr std::size_t MaximumMetadataBytes = 32 * 1024 * 1024;

struct Failure { CreateError error; };
[[noreturn]] void fail(CreateErrorCode code, const fs::path& path,
    const char* text, int number = 0) {
  throw Failure{{code, path, number ? std::error_code(number, std::generic_category())
                                  : std::error_code{}, text}};
}
bool sameIdentity(const Stamp& a, const Stamp& b) {
  return files::sameStateIdentity(a, b) && a.mode == b.mode;
}
bool same(const Stamp& a, const Stamp& b) {
  return sameIdentity(a, b) && a.size == b.size
      && files::sameStateTimestamp(a.modified, b.modified)
      && files::sameStateTimestamp(a.changed, b.changed);
}
Stamp describe(Handle fd, const fs::path& path) {
  auto value = files::stateStamp(fd);
  if (!value) { fail(CreateErrorCode::Read, path, "Cannot inspect opened source"); }
  return *value;
}

std::ptrdiff_t readAt(Handle fd, char* data, std::size_t size, std::uint64_t offset) {
  errno = 0;
  return files::readStateAt(fd, data, size, offset);
}

bool utf8(std::string_view text, bool allowWhitespace = false) {
  for (std::size_t at = 0; at < text.size();) {
    const auto first = static_cast<unsigned char>(text[at++]);
    if (first < 0x80) {
      if (first == 0 || first == 0x7f || (first < 0x20 && !(allowWhitespace
          && (first == '\n' || first == '\r' || first == '\t')))) { return false; }
      continue;
    }
    unsigned count = 0;
    std::uint32_t value = 0;
    if (first >= 0xc2 && first <= 0xdf) { count = 1; value = first & 0x1fu; }
    else if (first >= 0xe0 && first <= 0xef) { count = 2; value = first & 0x0fu; }
    else if (first >= 0xf0 && first <= 0xf4) { count = 3; value = first & 0x07u; }
    else { return false; }
    if (text.size() - at < count) { return false; }
    for (unsigned part = 0; part < count; ++part) {
      const auto next = static_cast<unsigned char>(text[at++]);
      if ((next & 0xc0u) != 0x80u) { return false; }
      value = (value << 6u) | (next & 0x3fu);
    }
    if ((count == 1 && value < 0x80) || (count == 2 && value < 0x800)
        || (count == 3 && value < 0x10000) || value > 0x10ffff
        || (value >= 0xd800 && value <= 0xdfff)) { return false; }
  }
  return true;
}
std::string genericPath(const fs::path& path) {
  const auto name = path.generic_u8string();
  return {name.begin(), name.end()};
}
bool nameValid(std::string_view name) {
  return !name.empty() && name.size() <= 255 && name != "." && name != ".."
      && name.find('/') == name.npos && name.find('\\') == name.npos && utf8(name);
}
std::vector<std::string> absoluteParts(const fs::path& path) {
  const auto text = pathToUtf8(path);
  if (!path.is_absolute() || text.size() > MaximumPathBytes
      || text.find('\0') != text.npos) {
    fail(CreateErrorCode::Path, path, "An absolute bounded path is required");
  }
  std::vector<std::string> result;
  for (const auto& part : path.relative_path()) {
    const auto value = pathToUtf8(part);
    if (!nameValid(value)) { fail(CreateErrorCode::Path, path, "Unsafe path component"); }
    result.push_back(value);
    if (result.size() > CreateMaximumDepth) { fail(CreateErrorCode::Limit, path, "Path depth limit exceeded"); }
  }
  return result;
}
Descriptor openDirectory(const fs::path& path) {
  (void)absoluteParts(path);
  auto directory = files::openStatePathDirectory(fs::path(path).make_preferred());
  if (!directory) { fail(CreateErrorCode::Path, path, "Cannot open directory without following links"); }
  return std::move(*directory);
}
struct Parent {
  fs::path path;
  std::string leaf;
  Descriptor directory;
  Stamp identity;
};
Parent openParent(const fs::path& path) {
  const auto parts = absoluteParts(path);
  if (parts.empty()) { fail(CreateErrorCode::Path, path, "A named file or directory is required"); }
  auto parent = openDirectory(path.parent_path());
  const auto identity = describe(parent.get(), path.parent_path());
  return {path.parent_path(), parts.back(), std::move(parent), identity};
}
Descriptor openChild(Handle parent, const std::string& name, const fs::path& path) {
  const auto leaf = pathFromUtf8(name);
#ifdef _WIN32
  const auto named = files::stateStampAt(parent, leaf);
  if (!named || !*named || (!(*named)->directory && !(*named)->regular)) {
    fail(CreateErrorCode::UnsafeSource, path, "Cannot inspect source without following links");
  }
  constexpr auto sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  constexpr auto rights = FILE_GENERIC_READ | FILE_TRAVERSE;
  const auto options = (*named)->directory ? platform::detail::DirectoryFile : platform::detail::NonDirectoryFile;
  auto opened = platform::detail::openRelativeFile(parent, leaf.native(), rights, sharing,
      platform::detail::OpenExisting, options);
  if (!opened) { fail(CreateErrorCode::UnsafeSource, path, "Cannot open source without following links"); }
  Descriptor file(*opened);
  if (!same(**named, describe(file.get(), path))) {
    fail(CreateErrorCode::Changed, path, "Source changed while opening");
  }
  return file;
#else
  Descriptor file(::openat(parent, leaf.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  if (file.get() < 0) { fail(CreateErrorCode::UnsafeSource, path, "Cannot open source without following links", errno); }
  return file;
#endif
}

struct Entry { std::string relative; Stamp observed; fs::path physical{}; std::optional<Stamp> physicalParent{}; };
struct Inventory {
  Stamp root;
  bool directory;
  std::vector<Entry> files;
  std::vector<Entry> directories;
  std::uint64_t total = 0;
  std::string logicalName{};
  std::vector<Entry> origins{};
};
struct Operation {
  const CreateOptions& options;
  const CreateProgressCallback& callback;
  std::stop_token stop;
  void cancelled() const {
    if (stop.stop_requested()) { fail(CreateErrorCode::Cancelled, options.source, "Torrent creation cancelled"); }
  }
  void publish(CreateProgress value) const {
    cancelled();
    if (callback) {
      try { callback(value); }
      catch (...) { fail(CreateErrorCode::Callback, options.source, "Torrent progress callback failed"); }
    }
    cancelled();
  }
};
std::string relativeChild(const std::string& parent, const std::string& name,
    const fs::path& source) {
  auto result = parent.empty() ? name : parent + '/' + name;
  if (result.size() > MaximumPathBytes) { fail(CreateErrorCode::Limit, source, "Relative source path limit exceeded"); }
  return result;
}
void scanDirectory(Handle fd, const std::string& relative, std::size_t depth,
    Inventory& result, const Operation& operation) {
  operation.cancelled();
  const auto path = operation.options.source / pathFromUtf8(relative);
  const auto before = describe(fd, path);
  if (!before.directory) { fail(CreateErrorCode::UnsafeSource, path, "Source directory changed type"); }
  const auto listed = files::listStateDirectoryNames(fd, CreateMaximumFiles + CreateMaximumDirectories + 2, operation.stop);
  if (!listed) {
    using Code = storage::ErrorCode;
    switch (listed.error().code) {
      case Code::Cancelled: operation.cancelled(); break;
      case Code::Unsupported: fail(CreateErrorCode::Limit, path, "Directory entry limit exceeded");
      default: break;
    }
    fail(CreateErrorCode::Read, path, "Cannot list source directory");
  }
  std::vector<std::string> names;
  names.reserve(listed->size());
  for (const auto& leaf : *listed) {
    operation.cancelled();
    auto name = pathToUtf8(leaf);
    if (name == "." || name == "..") { continue; }
    if (!nameValid(name)) { fail(CreateErrorCode::UnsafeSource, path, "Source name is not a safe UTF-8 component"); }
    names.push_back(std::move(name));
    if (names.size() > CreateMaximumFiles + CreateMaximumDirectories) {
      fail(CreateErrorCode::Limit, path, "Directory entry limit exceeded");
    }
  }
  std::sort(names.begin(), names.end(), [](std::string_view a, std::string_view b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
        [](char x, char y) { return static_cast<unsigned char>(x) < static_cast<unsigned char>(y); });
  });
  for (const auto& name : names) {
    operation.cancelled();
    const auto relativePath = relativeChild(relative, name, operation.options.source);
    const auto childPath = operation.options.source / pathFromUtf8(relativePath);
    auto child = openChild(fd, name, childPath);
    const auto observed = describe(child.get(), childPath);
    if (observed.directory) {
      if (depth >= CreateMaximumDepth || result.directories.size() >= CreateMaximumDirectories) {
        fail(CreateErrorCode::Limit, childPath, "Source directory bound exceeded");
      }
      result.directories.push_back({relativePath, observed});
      scanDirectory(child.get(), relativePath, depth + 1, result, operation);
    } else if (observed.regular) {
      if (observed.size < 0 || observed.size > lt::file_storage::max_file_size
          || static_cast<std::uint64_t>(observed.size) > CreateMaximumPayloadBytes - result.total
          || result.files.size() >= CreateMaximumFiles) {
        fail(CreateErrorCode::Limit, childPath, "Source file or payload bound exceeded");
      }
      result.total += static_cast<std::uint64_t>(observed.size);
      result.files.push_back({relativePath, observed});
    } else { fail(CreateErrorCode::UnsafeSource, childPath, "Only regular source files and directories are supported"); }
  }
  if (!same(before, describe(fd, path))) { fail(CreateErrorCode::Changed, path, "Source directory changed while enumerating"); }
}
Inventory scan(Handle root, const Operation& operation) {
  const auto observed = describe(root, operation.options.source);
  Inventory result{observed, observed.directory, {}, {}, 0};
  if (result.directory) { scanDirectory(root, {}, 0, result, operation); }
  else if (observed.regular) {
    if (observed.size < 0 || observed.size > lt::file_storage::max_file_size
        || static_cast<std::uint64_t>(observed.size) > CreateMaximumPayloadBytes) {
      fail(CreateErrorCode::Limit, operation.options.source, "Source payload bound exceeded");
    }
    result.files.push_back({{}, observed}); result.total = static_cast<std::uint64_t>(observed.size);
  } else { fail(CreateErrorCode::UnsafeSource, operation.options.source, "Only regular source files and directories are supported"); }
  return result;
}
void validateInventory(const Inventory& before, const Inventory& after, const fs::path& path) {
  if (!same(before.root, after.root) || before.directory != after.directory
      || before.total != after.total || before.files.size() != after.files.size()
      || before.directories.size() != after.directories.size()
      || before.logicalName != after.logicalName || before.origins.size() != after.origins.size()) {
    fail(CreateErrorCode::Changed, path, "Source inventory changed during hashing");
  }
  const auto compare = [&](const std::vector<Entry>& a, const std::vector<Entry>& b) {
    for (std::size_t index = 0; index < a.size(); ++index) {
      if (a[index].relative != b[index].relative || a[index].physical != b[index].physical
          || bool(a[index].physicalParent)!=bool(b[index].physicalParent)
          || (a[index].physicalParent&&!sameIdentity(*a[index].physicalParent,*b[index].physicalParent))
          || !same(a[index].observed, b[index].observed)) {
        fail(CreateErrorCode::Changed, path / pathFromUtf8(a[index].relative), "Source file or directory changed during hashing");
      }
    }
  };
  compare(before.files, after.files); compare(before.directories, after.directories);compare(before.origins,after.origins);
}
Descriptor openRelative(Handle root, const std::string& relative, const fs::path& path) {
  auto duplicate = files::duplicateStateDirectory(root);
  if (!duplicate) { fail(CreateErrorCode::Read, path, "Cannot retain source descriptor"); }
  auto current = std::move(*duplicate);
  std::size_t start = 0;
  while (start < relative.size()) {
    const auto slash = relative.find('/', start);
    const auto name = relative.substr(start, slash == relative.npos ? relative.npos : slash - start);
    if (slash == relative.npos) { return openChild(current.get(), name, path); }
    auto next = files::openDirectoryAt(current.get(), pathFromUtf8(name));
    if (!next) { fail(CreateErrorCode::Changed, path, "Source path changed or became unavailable"); }
    current = std::move(*next);
    start = slash + 1;
  }
  return current;
}

#include "virtual_creator.inc"

void rejectOutputWithinSource(Handle outputParent, const Stamp& source, const fs::path& path) {
#ifdef _WIN32
  const auto held = describe(outputParent, path);
  const auto resolved = files::stateDirectoryPath(outputParent);
  if (!resolved || !sameIdentity(held, describe(openDirectory(*resolved).get(), path))) {
    fail(CreateErrorCode::Write, path, "Cannot inspect output ancestry");
  }
  auto location = *resolved;
  for (std::size_t count = 0; count <= CreateMaximumDepth; ++count) {
    const auto directory = openDirectory(location);
    if (sameIdentity(describe(directory.get(), path), source)) {
      fail(CreateErrorCode::Path, path, "Output must be outside the source directory");
    }
    const auto root = files::filesystemRoot(location);
    if (root.empty()) { fail(CreateErrorCode::Path, path, "Cannot authenticate output ancestry"); }
    if (location == root) { return; }
    location = location.parent_path();
  }
#else
  Descriptor current(::fcntl(outputParent, F_DUPFD_CLOEXEC, 0));
  if (current.get() < 0) { fail(CreateErrorCode::Write, path, "Cannot inspect output ancestry", errno); }
  for (std::size_t count = 0; count <= CreateMaximumDepth; ++count) {
    const auto here = describe(current.get(), path);
    if (sameIdentity(here, source)) { fail(CreateErrorCode::Path, path, "Output must be outside the source directory"); }
    Descriptor parent(::openat(current.get(), "..", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (parent.get() < 0) { fail(CreateErrorCode::Path, path, "Cannot authenticate output ancestry", errno); }
    if (sameIdentity(here, describe(parent.get(), path))) { return; }
    current = std::move(parent);
  }
#endif
  fail(CreateErrorCode::Limit, path, "Output ancestry limit exceeded");
}
void validateOptions(const CreateOptions& options) {
  if (options.format!=CreateFormat::V1&&options.format!=CreateFormat::V2&&options.format!=CreateFormat::Hybrid) {
    fail(CreateErrorCode::Options,options.output,"Unknown torrent format");
  }
  const bool merkle=options.format!=CreateFormat::V1;
  const auto supported=merkle?std::span<const std::uint32_t>(CreateMerklePieceSizes):std::span<const std::uint32_t>(CreatePieceSizes);
  if(options.pieceSize!=0&&std::find(supported.begin(),supported.end(),options.pieceSize)==supported.end()) {
    fail(CreateErrorCode::Options,options.output,merkle?"V2 and hybrid require a supported power-of-two piece size":"Unsupported piece size");
  }
  if (options.comment.size() > CreateMaximumCommentBytes || !utf8(options.comment, true)
      || options.trackerTiers.size() > CreateMaximumTrackerTiers) {
    fail(CreateErrorCode::Options, options.output, "Invalid comment or tracker tiers");
  }
  if (options.sourceTag.size() > 16384 || !utf8(options.sourceTag,true)) {
    fail(CreateErrorCode::Options,options.output,"Invalid source tag");
  }
  if(options.extraHashes && options.format==CreateFormat::V2) {
    fail(CreateErrorCode::Options,options.output,"Original extra hashes require V1 or hybrid");
  }
  if(options.urlSeeds.size()+options.httpSeeds.size()>64) fail(CreateErrorCode::Options,options.output,"Too many web seeds");
  for(const auto* seeds:{&options.urlSeeds,&options.httpSeeds}) for(const auto& url:*seeds) {
    const auto scheme=url.starts_with("http://")?7u:url.starts_with("https://")?8u:0u;
    const auto end=url.find_first_of("/?#",scheme);
    if(!scheme||url.size()>2048||url.size()<=scheme||(end!=url.npos&&end==scheme)||!utf8(url)
        ||std::any_of(url.begin(),url.end(),[](unsigned char c){return c<=0x20||c==0x7f;})) {
      fail(CreateErrorCode::Options,options.output,"Web seed must be a bounded HTTP(S) URL with an authority");
    }
  }
  std::size_t count = 0;
  for (const auto& tier : options.trackerTiers) {
    if (tier.empty()) { fail(CreateErrorCode::Options, options.output, "Tracker tiers must not be empty"); }
    for (const auto& url : tier) {
      if (++count > CreateMaximumTrackers || url.size() > CreateMaximumTrackerBytes
          || !utf8(url) || std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 0x20 || c == 0x7f; })) {
        fail(CreateErrorCode::Options, options.output, "Invalid tracker URL or tracker count");
      }
      const auto scheme = url.starts_with("http://") ? 7u : url.starts_with("https://") ? 8u : url.starts_with("udp://") ? 6u : 0u;
      const auto end = url.find_first_of("/?#", scheme);
      if (scheme == 0 || url.size() <= scheme || (end != url.npos && end == scheme)) {
        fail(CreateErrorCode::Options, options.output, "Tracker must use http, https or udp with an authority");
      }
    }
  }
}
std::uint32_t pieceSize(std::uint64_t total, std::uint32_t requested) {
  if (requested) { return requested; }
  std::uint32_t size = 32768;
  while (size < 4194304 && total / size > 2048) { size *= 2; }
  return size;
}
struct V1PieceHashes {
  std::vector<lt::sha1_hash> values;
  explicit V1PieceHashes(std::size_t count) : values(count) {}
  void set_hash(lt::piece_index_t index, const lt::sha1_hash& hash) {
    values.at(static_cast<std::size_t>(static_cast<int>(index))) = hash;
  }
};

lt::entry generateLargeV1(const Inventory& inventory, const std::string& name,
    const CreateOptions& options, const V1PieceHashes& hashes) {
  lt::entry entry(lt::entry::dictionary_t);
  entry["created by"] = "BiglyBTC";
  if (!options.comment.empty()) { entry["comment"] = options.comment; }
  if (!options.trackerTiers.empty()) {
    entry["announce"] = options.trackerTiers.front().front();
    std::size_t total = 0;
    for (const auto& tier : options.trackerTiers) { total += tier.size(); }
    if (total > 1) {
      for (const auto& tier : options.trackerTiers) {
        lt::entry::list_type values;
        for (const auto& url : tier) { values.emplace_back(url); }
        entry["announce-list"].list().emplace_back(std::move(values));
      }
    }
  }
  auto& info = entry["info"];
  info["name"] = name;
  info["piece length"] = static_cast<int>(options.pieceSize);
  if (options.privateTorrent) { info["private"] = 1; }
  if (inventory.directory) {
    for (const auto& file : inventory.files) {
      lt::entry item(lt::entry::dictionary_t);
      item["length"] = file.observed.size;
      std::string_view remaining(file.relative);
      for (;;) {
        const auto slash = remaining.find('/');
        item["path"].list().emplace_back(std::string(remaining.substr(0, slash)));
        if (slash == std::string_view::npos) { break; }
        remaining.remove_prefix(slash + 1);
      }
      info["files"].list().emplace_back(std::move(item));
    }
  } else { info["length"] = static_cast<std::int64_t>(inventory.total); }
  std::string pieces;
  pieces.reserve(hashes.values.size() * 20);
  for (const auto& hash : hashes.values) { pieces.append(hash.data(), 20); }
  info["pieces"] = std::move(pieces);
  return entry;
}

// MD4 is used only for the source-defined optional ED2K metadata. Its legacy
// provider lives in a private OpenSSL context; ordinary process policy is untouched.
class Ed2k {
  static constexpr std::size_t Block=0x947000;
  struct ContextDelete {void operator()(OSSL_LIB_CTX* p)const{OSSL_LIB_CTX_free(p);}};
  struct ProviderDelete {void operator()(OSSL_PROVIDER* p)const{OSSL_PROVIDER_unload(p);}};
  struct MethodDelete {void operator()(EVP_MD* p)const{EVP_MD_free(p);}};
  struct DigestDelete {void operator()(EVP_MD_CTX* p)const{EVP_MD_CTX_free(p);}};
  std::unique_ptr<OSSL_LIB_CTX,ContextDelete> context_{OSSL_LIB_CTX_new()};
  std::unique_ptr<OSSL_PROVIDER,ProviderDelete> provider_;
  std::unique_ptr<EVP_MD,MethodDelete> method_;
  std::unique_ptr<EVP_MD_CTX,DigestDelete> chunk_{EVP_MD_CTX_new()},blocks_{EVP_MD_CTX_new()};
  std::size_t bytes_=0;bool multiple_=false;
  static void require(bool ok){if(!ok) fail(CreateErrorCode::Generator,{},"Optional ED2K digest unavailable");}
  std::array<unsigned char,16> finalChunk(){
    std::array<unsigned char,16> digest{};unsigned length=0;
    require(EVP_DigestFinal_ex(chunk_.get(),digest.data(),&length)==1&&length==digest.size());
    return digest;
  }
  void appendChunk(){
    const auto digest=finalChunk();require(EVP_DigestUpdate(blocks_.get(),digest.data(),digest.size())==1);
    require(EVP_DigestInit_ex(chunk_.get(),method_.get(),nullptr)==1);bytes_=0;multiple_=true;
  }
public:
  Ed2k(){
    require(bool(context_)&&bool(chunk_)&&bool(blocks_));
    provider_.reset(OSSL_PROVIDER_load(context_.get(),"legacy"));require(bool(provider_));
    method_.reset(EVP_MD_fetch(context_.get(),"MD4","provider=legacy"));require(bool(method_));
    require(EVP_DigestInit_ex(chunk_.get(),method_.get(),nullptr)==1
        &&EVP_DigestInit_ex(blocks_.get(),method_.get(),nullptr)==1);
  }
  void update(const char* data,std::size_t count){
    while(count){
      if(bytes_==Block) appendChunk();
      const auto n=std::min(count,Block-bytes_);
      require(EVP_DigestUpdate(chunk_.get(),data,n)==1);data+=n;count-=n;bytes_+=n;
    }
  }
  std::string final(){
    // Original ED2KHasher appends MD4(empty) at every exact positive boundary.
    if(bytes_==Block){appendChunk();const auto empty=finalChunk();require(EVP_DigestUpdate(blocks_.get(),empty.data(),empty.size())==1);}
    else if(multiple_){const auto tail=finalChunk();require(EVP_DigestUpdate(blocks_.get(),tail.data(),tail.size())==1);}
    else {const auto digest=finalChunk();return {reinterpret_cast<const char*>(digest.data()),digest.size()};}
    std::array<unsigned char,16> digest{};unsigned length=0;
    require(EVP_DigestFinal_ex(blocks_.get(),digest.data(),&length)==1&&length==digest.size());
    return {reinterpret_cast<const char*>(digest.data()),digest.size()};
  }
};
class ExtraHashes {
  lt::hasher overallSha1_;
  Ed2k overallEd2k_;
  std::unique_ptr<lt::hasher> fileSha1_;
  std::unique_ptr<Ed2k> fileEd2k_;
  std::map<std::string,std::pair<std::string,std::string>> files_;
public:
  void begin(){fileSha1_=std::make_unique<lt::hasher>();fileEd2k_=std::make_unique<Ed2k>();}
  void overall(const char* data,std::size_t bytes){overallSha1_.update(data,static_cast<int>(bytes));overallEd2k_.update(data,bytes);}
  void file(const char* data,std::size_t bytes){fileSha1_->update(data,static_cast<int>(bytes));fileEd2k_->update(data,bytes);}
  void end(const std::string& relative){files_.emplace(relative,std::pair{fileSha1_->final().to_string(),fileEd2k_->final()});fileSha1_.reset();fileEd2k_.reset();}
  void apply(lt::entry& entry,bool directory,const std::string& name){
    auto& info=entry["info"];info["sha1"]=overallSha1_.final().to_string();info["ed2k"]=overallEd2k_.final();
    if(!directory) return;
    for(auto& file:info["files"].list()) {
      const auto attr=file.dict().find("attr");if(attr!=file.dict().end()&&attr->second.string().find('p')!=std::string::npos) continue;
      std::string relative;
      for(const auto& part:file["path"].list()){if(!relative.empty()) relative+='/';relative+=part.string();}
      const auto found=files_.find(relative);
      if(found==files_.end()) fail(CreateErrorCode::Generator,name,"Extra hash file projection changed");
      file["sha1"]=found->second.first;file["ed2k"]=found->second.second;files_.erase(found);
    }
    if(!files_.empty()) fail(CreateErrorCode::Generator,name,"Extra hash file omitted");
  }
};
void applyCreationMetadata(lt::entry& entry,const CreateOptions& options){
  if(!options.sourceTag.empty()) entry["info"]["source"]=options.sourceTag;
  for(const auto& url:options.urlSeeds) entry["url-list"].list().emplace_back(url);
  for(const auto& url:options.httpSeeds) entry["httpseeds"].list().emplace_back(url);
  if(options.dhtBackup) entry["azureus_properties"]["dht_backup_enable"]=*options.dhtBackup?1:0;
}

template<class PieceSink>
void hashFiles(Handle root, const Inventory& inventory, PieceSink& torrent,
    const Operation& operation, std::uint32_t size, std::size_t pieces, ExtraHashes* extra) {
  std::array<char, 65536> buffer{};
  lt::hasher hash;
  std::uint64_t completed = 0;
  std::uint32_t inPiece = 0;
  std::size_t index = 0;
  operation.publish({CreateStage::Hashing, 0, inventory.total, inventory.files.size(), 0, pieces});
  for (const auto& file : inventory.files) {
    const auto path = file.physical.empty()?operation.options.source / pathFromUtf8(file.relative):file.physical;
    auto input = openEntry(root,file,path);
    if (!same(file.observed, describe(input.get(), path))) { fail(CreateErrorCode::Changed, path, "Source changed before hashing"); }
    if(extra) extra->begin();
    std::uint64_t offset = 0;
    while (offset < static_cast<std::uint64_t>(file.observed.size)) {
      operation.cancelled();
      const auto wanted = std::min({static_cast<std::uint64_t>(buffer.size()),
          static_cast<std::uint64_t>(file.observed.size) - offset,
          static_cast<std::uint64_t>(size - inPiece)});
      const auto read = readAt(input.get(), buffer.data(), static_cast<std::size_t>(wanted), offset);
      if (read < 0 && errno == EINTR) { continue; }
      if (read < 0) { fail(CreateErrorCode::Read, path, "Source read failed", errno); }
      if (read == 0) { fail(CreateErrorCode::Changed, path, "Source was truncated during hashing"); }
      hash.update(buffer.data(), static_cast<int>(read));
      if(extra){extra->overall(buffer.data(),static_cast<std::size_t>(read));extra->file(buffer.data(),static_cast<std::size_t>(read));}
      const auto amount = static_cast<std::uint32_t>(read);
      inPiece += amount; offset += amount; completed += amount;
      if (inPiece == size) {
        torrent.set_hash(lt::piece_index_t{static_cast<int>(index++)}, hash.final());
        hash.reset(); inPiece = 0;
      }
      operation.publish({CreateStage::Hashing, completed, inventory.total, inventory.files.size(), index, pieces});
    }
    if (!same(file.observed, describe(input.get(), path))) { fail(CreateErrorCode::Changed, path, "Source changed while hashing"); }
    if(extra) extra->end(file.relative);
  }
  if (inPiece != 0) { torrent.set_hash(lt::piece_index_t{static_cast<int>(index++)}, hash.final()); }
  if (index != pieces || completed != inventory.total) { fail(CreateErrorCode::Generator, operation.options.source, "Piece accounting mismatch"); }
}

void hashMerkleFiles(Handle root,const Inventory& inventory,lt::create_torrent& torrent,
    const Operation& operation,std::uint32_t size,std::size_t pieces,const std::string& sourceName,ExtraHashes* extra) {
  constexpr std::size_t Block=16384;
  std::array<char,Block> buffer{},zeros{};
  std::map<std::string,const Entry*> originals;
  for(const auto& file:inventory.files) {
    originals.emplace(inventory.directory?sourceName+'/'+file.relative:sourceName,&file);
  }
  std::set<std::string> visited;
  const auto& files=torrent.files();
  const bool v1=operation.options.format==CreateFormat::Hybrid;
  lt::hasher v1Hash;std::uint32_t inPiece=0;std::size_t pieceIndex=0;
  std::uint64_t completed=0,logical=0;
  auto feed=[&](const char* data,std::size_t bytes) {
    if(extra) extra->overall(data,bytes);
    while(bytes) {
      const auto count=std::min<std::size_t>(bytes,size-inPiece);
      if(v1) v1Hash.update(data,static_cast<int>(count));
      data+=count;bytes-=count;inPiece+=static_cast<std::uint32_t>(count);logical+=count;
      if(inPiece==size) {
        if(v1) torrent.set_hash(lt::piece_index_t{static_cast<int>(pieceIndex)},v1Hash.final());
        ++pieceIndex;inPiece=0;v1Hash.reset();
      }
    }
  };
  operation.publish({CreateStage::Hashing,0,inventory.total,inventory.files.size(),0,pieces});
  for(const auto index:files.file_range()) {
    operation.cancelled();const auto length=files.file_size(index);
    if(files.pad_file_at(index)) {
      for(std::int64_t left=length;left>0;) {
        operation.cancelled();const auto count=static_cast<std::size_t>(std::min<std::int64_t>(left,Block));
        feed(zeros.data(),count);left-=static_cast<std::int64_t>(count);
      }
      continue; // Virtual bytes are never source progress or a pathname read.
    }
    const auto found=originals.find(genericPath(pathFromUtf8(files.file_path(index))));
    if(found==originals.end()||!visited.insert(found->first).second||found->second->observed.size!=length) {
      fail(CreateErrorCode::Generator,operation.options.source,"Generator changed original real-file identity");
    }
    const auto& file=*found->second;const auto path=file.physical.empty()?operation.options.source/file.relative:file.physical;
    auto input=openEntry(root,file,path);
    if(!same(file.observed,describe(input.get(),path))) fail(CreateErrorCode::Changed,path,"Source changed before Merkle hashing");
    std::vector<lt::sha256_hash> leaves;leaves.reserve(size/Block);
    if(extra) extra->begin();
    std::int64_t offset=0;int filePiece=0;
    while(offset<length) {
      operation.cancelled();const auto wanted=static_cast<std::size_t>(std::min<std::int64_t>(Block,length-offset));
      std::size_t filled=0;
      while(filled<wanted) {
        operation.cancelled();const auto count=readAt(input.get(), buffer.data()+filled, wanted-filled, offset+filled);
        if(count<0&&errno==EINTR) continue;
        if(count<0) fail(CreateErrorCode::Read,path,"Source Merkle read failed",errno);
        if(count==0) fail(CreateErrorCode::Changed,path,"Source truncated during Merkle hashing");
        filled+=static_cast<std::size_t>(count);
      }
      if(extra) extra->file(buffer.data(),wanted);
      leaves.push_back(lt::hasher256(buffer.data(),static_cast<int>(wanted)).final());
      feed(buffer.data(),wanted);offset+=static_cast<std::int64_t>(wanted);completed+=wanted;
      if(leaves.size()==size/Block||offset==length) {
        // Exact LT on_hash rule: files smaller than one piece pad only to the
        // next block power-of-two; larger files pad every tail to piece width.
        std::size_t width=size/Block;
        if(length<static_cast<std::int64_t>(size)) {width=1;while(width<leaves.size()) width*=2;}
        leaves.resize(width); // Missing Merkle leaves are zero hashes, not hashed zero-data.
        for(auto n=width;n>1;n/=2) {
          for(std::size_t j=0;j<n;j+=2) {
            lt::hasher256 pair;pair.update(leaves[j].data(),32);pair.update(leaves[j+1].data(),32);leaves[j/2]=pair.final();
          }
        }
        torrent.set_hash2(index,lt::piece_index_t::diff_type{filePiece++},leaves[0]);leaves.clear();
      }
      operation.publish({CreateStage::Hashing,completed,inventory.total,inventory.files.size(),pieceIndex,pieces});
    }
    if(!same(file.observed,describe(input.get(),path))) fail(CreateErrorCode::Changed,path,"Source changed while Merkle hashing");
    if(extra) extra->end(file.relative);
  }
  if(inPiece) {
    if(v1) torrent.set_hash(lt::piece_index_t{static_cast<int>(pieceIndex)},v1Hash.final());
    ++pieceIndex;
  }
  if(visited.size()!=inventory.files.size()||completed!=inventory.total||logical!=static_cast<std::uint64_t>(files.total_size())||pieceIndex!=pieces) {
    fail(CreateErrorCode::Generator,operation.options.source,"Merkle/source piece accounting mismatch");
  }
}

void validateOriginalGeometry(const core::Document& document,const lt::torrent_info& parsed,
    bool merkle,const fs::path& output) {
  const auto layout=compat::originalLayout(document);
  if(!layout||!detail::projectOriginalLayout(*layout,parsed)) {
    fail(CreateErrorCode::Generator,output,"Generated file order/geometry differs from the original UTF16 layout");
  }
  if(merkle&&parsed.info_hashes().has_v1()) {
    // Geometry-only scratch copy, never exported or used as a hash subject.
    // The original also derives V2 roots by these exact tree indices in hybrid.
    auto limits=storage::compatibleConfigLimits();limits.inputBytes=MaximumMetadataBytes;limits.stringBytes=MaximumMetadataBytes;
    const auto* info=document.find("info");const auto* tree=info?info->find("file tree"):nullptr;
    const auto* name=info?info->find("name"):nullptr;const auto* length=info?info->find("piece length"):nullptr;
    if(!tree||!name||!length) fail(CreateErrorCode::Generator,output,"Generated hybrid lacks V2 fields");
    const auto raw=[&](const core::Node& n){return document.originalBytes().substr(n.begin,n.end-n.begin);};
    auto scratch=core::Document::parse("d4:infod9:file tree"+raw(*tree)+"12:meta versioni2e4:name"+raw(*name)+"12:piece length"+raw(*length)+"ee",limits);
    if(!scratch) fail(CreateErrorCode::Generator,output,"Generated V2 validation subject rejected");
    const auto v2=compat::originalLayout(*scratch);
    if(!v2||!detail::projectOriginalLayout(*v2,parsed)) {
      fail(CreateErrorCode::Generator,output,"Generated hybrid V1/V2 original indices differ");
    }
  }
}

#ifdef _WIN32
class Temporary {
public:
  explicit Temporary(Handle parent) : parent_(parent) {}
  ~Temporary() { cleanup(); }
  Temporary(const Temporary&) = delete;
  Temporary& operator=(const Temporary&) = delete;

  void stage(const std::vector<char>& bytes, const fs::path& path) {
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
      std::array<unsigned char, 16> random{};
      if (!platform::fillSystemEntropy(random.data(), random.size())) {
        fail(CreateErrorCode::Write, path, "Cannot allocate a private temporary name");
      }
      std::string candidate = ".biglybtc-create-";
      for (const auto value : random) { candidate += hex[value >> 4u]; candidate += hex[value & 0x0fu]; }
      auto leaf = pathFromUtf8(candidate);
      auto result = files::stageStateFile(parent_, leaf, std::string_view(bytes.data(), bytes.size()), 0600);
      if (!result) {
        if (result.error().code == storage::ErrorCode::Conflict) { continue; }
        fail(CreateErrorCode::Write, path, "Cannot create staged metadata");
      }
      file_ = std::move(*result);
      leaf_ = std::move(leaf);
      const auto identity = files::stateStamp(file_.get());
      if (!identity || !identity->regular || !identity->owned || identity->links != 1) {
        fail(CreateErrorCode::Write, path, "Cannot authenticate staged metadata");
      }
      identity_ = *identity;
      if (!files::syncStateFile(file_.get())) { fail(CreateErrorCode::Write, path, "Cannot sync staged metadata"); }
      return;
    }
    fail(CreateErrorCode::Write, path, "Cannot allocate private output staging file");
  }

  void publish(const fs::path& outputLeaf, const fs::path& path, CreateResult& result) {
    const auto held = files::stateStamp(file_.get());
    const auto named = files::stateStampAt(parent_, leaf_);
    if (!held || !named || !*named || !identity_
        || !files::sameStateSnapshot(*identity_, *held) || !files::sameStateSnapshot(*identity_, **named)) {
      fail(CreateErrorCode::Changed, path, "Staged metadata identity changed");
    }
    const auto renamed = platform::detail::renameRelativeFile(file_.get(), parent_, outputLeaf.native(), false);
    if (!renamed) {
      const auto status = renamed.error();
      fail(status == ERROR_FILE_EXISTS || status == ERROR_ALREADY_EXISTS
          ? CreateErrorCode::Conflict : CreateErrorCode::Write,
          path, "Cannot publish metadata without replacing an existing name");
    }
    leaf_.clear();
    if (!::FlushFileBuffers(file_.get())) {
      result.publication = CreatePublication::PublishedDirectorySyncFailed;
      result.publicationError = {static_cast<int>(::GetLastError()), std::system_category()};
    }
  }

private:
  void cleanup() noexcept {
    if (leaf_.empty() || !identity_) { return; }
    file_ = Descriptor{};
    try { (void)files::retireStateFile(parent_, leaf_, *identity_); }
    catch (...) {} // Never remove an unproven name or throw from the destructor.
  }

  Handle parent_;
  fs::path leaf_;
  Descriptor file_;
  std::optional<Stamp> identity_;
};
#else
// A private directory protects the temporary source of linkat from other users
// with write access to the output parent. The final link never replaces a name.
class Temporary {
public:
  Temporary(int parent, const fs::path& path) : parent_(parent) {
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
      std::array<unsigned char, 16> random{};
      if (!platform::fillSystemEntropy(random.data(), random.size())) {
        fail(CreateErrorCode::Write, path, "Cannot allocate a private temporary name", errno);
      }
      std::string candidate = ".biglybtc-create-";
      for (const auto value : random) { candidate += hex[value >> 4u]; candidate += hex[value & 0x0fu]; }
      if (::mkdirat(parent_, candidate.c_str(), 0700) == 0) { name_ = std::move(candidate); break; }
      if (errno != EEXIST) { fail(CreateErrorCode::Write, path, "Cannot create private output staging directory", errno); }
    }
    if (name_.empty()) { fail(CreateErrorCode::Write, path, "Cannot allocate private output staging directory"); }
    directory_ = Descriptor(::openat(parent_, name_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (directory_.get() < 0) {
      const int number = errno; ::unlinkat(parent_, name_.c_str(), AT_REMOVEDIR);
      fail(CreateErrorCode::Write, path, "Cannot open private staging directory", number);
    }
    struct stat status{};
    if (::fstat(directory_.get(), &status) != 0 || status.st_uid != ::geteuid() || (status.st_mode & 0777) != 0700) {
      ::unlinkat(parent_, name_.c_str(), AT_REMOVEDIR);
      fail(CreateErrorCode::Write, path, "Private staging directory identity rejected");
    }
    file_ = Descriptor(::openat(directory_.get(), "metadata", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (file_.get() < 0) {
      const int number = errno; ::unlinkat(parent_, name_.c_str(), AT_REMOVEDIR);
      fail(CreateErrorCode::Write, path, "Cannot create staged metadata", number);
    }
  }
  ~Temporary() { cleanup(); }
  Temporary(const Temporary&) = delete;
  Temporary& operator=(const Temporary&) = delete;
  int file() const noexcept { return file_.get(); }
  int directory() const noexcept { return directory_.get(); }
  int cleanup() noexcept {
    int failure = 0;
    if (directory_.get() >= 0 && ::unlinkat(directory_.get(), "metadata", 0) != 0 && errno != ENOENT) { failure = errno; }
    if (!name_.empty() && ::unlinkat(parent_, name_.c_str(), AT_REMOVEDIR) != 0 && errno != ENOENT) { failure = errno; }
    if (failure == 0) { name_.clear(); }
    return failure;
  }
private:
  int parent_;
  std::string name_;
  Descriptor directory_;
  Descriptor file_;
};
void writeAll(int fd, const std::vector<char>& bytes, const fs::path& path) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) { continue; }
    if (count <= 0) { fail(CreateErrorCode::Write, path, "Cannot write staged metadata", count < 0 ? errno : EIO); }
    offset += static_cast<std::size_t>(count);
  }
  if (::fsync(fd) != 0) { fail(CreateErrorCode::Write, path, "Cannot sync staged metadata", errno); }
}
#endif
} // namespace

std::expected<CreateResult, CreateError> createTorrent(const CreateOptions& submitted,
    CreateProgressCallback progress, std::stop_token stop) {
  try {
    // Progress callbacks may reenter their caller. Freeze all paths/options
    // before the first callback rather than observing mutable caller storage.
    const CreateOptions options = submitted;
    validateOptions(options);
    Operation operation{options, progress, stop};
    operation.publish({CreateStage::Scanning, 0, 0, 0, 0, 0});
    auto sourceParent = openParent(options.source);
    auto source = openChild(sourceParent.directory.get(), sourceParent.leaf, options.source);
    const auto inventory = options.layoutDescriptor?scanDescriptor(source.get(),operation):scan(source.get(), operation);
    const auto& sourceName=options.layoutDescriptor?inventory.logicalName:sourceParent.leaf;
    if (inventory.files.empty() || inventory.total == 0) {
      fail(CreateErrorCode::Empty, options.source, "Source files have zero total length");
    }
    auto outputParent = openParent(options.output);
    if (options.layoutDescriptor) {
      for(const auto& origin:inventory.origins) if(origin.observed.directory) rejectOutputWithinSource(outputParent.directory.get(),origin.observed,options.output);
    } else if (inventory.directory) { rejectOutputWithinSource(outputParent.directory.get(), inventory.root, options.output); }
    const auto existing = files::stateStampAt(outputParent.directory.get(), pathFromUtf8(outputParent.leaf));
    if (!existing) { fail(CreateErrorCode::Write, options.output, "Cannot inspect output name"); }
    if (*existing) { fail(CreateErrorCode::Conflict, options.output, "Output already exists"); }
    const auto size = pieceSize(inventory.total, options.pieceSize);
    const auto pieces = static_cast<std::size_t>((inventory.total + size - 1) / size);
    if (pieces > CreateMaximumPieces) { fail(CreateErrorCode::Limit, options.source, "Piece count limit exceeded; choose a larger piece size"); }
    lt::file_storage files;
    for (const auto& file : inventory.files) {
      const auto name = inventory.directory ? sourceName + '/' + file.relative : sourceName;
      files.add_file(name, file.observed.size);
    }
    std::unique_ptr<ExtraHashes> extra;
    if(options.extraHashes) extra=std::make_unique<ExtraHashes>();
    std::size_t generatedPieces = pieces;
    lt::entry entry;
    if (options.format == CreateFormat::V1 && size == 268435456U) {
      // Preserve the source-defined original final v1 choice without raising
      // LT's constructor ceiling or allocating a piece-sized hashing buffer.
      V1PieceHashes hashes(pieces);
      hashFiles(source.get(), inventory, hashes, operation, size, pieces, extra.get());
      entry = generateLargeV1(inventory, sourceName, options, hashes);
    } else {
      const auto flags=options.format==CreateFormat::V1?lt::create_torrent::v1_only
          :options.format==CreateFormat::V2?lt::create_torrent::v2_only:lt::create_flags_t{};
      lt::create_torrent torrent(files,static_cast<int>(size),flags);
      if(options.format==CreateFormat::Hybrid&&files.pad_file_at(lt::file_index_t{files.num_files()-1})) {
        // New, unhashed generator inventory only. Original hybrid pads before
        // following files, not after a final nonempty leaf. Keep real empty slots.
        files.remove_tail_padding();
        files.set_num_pieces(static_cast<int>(files.total_size()/size+(files.total_size()%size!=0)));
      }
      generatedPieces=static_cast<std::size_t>(torrent.num_pieces());
      if(generatedPieces>CreateMaximumPieces) fail(CreateErrorCode::Limit,options.source,"Padded piece count limit exceeded; choose a larger piece size");
      if(generatedPieces==0||files.num_files()>static_cast<int>(2*CreateMaximumFiles)
          ||(options.format==CreateFormat::V1&&(files.num_files()!=static_cast<int>(inventory.files.size())
            ||files.total_size()!=static_cast<std::int64_t>(inventory.total)||generatedPieces!=pieces))) {
        fail(CreateErrorCode::Generator,options.source,"Generator changed source layout or exceeded padded-piece bounds");
      }
      torrent.set_creation_date(0);
      torrent.set_creator("BiglyBTC");
      torrent.set_comment(options.comment.c_str());
      torrent.set_priv(options.privateTorrent);
      for (std::size_t tier = 0; tier < options.trackerTiers.size(); ++tier) {
        for (const auto& url : options.trackerTiers[tier]) { torrent.add_tracker(url, static_cast<int>(tier)); }
      }
      if(options.format==CreateFormat::V1) hashFiles(source.get(),inventory,torrent,operation,size,pieces,extra.get());
      else hashMerkleFiles(source.get(),inventory,torrent,operation,size,generatedPieces,sourceName,extra.get());
      entry = torrent.generate();
    }
    applyCreationMetadata(entry,options);
    if(extra) extra->apply(entry,inventory.directory,sourceName);
    if(options.layoutDescriptor) applyInitialLinkage(entry,inventory,sourceName);
    std::vector<char> bytes;
    lt::bencode(std::back_inserter(bytes), entry);
    if (bytes.size() > MaximumMetadataBytes) { fail(CreateErrorCode::Limit, options.output, "Generated metadata limit exceeded"); }
    lt::error_code generatorError;
    lt::torrent_info parsed(bytes.data(), static_cast<int>(bytes.size()), generatorError);
    const bool expectV1=options.format!=CreateFormat::V2,expectV2=options.format!=CreateFormat::V1;
    if(generatorError||parsed.info_hashes().has_v1()!=expectV1||parsed.info_hashes().has_v2()!=expectV2
        ||parsed.num_pieces()!=static_cast<int>(generatedPieces)||parsed.piece_length()!=static_cast<int>(size)) {
      fail(CreateErrorCode::Generator,options.output,"Generated metadata failed independent format/Merkle validation");
    }
    auto limits=storage::compatibleConfigLimits();limits.inputBytes=MaximumMetadataBytes;limits.stringBytes=MaximumMetadataBytes;
    auto document=core::Document::parse(std::string(bytes.data(),bytes.size()),limits);
    if(!document||!storage::originalTorrentInfoOrder(*document)) fail(CreateErrorCode::Generator,options.output,"Generated original key order is unsupported");
    validateOriginalGeometry(*document,parsed,expectV2,options.output);
    std::map<std::string,std::int64_t> expectedFiles;
    for(const auto& file:inventory.files) expectedFiles.emplace(inventory.directory?sourceName+'/'+file.relative:sourceName,file.observed.size);
    std::size_t realFiles=0;
    for(const auto index:parsed.files().file_range()) {
      if(parsed.files().pad_file_at(index)) continue;
      const auto found = expectedFiles.find(genericPath(pathFromUtf8(parsed.files().file_path(index))));
      if(found==expectedFiles.end()||found->second!=parsed.files().file_size(index)) fail(CreateErrorCode::Generator,options.source,"Generated metadata changed an actual source file");
      expectedFiles.erase(found);++realFiles;
    }
    if(!expectedFiles.empty()||realFiles!=inventory.files.size()) fail(CreateErrorCode::Generator,options.source,"Generated metadata lost an actual source/empty file");
    const auto selected=storage::originalTorrentIdentityFromLibtorrent(*document,parsed);
    if(!selected) fail(CreateErrorCode::Generator,options.output,"Generated raw full/selected identity rejected");
    core::TorrentHash legacy;
    std::optional<core::TorrentHash> fullV1,fullV2;
    if(expectV1) {const auto value=core::TorrentHash::parse(parsed.info_hashes().v1.to_string());if(!value) fail(CreateErrorCode::Generator,options.output,"Generated v1 digest rejected");legacy=*value;fullV1=*value;}
    if(expectV2) {const auto value=core::TorrentHash::parse(parsed.info_hashes().v2.to_string());if(!value) fail(CreateErrorCode::Generator,options.output,"Generated v2 digest rejected");fullV2=*value;}
    CreateResult result{options.output,legacy,inventory.total,inventory.files.size(),size,
        generatedPieces,CreatePublication::Durable,{},options.format,selected->originalKey,std::move(fullV1),std::move(fullV2)};
    operation.publish({CreateStage::Validating, inventory.total, inventory.total, inventory.files.size(), generatedPieces, generatedPieces});
    operation.publish({CreateStage::Publishing, inventory.total, inventory.total, inventory.files.size(), generatedPieces, generatedPieces});
#ifdef _WIN32
    Temporary temporary(outputParent.directory.get());
    temporary.stage(bytes, options.output);
#else
    Temporary temporary(outputParent.directory.get(), options.output);
    writeAll(temporary.file(), bytes, options.output);
#endif
    // No more user callbacks after these checks. Reopen every component and
    // rescan the entire source after hashing/staging, detecting stale opened
    // trees, added/removed entries, replacements and same-size content writes.
    auto currentSourceParent = openParent(options.source);
    if (!sameIdentity(sourceParent.identity, currentSourceParent.identity)) {
      fail(CreateErrorCode::Changed, options.source, "Source parent identity changed");
    }
    auto currentSource = openChild(currentSourceParent.directory.get(), currentSourceParent.leaf, options.source);
    if(options.layoutDescriptor&&!same(inventory.root,describe(currentSource.get(),options.source))) fail(CreateErrorCode::Changed,options.source,"Layout descriptor changed before publication");
    validateInventory(inventory, options.layoutDescriptor?scanDescriptor(currentSource.get(),operation):scan(currentSource.get(), operation), options.source);
    auto currentOutputParent = openDirectory(outputParent.path);
    if (!sameIdentity(outputParent.identity, describe(currentOutputParent.get(), outputParent.path))) {
      fail(CreateErrorCode::Changed, options.output, "Output parent identity changed");
    }
    operation.cancelled();
#ifdef _WIN32
    temporary.publish(pathFromUtf8(outputParent.leaf), options.output, result);
#else
    if (::linkat(temporary.directory(), "metadata", outputParent.directory.get(), outputParent.leaf.c_str(), 0) != 0) {
      fail(errno == EEXIST ? CreateErrorCode::Conflict : CreateErrorCode::Write,
          options.output, "Cannot publish metadata without replacing an existing name", errno);
    }
    // COMMIT POINT. No fallible allocation, callback, cancellation or generic
    // failure after this point. Return explicit published status on later IO.
    const int cleanup = temporary.cleanup();
    if (::fsync(outputParent.directory.get()) != 0) {
      result.publication = CreatePublication::PublishedDirectorySyncFailed;
      result.publicationError = {errno, std::generic_category()};
    } else if (cleanup != 0) {
      result.publication = CreatePublication::PublishedCleanupFailed;
      result.publicationError = {cleanup, std::generic_category()};
    }
#endif
    return result;
  } catch (Failure& failure) { return std::unexpected(std::move(failure.error)); }
  catch (const std::bad_alloc&) { return std::unexpected(CreateError{CreateErrorCode::Memory, {}, {}, "Torrent creation allocation failed"}); }
  catch (const std::exception&) { return std::unexpected(CreateError{CreateErrorCode::Generator, {}, {}, "Torrent metadata generator failed"}); }
}
} // namespace biglybtc::engine
