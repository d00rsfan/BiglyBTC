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

StateStamp metadata(const struct stat& info) noexcept {
  return {static_cast<std::uint64_t>(info.st_dev), static_cast<std::uint64_t>(info.st_ino),
      static_cast<std::uint64_t>(info.st_nlink), info.st_uid, info.st_gid, info.st_size,
      static_cast<unsigned>(info.st_mode), platform::modificationTime(info), platform::changeTime(info),
      S_ISREG(info.st_mode), S_ISDIR(info.st_mode), info.st_uid == ::geteuid()};
}
} // namespace

std::expected<StateDescriptor, Error> openStateDirectory(StateHandle parent, const std::string& name) {
  StateDescriptor directory(::openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (directory.get() < 0) {
    const auto code = errno == ENOENT ? ErrorCode::Missing : ErrorCode::UnsafePath;
    return error(code, "Cannot open original active-state directory safely");
  }
  return directory;
}

std::expected<StateDescriptor, Error> createStateDirectory(StateHandle parent, const std::string& name) {
  if (::mkdirat(parent, name.c_str(), 0700) != 0) {
    return error(ErrorCode::Conflict, "Cannot exclusively create original control directory");
  }
  auto synced = syncStateDirectory(parent);
  if (!synced) { return std::unexpected(synced.error()); }
  return openStateDirectory(parent, name);
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

std::expected<StateDescriptor, Error> openStateFile(StateHandle parent, const std::string& name) {
  StateDescriptor file(::openat(parent, name.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW));
  if (file.get() < 0) {
    const auto code = errno == ENOENT ? ErrorCode::Missing : errno == ELOOP ? ErrorCode::UnsafePath : ErrorCode::Io;
    return error(code, "Cannot open saved-state file safely");
  }
  return file;
}

std::expected<StateStamp, Error> stateStamp(StateHandle file) {
  struct stat info{};
  if (::fstat(file, &info) != 0) { return error(ErrorCode::Io, "Cannot inspect held saved-state entry"); }
  return metadata(info);
}

std::expected<std::optional<StateStamp>, Error> stateStampAt(StateHandle directory, const std::string& name) {
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

std::expected<StateDescriptor, Error> stageStateFile(StateHandle parent, const std::string& name,
    std::string_view data, unsigned mode) {
  return stageExclusive(parent, name, data, static_cast<mode_t>(mode));
}

std::expected<void, Error> publishStateFile(StateHandle directory, const std::string& source,
    const std::string& destination, StateHandle staged, bool replace, const StateStamp* expected) {
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
