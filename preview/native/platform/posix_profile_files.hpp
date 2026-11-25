// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdio>
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

inline bool sameModificationTime(const struct stat& left, const struct stat& right) noexcept {
  return sameTimestamp(modificationTime(left), modificationTime(right));
}

inline bool sameChangeTime(const struct stat& left, const struct stat& right) noexcept {
  return sameTimestamp(changeTime(left), changeTime(right));
}

// A single native rename preserves the destination on an existing-file race.
inline int renameNoReplaceAt(int directory, const char* source, const char* destination) noexcept {
#ifdef __APPLE__
  return ::renameatx_np(directory, source, directory, destination, RENAME_EXCL);
#else
  return ::renameat2(directory, source, directory, destination, RENAME_NOREPLACE);
#endif
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

} // namespace biglybtc::platform
