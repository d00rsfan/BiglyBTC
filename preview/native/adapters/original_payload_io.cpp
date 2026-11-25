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

namespace {
bool validLinkCount(OriginalPayloadAccess access, unsigned links) {
  return links == 1 || (links == 2 && access != OriginalPayloadAccess::CreateExclusive);
}
} // namespace

#ifdef _WIN32
namespace {
OVERLAPPED positioned(std::uint64_t offset) noexcept {
  OVERLAPPED position{};
  position.Offset = static_cast<DWORD>(offset);
  position.OffsetHigh = static_cast<DWORD>(offset >> 32);
  return position;
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

std::expected<OriginalPayloadDescriptor, Error> openOriginalPayloadAt(OriginalPayloadHandle directory,
    const std::filesystem::path& leaf, OriginalPayloadAccess access, unsigned authenticatedLinks,
    bool allowNameRemoval) {
  if (!validLinkCount(access, authenticatedLinks)) {
    return std::unexpected(Error{ErrorCode::UnsafePath, "Invalid authenticated payload link count"});
  }
  const auto& name = leaf.native();
  if (name.empty() || name.size() > 255 || name == L"." || name == L".."
      || name.find_first_of(L"\\/:*?\"<>|") != name.npos
      || name.find(L'\0') != name.npos || name.back() == L'.' || name.back() == L' ') {
    return std::unexpected(Error{ErrorCode::UnsafePath, "Payload name must be one ordinary file component"});
  }
  const bool create = access == OriginalPayloadAccess::CreateExclusive;
  BY_HANDLE_FILE_INFORMATION parent{};
  if (!::GetFileInformationByHandle(directory, &parent) || !(parent.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      || (parent.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
      || (create && !platform::detail::ownedByCurrentUser(directory))) {
    return std::unexpected(Error{ErrorCode::UnsafePath, "Payload parent handle is unsafe"});
  }
  // NtCreateFile resolves the single Unicode component against the held directory.
  // The native create options below are defined by the Windows DDK.
  constexpr ULONG Open = 1, Create = 2;
  constexpr ULONG SynchronousIo = 0x20, NonDirectoryFile = 0x40, OpenReparsePoint = 0x00200000;
  using CreateFileAt = decltype(&::NtCreateFile);
  using ErrorFromStatus = ULONG (NTAPI*)(NTSTATUS);
  const auto ntdll = ::GetModuleHandleW(L"ntdll.dll");
  const auto open = ntdll ? std::bit_cast<CreateFileAt>(::GetProcAddress(ntdll, "NtCreateFile")) : nullptr;
  const auto errorFromStatus = ntdll ? std::bit_cast<ErrorFromStatus>(::GetProcAddress(ntdll, "RtlNtStatusToDosError")) : nullptr;
  if (!open || !errorFromStatus) {
    return std::unexpected(Error{ErrorCode::Unsupported, "Relative payload opens are unavailable"});
  }
  const auto length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
  UNICODE_STRING relative{length, length, const_cast<wchar_t*>(name.data())};
  OBJECT_ATTRIBUTES attributes{};
  attributes.Length = sizeof(attributes);
  attributes.RootDirectory = directory;
  attributes.ObjectName = &relative;
  attributes.Attributes = OBJ_CASE_INSENSITIVE;
  IO_STATUS_BLOCK io{};
  HANDLE raw = nullptr;
  const ACCESS_MASK rights = FILE_GENERIC_READ | (access == OriginalPayloadAccess::ReadOnly ? 0 : FILE_GENERIC_WRITE);
  const ULONG sharing = FILE_SHARE_READ | (allowNameRemoval ? FILE_SHARE_DELETE : 0);
  const auto status = open(&raw, rights, &attributes, &io, nullptr, FILE_ATTRIBUTE_NORMAL,
      sharing, create ? Create : Open, SynchronousIo | NonDirectoryFile | OpenReparsePoint, nullptr, 0);
  if (status < 0) {
    const auto result = errorFromStatus(status);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) {
      return std::unexpected(Error{ErrorCode::Missing, "The payload leaf is missing"});
    }
    if (result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS || result == ERROR_SHARING_VIOLATION) {
      return std::unexpected(Error{ErrorCode::Conflict, "The payload leaf already exists or is in use"});
    }
    return std::unexpected(Error{ErrorCode::Io, "Cannot open the payload leaf relative to its directory"});
  }
  OriginalPayloadDescriptor file(raw);
  OriginalPayloadStat proof{};
  if (!statOriginalPayload(file.get(), proof) || proof.links != authenticatedLinks
      || !platform::detail::ownedByCurrentUser(file.get())) {
    return std::unexpected(Error{ErrorCode::UnsafePath, "The payload leaf failed its file type, owner or link check"});
  }
  return file;
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

bool canReadWriteOriginalPayload(OriginalPayloadHandle file) noexcept {
  constexpr ACCESS_MASK required = FILE_READ_DATA | FILE_WRITE_DATA;
  const auto access = grantedAccess(file);
  return access && (*access & required) == required;
}

bool isOwnedReadOnlyOriginalPayload(OriginalPayloadHandle file) {
  const auto access = grantedAccess(file);
  return access && (*access & FILE_READ_DATA) && !(*access & (FILE_WRITE_DATA | FILE_APPEND_DATA))
      && platform::detail::ownedByCurrentUser(file);
}

std::ptrdiff_t readOriginalPayload(OriginalPayloadHandle file, std::uint64_t offset,
    std::span<std::byte> output) noexcept {
  auto position = positioned(offset);
  DWORD processed = 0;
  if (!::ReadFile(file, output.data(), static_cast<DWORD>(output.size()), &processed, &position)) {
    return ::GetLastError() == ERROR_HANDLE_EOF ? 0 : -1;
  }
  return static_cast<std::ptrdiff_t>(processed);
}

std::ptrdiff_t writeOriginalPayload(OriginalPayloadHandle file, std::uint64_t offset,
    std::span<const std::byte> input) noexcept {
  auto position = positioned(offset);
  DWORD processed = 0;
  if (!::WriteFile(file, input.data(), static_cast<DWORD>(input.size()), &processed, &position)) { return -1; }
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

std::expected<OriginalPayloadDescriptor, Error> openOriginalPayloadAt(OriginalPayloadHandle directory,
    const std::filesystem::path& leaf, OriginalPayloadAccess access, unsigned authenticatedLinks, bool) {
  if (!validLinkCount(access, authenticatedLinks)) {
    return std::unexpected(Error{ErrorCode::UnsafePath, "Invalid authenticated payload link count"});
  }
  const auto& name = leaf.native();
  if (name.empty() || name == "." || name == ".." || name.find('/') != name.npos || name.find('\0') != name.npos) {
    return std::unexpected(Error{ErrorCode::UnsafePath, "Payload name must be one ordinary file component"});
  }
  int flags = O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
  flags |= access == OriginalPayloadAccess::ReadOnly ? O_RDONLY : O_RDWR;
  if (access == OriginalPayloadAccess::CreateExclusive) { flags |= O_CREAT | O_EXCL; }
  OriginalPayloadDescriptor file(::openat(directory, name.c_str(), flags, 0600));
  if (file.get() < 0) {
    const auto code = errno == EEXIST ? ErrorCode::Conflict : errno == ENOENT ? ErrorCode::Missing : ErrorCode::Io;
    return std::unexpected(Error{code, "Cannot open the payload leaf relative to its directory"});
  }
  OriginalPayloadStat proof{};
  if (!statOriginalPayload(file.get(), proof) || proof.links != authenticatedLinks) {
    return std::unexpected(Error{ErrorCode::UnsafePath, "The payload leaf failed its file type, owner or link check"});
  }
  return file;
}

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

bool canReadWriteOriginalPayload(OriginalPayloadHandle file) noexcept {
  const auto flags = ::fcntl(file, F_GETFL);
  return flags >= 0 && (flags & O_ACCMODE) == O_RDWR;
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
