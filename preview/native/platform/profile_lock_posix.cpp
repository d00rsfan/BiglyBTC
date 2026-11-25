// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_lock.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::platform {
namespace {

bool ownedRegularFile(const struct stat& file) noexcept {
  return S_ISREG(file.st_mode) && file.st_nlink == 1 && file.st_uid == ::geteuid();
}

} // namespace

struct ProfileLock::Handles {
  int directory = -1;
  int lock = -1;

  ~Handles() {
    if (lock >= 0) { ::close(lock); }
    if (directory >= 0) { ::close(directory); }
  }
};

std::expected<ProfileLock, ProfileLockError> ProfileLock::acquire(const std::filesystem::path& profile) {
  std::error_code ec;
  const auto canonical = std::filesystem::canonical(profile, ec);
  if (ec) { return std::unexpected(ProfileLockError::MissingDirectory); }

  auto handles = std::make_shared<Handles>();
  handles->directory = ::open(canonical.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (handles->directory < 0) { return std::unexpected(ProfileLockError::UnsafeDirectory); }
  handles->lock = ::openat(handles->directory, ".azlock",
      O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
  if (handles->lock < 0) {
    return std::unexpected(errno == ELOOP || errno == EISDIR
        ? ProfileLockError::UnsafeLock : ProfileLockError::Io);
  }

  struct stat held{};
  if (::fstat(handles->lock, &held) != 0 || !ownedRegularFile(held)) {
    return std::unexpected(ProfileLockError::UnsafeLock);
  }
  struct flock lock{};
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  // Linux and current macOS implement OFD locks that conflict with the
  // Java POSIX lock without the close-any-descriptor release rule.
  if (::fcntl(handles->lock, F_OFD_SETLK, &lock) != 0) {
    return std::unexpected(errno == EAGAIN || errno == EACCES
        ? ProfileLockError::Busy : ProfileLockError::Unavailable);
  }
  ProfileLock result(std::move(handles));
  return result.retain();
}

std::expected<ProfileLock, ProfileLockError> ProfileLock::retain() const {
  if (!valid()) { return std::unexpected(ProfileLockError::Changed); }
  struct stat directory{}, held{}, named{};
  if (::fstat(handles_->directory, &directory) != 0 || !S_ISDIR(directory.st_mode)
      || ::fstat(handles_->lock, &held) != 0 || !ownedRegularFile(held)
      || ::fstatat(handles_->directory, ".azlock", &named, AT_SYMLINK_NOFOLLOW) != 0
      || !S_ISREG(named.st_mode) || named.st_dev != held.st_dev || named.st_ino != held.st_ino) {
    return std::unexpected(ProfileLockError::Changed);
  }
  return ProfileLock(handles_);
}

int ProfileLock::directoryHandle() const noexcept {
  return handles_ ? handles_->directory : -1;
}

} // namespace biglybtc::platform
