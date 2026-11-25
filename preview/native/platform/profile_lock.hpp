// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <utility>

namespace biglybtc::platform {

enum class ProfileLockError {
  MissingDirectory,
  UnsafeDirectory,
  UnsafeLock,
  Busy,
  Unavailable,
  Changed,
  Io,
};

// Owns the same .azlock byte range as the original Java client. A retained
// reference shares the operating system lock until the last owner closes it.
class ProfileLock final {
public:
  static std::expected<ProfileLock, ProfileLockError> acquire(const std::filesystem::path&);
  ProfileLock(const ProfileLock&) = delete;
  ProfileLock& operator=(const ProfileLock&) = delete;
  ProfileLock(ProfileLock&&) noexcept = default;
  ProfileLock& operator=(ProfileLock&&) noexcept = default;

  std::expected<ProfileLock, ProfileLockError> retain() const;
  bool valid() const noexcept { return bool(handles_); }
  // A borrowed native directory handle, valid while this lease is held.
#ifdef _WIN32
  void* directoryHandle() const noexcept;
#else
  int directoryHandle() const noexcept;
#endif

private:
  struct Handles;
  explicit ProfileLock(std::shared_ptr<Handles> handles) : handles_(std::move(handles)) {}
  std::shared_ptr<Handles> handles_;
};

} // namespace biglybtc::platform
