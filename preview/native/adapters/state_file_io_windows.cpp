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

std::expected<StateDescriptor, Error> open(StateHandle dir, std::wstring_view name,
    ACCESS_MASK rights, ULONG sharing, ULONG disposition, ULONG options, PSECURITY_DESCRIPTOR security = nullptr) {
  auto result = win::openRelativeFile(dir, name, rights, sharing, disposition, options, {}, security);
  if (!result) { return error(openError(result.error()), "Unable to open the relative saved-state entry"); }
  return StateDescriptor(*result);
}

std::expected<StateDescriptor, Error> open(StateHandle dir, const std::string& name,
    ACCESS_MASK rights, ULONG sharing, ULONG disposition, ULONG options) {
  return open(dir, wideName(name), rights, sharing, disposition, options);
}

std::expected<StateDescriptor, Error> openDirectory(const std::filesystem::path& path) {
  auto name = std::filesystem::path(path).make_preferred().native();
  if (name.starts_with(L"\\\\")) { name = L"\\\\?\\UNC\\" + name.substr(2); }
  else { name = L"\\\\?\\" + name; }
  const auto opened = ::CreateFileW(name.c_str(), FILE_READ_ATTRIBUTES | FILE_TRAVERSE | READ_CONTROL,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (opened == INVALID_HANDLE_VALUE) {
    const auto status=::GetLastError();
    return std::unexpected(Error{openError(status),"Cannot open filesystem directory (Win32 error "+std::to_string(status)+')'});
  }
  StateDescriptor directory(opened);
  const auto info = stateStamp(directory.get());
  if (!info || !info->directory) { return error(ErrorCode::UnsafePath, "Filesystem directory is unsafe"); }
  return directory;
}

std::expected<StateDescriptor, Error> directoryAt(StateHandle parent, std::wstring_view name, ULONG disposition) {
  auto directory = open(parent, name, FILE_GENERIC_READ | FILE_TRAVERSE, FILE_SHARE_READ | FILE_SHARE_WRITE,
      disposition, win::DirectoryFile);
  if (!directory) { return directory; }
  const auto info = stateStamp(directory->get());
  if (!info || !info->directory) { return error(ErrorCode::UnsafePath, "Filesystem directory is unsafe"); }
  return directory;
}

std::expected<void, Error> safeStateFile(StateHandle file) {
  auto info = stateStamp(file);
  if (!info) { return std::unexpected(info.error()); }
  if (!info->regular || !info->owned || info->links != 1) {
    return error(ErrorCode::UnsafePath, "Saved-state entry must be an owned, single-link regular file");
  }
  return {};
}

std::expected<StateDescriptor, Error> createPrivateStateStream(StateHandle parent, std::wstring_view name,
    ACCESS_MASK rights, ULONG sharing, ULONG options) {
  const auto failed=[] { return error(ErrorCode::Io,"Cannot create private Windows saved-state permissions"); };
  HANDLE rawToken=nullptr;
  if(!::OpenThreadToken(::GetCurrentThread(),TOKEN_QUERY,TRUE,&rawToken)
      && (::GetLastError()!=ERROR_NO_TOKEN || !::OpenProcessToken(::GetCurrentProcess(),TOKEN_QUERY,&rawToken))){
    return failed();
  }
  const StateDescriptor token(rawToken);
  DWORD size=0;
  ::GetTokenInformation(token.get(),TokenUser,nullptr,0,&size);
  if(!size){return failed();}
  std::vector<unsigned char> user(size);
  if(!::GetTokenInformation(token.get(),TokenUser,user.data(),size,&size)){return failed();}
  const auto sid=reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid;
  EXPLICIT_ACCESS_W access{};
  access.grfAccessPermissions=FILE_ALL_ACCESS;
  access.grfAccessMode=SET_ACCESS;
  ::BuildTrusteeWithSidW(&access.Trustee,sid);
  PACL rawAcl=nullptr;
  const auto status=::SetEntriesInAclW(1,&access,nullptr,&rawAcl);
  const std::unique_ptr<void,decltype(&::LocalFree)> acl(rawAcl,::LocalFree);
  SECURITY_DESCRIPTOR descriptor{};
  if(status!=ERROR_SUCCESS||!rawAcl
      ||!::InitializeSecurityDescriptor(&descriptor,SECURITY_DESCRIPTOR_REVISION)
      ||!::SetSecurityDescriptorDacl(&descriptor,TRUE,rawAcl,FALSE)
      ||!::SetSecurityDescriptorControl(&descriptor,SE_DACL_PROTECTED,SE_DACL_PROTECTED)){return failed();}
  return open(parent,name,rights,sharing,win::CreateExclusive,options,&descriptor);
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
  auto directory = directoryAt(parent, wideName(name), win::OpenExisting);
  if (!directory) { return std::unexpected(directory.error()); }
  const auto info = stateStamp(directory->get());
  if (!info || !info->directory || !info->owned) {
    return error(ErrorCode::UnsafePath, "Cannot open original active-state directory safely");
  }
  return directory;
}

std::expected<StateDescriptor, Error> createStateDirectory(StateHandle parent, const std::string& name) {
  auto directory = directoryAt(parent, wideName(name), win::CreateExclusive);
  if (!directory) { return std::unexpected(directory.error()); }
  const auto info = stateStamp(directory->get());
  if (!info || !info->directory || !info->owned) {
    return error(ErrorCode::UnsafePath, "New original control directory is unsafe");
  }
  return directory;
}

std::expected<StateDescriptor, Error> openDirectoryAt(StateHandle parent, const std::filesystem::path& leaf) {
  return directoryAt(parent, leaf.native(), win::OpenExisting);
}

std::expected<StateDescriptor, Error> createDirectoryAt(StateHandle parent, const std::filesystem::path& leaf) {
  return directoryAt(parent, leaf.native(), win::CreateExclusive);
}

std::filesystem::path filesystemRoot(const std::filesystem::path& path) {
  if (!path.is_absolute()) { return {}; }
  const auto drive = path.root_name().native();
  if (drive.size() == 2 && drive.back() == L':'
      && ((drive.front() >= L'A' && drive.front() <= L'Z') || (drive.front() >= L'a' && drive.front() <= L'z'))) {
    return std::filesystem::path(drive + L'\\');
  }
  if (drive.starts_with(L"\\\\") && win::validRelativeLeaf(std::wstring_view(drive).substr(2))) {
    const auto relative = path.relative_path();
    if (!relative.empty() && win::validRelativeLeaf(relative.begin()->native())) {
      return std::filesystem::path(drive + L'\\' + relative.begin()->native());
    }
  }
  return {};
}

int compareFilesystemComponent(const std::filesystem::path& a, const std::filesystem::path& b) noexcept {
  const auto order = ::CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE);
  return order ? order - CSTR_EQUAL : a.compare(b);
}

bool FilesystemPathLess::operator()(const std::filesystem::path& a, const std::filesystem::path& b) const noexcept {
  auto left = a.begin();
  auto right = b.begin();
  for (; left != a.end() && right != b.end(); ++left, ++right) {
    const auto order = compareFilesystemComponent(*left, *right);
    if (order != 0) { return order < 0; }
  }
  return left == a.end() && right != b.end();
}

std::expected<StateDescriptor, Error> openFilesystemRoot(const std::filesystem::path& root) {
  if (root.empty() || filesystemRoot(root) != root) { return error(ErrorCode::UnsafePath, "Unsafe filesystem root"); }
  return openDirectory(root);
}

std::expected<StateDescriptor, Error> duplicateStateDirectory(StateHandle directory) {
  const auto process = ::GetCurrentProcess();
  HANDLE copy = nullptr;
  if (!::DuplicateHandle(process, directory, process, &copy, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
    return error(ErrorCode::Io, "Cannot retain original profile directory");
  }
  return StateDescriptor(copy);
}

std::expected<std::filesystem::path, Error> stateDirectoryPath(StateHandle directory) {
  constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
  const auto size = ::GetFinalPathNameByHandleW(directory, nullptr, 0, flags);
  if (size == 0 || size > 32768) { return error(ErrorCode::UnsafePath, "Cannot resolve original profile identity"); }
  std::wstring path(size, L'\0');
  const auto written = ::GetFinalPathNameByHandleW(directory, path.data(), size, flags);
  if (written == 0 || written >= size) { return error(ErrorCode::UnsafePath, "Original profile identity changed"); }
  path.resize(written);
  if (path.starts_with(L"\\\\?\\UNC\\")) { path.replace(0, 8, L"\\\\"); }
  else if (path.starts_with(L"\\\\?\\")) { path.erase(0, 4); }
  return std::filesystem::path(std::move(path));
}

std::expected<StateDescriptor, Error> openStatePathDirectory(const std::filesystem::path& path) {
  if (filesystemRoot(path).empty()) { return error(ErrorCode::UnsafePath, "Unsafe original profile path"); }
  auto directory = openDirectory(path);
  if (!directory) { return std::unexpected(directory.error()); }
  const auto resolved = stateDirectoryPath(directory->get());
  if (!resolved || *resolved != path) {
    return error(ErrorCode::Conflict, "Original profile directory path changed");
  }
  return directory;
}

std::expected<StateDescriptor, Error> openStateFile(StateHandle parent, const std::filesystem::path& name) {
  auto file = open(parent, name.native(), FILE_GENERIC_READ, FILE_SHARE_READ, win::OpenExisting, win::NonDirectoryFile);
  if (!file) { return file; }
  auto safe = safeStateFile(file->get());
  if (!safe) { return std::unexpected(safe.error()); }
  return file;
}

std::expected<StateDescriptor, Error> openStateWitness(StateHandle parent, const std::filesystem::path& name) {
  return open(parent, name.native(), FILE_READ_ATTRIBUTES | READ_CONTROL, InspectSharing,
      win::OpenExisting, win::NonDirectoryFile);
}

std::expected<StateDescriptor, Error> openWritableStateFile(StateHandle parent,
    const std::filesystem::path& name, bool create) {
  constexpr auto rights=FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE;
  constexpr auto sharing=FILE_SHARE_READ | FILE_SHARE_DELETE;
  constexpr auto options=win::NonDirectoryFile | win::WriteThrough;
  auto file=create ? createPrivateStateStream(parent,name.native(),rights,sharing,options)
      : open(parent,name.native(),rights,sharing,win::OpenExisting,options);
  if (!file) { return file; }
  auto safe=safeStateFile(file->get());
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

std::expected<std::optional<StateStamp>, Error> stateStampAt(StateHandle directory, const std::filesystem::path& name) {
  auto file = open(directory, name.native(), FILE_READ_ATTRIBUTES | READ_CONTROL, InspectSharing, win::OpenExisting, 0);
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

std::expected<StateDescriptor, Error> stageStateFile(StateHandle parent, const std::filesystem::path& name,
    std::string_view data, unsigned mode) {
  constexpr auto rights=FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES | READ_CONTROL | DELETE;
  constexpr auto options=win::NonDirectoryFile | win::WriteThrough;
  auto file=mode==0600 ? createPrivateStateStream(parent,name.native(),rights,FILE_SHARE_READ,options)
      : open(parent,name.native(),rights,FILE_SHARE_READ,win::CreateExclusive,options);
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

std::expected<void, Error> publishStateFile(StateHandle directory, const std::filesystem::path& source,
    const std::filesystem::path& destination, StateHandle staged, bool replace, const StateStamp* expected) {
  StateDescriptor retained;
  if (!staged) {
    auto file = open(directory, source.native(), FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES | READ_CONTROL | DELETE,
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
  if (expected && !sameStateSnapshot(*opened, *expected)) {
    return error(ErrorCode::Conflict, "Saved-state source changed before publication");
  }
  const auto target = stateStampAt(directory, destination);
  if (!target) { return std::unexpected(target.error()); }
  if (*target && (!(*target)->regular || !(*target)->owned || (*target)->links != 1)) {
    return error(ErrorCode::UnsafePath, "Cannot replace an unsafe saved-state destination");
  }
  const auto published = win::renameRelativeFile(staged, directory, destination.native(), replace && target->has_value());
  if (!published) {
    return error(openError(published.error()), "Cannot publish saved-state entry");
  }
  return syncStateFile(staged);
}

std::expected<void, Error> retireStateFile(StateHandle directory, const std::string& name, const StateStamp& expected) {
  auto file = open(directory, name, FILE_READ_ATTRIBUTES | READ_CONTROL | DELETE,
      FILE_SHARE_READ, win::OpenExisting, win::NonDirectoryFile);
  if (!file) { return std::unexpected(file.error()); }
  const auto info = stateStamp(file->get());
  if (!info || !info->regular || !info->owned || info->links != 1 || !sameStateSnapshot(*info, expected)) {
    return error(ErrorCode::Conflict, "Saved-state entry changed before retirement");
  }
  FILE_DISPOSITION_INFO disposition{TRUE};
  if (!::SetFileInformationByHandle(file->get(), FileDispositionInfo, &disposition, sizeof(disposition))) {
    return error(ErrorCode::Io, "Cannot retire original control");
  }
  return {};
}

std::expected<void, Error> linkPayloadName(StateHandle directory, StateHandle source,
    const std::filesystem::path& existing, const std::filesystem::path& added) {
  const auto opened = stateStamp(source);
  const auto named = stateStampAt(directory, existing);
  if (!opened || !named || !*named || !opened->regular || !opened->owned || !sameStateSnapshot(*opened, **named)) {
    return error(ErrorCode::Conflict, "Managed completion source name changed");
  }
  const auto linked = win::linkRelativeFile(source, directory, added.native());
  if (!linked) { return error(openError(linked.error()), "Cannot link managed completion name"); }
  return syncStateFile(source);
}

std::expected<void, Error> removePayloadName(StateHandle directory,
    const std::filesystem::path& name, const StateStamp& expected) {
  auto file = open(directory, name.native(), FILE_READ_ATTRIBUTES | READ_CONTROL | DELETE,
      InspectSharing, win::OpenExisting, win::NonDirectoryFile);
  if (!file) { return std::unexpected(file.error()); }
  const auto info = stateStamp(file->get());
  if (!info || !info->regular || !info->owned || !sameStateSnapshot(*info, expected)) {
    return error(ErrorCode::Conflict, "Managed payload name changed before removal");
  }
  FILE_DISPOSITION_INFO_EX disposition{FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS};
  if (!::SetFileInformationByHandle(file->get(), FileDispositionInfoEx, &disposition, sizeof(disposition))) {
    return error(ErrorCode::Io, "Cannot remove the authenticated payload name");
  }
  return {};
}

std::expected<void, Error> protectPrivateStateFile(StateHandle file) {
  // The relative create inherits the held profile directory's security descriptor.
  return safeStateFile(file);
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
