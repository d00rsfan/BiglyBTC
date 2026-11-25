// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_config_native.hpp"

#include "../platform/windows_relative_file.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

namespace biglybtc::storage::detail {
namespace {
namespace win = platform::detail;

struct CloseFile {
  void operator()(HANDLE file) const noexcept { ::CloseHandle(file); }
};
using File = std::unique_ptr<void, CloseFile>;

std::unexpected<Error> error(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}

std::wstring wideName(std::string_view name) { return {name.begin(), name.end()}; }

bool readMetadata(HANDLE file, FILE_BASIC_INFO& metadata, LARGE_INTEGER& size) {
  return ::GetFileInformationByHandleEx(file, FileBasicInfo, &metadata, static_cast<DWORD>(sizeof(metadata)))
      && ::GetFileSizeEx(file, &size);
}


std::expected<bool, Error> fileExists(HANDLE directory, std::string_view name) {
  auto opened = win::openRelativeFile(directory, wideName(name), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, win::OpenExisting, 0);
  if (opened) { const File file(*opened); return true; }
  if (opened.error() == ERROR_FILE_NOT_FOUND) { return false; }
  return error(ErrorCode::Io, "Unable to inspect configuration pathname");
}

std::expected<void, Error> requireInventoryAvailable(
    HANDLE directory, std::string_view filename) {
  constexpr std::string_view inventory = "downloads.config";
  const auto lower = [](char value) {
    return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
  };
  if (!std::ranges::equal(filename, inventory, {}, lower)) { return {}; }
  for (const auto* marker : {"BiglyBTC.subscription-acquire", "BiglyBTC.subscription-acquire.saving",
       "BiglyBTC.subscription-acquire.bak"}) {
    const auto present = fileExists(directory, marker);
    if (!present) { return std::unexpected(present.error()); }
    if (*present) {
      return error(ErrorCode::Conflict, "Pending subscription acquisition must be resolved before inventory writes");
    }
  }
  return {};
}

bool isRegularFile(HANDLE handle, bool requireSingleLink) {
  BY_HANDLE_FILE_INFORMATION info{};
  return ::GetFileType(handle) == FILE_TYPE_DISK && ::GetFileInformationByHandle(handle, &info)
      && !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
      && (!requireSingleLink || info.nNumberOfLinks == 1);
}

std::expected<File, Error> stageConfig(HANDLE directory, std::string_view name, std::string_view contents) {
  const auto raw = win::openRelativeFile(directory, wideName(name), FILE_GENERIC_WRITE | DELETE | FILE_READ_ATTRIBUTES,
      0, win::CreateExclusive, win::WriteThrough | win::NonDirectoryFile);
  if (!raw) {
    const auto code = raw.error();
    return error(code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS ? ErrorCode::Conflict : ErrorCode::Io,
        "Unable to create exclusive saving file");
  }
  File file(*raw);
  for (std::size_t offset = 0; offset < contents.size();) {
    const auto requested = static_cast<DWORD>(std::min<std::size_t>(contents.size() - offset, 1024 * 1024));
    DWORD processed = 0;
    if (!::WriteFile(file.get(), contents.data() + offset, requested, &processed, nullptr) || !processed) {
      FILE_DISPOSITION_INFO partial{TRUE};
      ::SetFileInformationByHandle(file.get(), FileDispositionInfo, &partial, static_cast<DWORD>(sizeof(partial)));
      return error(ErrorCode::Io, "Unable to write complete saving file; original preserved");
    }
    offset += processed;
  }
  if (!::FlushFileBuffers(file.get())) {
    return error(ErrorCode::Io, "Unable to synchronize saving file; original preserved");
  }
  return file;
}

std::expected<void, Error> publishConfig(HANDLE file, HANDLE directory, std::string_view name, bool replace) {
  const auto published = win::renameRelativeFile(file, directory, wideName(name), replace);
  if (!published) {
    const auto code = published.error();
    return error(!replace && (code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS)
        ? ErrorCode::Conflict : ErrorCode::Io, "Unable to publish configuration; recovery files retained");
  }
  return {};
}

std::expected<File, Error> openConfigForBackup(HANDLE directory, std::string_view name) {
  const auto opened = win::openRelativeFile(directory, wideName(name), DELETE | FILE_READ_ATTRIBUTES,
      0, win::OpenExisting, win::WriteThrough | win::NonDirectoryFile);
  if (!opened) { return error(ErrorCode::Io, "Unable to open primary for backup; saving file retained"); }
  return File(*opened);
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
  const auto raw = win::openRelativeFile((*retained)->directoryHandle(), wideName(filename), FILE_GENERIC_READ,
      FILE_SHARE_READ, win::OpenExisting, 0);
  if (!raw) {
    if (raw.error() == ERROR_FILE_NOT_FOUND) { return std::nullopt; }
    return error(ErrorCode::Io, "Unable to open configuration safely");
  }

  const File file(*raw);
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

namespace biglybtc::storage {

std::expected<void, Error> requireNoPendingSubscriptionAcquisition(const ProfileLease& lease) {
  const auto held = lease.retain();
  if (!held) { return std::unexpected(held.error()); }
  return detail::requireInventoryAvailable((*held)->directoryHandle(), "downloads.config");
}

std::expected<ConfigFile, Error> ConfigFile::createMissing(const ProfileLease& lease,
    std::string filename, std::string_view encoded, core::Limits limits, void (*observer)(SaveStage)) {
  auto access = lease.sourceAccess(true);
  if (!access) { return std::unexpected(access.error()); }
  if (observer && !access->observationsAllowed()) {
    return detail::error(ErrorCode::Unsupported, "Observed configuration writes require an unnested source boundary");
  }
  try {
    if (!detail::validConfigName(filename)) {
      return detail::error(ErrorCode::UnsafePath, "Safe configuration filename required");
    }
    const auto held = lease.retain();
    if (!held) { return std::unexpected(held.error()); }
    const auto& retained = **held;
    const auto identity = detail::profileConfigDirectory(retained);
    const auto directory = retained.directoryHandle();
    if (!identity) { return std::unexpected(identity.error()); }
    auto inventory = detail::requireInventoryAvailable(directory, filename);
    if (!inventory) { return std::unexpected(inventory.error()); }
    if (encoded.size() > limits.inputBytes) {
      return detail::error(ErrorCode::InvalidFormat, "New configuration exceeds the selected input limit");
    }
    auto parsed = core::Document::parse(std::string(encoded), limits);
    if (!parsed || parsed->root().type != core::Node::Type::Dictionary) {
      return detail::error(ErrorCode::InvalidFormat, "New configuration must be a complete bounded dictionary");
    }
    const auto primary = filename;
    const auto backup = filename + ".bak";
    const auto saving = filename + ".saving";
    std::expected<ConfigFile, Error> prepared = ConfigFile(std::move(filename), std::move(*parsed),
        RecoverySource::Primary, std::nullopt, false, identity->volume, identity->file, limits);
    static_assert(std::is_nothrow_move_constructible_v<decltype(prepared)>);
    const auto requireAbsent = [directory](auto paths) -> std::expected<void, Error> {
      for (const auto* path : paths) {
        const auto present = detail::fileExists(directory, *path);
        if (!present) { return std::unexpected(present.error()); }
        if (*present) { return detail::error(ErrorCode::Conflict, "First configuration path already exists"); }
      }
      return {};
    };
    const auto absent = requireAbsent(std::array{&primary, &backup, &saving});
    if (!absent) { return std::unexpected(absent.error()); }
    auto stage = detail::stageConfig(directory, saving, prepared->document_.originalBytes());
    if (!stage) { return std::unexpected(stage.error()); }
    if (observer) { access->outside([&] { observer(SaveStage::TemporarySynced); }); }
    if (!lease.sharesOwnership(retained) || !retained.retain()
        || !detail::isRegularFile(stage->get(), true)) {
      return detail::error(ErrorCode::Conflict, "Profile or saving file changed before first publication");
    }
    const auto stillAbsent = requireAbsent(std::array{&primary, &backup});
    if (!stillAbsent) { return std::unexpected(stillAbsent.error()); }
    inventory = detail::requireInventoryAvailable(directory, prepared->filename_);
    if (!inventory) { return std::unexpected(inventory.error()); }
    const auto published = detail::publishConfig(stage->get(), directory, primary, false);
    if (!published) { return std::unexpected(published.error()); }
    stage->reset();
    if (observer) { access->outside([&] { observer(SaveStage::PrimaryRenamed); }); }
    return prepared;
  } catch (const std::bad_alloc&) {
    return detail::error(ErrorCode::Io, "Insufficient memory during first configuration publication");
  }
}

std::expected<void, Error> ConfigFile::saveImpl(const ProfileLease& lease,
    void (*observer)(SaveStage), std::optional<core::Document> replacement) {
  auto access = lease.sourceAccess(true);
  if (!access) { return std::unexpected(access.error()); }
  if (observer && !access->observationsAllowed()) {
    return detail::error(ErrorCode::Unsupported, "Observed configuration writes require an unnested source boundary");
  }
  const auto held = lease.retain();
  if (!held) { return std::unexpected(held.error()); }
  const auto& retained = **held;
  const auto identity = detail::profileConfigDirectory(retained);
  if (!identity || identity->volume != device_ || identity->file != inode_) {
    return detail::error(ErrorCode::UnsafePath, "Configuration belongs to a different or released profile lease");
  }
  const auto directory = retained.directoryHandle();
  auto inventory = detail::requireInventoryAvailable(directory, filename_);
  if (!inventory) { return std::unexpected(inventory.error()); }
  if (source_ == RecoverySource::Saving) {
    return detail::error(ErrorCode::Conflict, "Existing recovery file must be resolved before saving");
  }
  const std::string_view expected = source_ == RecoverySource::Primary ? std::string_view(document_.originalBytes())
      : (damagedPrimary_ ? std::string_view(*damagedPrimary_) : std::string_view{});
  const auto checkCurrent = [&] -> std::expected<void, Error> {
    auto current = detail::readProfileConfig(retained, filename_, limits_.inputBytes);
    if (!current) { return std::unexpected(current.error()); }
    if (current->has_value() == primaryMissing_ || (current->has_value() && **current != expected)) {
      return detail::error(ErrorCode::Conflict, "Configuration changed since loading");
    }
    return {};
  };
  auto unchanged = checkCurrent();
  if (!unchanged) { return unchanged; }
  const bool rememberReplacement = replacement.has_value();
  if (!replacement) {
    auto parsed = core::Document::parse(document_.serialize(), limits_);
    if (!parsed) { return detail::error(ErrorCode::InvalidFormat, "Edited configuration exceeds the selected limits"); }
    replacement.emplace(std::move(*parsed));
  }
  const auto& primary = filename_;
  const auto backup = filename_ + ".bak";
  const auto saving = filename_ + ".saving";
  auto stage = detail::stageConfig(directory, saving, replacement->originalBytes());
  if (!stage) { return std::unexpected(stage.error()); }
  if (observer) { access->outside([&] { observer(SaveStage::TemporarySynced); }); }
  unchanged = checkCurrent();
  if (!unchanged) { return unchanged; }
  inventory = detail::requireInventoryAvailable(directory, filename_);
  if (!inventory) { return std::unexpected(inventory.error()); }
  bool replacePrimary = !primaryMissing_;
  if (source_ == RecoverySource::Primary) {
    auto original = detail::openConfigForBackup(directory, primary);
    if (!original) { return std::unexpected(original.error()); }
    if (!detail::isRegularFile(original->get(), false)) {
      return detail::error(ErrorCode::UnsafePath, "Primary configuration must be a regular file");
    }
    const auto backedUp = detail::publishConfig(original->get(), directory, backup, true);
    if (!backedUp) { return backedUp; }
    original->reset();
    replacePrimary = false;
    if (observer) { access->outside([&] { observer(SaveStage::BackupRenamed); }); }
  }
  inventory = detail::requireInventoryAvailable(directory, filename_);
  if (!inventory) { return std::unexpected(inventory.error()); }
  if (!lease.sharesOwnership(retained) || !retained.retain() || !detail::isRegularFile(stage->get(), true)) {
    return detail::error(ErrorCode::Conflict, "Profile or saving file changed before replacement");
  }
  const auto published = detail::publishConfig(stage->get(), directory, primary, replacePrimary);
  if (!published) { return published; }
  stage->reset();
  if (observer) { access->outside([&] { observer(SaveStage::PrimaryRenamed); }); }
  recordSuccessfulSave(lease, std::move(*replacement), rememberReplacement);
  return {};
}

} // namespace biglybtc::storage
