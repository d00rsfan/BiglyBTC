// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_config_native.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <memory>

namespace biglybtc::storage::detail {
namespace {

struct CloseFile {
  void operator()(HANDLE file) const noexcept { ::CloseHandle(file); }
};
using File = std::unique_ptr<void, CloseFile>;

std::unexpected<Error> error(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}

std::expected<std::wstring, Error> directoryPath(HANDLE directory) {
  // The volume path also binds the directory to the original disk if its
  // drive letter is changed. Network shares have no volume GUID.
  constexpr DWORD options[] = {FILE_NAME_NORMALIZED | VOLUME_NAME_GUID,
                               FILE_NAME_NORMALIZED | VOLUME_NAME_DOS};
  for (const auto flags : options) {
    const auto size = ::GetFinalPathNameByHandleW(directory, nullptr, 0, flags);
    if (!size || size > 32768) { continue; }
    std::wstring path(size, L'\0');
    const auto actual = ::GetFinalPathNameByHandleW(directory, path.data(), size, flags);
    if (!actual || actual >= size) { continue; }
    path.resize(actual);
    if (!path.ends_with(L'\\')) { path += L'\\'; }
    return path;
  }
  return error(ErrorCode::Io, "Unable to resolve held profile directory");
}

bool readMetadata(HANDLE file, FILE_BASIC_INFO& metadata, LARGE_INTEGER& size) {
  return ::GetFileInformationByHandleEx(file, FileBasicInfo, &metadata, static_cast<DWORD>(sizeof(metadata)))
      && ::GetFileSizeEx(file, &size);
}

} // namespace

std::expected<ConfigDirectoryIdentity, Error> profileConfigDirectory(const ProfileLease& lease) {
  const auto retained = lease.retain();
  if (!retained) { return std::unexpected(retained.error()); }
  BY_HANDLE_FILE_INFORMATION info{};
  if (!::GetFileInformationByHandle((*retained)->directoryHandle(), &info)) {
    return error(ErrorCode::Io, "Unable to identify profile directory");
  }
  return ConfigDirectoryIdentity{info.dwVolumeSerialNumber,
      (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow};
}

std::expected<std::optional<std::string>, Error> readProfileConfig(const ProfileLease& lease,
    const std::string& filename, std::size_t maximum) {
  const auto retained = lease.retain();
  if (!retained) { return std::unexpected(retained.error()); }
  auto path = directoryPath((*retained)->directoryHandle());
  if (!path) { return std::unexpected(path.error()); }
  path->append(filename.begin(), filename.end());
  const auto raw = ::CreateFileW(path->c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (raw == INVALID_HANDLE_VALUE) {
    if (::GetLastError() == ERROR_FILE_NOT_FOUND) { return std::nullopt; }
    return error(ErrorCode::Io, "Unable to open configuration safely");
  }

  const File file(raw);
  FILE_BASIC_INFO before{};
  LARGE_INTEGER size{};
  if (::GetFileType(file.get()) != FILE_TYPE_DISK || !readMetadata(file.get(), before, size)
      || (before.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
    return error(ErrorCode::UnsafePath, "Configuration must be a regular file");
  }
  if (size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) > maximum) {
    return error(ErrorCode::InvalidFormat, "Configuration exceeds the selected input limit");
  }
  std::string data(static_cast<std::size_t>(size.QuadPart), '\0');
  for (std::size_t offset = 0; offset < data.size();) {
    const auto requested = static_cast<DWORD>(std::min<std::size_t>(data.size() - offset, 1024 * 1024));
    DWORD processed = 0;
    if (!::ReadFile(file.get(), data.data() + offset, requested, &processed, nullptr) || !processed) {
      return error(ErrorCode::Io, "Configuration read was incomplete");
    }
    offset += processed;
  }
  FILE_BASIC_INFO after{};
  LARGE_INTEGER newSize{};
  if (!readMetadata(file.get(), after, newSize) || newSize.QuadPart != size.QuadPart
      || after.LastWriteTime.QuadPart != before.LastWriteTime.QuadPart
      || after.ChangeTime.QuadPart != before.ChangeTime.QuadPart || !(**retained).retain()) {
    return error(ErrorCode::Conflict, "Configuration changed while being read");
  }
  return std::optional<std::string>(std::move(data));
}

} // namespace biglybtc::storage::detail
