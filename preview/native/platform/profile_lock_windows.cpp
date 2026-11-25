// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_lock.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>

#include <vector>

namespace biglybtc::platform {
namespace {

struct CloseHandleOnExit {
  void operator()(HANDLE handle) const noexcept { ::CloseHandle(handle); }
};
using Handle = std::unique_ptr<void, CloseHandleOnExit>;

Handle openPath(const std::filesystem::path& path, DWORD access, DWORD disposition) noexcept {
  // Keep the directory and lock name from being replaced while held. Sharing
  // reads and writes allows the Java client to report the byte-lock conflict.
  HANDLE file = ::CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr, disposition, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  return Handle(file == INVALID_HANDLE_VALUE ? nullptr : file);
}

bool safeDirectory(HANDLE handle, BY_HANDLE_FILE_INFORMATION& info) noexcept {
  if (!::GetFileInformationByHandle(handle, &info)) { return false; }
  const auto type = info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT);
  return type == FILE_ATTRIBUTE_DIRECTORY;
}

bool safeLock(HANDLE handle, BY_HANDLE_FILE_INFORMATION& info) noexcept {
  return ::GetFileType(handle) == FILE_TYPE_DISK && ::GetFileInformationByHandle(handle, &info)
      && !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
      && info.nNumberOfLinks == 1;
}

bool sameFile(const BY_HANDLE_FILE_INFORMATION& left, const BY_HANDLE_FILE_INFORMATION& right) noexcept {
  return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber
      && left.nFileIndexHigh == right.nFileIndexHigh && left.nFileIndexLow == right.nFileIndexLow;
}

bool ownedByCurrentUser(HANDLE file) {
  HANDLE rawToken = nullptr;
  if (!::OpenThreadToken(::GetCurrentThread(), TOKEN_QUERY, TRUE, &rawToken)
      && (::GetLastError() != ERROR_NO_TOKEN
          || !::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &rawToken))) {
    return false;
  }
  const Handle token(rawToken);
  PSID owner = nullptr;
  PSECURITY_DESCRIPTOR rawDescriptor = nullptr;
  const auto status = ::GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
      &owner, nullptr, nullptr, nullptr, &rawDescriptor);
  const std::unique_ptr<void, decltype(&::LocalFree)> descriptor(rawDescriptor, ::LocalFree);
  if (status != ERROR_SUCCESS || !owner) { return false; }

  const auto matches = [&](TOKEN_INFORMATION_CLASS type, auto sid) {
    DWORD bytes = 0;
    ::GetTokenInformation(token.get(), type, nullptr, 0, &bytes);
    if (!bytes) { return false; }
    std::vector<unsigned char> result(bytes);
    return ::GetTokenInformation(token.get(), type, result.data(), bytes, &bytes)
        && ::EqualSid(owner, sid(result.data()));
  };
  return matches(TokenUser, [](const void* info) { return static_cast<const TOKEN_USER*>(info)->User.Sid; })
      || matches(TokenOwner, [](const void* info) { return static_cast<const TOKEN_OWNER*>(info)->Owner; });
}

bool contention(DWORD error) noexcept {
  return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING;
}

} // namespace

struct ProfileLock::Handles {
  std::filesystem::path path;
  Handle directory;
  Handle lock;
  bool locked = false;

  ~Handles() {
    if (locked) {
      OVERLAPPED range{};
      ::UnlockFileEx(lock.get(), 0, 0xffffffffUL, 0x7fffffffUL, &range);
    }
  }
};

std::expected<ProfileLock, ProfileLockError> ProfileLock::acquire(const std::filesystem::path& profile) {
  std::error_code ec;
  auto canonical = std::filesystem::canonical(profile, ec);
  if (ec) { return std::unexpected(ProfileLockError::MissingDirectory); }

  auto handles = std::make_shared<Handles>();
  handles->path = std::move(canonical);
  handles->directory = openPath(handles->path, FILE_READ_ATTRIBUTES, OPEN_EXISTING);
  BY_HANDLE_FILE_INFORMATION directory{};
  if (!handles->directory || !safeDirectory(handles->directory.get(), directory)) {
    return std::unexpected(ProfileLockError::UnsafeDirectory);
  }

  handles->lock = openPath(handles->path / L".azlock", GENERIC_READ | GENERIC_WRITE | READ_CONTROL, OPEN_ALWAYS);
  if (!handles->lock) {
    return std::unexpected(contention(::GetLastError()) ? ProfileLockError::Busy : ProfileLockError::Io);
  }
  BY_HANDLE_FILE_INFORMATION held{};
  if (!safeLock(handles->lock.get(), held) || !ownedByCurrentUser(handles->lock.get())) {
    return std::unexpected(ProfileLockError::UnsafeLock);
  }

  // FileChannel.tryLock() reserves Long.MAX_VALUE bytes from offset zero.
  OVERLAPPED range{};
  if (!::LockFileEx(handles->lock.get(), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
      0, 0xffffffffUL, 0x7fffffffUL, &range)) {
    return std::unexpected(contention(::GetLastError()) ? ProfileLockError::Busy : ProfileLockError::Unavailable);
  }
  handles->locked = true;
  ProfileLock result(std::move(handles));
  return result.retain();
}

std::expected<ProfileLock, ProfileLockError> ProfileLock::retain() const {
  if (!valid()) { return std::unexpected(ProfileLockError::Changed); }
  const auto directory = openPath(handles_->path, FILE_READ_ATTRIBUTES, OPEN_EXISTING);
  const auto lock = openPath(handles_->path / L".azlock", FILE_READ_ATTRIBUTES, OPEN_EXISTING);
  BY_HANDLE_FILE_INFORMATION heldDirectory{}, namedDirectory{}, heldLock{}, namedLock{};
  if (!directory || !lock || !safeDirectory(handles_->directory.get(), heldDirectory)
      || !safeDirectory(directory.get(), namedDirectory) || !sameFile(heldDirectory, namedDirectory)
      || !safeLock(handles_->lock.get(), heldLock) || !safeLock(lock.get(), namedLock)
      || !sameFile(heldLock, namedLock)) {
    return std::unexpected(ProfileLockError::Changed);
  }
  return ProfileLock(handles_);
}

} // namespace biglybtc::platform
