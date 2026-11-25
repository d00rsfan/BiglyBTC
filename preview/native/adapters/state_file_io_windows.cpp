// SPDX-License-Identifier: GPL-2.0-or-later
#include "state_file_io.hpp"
#include "../platform/windows_relative_file.hpp"

#include <algorithm>
#include <limits>

namespace biglybtc::storage::detail {
namespace {
namespace win = platform::detail;
constexpr ULONG InspectSharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
constexpr std::int64_t WindowsEpoch = 116444736000000000LL;
constexpr std::int64_t TicksPerSecond = 10000000;

std::unexpected<Error> error(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}

ErrorCode openError(DWORD code) {
  switch (code) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return ErrorCode::Missing;
    case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS: case ERROR_SHARING_VIOLATION: return ErrorCode::Conflict;
    case ERROR_INVALID_NAME: case ERROR_CANT_ACCESS_FILE: case ERROR_DIRECTORY: return ErrorCode::UnsafePath;
    default: return ErrorCode::Io;
  }
}

std::wstring wideName(std::string_view leaf) {
  // The state schema uses only ASCII torrent hashes and fixed suffixes.
  for (const unsigned char ch : leaf) {
    if (ch > 127) { return {}; }
  }
  return std::wstring(leaf.begin(), leaf.end());
}

timespec timestamp(LARGE_INTEGER time) noexcept {
  auto seconds = (time.QuadPart - WindowsEpoch) / TicksPerSecond;
  auto ticks = (time.QuadPart - WindowsEpoch) % TicksPerSecond;
  if (ticks < 0) { --seconds; ticks += TicksPerSecond; }
  return {static_cast<time_t>(seconds), static_cast<long>(ticks * 100)};
}

std::expected<StateDescriptor, Error> open(StateHandle dir, const std::string& name,
    ACCESS_MASK rights, ULONG sharing, ULONG disposition, ULONG options) {
  auto result = win::openRelativeFile(dir, wideName(name), rights, sharing, disposition, options);
  if (!result) { return error(openError(result.error()), "Unable to open the relative saved-state entry"); }
  return StateDescriptor(*result);
}

std::expected<void, Error> safeStateFile(StateHandle file) {
  auto info = stateStamp(file);
  if (!info) { return std::unexpected(info.error()); }
  if (!info->regular || !info->owned || info->links != 1) {
    return error(ErrorCode::UnsafePath, "Saved-state entry must be an owned, single-link regular file");
  }
  return {};
}
} // namespace

StateDescriptor& StateDescriptor::operator=(StateDescriptor&& other) noexcept {
  if (this != &other) {
    if (handle_) { ::CloseHandle(handle_); }
    handle_ = other.release();
  }
  return *this;
}

StateDescriptor::~StateDescriptor() {
  if (handle_) { ::CloseHandle(handle_); }
}

std::expected<StateDescriptor, Error> openStateDirectory(StateHandle parent, const std::string& name) {
  auto directory = open(parent, name, FILE_GENERIC_READ | FILE_TRAVERSE, FILE_SHARE_READ | FILE_SHARE_WRITE,
      win::OpenExisting, win::DirectoryFile);
  if (!directory) { return std::unexpected(directory.error()); }
  const auto info = stateStamp(directory->get());
  if (!info || !info->directory || !info->owned) {
    return error(ErrorCode::UnsafePath, "Cannot open original active-state directory safely");
  }
  return directory;
}

std::expected<StateDescriptor, Error> openStateFile(StateHandle parent, const std::string& name) {
  auto file = open(parent, name, FILE_GENERIC_READ, FILE_SHARE_READ, win::OpenExisting, win::NonDirectoryFile);
  if (!file) { return file; }
  auto safe = safeStateFile(file->get());
  if (!safe) { return std::unexpected(safe.error()); }
  return file;
}

std::expected<StateStamp, Error> stateStamp(StateHandle file) {
  BY_HANDLE_FILE_INFORMATION info{};
  FILE_BASIC_INFO basic{};
  if (!file || ::GetFileType(file) != FILE_TYPE_DISK || !::GetFileInformationByHandle(file, &info)
      || !::GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic))
      || basic.LastWriteTime.QuadPart < 0 || basic.ChangeTime.QuadPart < 0) {
    return error(ErrorCode::Io, "Cannot inspect held saved-state entry");
  }
  const bool reparse = (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
  const bool directory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  const auto size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
  if (size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return error(ErrorCode::InvalidFormat, "Saved-state entry exceeds the supported size");
  }
  return StateStamp{info.dwVolumeSerialNumber, (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow,
      info.nNumberOfLinks, 0, 0, static_cast<std::int64_t>(size), info.dwFileAttributes,
      timestamp(basic.LastWriteTime), timestamp(basic.ChangeTime), !directory && !reparse,
      directory && !reparse, win::ownedByCurrentUser(file)};
}

std::expected<std::optional<StateStamp>, Error> stateStampAt(StateHandle directory, const std::string& name) {
  auto file = open(directory, name, FILE_READ_ATTRIBUTES | READ_CONTROL, InspectSharing, win::OpenExisting, 0);
  if (!file && file.error().code == ErrorCode::Missing) { return std::nullopt; }
  if (!file) { return std::unexpected(file.error()); }
  auto info = stateStamp(file->get());
  if (!info) { return std::unexpected(info.error()); }
  return std::optional{*info};
}

std::ptrdiff_t readStateAt(StateHandle file, char* data, std::size_t bytes, std::uint64_t offset) {
  LARGE_INTEGER position{};
  position.QuadPart = static_cast<std::int64_t>(offset);
  if (!::SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) { return -1; }
  DWORD count = 0;
  const auto limit = static_cast<DWORD>(std::min<std::size_t>(bytes, 1024 * 1024));
  if (!::ReadFile(file, data, limit, &count, nullptr)) { return -1; }
  return static_cast<std::ptrdiff_t>(count);
}

std::expected<std::optional<std::string>, Error> readStateFile(
    StateHandle parent, const std::string& name, std::size_t maximum) {
  auto file = openStateFile(parent, name);
  if (!file && file.error().code == ErrorCode::Missing) { return std::nullopt; }
  if (!file) { return std::unexpected(file.error()); }
  auto before = stateStamp(file->get());
  if (!before) { return std::unexpected(before.error()); }
  if (before->size < 0 || static_cast<std::uint64_t>(before->size) > maximum) {
    return error(ErrorCode::InvalidFormat, "Saved-state entry exceeds the input limit");
  }
  std::string bytes(static_cast<std::size_t>(before->size), '\0');
  for (std::size_t offset = 0; offset < bytes.size();) {
    const auto count = readStateAt(file->get(), bytes.data() + offset, bytes.size() - offset, offset);
    if (count <= 0) { return error(ErrorCode::Io, "Saved-state read was incomplete"); }
    offset += static_cast<std::size_t>(count);
  }
  const auto after = stateStamp(file->get());
  if (!after || !sameStateSnapshot(*before, *after)) {
    return error(ErrorCode::Conflict, "Saved-state entry changed during the read");
  }
  return std::optional{std::move(bytes)};
}

std::expected<StateDescriptor, Error> stageStateFile(StateHandle parent, const std::string& name,
    std::string_view data, unsigned) {
  auto file = open(parent, name, FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES | READ_CONTROL | DELETE,
      FILE_SHARE_READ, win::CreateExclusive, win::NonDirectoryFile | win::WriteThrough);
  if (!file) { return file; }
  auto safe = safeStateFile(file->get());
  if (!safe) { return std::unexpected(safe.error()); }
  for (std::size_t offset = 0; offset < data.size();) {
    const auto bytes = static_cast<DWORD>(std::min<std::size_t>(data.size() - offset, 1024 * 1024));
    DWORD count = 0;
    if (!::WriteFile(file->get(), data.data() + offset, bytes, &count, nullptr) || count == 0) {
      FILE_DISPOSITION_INFO partial{TRUE};
      ::SetFileInformationByHandle(file->get(), FileDispositionInfo, &partial, sizeof(partial));
      return error(ErrorCode::Io, "Cannot write the complete saved-state temporary");
    }
    offset += count;
  }
  return file;
}

std::expected<void, Error> publishStateFile(StateHandle directory, const std::string& source,
    const std::string& destination, StateHandle staged) {
  StateDescriptor retained;
  if (!staged) {
    auto file = open(directory, source, FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES | READ_CONTROL | DELETE,
        FILE_SHARE_READ, win::OpenExisting, win::NonDirectoryFile | win::WriteThrough);
    if (!file) { return std::unexpected(file.error()); }
    retained = std::move(*file);
    staged = retained.get();
  }
  auto safe = safeStateFile(staged);
  if (!safe) { return safe; }
  const auto opened = stateStamp(staged);
  const auto named = stateStampAt(directory, source);
  if (!opened || !named || !*named || !sameStateIdentity(*opened, **named)) {
    return error(ErrorCode::Conflict, "Saved-state temporary was replaced");
  }
  const auto target = stateStampAt(directory, destination);
  if (!target) { return std::unexpected(target.error()); }
  if (*target && (!(*target)->regular || !(*target)->owned || (*target)->links != 1)) {
    return error(ErrorCode::UnsafePath, "Cannot replace an unsafe saved-state destination");
  }
  const auto published = win::renameRelativeFile(staged, directory, wideName(destination), target->has_value());
  if (!published) {
    return error(openError(published.error()), "Cannot publish saved-state entry");
  }
  return syncStateFile(staged);
}

std::expected<void, Error> syncStateFile(StateHandle file) {
  if (!::FlushFileBuffers(file)) { return error(ErrorCode::Io, "Cannot synchronize saved-state file"); }
  return {};
}

std::expected<void, Error> syncStateDirectory(StateHandle) {
  // The Windows publication boundary flushes the renamed file by its held
  // handle. The separate directory fence belongs to the POSIX backend.
  return {};
}

std::expected<void, Error> setStateMtime(StateHandle file, timespec modified) {
  if (modified.tv_sec < -WindowsEpoch / TicksPerSecond ||
      modified.tv_sec >= (std::numeric_limits<std::int64_t>::max() - WindowsEpoch) / TicksPerSecond ||
      modified.tv_nsec < 0 || modified.tv_nsec >= 1000000000) {
    return error(ErrorCode::InvalidFormat, "Original resume timestamp is out of range");
  }
  ULARGE_INTEGER ticks{};
  ticks.QuadPart = static_cast<std::uint64_t>(WindowsEpoch + modified.tv_sec * TicksPerSecond + modified.tv_nsec / 100);
  const FILETIME value{ticks.LowPart, ticks.HighPart};
  if (!::SetFileTime(file, nullptr, nullptr, &value)) {
    return error(ErrorCode::Io, "Cannot retain original resume timestamp");
  }
  return {};
}

unsigned stateWriteMode(const StateStamp&) noexcept { return 0; }
} // namespace biglybtc::storage::detail
