#include "profile.hpp"
#include "file_io.hpp"
#include "profile_config_native.hpp"
#include "posix_profile_files.hpp"

#include <array>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>

#include <utility>

namespace biglybtc::storage {
namespace {
using detail::Descriptor;
using detail::readFile;
using detail::writeExclusive;

std::unexpected<Error> error(ErrorCode code, std::string message) {
  return std::unexpected(Error{code, std::move(message)});
}


std::expected<void, Error> requireInventoryAvailable(int directory, std::string_view filename) {
  if (filename != "downloads.config") { return {}; }
  for (const auto* name : {"BiglyBTC.subscription-acquire", "BiglyBTC.subscription-acquire.saving",
      "BiglyBTC.subscription-acquire.bak"}) {
    struct stat entry{};
    if (::fstatat(directory, name, &entry, AT_SYMLINK_NOFOLLOW) == 0) {
      return error(ErrorCode::Conflict, "Pending subscription acquisition must be resolved before inventory writes");
    }
    if (errno != ENOENT) { return error(ErrorCode::Io, "Unable to check pending subscription acquisition"); }
  }
  return {};
}


} // namespace

std::expected<void, Error> requireNoPendingSubscriptionAcquisition(const ProfileLease& lease) {
  if (!lease.valid()) { return error(ErrorCode::UnsafePath, "Held profile lease required"); }
  return requireInventoryAvailable(lease.directoryHandle(), "downloads.config");
}

std::expected<ConfigFile, Error> ConfigFile::createMissing(const ProfileLease& lease,
    std::string filename, std::string_view encoded, core::Limits limits, void (*observer)(SaveStage)) {
  auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
  if(observer&&!access->observationsAllowed()){return error(ErrorCode::Unsupported,"Observed configuration writes require an unnested source boundary");}
  try {
    if (!lease.valid() || !detail::validConfigName(filename)) {
      return error(ErrorCode::UnsafePath, "Held profile lease and safe configuration filename required");
    }
    const int handle = lease.directoryHandle();
    struct stat directory{};
    if (::fstat(handle, &directory) != 0 || !S_ISDIR(directory.st_mode)) {
      return error(ErrorCode::UnsafePath, "Unable to identify held profile directory safely");
    }
    const auto inventory = requireInventoryAvailable(handle, filename);
    if (!inventory) { return std::unexpected(inventory.error()); }
    if (encoded.size() > limits.inputBytes) {
      return error(ErrorCode::InvalidFormat, "New configuration exceeds the selected input limit");
    }
    auto parsed = core::Document::parse(std::string(encoded), limits);
    if (!parsed || parsed->root().type != core::Node::Type::Dictionary) {
      return error(ErrorCode::InvalidFormat, "New configuration must be a complete bounded dictionary");
    }
    const auto saving = filename + ".saving";
    const auto backup = filename + ".bak";
    // Allocate every successful-return field before even creating .saving.
    std::expected<ConfigFile, Error> prepared = ConfigFile(std::move(filename), std::move(*parsed),
        RecoverySource::Primary, std::nullopt, false, directory.st_dev, directory.st_ino, limits);
    static_assert(std::is_nothrow_move_constructible_v<decltype(prepared)>);
    const auto absent = [&](const std::string& name) -> std::expected<void, Error> {
      struct stat current{};
      if (::fstatat(handle, name.c_str(), &current, AT_SYMLINK_NOFOLLOW) == 0) {
        return error(ErrorCode::Conflict, "First creation requires absent primary, backup and saving pathnames");
      }
      if (errno != ENOENT) { return error(ErrorCode::Io, "Unable to confirm configuration pathname absence"); }
      return {};
    };
    for (const auto* name : std::array<const std::string*, 3>{&prepared->filename_, &backup, &saving}) {
      const auto checked = absent(*name);
      if (!checked) { return std::unexpected(checked.error()); }
    }
    auto file = detail::writeExclusiveOwned(handle, saving, prepared->document_.originalBytes(), 0600);
    if (!file) { return std::unexpected(std::move(file.error())); }
    struct stat created{};
    if (::fstat(file->get(), &created) != 0) {
      return error(ErrorCode::Io, "Unable to identify created saving file; saving pathname retained");
    }
    if (!S_ISREG(created.st_mode) || created.st_uid != ::geteuid() || created.st_nlink != 1) {
      return error(ErrorCode::Conflict, "Created saving file ownership changed; pathnames retained");
    }
    // open(...,0600) is filtered by process umask. Normalize only this actual
    // owned descriptor and synchronize metadata if an unusually strict umask
    // removed requested owner bits; never chmod a subsequently opened pathname.
    if ((created.st_mode & 07777) != 0600) {
      if (::fchmod(file->get(), 0600) != 0 || ::fsync(file->get()) != 0 || ::fstat(file->get(), &created) != 0) {
        return error(ErrorCode::Io, "Unable to synchronize private saving mode; saving pathname retained");
      }
    }
    const auto owned = [&]() -> std::expected<void, Error> {
      struct stat opened{}, named{};
      if (::fstat(file->get(), &opened) != 0) {
        return error(ErrorCode::Io, "Unable to verify created saving descriptor; pathnames retained");
      }
      if (::fstatat(handle, saving.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0) {
        return error(errno == ENOENT ? ErrorCode::Conflict : ErrorCode::Io,
            "Saving pathname changed before publication; pathnames retained");
      }
      const auto same = [&](const struct stat& info) {
        return S_ISREG(info.st_mode) && info.st_dev == created.st_dev && info.st_ino == created.st_ino
            && info.st_uid == created.st_uid && info.st_uid == ::geteuid() && info.st_nlink == 1
            && (info.st_mode & 07777) == 0600 && info.st_size == created.st_size
            && platform::sameModificationTime(info, created) && platform::sameChangeTime(info, created);
      };
      if (!same(opened) || !same(named)) {
        return error(ErrorCode::Conflict, "Created saving file changed before publication; pathnames retained");
      }
      return {};
    };
    auto identity = owned();
    if (!identity) { return std::unexpected(identity.error()); }
    if (observer) { access->outside([&]{observer(SaveStage::TemporarySynced);}); }
    struct stat currentDirectory{};
    if (!lease.valid() || lease.directoryHandle() != handle || ::fstat(handle, &currentDirectory) != 0
        || !S_ISDIR(currentDirectory.st_mode) || currentDirectory.st_dev != directory.st_dev
        || currentDirectory.st_ino != directory.st_ino) {
      return error(ErrorCode::UnsafePath, "Held profile directory changed before publication; saving pathname retained");
    }
    for (const auto* name : std::array<const std::string*, 2>{&prepared->filename_, &backup}) {
      const auto checked = absent(*name);
      if (!checked) { return std::unexpected(checked.error()); }
    }
    identity = owned();
    if (!identity) { return std::unexpected(identity.error()); }
    const auto beforePrimary = requireInventoryAvailable(handle, prepared->filename_);
    if (!beforePrimary) { return std::unexpected(beforePrimary.error()); }
    if (platform::renameNoReplaceAt(handle, saving.c_str(), prepared->filename_.c_str()) != 0) {
      const auto code = errno == EEXIST || errno == ENOTEMPTY ? ErrorCode::Conflict
          : errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP ? ErrorCode::Unsupported : ErrorCode::Io;
      return error(code, "First configuration publication failed without replacement; saving pathname retained");
    }
    if (observer) { access->outside([&]{observer(SaveStage::PrimaryRenamed);}); }
    if (::fsync(handle) != 0) {
      return error(ErrorCode::Io, "Directory synchronization failed; first configuration publication already occurred");
    }
    return prepared; // No successful-path allocation after either publication.
  } catch (const std::bad_alloc&) {
    return error(ErrorCode::Io, "Insufficient memory during first creation; saving or primary may remain if publication began");
  }
}

std::expected<void, Error> ConfigFile::saveImpl(const ProfileLease& lease, void (*observer)(SaveStage),
    std::optional<core::Document> replacement) {
  auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
  if(observer&&!access->observationsAllowed()){return error(ErrorCode::Unsupported,"Observed configuration writes require an unnested source boundary");}
  struct stat directory{};
  if (!lease.valid() || ::fstat(lease.directoryHandle(), &directory) != 0
      || static_cast<std::uint64_t>(directory.st_dev) != device_
      || static_cast<std::uint64_t>(directory.st_ino) != inode_) {
    return error(ErrorCode::UnsafePath, "Configuration belongs to a different or released profile lease");
  }
  const auto inventory = requireInventoryAvailable(lease.directoryHandle(), filename_);
  if (!inventory) { return std::unexpected(inventory.error()); }
  if (source_ == RecoverySource::Saving) {
    return error(ErrorCode::Conflict, "Existing recovery file must be resolved before saving");
  }
  auto current = readFile(lease.directoryHandle(), filename_, limits_.inputBytes);
  if (!current) {
    return std::unexpected(current.error());
  }
  const std::string_view expected = source_ == RecoverySource::Primary ? std::string_view(document_.originalBytes())
      : (damagedPrimary_ ? std::string_view(*damagedPrimary_) : std::string_view{});
  if (current->has_value() == primaryMissing_ || (current->has_value() && **current != expected)) {
    return error(ErrorCode::Conflict, "Configuration changed since loading");
  }
  const bool rememberReplacement = replacement.has_value();
  if (!replacement) {
    auto parsed = core::Document::parse(document_.serialize(), limits_);
    if (!parsed) {
      return error(ErrorCode::InvalidFormat, "Edited configuration exceeds the selected limits");
    }
    replacement.emplace(std::move(*parsed));
  }
  const auto& data = replacement->originalBytes();
  struct stat metadata{};
  mode_t mode = 0600;
  if (::fstatat(lease.directoryHandle(), filename_.c_str(), &metadata, AT_SYMLINK_NOFOLLOW) == 0) {
    mode = metadata.st_mode & 0777;
  }
  const auto saving = filename_ + ".saving";
  auto written = writeExclusive(lease.directoryHandle(), saving, data, mode);
  if (!written) {
    return std::unexpected(written.error());
  }
  if (observer) { access->outside([&]{observer(SaveStage::TemporarySynced);}); }
  const auto afterTemporary = requireInventoryAvailable(lease.directoryHandle(), filename_);
  if (!afterTemporary) { return std::unexpected(afterTemporary.error()); }
  if (source_ == RecoverySource::Primary) {
    const auto backup = filename_ + ".bak";
    if (::renameat(lease.directoryHandle(), filename_.c_str(), lease.directoryHandle(), backup.c_str()) != 0
        || ::fsync(lease.directoryHandle()) != 0) {
      return error(ErrorCode::Io, "Backup transition failed; saving file retained for recovery");
    }
    if (observer) { access->outside([&]{observer(SaveStage::BackupRenamed);}); }
  }
  const auto beforePrimary = requireInventoryAvailable(lease.directoryHandle(), filename_);
  if (!beforePrimary) { return std::unexpected(beforePrimary.error()); }
  if (::renameat(lease.directoryHandle(), saving.c_str(), lease.directoryHandle(), filename_.c_str()) != 0) {
    return error(ErrorCode::Io, "Configuration replacement failed; recovery files retained");
  }
  if (observer) { access->outside([&]{observer(SaveStage::PrimaryRenamed);}); }
  if (::fsync(lease.directoryHandle()) != 0) {
    return error(ErrorCode::Io, "Directory synchronization failed; configuration replacement already occurred");
  }
  recordSuccessfulSave(lease, std::move(*replacement), rememberReplacement);
  return {};
}

} // namespace biglybtc::storage
