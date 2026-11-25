// SPDX-License-Identifier: GPL-2.0-or-later
#include "state_file_io.hpp"
#include "file_io.hpp"
#include "../platform/posix_profile_files.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::storage::detail {
namespace {
std::unexpected<Error> error(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}

bool ordinaryLeaf(const std::filesystem::path& name) {
  return !name.empty() && name == name.filename() && name != "." && name != ".."
      && name.native().find('\0') == name.native().npos;
}

StateStamp metadata(const struct stat& info) noexcept {
  return {static_cast<std::uint64_t>(info.st_dev), static_cast<std::uint64_t>(info.st_ino),
      static_cast<std::uint64_t>(info.st_nlink), info.st_uid, info.st_gid, info.st_size,
      static_cast<unsigned>(info.st_mode), platform::modificationTime(info), platform::changeTime(info),
      S_ISREG(info.st_mode), S_ISDIR(info.st_mode), info.st_uid == ::geteuid()};
}
} // namespace

std::expected<StateDescriptor, Error> openStateDirectory(StateHandle parent, const std::string& name) {
  return openDirectoryAt(parent, name);
}

std::expected<StateDescriptor, Error> openDirectoryAt(StateHandle parent, const std::filesystem::path& name) {
  if (!ordinaryLeaf(name)) {
    return error(ErrorCode::UnsafePath, "Expected one ordinary directory component");
  }
  StateDescriptor directory(::openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (directory.get() < 0) {
    const auto code = errno == ENOENT ? ErrorCode::Missing : ErrorCode::UnsafePath;
    return error(code, "Cannot open original active-state directory safely");
  }
  return directory;
}

std::expected<StateDescriptor, Error> createStateDirectory(StateHandle parent, const std::string& name) {
  auto directory = createDirectoryAt(parent, name);
  if (!directory) { return directory; }
  auto synced = syncStateDirectory(parent);
  if (!synced) { return std::unexpected(synced.error()); }
  return directory;
}

std::expected<StateDescriptor, Error> createDirectoryAt(StateHandle parent, const std::filesystem::path& name) {
  if (!ordinaryLeaf(name)) {
    return error(ErrorCode::UnsafePath, "Expected one ordinary directory component");
  }
  if (::mkdirat(parent, name.c_str(), 0700) != 0) {
    return error(errno == EEXIST ? ErrorCode::Conflict : ErrorCode::Io, "Cannot exclusively create directory");
  }
  return openDirectoryAt(parent, name);
}

std::filesystem::path filesystemRoot(const std::filesystem::path& path) { return path.root_path(); }

int compareFilesystemComponent(const std::filesystem::path& a, const std::filesystem::path& b) noexcept {
  return a.compare(b);
}

bool FilesystemPathLess::operator()(const std::filesystem::path& a, const std::filesystem::path& b) const noexcept {
  return a < b;
}

std::expected<StateDescriptor, Error> openFilesystemRoot(const std::filesystem::path& path) {
  if (path != "/") { return error(ErrorCode::UnsafePath, "Unsafe filesystem root"); }
  StateDescriptor root(::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (root.get() < 0) { return error(ErrorCode::Io, "Cannot open filesystem root"); }
  return root;
}

std::expected<StateDescriptor, Error> duplicateStateDirectory(StateHandle directory) {
  StateDescriptor result(::fcntl(directory, F_DUPFD_CLOEXEC, 0));
  if (result.get() < 0) { return error(ErrorCode::Io, "Cannot retain original profile directory"); }
  return result;
}

std::expected<std::filesystem::path, Error> stateDirectoryPath(StateHandle directory) {
  auto path = platform::namedDescriptorPath(directory);
  if (!path) { return error(ErrorCode::UnsafePath, "Cannot resolve original profile identity"); }
  return std::filesystem::path(std::move(*path));
}

std::expected<StateDescriptor, Error> openStatePathDirectory(const std::filesystem::path& path) {
  if (!path.is_absolute() || path.lexically_normal() != path) {
    return error(ErrorCode::UnsafePath, "Unsafe original profile path");
  }
  StateDescriptor current(::open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (current.get() < 0) { return error(ErrorCode::Io, "Cannot open filesystem root"); }
  for (const auto& part : path.relative_path()) {
    if (part.empty() || part == "." || part == "..") {
      return error(ErrorCode::UnsafePath, "Unsafe original profile component");
    }
    auto next = openStateDirectory(current.get(), part.native());
    if (!next) { return error(ErrorCode::Conflict, "Original profile directory chain changed"); }
    const auto held = stateStamp(next->get());
    const auto named = stateStampAt(current.get(), part.native());
    if (!held || !named || !*named || !(*named)->directory || !sameStateIdentity(*held, **named)) {
      return error(ErrorCode::Conflict, "Original profile directory chain changed");
    }
    current = std::move(*next);
  }
  return current;
}

std::expected<StateDescriptor, Error> openStateFile(StateHandle parent, const std::filesystem::path& name) {
  if (!ordinaryLeaf(name)) { return error(ErrorCode::UnsafePath, "Expected one ordinary state file name"); }
  StateDescriptor file(::openat(parent, name.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW));
  if (file.get() < 0) {
    const auto code = errno == ENOENT ? ErrorCode::Missing : errno == ELOOP ? ErrorCode::UnsafePath : ErrorCode::Io;
    return error(code, "Cannot open saved-state file safely");
  }
  return file;
}

std::expected<StateDescriptor, Error> openStateWitness(StateHandle parent, const std::filesystem::path& name) {
  return openStateFile(parent, name);
}

std::expected<StateDescriptor, Error> openWritableStateFile(StateHandle parent,
    const std::filesystem::path& name, bool create) {
  if (!ordinaryLeaf(name)) { return error(ErrorCode::UnsafePath, "Expected one ordinary state file name"); }
  const int flags=O_RDWR | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW | (create ? O_CREAT | O_EXCL : 0);
  StateDescriptor file(::openat(parent,name.c_str(),flags,0600));
  if (file.get()<0) {
    const auto code=errno==EEXIST ? ErrorCode::Conflict : errno==ENOENT ? ErrorCode::Missing
        : errno==ELOOP ? ErrorCode::UnsafePath : ErrorCode::Io;
    return error(code,"Cannot open writable state stream");
  }
  const auto held=stateStamp(file.get());
  if (!held || !held->regular || !held->owned || held->links!=1) {
    return error(ErrorCode::UnsafePath,"Writable state stream is not an owned regular single-link file");
  }
  return file;
}

std::expected<StateStamp, Error> stateStamp(StateHandle file) {
  struct stat info{};
  if (::fstat(file, &info) != 0) { return error(ErrorCode::Io, "Cannot inspect held saved-state entry"); }
  return metadata(info);
}

std::expected<std::optional<StateStamp>, Error> stateStampAt(StateHandle directory, const std::filesystem::path& name) {
  if (!ordinaryLeaf(name)) {
    return error(ErrorCode::UnsafePath, "Expected one ordinary file component");
  }
  struct stat info{};
  if (::fstatat(directory, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0) { return metadata(info); }
  if (errno == ENOENT) { return std::nullopt; }
  return error(ErrorCode::Io, "Cannot inspect named saved-state entry");
}

std::ptrdiff_t readStateAt(StateHandle file, char* data, std::size_t bytes, std::uint64_t offset) {
  std::ptrdiff_t count;
  do { count = ::pread(file, data, bytes, static_cast<off_t>(offset)); } while (count < 0 && errno == EINTR);
  return count;
}

std::expected<std::optional<std::string>, Error> readStateFile(
    StateHandle parent, const std::string& name, std::size_t maximum) {
  return readFile(parent, name, maximum);
}

std::expected<StateDescriptor, Error> stageStateFile(StateHandle parent, const std::filesystem::path& name,
    std::string_view data, unsigned mode) {
  if (!ordinaryLeaf(name)) { return error(ErrorCode::UnsafePath, "Expected one ordinary saving file name"); }
  return stageExclusive(parent, name.native(), data, static_cast<mode_t>(mode));
}

std::expected<void, Error> publishStateFile(StateHandle directory, const std::filesystem::path& source,
    const std::filesystem::path& destination, StateHandle staged, bool replace, const StateStamp* expected) {
  if (!ordinaryLeaf(source) || !ordinaryLeaf(destination)) {
    return error(ErrorCode::UnsafePath, "Expected ordinary state file names");
  }
  if (staged != InvalidStateHandle) {
    const auto held = stateStamp(staged);
    const auto named = stateStampAt(directory, source);
    if (!held || !named || !*named || !held->regular || held->links != 1
        || !sameStateSnapshot(*held, **named)) {
      return error(ErrorCode::Conflict, "Saved-state temporary was replaced");
    }
  }
  if (expected) {
    const auto current = stateStampAt(directory, source);
    if (!current || !*current || !sameStateSnapshot(**current, *expected)) {
      return error(ErrorCode::Conflict, "Saved-state source changed before publication");
    }
  }
  const auto published = replace ? ::renameat(directory, source.c_str(), directory, destination.c_str())
      : platform::renameNoReplaceAt(directory, source.c_str(), destination.c_str());
  if (published != 0) {
    return error(errno == EEXIST ? ErrorCode::Conflict : ErrorCode::Io, "Cannot publish saved-state entry");
  }
  return {};
}

std::expected<void, Error> retireStateFile(StateHandle directory, const std::string& name, const StateStamp& expected) {
  return removePayloadName(directory, name, expected);
}

std::expected<void, Error> linkPayloadName(StateHandle directory, StateHandle source,
    const std::filesystem::path& existing, const std::filesystem::path& added) {
  if (!ordinaryLeaf(added)) { return error(ErrorCode::UnsafePath, "Expected one ordinary completion name"); }
  const auto held = stateStamp(source);
  const auto named = stateStampAt(directory, existing);
  if (!held || !named || !*named || !held->regular || !held->owned || !sameStateSnapshot(*held, **named)) {
    return error(ErrorCode::Conflict, "Managed payload changed before publication");
  }
  if (::linkat(directory, existing.c_str(), directory, added.c_str(), 0) != 0) {
    return error(errno == EEXIST ? ErrorCode::Conflict : ErrorCode::Io, "Cannot link managed completion name");
  }
  return {};
}

std::expected<void, Error> removePayloadName(StateHandle directory,
    const std::filesystem::path& name, const StateStamp& expected) {
  const auto current = stateStampAt(directory, name);
  if (!current || !*current || !sameStateSnapshot(**current, expected)) {
    return error(ErrorCode::Conflict, "Saved-state entry changed before retirement");
  }
  if (::unlinkat(directory, name.c_str(), 0) != 0) { return error(ErrorCode::Io, "Cannot retire original control"); }
  return {};
}

std::expected<void, Error> protectPrivateStateFile(StateHandle file) {
  if (::fchmod(file, 0600) != 0) { return error(ErrorCode::Io, "Cannot protect original control temporary"); }
  return {};
}

std::expected<void, Error> syncStateFile(StateHandle file) {
  if (::fsync(file) != 0) { return error(ErrorCode::Io, "Cannot synchronize saved-state file"); }
  return {};
}

std::expected<void, Error> syncStateDirectory(StateHandle directory) {
  if (::fsync(directory) != 0) { return error(ErrorCode::Io, "Unable to synchronize saved-state directory"); }
  return {};
}

std::expected<void, Error> setStateMtime(StateHandle file, timespec modified) {
  const timespec times[2]{{0, UTIME_OMIT}, modified};
  if (::futimens(file, times) != 0) { return error(ErrorCode::Io, "Cannot retain original resume timestamp"); }
  return {};
}

unsigned stateWriteMode(const StateStamp& info) noexcept {
  return info.mode & 0777;
}
} // namespace biglybtc::storage::detail
