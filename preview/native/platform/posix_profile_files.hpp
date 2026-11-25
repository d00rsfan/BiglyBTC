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

inline bool sameModificationTime(const struct stat& left, const struct stat& right) noexcept {
#ifdef __APPLE__
  return sameTimestamp(left.st_mtimespec, right.st_mtimespec);
#else
  return sameTimestamp(left.st_mtim, right.st_mtim);
#endif
}

inline bool sameChangeTime(const struct stat& left, const struct stat& right) noexcept {
#ifdef __APPLE__
  return sameTimestamp(left.st_ctimespec, right.st_ctimespec);
#else
  return sameTimestamp(left.st_ctim, right.st_ctim);
#endif
}

// A single native rename preserves the destination on an existing-file race.
inline int renameNoReplaceAt(int directory, const char* source, const char* destination) noexcept {
#ifdef __APPLE__
  return ::renameatx_np(directory, source, directory, destination, RENAME_EXCL);
#else
  return ::renameat2(directory, source, directory, destination, RENAME_NOREPLACE);
#endif
}

} // namespace biglybtc::platform
