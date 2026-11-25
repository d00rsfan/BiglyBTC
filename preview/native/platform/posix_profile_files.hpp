// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstdio>
#include <fcntl.h>
#include <limits.h>
#include <optional>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

#ifdef __linux__
#include <linux/fs.h>
#endif

namespace biglybtc::platform {

inline bool sameTimestamp(const timespec& left, const timespec& right) noexcept {
  return left.tv_sec == right.tv_sec && left.tv_nsec == right.tv_nsec;
}

inline const timespec& modificationTime(const struct stat& entry) noexcept {
#ifdef __APPLE__
  return entry.st_mtimespec;
#else
  return entry.st_mtim;
#endif
}

inline const timespec& changeTime(const struct stat& entry) noexcept {
#ifdef __APPLE__
  return entry.st_ctimespec;
#else
  return entry.st_ctim;
#endif
}

inline void setTimestamps(struct stat& entry, timespec modified, timespec changed) noexcept {
#ifdef __APPLE__
  entry.st_mtimespec = modified;
  entry.st_ctimespec = changed;
#else
  entry.st_mtim = modified;
  entry.st_ctim = changed;
#endif
}

inline bool sameModificationTime(const struct stat& left, const struct stat& right) noexcept {
  return sameTimestamp(modificationTime(left), modificationTime(right));
}

inline bool sameChangeTime(const struct stat& left, const struct stat& right) noexcept {
  return sameTimestamp(changeTime(left), changeTime(right));
}

// A single native rename preserves the destination on an existing-file race.
inline int renameNoReplaceAt(int sourceDirectory, const char* source,
                             int destinationDirectory, const char* destination) noexcept {
#ifdef __APPLE__
  return ::renameatx_np(sourceDirectory, source, destinationDirectory, destination, RENAME_EXCL);
#else
  return ::renameat2(sourceDirectory, source, destinationDirectory, destination, RENAME_NOREPLACE);
#endif
}

inline int renameNoReplaceAt(int directory, const char* source, const char* destination) noexcept {
  return renameNoReplaceAt(directory, source, directory, destination);
}

inline int publishProfileFileAt(int directory, const char* source, const char* destination,
                                bool replace) noexcept {
  if (!replace) { return renameNoReplaceAt(directory, source, destination); }
#ifdef __APPLE__
  return ::renameat(directory, source, directory, destination);
#else
  return ::renameat2(directory, source, directory, destination, 0);
#endif
}

// Resolve the name of an already-held file. Callers still authenticate the
// named file against the descriptor before granting pathname authority.
inline std::optional<std::string> namedDescriptorPath(int descriptor) {
#ifdef __APPLE__
  std::array<char, PATH_MAX> name{};
  if (::fcntl(descriptor, F_GETPATH, name.data()) < 0 || name.front() != '/' || name.back() != '\0') {
    return std::nullopt;
  }
  return std::string(name.data());
#else
  std::array<char, 4097> name{};
  const auto procLink = "/proc/self/fd/" + std::to_string(descriptor);
  const auto size = ::readlink(procLink.c_str(), name.data(), name.size());
  if (size <= 0 || static_cast<std::size_t>(size) >= name.size() || name.front() != '/') {
    return std::nullopt;
  }
  return std::string(name.data(), static_cast<std::size_t>(size));
#endif
}

} // namespace biglybtc::platform
