// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "file_operation_io.hpp"
#include "../platform/posix_profile_files.hpp"
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::storage::fileop {

// Conversion for callers that still perform their own POSIX file operations.
inline FileStamp fileStampFromPosix(const struct stat& value) {
  return {static_cast<std::uint64_t>(value.st_dev), static_cast<std::uint64_t>(value.st_ino),
      static_cast<std::uint64_t>(value.st_nlink), value.st_uid, value.st_gid, value.st_size,
      static_cast<unsigned>(value.st_mode), platform::modificationTime(value), platform::changeTime(value),
      S_ISREG(value.st_mode), S_ISDIR(value.st_mode), value.st_uid == ::geteuid()};
}

inline struct stat posixFileStamp(const FileStamp& value) {
  struct stat result{};
  result.st_dev = static_cast<dev_t>(value.device);
  result.st_ino = static_cast<ino_t>(value.inode);
  result.st_nlink = static_cast<nlink_t>(value.links);
  result.st_uid = static_cast<uid_t>(value.owner);
  result.st_gid = static_cast<gid_t>(value.group);
  result.st_mode = static_cast<mode_t>(value.mode);
  result.st_size = value.size;
  platform::setTimestamps(result, value.modified, value.changed);
  return result;
}

inline bool identity(const struct stat& a, const struct stat& b) {
  return identity(fileStampFromPosix(a), fileStampFromPosix(b));
}

inline bool unchanged(const struct stat& a, const struct stat& b) {
  return unchanged(fileStampFromPosix(a), fileStampFromPosix(b));
}

inline bool unchanged(const struct stat& a, const FileStamp& b) { return unchanged(fileStampFromPosix(a), b); }

} // namespace biglybtc::storage::fileop
