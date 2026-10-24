#include "torrent_creator.hpp"

#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/version.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <dirent.h>
#include <fcntl.h>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

static_assert(LIBTORRENT_VERSION_NUM == 20012, "creator qualified against libtorrent 2.0.12");

namespace biglybtc::engine {
namespace {
namespace fs = std::filesystem;
namespace lt = libtorrent;
constexpr std::size_t MaximumPathBytes = 4096;
constexpr std::size_t MaximumMetadataBytes = 32 * 1024 * 1024;

struct Failure { CreateError error; };
[[noreturn]] void fail(CreateErrorCode code, const fs::path& path,
    const char* text, int number = 0) {
  throw Failure{{code, path, number ? std::error_code(number, std::generic_category())
                                  : std::error_code{}, text}};
}
class Descriptor {
public:
  explicit Descriptor(int value = -1) noexcept : value_(value) {}
  ~Descriptor() { if (value_ >= 0) { ::close(value_); } }
  Descriptor(Descriptor&& other) noexcept : value_(std::exchange(other.value_, -1)) {}
  Descriptor& operator=(Descriptor&& other) noexcept {
    if (this != &other) {
      if (value_ >= 0) { ::close(value_); }
      value_ = std::exchange(other.value_, -1);
    }
    return *this;
  }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  int get() const noexcept { return value_; }
private:
  int value_;
};
struct Stamp {
  dev_t device;
  ino_t inode;
  mode_t mode;
  off_t size;
  timespec modified;
  timespec changed;
};
Stamp stamp(const struct stat& value) {
  return {value.st_dev, value.st_ino, value.st_mode, value.st_size,
          value.st_mtim, value.st_ctim};
}
bool sameIdentity(const Stamp& a, const Stamp& b) {
  return a.device == b.device && a.inode == b.inode && a.mode == b.mode;
}
bool same(const Stamp& a, const Stamp& b) {
  return sameIdentity(a, b) && a.size == b.size
      && a.modified.tv_sec == b.modified.tv_sec && a.modified.tv_nsec == b.modified.tv_nsec
      && a.changed.tv_sec == b.changed.tv_sec && a.changed.tv_nsec == b.changed.tv_nsec;
}
Stamp describe(int fd, const fs::path& path) {
  struct stat value{};
  if (::fstat(fd, &value) != 0) { fail(CreateErrorCode::Read, path, "Cannot inspect opened source", errno); }
  return stamp(value);
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
bool nameValid(std::string_view name) {
  return !name.empty() && name.size() <= 255 && name != "." && name != ".."
      && name.find('/') == name.npos && name.find('\\') == name.npos && utf8(name);
}
std::vector<std::string> absoluteParts(const fs::path& path) {
  const auto text = path.native();
  if (!path.is_absolute() || text.size() > MaximumPathBytes
      || text.find('\0') != text.npos) {
    fail(CreateErrorCode::Path, path, "An absolute bounded path is required");
  }
  std::vector<std::string> result;
  for (const auto& part : path.relative_path()) {
    const auto value = part.native();
    if (!nameValid(value)) { fail(CreateErrorCode::Path, path, "Unsafe path component"); }
    result.push_back(value);
    if (result.size() > CreateMaximumDepth) { fail(CreateErrorCode::Limit, path, "Path depth limit exceeded"); }
  }
  return result;
}
Descriptor openDirectory(const fs::path& path) {
  const auto parts = absoluteParts(path);
  Descriptor current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (current.get() < 0) { fail(CreateErrorCode::Read, path, "Cannot open filesystem root", errno); }
  for (const auto& part : parts) {
    Descriptor next(::openat(current.get(), part.c_str(), O_RDONLY | O_DIRECTORY
        | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (next.get() < 0) { fail(CreateErrorCode::Path, path, "Cannot open directory without following links", errno); }
    current = std::move(next);
  }
  return current;
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
Descriptor openChild(int parent, const std::string& name, const fs::path& path) {
  Descriptor value(::openat(parent, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  if (value.get() < 0) { fail(CreateErrorCode::UnsafeSource, path, "Cannot open source without following links", errno); }
  return value;
}

struct Entry { std::string relative; Stamp observed; };
struct DirectoryCloser { void operator()(DIR* value) const noexcept { if (value) { ::closedir(value); } } };
struct Inventory {
  Stamp root;
  bool directory;
  std::vector<Entry> files;
  std::vector<Entry> directories;
  std::uint64_t total = 0;
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
void scanDirectory(int fd, const std::string& relative, std::size_t depth,
    Inventory& result, const Operation& operation) {
  operation.cancelled();
  const auto path = operation.options.source / relative;
  const auto before = describe(fd, path);
  if (!S_ISDIR(before.mode)) { fail(CreateErrorCode::UnsafeSource, path, "Source directory changed type"); }
  // A fresh descriptor gives each scan its own directory cursor. dup() would
  // share an offset and could make the final inventory accidentally empty.
  const int listing = ::openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (listing < 0) { fail(CreateErrorCode::Read, path, "Cannot list source directory", errno); }
  std::unique_ptr<DIR, DirectoryCloser> directory(::fdopendir(listing));
  if (!directory) { const int number = errno; ::close(listing); fail(CreateErrorCode::Read, path, "Cannot list source directory", number); }
  std::vector<std::string> names;
  for (;;) {
    operation.cancelled(); errno = 0;
    const auto* item = ::readdir(directory.get());
    if (!item) {
      if (errno) { fail(CreateErrorCode::Read, path, "Source directory read failed", errno); }
      break;
    }
    const std::string_view name(item->d_name);
    if (name == "." || name == "..") { continue; }
    if (!nameValid(name)) { fail(CreateErrorCode::UnsafeSource, path, "Source name is not a safe UTF-8 component"); }
    names.emplace_back(name);
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
    const auto childPath = operation.options.source / relativePath;
    auto child = openChild(fd, name, childPath);
    const auto observed = describe(child.get(), childPath);
    if (S_ISDIR(observed.mode)) {
      if (depth >= CreateMaximumDepth || result.directories.size() >= CreateMaximumDirectories) {
        fail(CreateErrorCode::Limit, childPath, "Source directory bound exceeded");
      }
      result.directories.push_back({relativePath, observed});
      scanDirectory(child.get(), relativePath, depth + 1, result, operation);
    } else if (S_ISREG(observed.mode)) {
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
Inventory scan(int root, const Operation& operation) {
  const auto observed = describe(root, operation.options.source);
  Inventory result{observed, S_ISDIR(observed.mode), {}, {}, 0};
  if (result.directory) { scanDirectory(root, {}, 0, result, operation); }
  else if (S_ISREG(observed.mode)) {
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
      || before.directories.size() != after.directories.size()) {
    fail(CreateErrorCode::Changed, path, "Source inventory changed during hashing");
  }
  const auto compare = [&](const std::vector<Entry>& a, const std::vector<Entry>& b) {
    for (std::size_t index = 0; index < a.size(); ++index) {
      if (a[index].relative != b[index].relative || !same(a[index].observed, b[index].observed)) {
        fail(CreateErrorCode::Changed, path / a[index].relative, "Source file or directory changed during hashing");
      }
    }
  };
  compare(before.files, after.files); compare(before.directories, after.directories);
}
Descriptor openRelative(int root, const std::string& relative, const fs::path& path) {
  Descriptor current(::fcntl(root, F_DUPFD_CLOEXEC, 0));
  if (current.get() < 0) { fail(CreateErrorCode::Read, path, "Cannot retain source descriptor", errno); }
  std::size_t start = 0;
  while (start < relative.size()) {
    const auto slash = relative.find('/', start);
    const auto name = relative.substr(start, slash == relative.npos ? relative.npos : slash - start);
    const bool last = slash == relative.npos;
    Descriptor next(::openat(current.get(), name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW
        | O_NONBLOCK | (last ? 0 : O_DIRECTORY)));
    if (next.get() < 0) { fail(CreateErrorCode::Changed, path, "Source path changed or became unavailable", errno); }
    current = std::move(next);
    if (last) { break; }
    start = slash + 1;
  }
  return current;
}
void rejectOutputWithinSource(int outputParent, const Stamp& source, const fs::path& path) {
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
  fail(CreateErrorCode::Limit, path, "Output ancestry limit exceeded");
}
void validateOptions(const CreateOptions& options) {
  if (options.pieceSize != 0 && std::find(CreatePieceSizes.begin(), CreatePieceSizes.end(), options.pieceSize) == CreatePieceSizes.end()) {
    fail(CreateErrorCode::Options, options.output, "Unsupported piece size");
  }
  if (options.comment.size() > CreateMaximumCommentBytes || !utf8(options.comment, true)
      || options.trackerTiers.size() > CreateMaximumTrackerTiers) {
    fail(CreateErrorCode::Options, options.output, "Invalid comment or tracker tiers");
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
void hashFiles(int root, const Inventory& inventory, lt::create_torrent& torrent,
    const Operation& operation, std::uint32_t size, std::size_t pieces) {
  std::array<char, 65536> buffer{};
  lt::hasher hash;
  std::uint64_t completed = 0;
  std::uint32_t inPiece = 0;
  std::size_t index = 0;
  operation.publish({CreateStage::Hashing, 0, inventory.total, inventory.files.size(), 0, pieces});
  for (const auto& file : inventory.files) {
    const auto path = operation.options.source / file.relative;
    auto input = openRelative(root, file.relative, path);
    if (!same(file.observed, describe(input.get(), path))) { fail(CreateErrorCode::Changed, path, "Source changed before hashing"); }
    std::uint64_t offset = 0;
    while (offset < static_cast<std::uint64_t>(file.observed.size)) {
      operation.cancelled();
      const auto wanted = std::min({static_cast<std::uint64_t>(buffer.size()),
          static_cast<std::uint64_t>(file.observed.size) - offset,
          static_cast<std::uint64_t>(size - inPiece)});
      const auto read = ::pread(input.get(), buffer.data(), static_cast<std::size_t>(wanted), static_cast<off_t>(offset));
      if (read < 0 && errno == EINTR) { continue; }
      if (read < 0) { fail(CreateErrorCode::Read, path, "Source read failed", errno); }
      if (read == 0) { fail(CreateErrorCode::Changed, path, "Source was truncated during hashing"); }
      hash.update(buffer.data(), static_cast<int>(read));
      const auto amount = static_cast<std::uint32_t>(read);
      inPiece += amount; offset += amount; completed += amount;
      if (inPiece == size) {
        torrent.set_hash(lt::piece_index_t{static_cast<int>(index++)}, hash.final());
        hash.reset(); inPiece = 0;
      }
      operation.publish({CreateStage::Hashing, completed, inventory.total, inventory.files.size(), index, pieces});
    }
    if (!same(file.observed, describe(input.get(), path))) { fail(CreateErrorCode::Changed, path, "Source changed while hashing"); }
  }
  if (inPiece != 0) { torrent.set_hash(lt::piece_index_t{static_cast<int>(index++)}, hash.final()); }
  if (index != pieces || completed != inventory.total) { fail(CreateErrorCode::Generator, operation.options.source, "Piece accounting mismatch"); }
}

// A private directory protects the temporary source of linkat from other users
// with write access to the output parent. The final link never replaces a name.
class Temporary {
public:
  Temporary(int parent, const fs::path& path) : parent_(parent) {
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
      std::array<unsigned char, 16> random{};
      std::size_t filled = 0;
      while (filled < random.size()) {
        const auto result = ::getrandom(random.data() + filled, random.size() - filled, 0);
        if (result < 0 && errno == EINTR) { continue; }
        if (result <= 0) { fail(CreateErrorCode::Write, path, "Cannot allocate a private temporary name", errno); }
        filled += static_cast<std::size_t>(result);
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
    const auto inventory = scan(source.get(), operation);
    if (inventory.files.empty() || inventory.total == 0) {
      fail(CreateErrorCode::Empty, options.source, "Source files have zero total length");
    }
    auto outputParent = openParent(options.output);
    if (inventory.directory) { rejectOutputWithinSource(outputParent.directory.get(), inventory.root, options.output); }
    struct stat existing{};
    if (::fstatat(outputParent.directory.get(), outputParent.leaf.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0) {
      fail(CreateErrorCode::Conflict, options.output, "Output already exists");
    }
    if (errno != ENOENT) { fail(CreateErrorCode::Write, options.output, "Cannot inspect output name", errno); }
    const auto size = pieceSize(inventory.total, options.pieceSize);
    const auto pieces = static_cast<std::size_t>((inventory.total + size - 1) / size);
    if (pieces > CreateMaximumPieces) { fail(CreateErrorCode::Limit, options.source, "Piece count limit exceeded; choose a larger piece size"); }
    lt::file_storage files;
    for (const auto& file : inventory.files) {
      const auto name = inventory.directory ? sourceParent.leaf + '/' + file.relative : sourceParent.leaf;
      files.add_file(name, file.observed.size);
    }
    lt::create_torrent torrent(files, static_cast<int>(size), lt::create_torrent::v1_only);
    if (files.num_files() != static_cast<int>(inventory.files.size()) || files.total_size() != static_cast<std::int64_t>(inventory.total)
        || torrent.num_pieces() != static_cast<int>(pieces)) {
      fail(CreateErrorCode::Generator, options.source, "Generator changed source layout");
    }
    torrent.set_creation_date(0);
    torrent.set_creator("BiglyBTC");
    torrent.set_comment(options.comment.c_str());
    torrent.set_priv(options.privateTorrent);
    for (std::size_t tier = 0; tier < options.trackerTiers.size(); ++tier) {
      for (const auto& url : options.trackerTiers[tier]) { torrent.add_tracker(url, static_cast<int>(tier)); }
    }
    hashFiles(source.get(), inventory, torrent, operation, size, pieces);
    const auto entry = torrent.generate();
    std::vector<char> bytes;
    lt::bencode(std::back_inserter(bytes), entry);
    if (bytes.size() > MaximumMetadataBytes) { fail(CreateErrorCode::Limit, options.output, "Generated metadata limit exceeded"); }
    lt::error_code generatorError;
    lt::torrent_info parsed(bytes.data(), static_cast<int>(bytes.size()), generatorError);
    if (generatorError || !parsed.info_hashes().has_v1() || parsed.info_hashes().has_v2()
        || parsed.total_size() != static_cast<std::int64_t>(inventory.total)
        || parsed.num_files() != static_cast<int>(inventory.files.size()) || parsed.num_pieces() != static_cast<int>(pieces)
        || parsed.piece_length() != static_cast<int>(size)) {
      fail(CreateErrorCode::Generator, options.output, "Generated metadata failed independent decoder validation");
    }
    for (std::size_t index = 0; index < inventory.files.size(); ++index) {
      const auto fileIndex = lt::file_index_t{static_cast<int>(index)};
      const auto& file = inventory.files[index];
      const auto expected = inventory.directory ? sourceParent.leaf + '/' + file.relative : sourceParent.leaf;
      if (parsed.files().file_path(fileIndex) != expected || parsed.files().file_size(fileIndex) != file.observed.size) {
        fail(CreateErrorCode::Generator, options.source / file.relative, "Generator or decoder changed a source path");
      }
    }
    const auto digest = parsed.info_hashes().v1;
    const auto identity = core::TorrentHash::parse(std::string_view(digest.data(), 20));
    if (!identity) { fail(CreateErrorCode::Generator, options.output, "Generated v1 identity rejected"); }
    CreateResult result{options.output, *identity, inventory.total, inventory.files.size(), size,
        pieces, CreatePublication::Durable, {}};
    operation.publish({CreateStage::Validating, inventory.total, inventory.total, inventory.files.size(), pieces, pieces});
    operation.publish({CreateStage::Publishing, inventory.total, inventory.total, inventory.files.size(), pieces, pieces});
    Temporary temporary(outputParent.directory.get(), options.output);
    writeAll(temporary.file(), bytes, options.output);
    // No more user callbacks after these checks. Reopen every component and
    // rescan the entire source after hashing/staging, detecting stale opened
    // trees, added/removed entries, replacements and same-size content writes.
    auto currentSourceParent = openParent(options.source);
    if (!sameIdentity(sourceParent.identity, currentSourceParent.identity)) {
      fail(CreateErrorCode::Changed, options.source, "Source parent identity changed");
    }
    auto currentSource = openChild(currentSourceParent.directory.get(), currentSourceParent.leaf, options.source);
    validateInventory(inventory, scan(currentSource.get(), operation), options.source);
    auto currentOutputParent = openDirectory(outputParent.path);
    if (!sameIdentity(outputParent.identity, describe(currentOutputParent.get(), outputParent.path))) {
      fail(CreateErrorCode::Changed, options.output, "Output parent identity changed");
    }
    operation.cancelled();
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
    return result;
  } catch (Failure& failure) { return std::unexpected(std::move(failure.error)); }
  catch (const std::bad_alloc&) { return std::unexpected(CreateError{CreateErrorCode::Memory, {}, {}, "Torrent creation allocation failed"}); }
  catch (const std::exception&) { return std::unexpected(CreateError{CreateErrorCode::Generator, {}, {}, "Torrent metadata generator failed"}); }
}
} // namespace biglybtc::engine
