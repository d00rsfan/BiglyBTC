// SPDX-License-Identifier: GPL-2.0-or-later
#include "file_operation_io.hpp"
#include "original_payload_io.hpp"
#include "../platform/utf8_path.hpp"

#include <array>
#include <openssl/evp.h>

#ifdef _WIN32
#include "../platform/windows_relative_file.hpp"
#include <cstring>
#else
#include "posix_profile_files.hpp"
#include <cerrno>
#include <sys/xattr.h>
#include <unistd.h>
#endif

namespace biglybtc::storage::fileop {
namespace fs = std::filesystem;
namespace {
using detail::StateStamp;
using detail::stateStamp;
using detail::stateStampAt;
using detail::sameStateIdentity;
using detail::sameStateSnapshot;
using detail::sameStateTimestamp;
using detail::InvalidStateHandle;

template<class T> T take(std::expected<T, Error> value) {
  if (!value) { throw Failure{std::move(value.error())}; }
  if constexpr (!std::is_void_v<T>) { return std::move(*value); }
}

bool samePayload(const StateStamp& a, const StateStamp& b) {
  return sameStateIdentity(a, b) && a.size == b.size && sameStateTimestamp(a.modified, b.modified);
}

std::optional<StateStamp> inspectAt(FileHandle directory, const fs::path& leaf) {
  const auto entry = take(stateStampAt(directory, leaf));
  if (entry) {
    require(entry->regular && entry->owned && entry->links >= 1 && entry->links <= 2 && entry->size >= 0,
        ErrorCode::UnsafePath, "File-operation leaf must be owned regular data with bounded links");
  }
  return entry;
}

std::optional<StateStamp> inspectPath(const fs::path& path) {
  pathShape(path);
  const auto parent = directory(path.parent_path(), false, true);
  if (parent.get() == InvalidStateHandle) { return {}; }
  return inspectAt(parent.get(), path.filename());
}

std::ptrdiff_t readChunk(FileHandle file, char* buffer, std::size_t count, std::uint64_t offset) {
#ifdef _WIN32
  return detail::readStateAt(file, buffer, count, offset);
#else
  (void)offset;
  std::ptrdiff_t got;
  do { got = ::read(file, buffer, count); } while (got < 0 && errno == EINTR);
  return got;
#endif
}
} // namespace

void sync(FileHandle fd) {
#ifdef _WIN32
  const auto stamp = take(stateStamp(fd));
  take(stamp.directory ? detail::syncStateDirectory(fd) : detail::syncStateFile(fd));
#else
  require(::fsync(fd) == 0, ErrorCode::Io, "Cannot sync file operation");
#endif
}

bool identity(const FileStamp& a, const FileStamp& b) {
  return sameStateIdentity(a, b);
}

bool unchanged(const FileStamp& a, const FileStamp& b) {
  return samePayload(a, b) && a.mode == b.mode && a.owner == b.owner
      && a.owned == b.owned && a.links == b.links && sameStateTimestamp(a.changed, b.changed);
}

void pathShape(const fs::path& path) {
  const auto& name = path.native();
  require(path.is_absolute() && path.lexically_normal() == path && !detail::filesystemRoot(path).empty()
      && name.size() <= 4096 && name.find('\0') == name.npos && name.find('\n') == name.npos,
      ErrorCode::UnsafePath, "File operation requires absolute normalized paths without newlines");
  for (const auto& part : path.relative_path()) {
    require(!part.empty() && part != "." && part != ".." && part.native().size() <= 255,
        ErrorCode::UnsafePath, "Unsafe path component");
  }
}

Fd directory(const fs::path& path, bool create, bool missing) {
  pathShape(path);
  const auto root = detail::filesystemRoot(path);
  auto current = take(detail::openFilesystemRoot(root));
  if (path == root) { return current; }
  const auto relative = root == path.root_path() ? path.relative_path() : path.lexically_relative(root);
  require(!relative.empty(), ErrorCode::UnsafePath, "File-operation path is outside its filesystem root");
  for (const auto& part : relative) {
    auto next = detail::openDirectoryAt(current.get(), part);
    if (!next && next.error().code == ErrorCode::Missing) {
      if (!create && missing) { return {}; }
      require(create, ErrorCode::Missing, "Required parent directory is absent");
      require(take(stateStamp(current.get())).owned, ErrorCode::UnsafePath,
          "Cannot create file-operation directory in an unowned parent");
      next = detail::createDirectoryAt(current.get(), part);
      require(next.has_value(), ErrorCode::Conflict, "File-operation directory appeared or cannot be created");
      take(detail::syncStateDirectory(current.get()));
    }
    require(next.has_value(), ErrorCode::UnsafePath, "Parent is inaccessible or symbolic");
    const auto held = take(stateStamp(next->get()));
    const auto named = take(stateStampAt(current.get(), part));
    require(named && named->directory && held.links && sameStateIdentity(held, *named),
        ErrorCode::Conflict, "Parent directory identity changed");
    current = std::move(*next);
  }
  return current;
}

std::string profilePath(const ProfileLease& lease) {
  require(lease.valid(), ErrorCode::Busy, "File operation requires held profile lease");
  const auto path = take(detail::stateDirectoryPath(lease.directoryHandle()));
  const auto named = directory(path);
  const auto held = take(stateStamp(lease.directoryHandle()));
  require(held.owned && sameStateIdentity(held, take(stateStamp(named.get()))),
      ErrorCode::Conflict, "Held profile path identity changed");
  return platform::pathToUtf8(path);
}

std::optional<FileStamp> inspect(const fs::path& path) {
  return inspectPath(path);
}

Raw read(const fs::path& path, std::size_t maximum) {
  const auto named = inspectPath(path);
  if (!named) { return {}; }
  require(named->links == 1 && static_cast<std::uint64_t>(named->size) <= maximum,
      ErrorCode::UnsafePath, "File-operation document is linked or oversized");
  const auto parent = directory(path.parent_path());
  auto file = detail::openStateFile(parent.get(), path.filename());
  require(file.has_value(), ErrorCode::Conflict, "File-operation document changed before open");
  const auto before = take(stateStamp(file->get()));
  require(sameStateSnapshot(*named, before), ErrorCode::Conflict, "File-operation document changed before open");
  std::string bytes(static_cast<std::size_t>(before.size), '\0');
  for (std::size_t offset = 0; offset < bytes.size();) {
    const auto count = readChunk(file->get(), bytes.data() + offset, bytes.size() - offset, offset);
    require(count > 0, ErrorCode::Io, "Incomplete file-operation document read");
    offset += static_cast<std::size_t>(count);
  }
  const auto after = take(stateStamp(file->get()));
  const auto current = inspectPath(path);
  require(current && sameStateSnapshot(before, after) && sameStateSnapshot(before, *current),
      ErrorCode::Conflict, "Document changed during read");
  return {std::move(bytes), before};
}

void check(const fs::path& path, const Raw& before, std::size_t maximum) {
  const auto current = read(path, maximum);
  require(current.bytes == before.bytes
      && (!before.stamp || (current.stamp && unchanged(*before.stamp, *current.stamp))),
      ErrorCode::Conflict, "Document changed after preparation");
}

void publish(const fs::path& path, const Raw& before, std::string_view data, std::size_t maximum) {
  require(data.size() <= maximum, ErrorCode::InvalidFormat, "File-operation document exceeds bound");
  check(path, before, maximum);
  const auto parent = directory(path.parent_path());
  auto temporary = path.filename();
  temporary += ".BiglyBTC.fileop-saving";
  const auto staged = take(detail::stageStateFile(parent.get(), temporary, data, 0600));
  take(detail::syncStateFile(staged.get()));
  const auto held = take(stateStamp(staged.get()));
  require(held.regular && held.owned && held.links == 1, ErrorCode::Conflict, "Unsafe document temporary");
  check(path, before, maximum);
  const auto namedParent = directory(path.parent_path());
  require(sameStateIdentity(take(stateStamp(parent.get())), take(stateStamp(namedParent.get()))),
      ErrorCode::Conflict, "Document directory changed before publication");
  take(detail::publishStateFile(parent.get(), temporary, path.filename(), staged.get(), before.bytes.has_value(), &held));
  take(detail::syncStateDirectory(parent.get()));
}

void erase(const fs::path& path, const FileStamp& expected) {
  const auto now = inspectPath(path);
  if (!now) { return; }
  require(samePayload(*now, expected), ErrorCode::Conflict, "Removal target identity/content metadata changed");
  const auto parent = directory(path.parent_path());
  const auto named = inspectAt(parent.get(), path.filename());
  require(named && samePayload(*named, *now), ErrorCode::Conflict, "Removal name changed before unlink");
  take(detail::removePayloadName(parent.get(), path.filename(), *named));
  take(detail::syncStateDirectory(parent.get()));
}

FileStamp heldFileStamp(FileHandle file) { return take(stateStamp(file)); }

bool validJournalFileStamp(const FileStamp& stamp) noexcept {
#ifdef _WIN32
  const bool ownedRegular = stamp.owner == 0 && !(stamp.mode & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
#else
  const bool ownedRegular = S_ISREG(stamp.mode) && stamp.owner == ::geteuid();
#endif
  return ownedRegular && stamp.size >= 0 && stamp.links >= 1 && stamp.links <= 2
      && stamp.modified.tv_nsec >= 0 && stamp.modified.tv_nsec < 1000000000
      && stamp.changed.tv_nsec >= 0 && stamp.changed.tv_nsec < 1000000000;
}

namespace {
#ifdef _WIN32
bool ownerAccessOnly(FileHandle file, DWORD restrictedRights) {
  PSID owner = nullptr;
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR raw = nullptr;
  const auto status = ::GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
      &owner, nullptr, &dacl, nullptr, &raw);
  const std::unique_ptr<void, decltype(&::LocalFree)> descriptor(raw, ::LocalFree);
  if (status != ERROR_SUCCESS || !owner || !dacl) { return false; }
  HANDLE rawToken = nullptr;
  if (!::OpenThreadToken(::GetCurrentThread(), TOKEN_QUERY, TRUE, &rawToken)
      && (::GetLastError() != ERROR_NO_TOKEN || !::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &rawToken))) {
    return false;
  }
  const Fd token(rawToken);
  DWORD bytes = 0;
  ::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
  if (!bytes) { return false; }
  std::vector<unsigned char> user(bytes);
  if (!::GetTokenInformation(token.get(), TokenUser, user.data(), bytes, &bytes)) { return false; }
  const auto userSid = reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid;
  for (DWORD index = 0; index < dacl->AceCount; ++index) {
    void* entry = nullptr;
    if (!::GetAce(dacl, index, &entry)) { return false; }
    const auto type = static_cast<const ACE_HEADER*>(entry)->AceType;
    if (type == ACCESS_DENIED_ACE_TYPE) { continue; }
    if (type != ACCESS_ALLOWED_ACE_TYPE) { return false; }
    auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(entry);
    if ((allowed->Mask & restrictedRights) == 0) { continue; }
    auto* sid = &allowed->SidStart;
    if (!::EqualSid(sid, owner) && !::EqualSid(sid, userSid) && !::IsWellKnownSid(sid, WinLocalSystemSid)
        && !::IsWellKnownSid(sid, WinBuiltinAdministratorsSid)) { return false; }
  }
  return true;
}
#endif

} // namespace

bool privateEntry(FileHandle file) {
  const auto stamp = heldFileStamp(file);
  if ((!stamp.directory && !stamp.regular) || !stamp.owned) { return false; }
#ifdef _WIN32
  return ownerAccessOnly(file, ~DWORD{0});
#else
  return (stamp.mode & 0077) == 0;
#endif
}

bool nonOwnerWritesBlocked(FileHandle file) {
  const auto stamp = stateStamp(file);
  if (!stamp || (!stamp->directory && !stamp->regular) || !stamp->owned) { return false; }
#ifdef _WIN32
  constexpr DWORD WriteRights = GENERIC_ALL | GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA
      | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER;
  return ownerAccessOnly(file, WriteRights);
#else
  return (stamp->mode & 0022) == 0;
#endif
}

bool tryLinkPayload(FileHandle sourceDirectory, const fs::path& source, const FileStamp& expected,
    FileHandle destinationDirectory, const fs::path& destination) {
  const auto named = inspectAt(sourceDirectory, source);
  require(named && named->links == 1 && samePayload(*named, expected),
      ErrorCode::Conflict, "Relocation source changed before linking");
  require(!destination.empty() && destination.filename() == destination && destination != "." && destination != ".."
      && destination.native().find('\0') == destination.native().npos, ErrorCode::UnsafePath,
      "Destination must be one ordinary file name");
#ifdef _WIN32
  const auto file = take(detail::openOriginalPayloadAt(sourceDirectory, source, detail::OriginalPayloadAccess::ReadOnly, 1, true));
  require(sameStateSnapshot(take(stateStamp(file.get())), *named), ErrorCode::Conflict,
      "Relocation source changed while opening for linking");
  const auto result = platform::detail::linkRelativeFile(file.get(), destinationDirectory, destination.native());
  if (!result) {
    const auto error = result.error();
    if (error == ERROR_NOT_SAME_DEVICE || error == ERROR_NOT_SUPPORTED || error == ERROR_ACCESS_DENIED
        || error == ERROR_INVALID_FUNCTION) { return false; }
    fail(ErrorCode::Conflict, "Cannot link relocation destination without overwrite");
  }
#else
  if (::linkat(sourceDirectory, source.c_str(), destinationDirectory, destination.c_str(), 0) != 0) {
    if (errno == EXDEV || errno == EPERM || errno == EOPNOTSUPP) { return false; }
    fail(ErrorCode::Conflict, "Cannot link relocation destination without overwrite");
  }
#endif
  const auto linked = inspectAt(destinationDirectory, destination);
  require(linked && samePayload(*linked, *named), ErrorCode::Conflict, "Linked destination identity changed");
  take(detail::syncStateDirectory(destinationDirectory));
  return true;
}

Fd openCopyScratch(FileHandle directory, const fs::path& name, const std::optional<FileStamp>& previous) {
  using Access = detail::OriginalPayloadAccess;
  auto opened = detail::openOriginalPayloadAt(directory, name, previous ? Access::ReadWrite : Access::CreateExclusive, 1, true);
  require(opened.has_value(), ErrorCode::Conflict, "Cannot open the exact relocation scratch");
  if (previous) {
    const auto held = take(stateStamp(opened->get()));
    require(sameStateSnapshot(*previous, held), ErrorCode::Conflict, "Relocation scratch changed before open");
    require(detail::resizeOriginalPayload(opened->get(), 0), ErrorCode::Io, "Cannot restart owned relocation scratch");
  }
  return Fd(opened->release());
}

void copyPayload(FileHandle directory, const fs::path& source, const FileStamp& expected, FileHandle output) {
  const auto before = inspectAt(directory, source);
  require(before && samePayload(*before, expected), ErrorCode::Conflict, "Copy source changed before open");
  const auto input = take(detail::openOriginalPayloadAt(directory, source, detail::OriginalPayloadAccess::ReadOnly,
      static_cast<unsigned>(before->links), true));
  require(sameStateSnapshot(take(stateStamp(input.get())), *before), ErrorCode::Conflict, "Copy source changed before reading");
  const auto maximum = static_cast<std::uint64_t>(before->size);
  std::array<char, 65536> bytes{};
  std::uint64_t copied = 0;
  for (;;) {
    const auto count = readChunk(input.get(), bytes.data(), bytes.size(), copied);
    require(count >= 0, ErrorCode::Io, "Cannot read relocation source");
    if (!count) { break; }
    const auto length = static_cast<std::size_t>(count);
    require(length <= maximum - copied, ErrorCode::Conflict, "Relocation source grew");
    const auto block = std::as_bytes(std::span(bytes).first(length));
    for (std::size_t offset = 0; offset < length;) {
      const auto written = detail::writeOriginalPayload(output, copied + offset, block.subspan(offset));
      require(written > 0, ErrorCode::Io, "Cannot write relocation scratch");
      offset += static_cast<std::size_t>(written);
    }
    copied += length;
  }
  const auto after = inspectAt(directory, source);
  require(copied == maximum && after && sameStateSnapshot(*before, *after)
      && sameStateSnapshot(*before, take(stateStamp(input.get()))), ErrorCode::Conflict,
      "Relocation source changed during copy");
#ifndef _WIN32
  require(::fchmod(output, before->mode & 0777) == 0, ErrorCode::Io, "Cannot preserve payload permissions");
#endif
  take(detail::syncStateFile(output));
}

void renamePayload(FileHandle from, const fs::path& source, const FileStamp& expected,
    FileHandle to, const fs::path& destination) {
  const auto named = inspectAt(from, source);
  require(named && named->links == 1 && sameStateSnapshot(*named, expected), ErrorCode::Conflict,
      "Relocation scratch changed before publication");
  require(!destination.empty() && destination.filename() == destination && destination != "." && destination != ".."
      && destination.native().find('\0') == destination.native().npos, ErrorCode::UnsafePath,
      "Destination must be one ordinary file name");
#ifdef _WIN32
  namespace win = platform::detail;
  const auto handle = win::openRelativeFile(from, source.native(), FILE_READ_ATTRIBUTES | READ_CONTROL | DELETE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, win::OpenExisting, win::NonDirectoryFile);
  require(handle.has_value(), ErrorCode::Conflict, "Relocation scratch changed before rename");
  Fd held(*handle);
  require(sameStateSnapshot(*named, take(stateStamp(held.get()))), ErrorCode::Conflict,
      "Relocation scratch changed while opening for rename");
  require(win::renameRelativeFile(held.get(), to, destination.native(), false).has_value(),
      ErrorCode::Conflict, "Relocation destination appeared before publication");
#else
  require(platform::renameNoReplaceAt(from, source.c_str(), to, destination.c_str()) == 0,
      ErrorCode::Conflict, "Relocation destination appeared before publication");
#endif
  take(detail::syncStateDirectory(to));
  if (from != to) { take(detail::syncStateDirectory(from)); }
}

#ifdef _WIN32
namespace {
bool sameAllocationFile(FileHandle file, FileHandle stream) noexcept {
  FILE_ID_INFO base{}, token{};
  constexpr FILE_ID_128 empty{};
  return ::GetFileInformationByHandleEx(file, FileIdInfo, &base, sizeof(base))
      && ::GetFileInformationByHandleEx(stream, FileIdInfo, &token, sizeof(token))
      && base.VolumeSerialNumber == token.VolumeSerialNumber
      && std::memcmp(base.FileId.Identifier, empty.Identifier, sizeof(empty.Identifier)) != 0
      && std::memcmp(base.FileId.Identifier, token.FileId.Identifier, sizeof(base.FileId.Identifier)) == 0;
}
}
#endif

std::optional<std::string> readAllocationToken(FileHandle directory, const fs::path& leaf, FileHandle file) {
  std::string token(64, '\0');
#ifdef _WIN32
  namespace win = platform::detail;
  const auto stream = win::openRelativeFile(directory, leaf.native(), FILE_GENERIC_READ,
      FILE_SHARE_READ, win::OpenExisting, win::NonDirectoryFile, L"BiglyBTC.allocationReceipt");
  if (!stream) { return {}; }
  const Fd held(*stream);
  LARGE_INTEGER bytes{};
  if (!sameAllocationFile(file, held.get()) || !::GetFileSizeEx(held.get(), &bytes) || bytes.QuadPart != 64) { return {}; }
  const auto count = detail::readStateAt(held.get(), token.data(), token.size(), 0);
#else
  (void)directory;
  (void)leaf;
#ifdef __APPLE__
  const auto count = ::fgetxattr(file, "user.BiglyBTC.allocationReceipt", token.data(), token.size(), 0, 0);
#else
  const auto count = ::fgetxattr(file, "user.BiglyBTC.allocationReceipt", token.data(), token.size());
#endif
#endif
  if (count != 64) { return {}; }
  return token;
}

bool writeAllocationToken(FileHandle directory, const fs::path& leaf, FileHandle file, std::string_view token) {
  require(token.size() == 64, ErrorCode::InvalidFormat, "Invalid allocation token length");
#ifdef _WIN32
  namespace win = platform::detail;
  DWORD features = 0;
  require(::GetVolumeInformationByHandleW(file, nullptr, 0, nullptr, nullptr, &features, nullptr, 0) != FALSE,
      ErrorCode::Io, "Cannot inspect allocation volume capabilities");
  if (!(features & FILE_NAMED_STREAMS)) { return false; }
  const auto rawGuard = win::openRelativeFile(directory, leaf.native(), FILE_READ_ATTRIBUTES | READ_CONTROL,
      FILE_SHARE_READ | FILE_SHARE_WRITE, win::OpenExisting, win::NonDirectoryFile);
  require(rawGuard.has_value(), ErrorCode::Conflict, "Cannot retain allocation name while binding token");
  const Fd guard(*rawGuard);
  require(sameStateSnapshot(heldFileStamp(guard.get()), heldFileStamp(file)), ErrorCode::Conflict,
      "Allocation name changed before binding token");
  const auto stream = win::openRelativeFile(directory, leaf.native(), FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES | READ_CONTROL,
      FILE_SHARE_READ, win::CreateExclusive, win::NonDirectoryFile | win::WriteThrough, L"BiglyBTC.allocationReceipt");
  require(stream.has_value(), ErrorCode::Io, "Cannot create allocation token stream");
  const Fd held(*stream);
  require(sameAllocationFile(file, held.get()), ErrorCode::Conflict,
      "Allocation token stream belongs to a different file");
  DWORD written = 0;
  require(::WriteFile(held.get(), token.data(), 64, &written, nullptr) && written == 64, ErrorCode::Io,
      "Cannot write allocation token stream");
  take(detail::syncStateFile(held.get()));
#else
  (void)directory;
  (void)leaf;
#ifdef __APPLE__
  const auto result = ::fsetxattr(file, "user.BiglyBTC.allocationReceipt", token.data(), token.size(), 0, XATTR_CREATE);
#else
  const auto result = ::fsetxattr(file, "user.BiglyBTC.allocationReceipt", token.data(), token.size(), XATTR_CREATE);
#endif
  if (result != 0) {
    require(errno == ENOTSUP || errno == EOPNOTSUPP || errno == ENOSYS, ErrorCode::Io, "Cannot bind allocation creation receipt");
    return false;
  }
#endif
  return true;
}

namespace {
PayloadDigest readPayloadDigest(const fs::path& path, const StateStamp& expected, std::stop_token stop) {
  const auto before = inspectPath(path);
  require(before && samePayload(*before, expected), ErrorCode::Conflict, "Payload changed before hashing");
  const auto parent = directory(path.parent_path());
  auto file = detail::openOriginalPayloadAt(parent.get(), path.filename(), detail::OriginalPayloadAccess::ReadOnly,
      static_cast<unsigned>(before->links), true);
  require(file.has_value(), ErrorCode::Conflict, "Payload changed before open");
  const auto opened = take(stateStamp(file->get()));
  require(sameStateSnapshot(*before, opened), ErrorCode::Conflict, "Payload changed before open");
  auto context = beginSha256();
  std::array<char, 65536> bytes{};
  std::uint64_t total = 0;
  for (;;) {
    require(!stop.stop_requested(), ErrorCode::Cancelled, "File operation cancelled before journal");
    const auto count = readChunk(file->get(), bytes.data(), bytes.size(), total);
    require(count >= 0, ErrorCode::Io, "Cannot read payload for file operation");
    if (!count) { break; }
    total += static_cast<std::uint64_t>(count);
    require(total <= static_cast<std::uint64_t>(expected.size), ErrorCode::Conflict, "Payload grew during hash");
    require(EVP_DigestUpdate(context.get(), bytes.data(), static_cast<std::size_t>(count)) == 1,
        ErrorCode::Io, "Cannot hash payload");
  }
  const auto after = inspectPath(path);
  const auto held = take(stateStamp(file->get()));
  require(after && sameStateSnapshot(opened, *after) && sameStateSnapshot(opened, held)
      && total == static_cast<std::uint64_t>(expected.size), ErrorCode::Conflict, "Payload changed while hashing");
  return {Fd(file->release()), held, finishSha256(context)};
}
} // namespace

std::string digestFile(const fs::path& path, const FileStamp& expected, std::stop_token stop) {
  return readPayloadDigest(path, expected, stop).sha256;
}

std::string PayloadDigestCache::digest(const fs::path& path, const FileStamp& expected, std::stop_token stop) {
  require(!stop.stop_requested(), ErrorCode::Cancelled, "File operation cancelled before journal");
  const auto named = inspectPath(path);
  const auto initial = expected;
  require(named && samePayload(*named, initial), ErrorCode::Conflict, "Payload changed before hashing");
  for (const auto& proof : proofs_) {
    if (!proof) { continue; }
    const auto stamp = proof->stamp;
    if (!sameStateSnapshot(*named, stamp)) { continue; }
    const auto held = stateStamp(proof->descriptor.get());
    const auto current = inspectPath(path);
    if (held && current && sameStateSnapshot(stamp, *held) && sameStateSnapshot(stamp, *current)) {
      return proof->sha256;
    }
  }
  auto proof = readPayloadDigest(path, initial, stop);
  auto result = proof.sha256;
  proofs_[next_] = std::move(proof);
  next_ = (next_ + 1) % Capacity;
  return result;
}

} // namespace biglybtc::storage::fileop
