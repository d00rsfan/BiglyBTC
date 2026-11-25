// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_payload_io.hpp"

#include <bit>
#include <limits>

#ifdef _WIN32
#include "../platform/windows_file_owner.hpp"
#include <winioctl.h>
#include <winternl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace biglybtc::storage::detail {

#ifdef _WIN32
namespace {
bool seek(OriginalPayloadHandle file, std::uint64_t offset) noexcept {
  LARGE_INTEGER position{};
  position.QuadPart = static_cast<std::int64_t>(offset);
  return ::SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != FALSE;
}

std::optional<ACCESS_MASK> grantedAccess(OriginalPayloadHandle file) noexcept {
  using QueryObject = NTSTATUS (NTAPI*)(HANDLE, OBJECT_INFORMATION_CLASS, PVOID, ULONG, PULONG);
  static const auto query = [] {
    const auto module = ::GetModuleHandleW(L"ntdll.dll");
    const auto function = module ? ::GetProcAddress(module, "NtQueryObject") : nullptr;
    return std::bit_cast<QueryObject>(function);
  }();
  PUBLIC_OBJECT_BASIC_INFORMATION info{};
  if (!query || query(file, ObjectBasicInformation, &info, static_cast<ULONG>(sizeof(info)), nullptr) < 0) {
    return std::nullopt;
  }
  return info.GrantedAccess;
}
} // namespace

void CloseOriginalPayload::operator()(OriginalPayloadHandle file) const noexcept {
  ::CloseHandle(file);
}

std::uint64_t originalPayloadMaximumOffset() noexcept {
  return static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}

bool statOriginalPayload(OriginalPayloadHandle file, OriginalPayloadStat& result) noexcept {
  BY_HANDLE_FILE_INFORMATION identity{};
  FILE_BASIC_INFO times{};
  LARGE_INTEGER size{};
  if (file == nullptr || ::GetFileType(file) != FILE_TYPE_DISK
      || !::GetFileInformationByHandle(file, &identity)
      || (identity.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
      || !::GetFileInformationByHandleEx(file, FileBasicInfo, &times, static_cast<DWORD>(sizeof(times)))
      || !::GetFileSizeEx(file, &size) || size.QuadPart < 0) {
    return false;
  }
  result = {identity.dwVolumeSerialNumber,
      (std::uint64_t(identity.nFileIndexHigh) << 32) | identity.nFileIndexLow,
      static_cast<std::uint64_t>(size.QuadPart), identity.nNumberOfLinks,
      identity.dwFileAttributes, 0, times.LastWriteTime.QuadPart, 0, times.ChangeTime.QuadPart, 0};
  return true;
}

bool canWriteOriginalPayload(OriginalPayloadHandle file) noexcept {
  const auto access = grantedAccess(file);
  return access && (*access & FILE_WRITE_DATA);
}

bool isOwnedReadOnlyOriginalPayload(OriginalPayloadHandle file) {
  const auto access = grantedAccess(file);
  return access && (*access & FILE_READ_DATA) && !(*access & (FILE_WRITE_DATA | FILE_APPEND_DATA))
      && platform::detail::ownedByCurrentUser(file);
}

std::ptrdiff_t readOriginalPayload(OriginalPayloadHandle file, std::uint64_t offset,
    std::span<std::byte> output) noexcept {
  if (!seek(file, offset)) { return -1; }
  DWORD processed = 0;
  if (!::ReadFile(file, output.data(), static_cast<DWORD>(output.size()), &processed, nullptr)) {
    return ::GetLastError() == ERROR_HANDLE_EOF ? 0 : -1;
  }
  return static_cast<std::ptrdiff_t>(processed);
}

std::ptrdiff_t writeOriginalPayload(OriginalPayloadHandle file, std::uint64_t offset,
    std::span<const std::byte> input) noexcept {
  if (!seek(file, offset)) { return -1; }
  DWORD processed = 0;
  if (!::WriteFile(file, input.data(), static_cast<DWORD>(input.size()), &processed, nullptr)) { return -1; }
  return static_cast<std::ptrdiff_t>(processed);
}

bool resizeOriginalPayload(OriginalPayloadHandle file, std::uint64_t length) noexcept {
  FILE_END_OF_FILE_INFO end{};
  end.EndOfFile.QuadPart = static_cast<std::int64_t>(length);
  return ::SetFileInformationByHandle(file, FileEndOfFileInfo, &end, static_cast<DWORD>(sizeof(end))) != FALSE;
}

bool syncOriginalPayload(OriginalPayloadHandle file) noexcept { return ::FlushFileBuffers(file) != FALSE; }

std::expected<void, Error> enableSparseOriginalPayload(OriginalPayloadHandle file) {
  DWORD flags = 0;
  if (!::GetVolumeInformationByHandleW(file, nullptr, 0, nullptr, nullptr, &flags, nullptr, 0)) {
    return std::unexpected(Error{ErrorCode::Io, "Cannot inspect sparse support for the payload volume"});
  }
  if (!(flags & FILE_SUPPORTS_SPARSE_FILES)) {
    return std::unexpected(Error{ErrorCode::Unsupported, "The payload volume does not support sparse files"});
  }
  DWORD processed = 0;
  if (!::DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &processed, nullptr)) {
    return std::unexpected(Error{ErrorCode::Io, "Cannot enable sparse allocation on the payload"});
  }
  return {};
}

#else

std::uint64_t originalPayloadMaximumOffset() noexcept {
  return static_cast<std::uint64_t>(std::numeric_limits<off_t>::max());
}

bool statOriginalPayload(OriginalPayloadHandle file, OriginalPayloadStat& result) noexcept {
  struct stat info{};
  if (::fstat(file, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid() || info.st_size < 0) {
    return false;
  }
#ifdef __APPLE__
  const auto& modified = info.st_mtimespec;
  const auto& changed = info.st_ctimespec;
#else
  const auto& modified = info.st_mtim;
  const auto& changed = info.st_ctim;
#endif
  result = {static_cast<std::uint64_t>(info.st_dev), static_cast<std::uint64_t>(info.st_ino),
      static_cast<std::uint64_t>(info.st_size), static_cast<std::uint64_t>(info.st_nlink),
      static_cast<std::uint64_t>(info.st_mode), static_cast<std::uint64_t>(info.st_uid),
      modified.tv_sec, modified.tv_nsec, changed.tv_sec, changed.tv_nsec};
  return true;
}

bool canWriteOriginalPayload(OriginalPayloadHandle file) noexcept {
  const auto flags = ::fcntl(file, F_GETFL);
  return flags >= 0 && (flags & O_ACCMODE) != O_RDONLY;
}

bool isOwnedReadOnlyOriginalPayload(OriginalPayloadHandle file) {
  // statOriginalPayload separately verifies the effective user owns the file.
  const auto flags = ::fcntl(file, F_GETFL);
  return flags >= 0 && (flags & O_ACCMODE) == O_RDONLY;
}

std::ptrdiff_t readOriginalPayload(OriginalPayloadHandle file, std::uint64_t offset,
    std::span<std::byte> output) noexcept {
  std::ptrdiff_t count;
  do {
    count = ::pread(file, output.data(), output.size(), static_cast<off_t>(offset));
  } while (count < 0 && errno == EINTR);
  return count;
}

std::ptrdiff_t writeOriginalPayload(OriginalPayloadHandle file, std::uint64_t offset,
    std::span<const std::byte> input) noexcept {
  std::ptrdiff_t count;
  do {
    count = ::pwrite(file, input.data(), input.size(), static_cast<off_t>(offset));
  } while (count < 0 && errno == EINTR);
  return count;
}

bool resizeOriginalPayload(OriginalPayloadHandle file, std::uint64_t length) noexcept {
  return ::ftruncate(file, static_cast<off_t>(length)) == 0;
}

bool syncOriginalPayload(OriginalPayloadHandle file) noexcept { return ::fsync(file) == 0; }

std::expected<void, Error> enableSparseOriginalPayload(OriginalPayloadHandle) { return {}; }

#endif

} // namespace biglybtc::storage::detail
