// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile.hpp"

namespace biglybtc::storage {
namespace {
std::unexpected<Error> error(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}
} // namespace

std::expected<ProfileLease, Error> ProfileLease::acquire(const std::filesystem::path& profile) {
  auto lock = platform::ProfileLock::acquire(profile);
  if (!lock) {
    switch (lock.error()) {
      case platform::ProfileLockError::MissingDirectory:
        return error(ErrorCode::Missing, "Profile directory is unavailable");
      case platform::ProfileLockError::UnsafeDirectory:
        return error(ErrorCode::UnsafePath, "Profile directory cannot be opened safely");
      case platform::ProfileLockError::UnsafeLock:
        return error(ErrorCode::UnsafePath, "Profile lock must be an owned regular file");
      case platform::ProfileLockError::Busy:
        return error(ErrorCode::Busy, "Profile is already in use");
      case platform::ProfileLockError::Unavailable:
        return error(ErrorCode::Unsupported, "Compatible profile locking is unavailable");
      case platform::ProfileLockError::Changed:
        return error(ErrorCode::Conflict, "Held profile lock pathname changed");
      case platform::ProfileLockError::Io:
        return error(ErrorCode::Io, "Profile lock cannot be opened safely");
    }
    return error(ErrorCode::Io, "Profile lock cannot be opened safely");
  }
  return ProfileLease(std::move(*lock), std::make_shared<ProfileSourceState>());
}

ProfileLease::ProfileLease(ProfileLease&&) noexcept = default;
ProfileLease& ProfileLease::operator=(ProfileLease&&) noexcept = default;
ProfileLease::~ProfileLease() = default;

std::expected<std::shared_ptr<const ProfileLease>, Error> ProfileLease::retain() const {
  if (!valid() || !ownership_) { return error(ErrorCode::Conflict, "Profile lease is no longer held"); }
  auto lock = lock_.retain();
  if (!lock) { return error(ErrorCode::Conflict, "Held profile lock pathname changed"); }
  return std::shared_ptr<const ProfileLease>(new ProfileLease(std::move(*lock), ownership_));
}

} // namespace biglybtc::storage
