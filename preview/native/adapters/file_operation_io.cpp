// SPDX-License-Identifier: GPL-2.0-or-later
#include "file_operation_io.hpp"
#include "original_payload_io.hpp"
#include "../platform/utf8_path.hpp"

#include <array>
#include <openssl/evp.h>

#ifdef _WIN32
#include "../platform/windows_relative_file.hpp"
#else
#include "posix_profile_files.hpp"
#include <cerrno>
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

#ifdef _WIN32
StateStamp nativeStamp(const FileStamp& value) { return value; }
FileStamp fileStamp(const StateStamp& value) { return value; }
#else
// POSIX transaction journals and their callers still serialize stat fields.
StateStamp nativeStamp(const FileStamp& value) {
  return {static_cast<std::uint64_t>(value.st_dev), static_cast<std::uint64_t>(value.st_ino),
      static_cast<std::uint64_t>(value.st_nlink), value.st_uid, value.st_gid, value.st_size,
      static_cast<unsigned>(value.st_mode), platform::modificationTime(value), platform::changeTime(value),
      S_ISREG(value.st_mode), S_ISDIR(value.st_mode), value.st_uid == ::geteuid()};
}

FileStamp fileStamp(const StateStamp& value) {
  FileStamp result{};
  result.st_dev = static_cast<dev_t>(value.device);
  result.st_ino = static_cast<ino_t>(value.inode);
  result.st_nlink = static_cast<nlink_t>(value.links);
  result.st_uid = static_cast<uid_t>(value.owner);
  result.st_gid = static_cast<gid_t>(value.group);
  result.st_mode = static_cast<mode_t>(value.mode);
  result.st_size = value.size;
#ifdef __APPLE__
  result.st_mtimespec = value.modified;
  result.st_ctimespec = value.changed;
#else
  result.st_mtim = value.modified;
  result.st_ctim = value.changed;
#endif
  return result;
}
#endif

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
  return sameStateIdentity(nativeStamp(a), nativeStamp(b));
}

bool unchanged(const FileStamp& a, const FileStamp& b) {
  const auto first = nativeStamp(a), second = nativeStamp(b);
  return samePayload(first, second) && first.mode == second.mode && first.owner == second.owner
      && first.owned == second.owned && first.links == second.links && sameStateTimestamp(first.changed, second.changed);
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
  return inspectPath(path).transform(fileStamp);
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
  return {std::move(bytes), fileStamp(before)};
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
  require(samePayload(*now, nativeStamp(expected)), ErrorCode::Conflict, "Removal target identity/content metadata changed");
  const auto parent = directory(path.parent_path());
  const auto named = inspectAt(parent.get(), path.filename());
  require(named && samePayload(*named, *now), ErrorCode::Conflict, "Removal name changed before unlink");
  take(detail::removePayloadName(parent.get(), path.filename(), *named));
  take(detail::syncStateDirectory(parent.get()));
}

FileStamp heldFileStamp(FileHandle file) { return fileStamp(take(stateStamp(file))); }

bool tryLinkPayload(FileHandle sourceDirectory, const fs::path& source, const FileStamp& expected,
    FileHandle destinationDirectory, const fs::path& destination) {
  const auto named = inspectAt(sourceDirectory, source);
  require(named && named->links == 1 && samePayload(*named, nativeStamp(expected)),
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
    require(sameStateSnapshot(nativeStamp(*previous), held), ErrorCode::Conflict, "Relocation scratch changed before open");
    require(detail::resizeOriginalPayload(opened->get(), 0), ErrorCode::Io, "Cannot restart owned relocation scratch");
  }
  return Fd(opened->release());
}

void copyPayload(FileHandle directory, const fs::path& source, const FileStamp& expected, FileHandle output) {
  const auto before = inspectAt(directory, source);
  require(before && samePayload(*before, nativeStamp(expected)), ErrorCode::Conflict, "Copy source changed before open");
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

void publishPayloadCopy(FileHandle directory, const fs::path& temporary, const FileStamp& expected,
    const fs::path& destination) {
  const auto named = inspectAt(directory, temporary);
  require(named && named->links == 1 && sameStateSnapshot(*named, nativeStamp(expected)), ErrorCode::Conflict,
      "Relocation scratch changed before publication");
  require(!destination.empty() && destination.filename() == destination && destination != "." && destination != ".."
      && destination.native().find('\0') == destination.native().npos, ErrorCode::UnsafePath,
      "Destination must be one ordinary file name");
#ifdef _WIN32
  namespace win = platform::detail;
  const auto handle = win::openRelativeFile(directory, temporary.native(), FILE_READ_ATTRIBUTES | READ_CONTROL | DELETE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, win::OpenExisting, win::NonDirectoryFile);
  require(handle.has_value(), ErrorCode::Conflict, "Relocation scratch changed before rename");
  Fd held(*handle);
  require(sameStateSnapshot(*named, take(stateStamp(held.get()))), ErrorCode::Conflict,
      "Relocation scratch changed while opening for rename");
  require(win::renameRelativeFile(held.get(), directory, destination.native(), false).has_value(),
      ErrorCode::Conflict, "Relocation destination appeared before publication");
#else
  require(platform::renameNoReplaceAt(directory, temporary.c_str(), destination.c_str()) == 0,
      ErrorCode::Conflict, "Relocation destination appeared before publication");
#endif
  take(detail::syncStateDirectory(directory));
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
  return {Fd(file->release()), fileStamp(held), finishSha256(context)};
}
} // namespace

std::string digestFile(const fs::path& path, const FileStamp& expected, std::stop_token stop) {
  return readPayloadDigest(path, nativeStamp(expected), stop).sha256;
}

std::string PayloadDigestCache::digest(const fs::path& path, const FileStamp& expected, std::stop_token stop) {
  require(!stop.stop_requested(), ErrorCode::Cancelled, "File operation cancelled before journal");
  const auto named = inspectPath(path);
  const auto initial = nativeStamp(expected);
  require(named && samePayload(*named, initial), ErrorCode::Conflict, "Payload changed before hashing");
  for (const auto& proof : proofs_) {
    if (!proof) { continue; }
    const auto stamp = nativeStamp(proof->stamp);
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
