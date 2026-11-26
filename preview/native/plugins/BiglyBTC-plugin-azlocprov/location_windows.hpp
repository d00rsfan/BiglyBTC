// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "../../platform/windows_relative_file.hpp"
#include "../../platform/utf8_path.hpp"
#include <fcntl.h>
#include <io.h>
#include <filesystem>

namespace biglybtc::location::windows {
namespace win = platform::detail;

inline bool systemOwner(PSID owner) {
  return ::IsWellKnownSid(owner, WinLocalSystemSid) || ::IsWellKnownSid(owner, WinBuiltinAdministratorsSid);
}

inline bool trustedAsset(HANDLE file, bool allowSystem) {
  PSID owner = nullptr;
  PACL permissions = nullptr;
  PSECURITY_DESCRIPTOR raw = nullptr;
  const auto read = ::GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
      &owner, nullptr, &permissions, nullptr, &raw);
  const std::unique_ptr<void, decltype(&::LocalFree)> descriptor(raw, ::LocalFree);
  if (read != ERROR_SUCCESS || !owner || !permissions
      || (!win::ownedByCurrentUser(file) && !(allowSystem && systemOwner(owner)))) { return false; }
  constexpr DWORD write = GENERIC_ALL | GENERIC_WRITE | DELETE | WRITE_DAC | WRITE_OWNER
      | FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | FILE_DELETE_CHILD;
  for (DWORD index = 0; index < permissions->AceCount; ++index) {
    void* item = nullptr;
    if (!::GetAce(permissions, index, &item)) { return false; }
    const auto* header = static_cast<const ACE_HEADER*>(item);
    if (header->AceType == ACCESS_DENIED_ACE_TYPE || (header->AceFlags & INHERIT_ONLY_ACE)) { continue; }
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) { return false; }
    const auto* allow = static_cast<const ACCESS_ALLOWED_ACE*>(item);
    auto sid = const_cast<DWORD*>(&allow->SidStart);
    if ((allow->Mask & write) && !::EqualSid(owner, sid) && !systemOwner(sid)) { return false; }
  }
  return true;
}

inline bool shape(HANDLE file, bool directory, BY_HANDLE_FILE_INFORMATION& info) {
  return file && ::GetFileType(file) == FILE_TYPE_DISK && ::GetFileInformationByHandle(file, &info)
      && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
      && bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory
      && (directory ? info.nNumberOfLinks != 0 : info.nNumberOfLinks == 1);
}

inline std::uint64_t size(const BY_HANDLE_FILE_INFORMATION& info) {
  return (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}

inline HANDLE directoryHandle(int fd) {
  if (fd < 0) { return nullptr; }
  const auto raw = ::_get_osfhandle(fd);
  return raw == -1 ? nullptr : reinterpret_cast<HANDLE>(raw);
}

inline std::filesystem::path resolvedPath(HANDLE file) {
  constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
  const auto size = ::GetFinalPathNameByHandleW(file, nullptr, 0, flags);
  if (!size || size > 32768) { return {}; }
  std::wstring value(size, L'\0');
  const auto actual = ::GetFinalPathNameByHandleW(file, value.data(), size, flags);
  if (!actual || actual >= size) { return {}; }
  value.resize(actual);
  return std::filesystem::path(std::move(value));
}
} // namespace biglybtc::location::windows
