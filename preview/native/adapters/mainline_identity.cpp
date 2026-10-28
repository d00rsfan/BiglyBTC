#include "mainline_identity.hpp"
#include "file_io.hpp"
#include "plugin_settings.hpp"
#include "identity.hpp" // Existing source-pinned mlDHT plugin target; private dependency.

#include <cerrno>
#include <cstdio>
#include <new>
#include <utility>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::storage {
namespace {
constexpr auto Leaf = "baseID.config";
std::unexpected<Error> error(ErrorCode code, const char* text) {
  return std::unexpected(Error{code, text});
}
bool sameDirectory(const struct stat& a, const struct stat& b) {
  return S_ISDIR(a.st_mode) && S_ISDIR(b.st_mode) && a.st_dev == b.st_dev
      && a.st_ino == b.st_ino && a.st_mode == b.st_mode && a.st_uid == b.st_uid
      && a.st_gid == b.st_gid && b.st_uid == ::geteuid();
}
bool sameFile(const struct stat& a, const struct stat& b) {
  return S_ISREG(a.st_mode) && S_ISREG(b.st_mode) && a.st_dev == b.st_dev
      && a.st_ino == b.st_ino && a.st_mode == b.st_mode && a.st_uid == b.st_uid
      && a.st_gid == b.st_gid && a.st_nlink == b.st_nlink && b.st_nlink == 1
      && b.st_uid == ::geteuid() && a.st_size == b.st_size
      && a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec
      && a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
std::expected<struct stat, Error> profile(const ProfileLease& lease) {
  struct stat result{};
  if (!lease.valid() || ::fstat(lease.directoryHandle(), &result) != 0
      || !S_ISDIR(result.st_mode) || result.st_uid != ::geteuid()) {
    return error(ErrorCode::UnsafePath, "Original Mainline identity requires an owned held profile");
  }
  return result;
}
std::expected<bool, Error> absent(int parent, const char* name) {
  struct stat value{};
  if (::fstatat(parent, name, &value, AT_SYMLINK_NOFOLLOW) == 0) { return false; }
  if (errno == ENOENT) { return true; }
  return error(ErrorCode::Io, "Cannot inspect original Mainline identity pathname");
}
std::expected<detail::Descriptor, Error> directory(int parent, const char* name, struct stat& stamp) {
  detail::Descriptor result(::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (result.get() < 0) {
    if (errno == ENOENT) { return detail::Descriptor{-1}; }
    return error(errno == ELOOP || errno == ENOTDIR ? ErrorCode::UnsafePath : ErrorCode::Io,
        "Cannot open original Mainline identity directory safely");
  }
  struct stat named{};
  if (::fstat(result.get(), &stamp) != 0 || ::fstatat(parent, name, &named, AT_SYMLINK_NOFOLLOW) != 0
      || !sameDirectory(stamp, named)) {
    return error(ErrorCode::UnsafePath, "Original Mainline identity directory is not owned and bound");
  }
  return result;
}
bool currentDirectory(int parent, const char* name, int fd, const struct stat& approved) {
  struct stat held{}, named{};
  return ::fstat(fd, &held) == 0 && ::fstatat(parent, name, &named, AT_SYMLINK_NOFOLLOW) == 0
      && sameDirectory(approved, held) && sameDirectory(held, named);
}
std::expected<std::string, Error> contents(int fd, const struct stat& stamp) {
  std::string result(static_cast<std::size_t>(stamp.st_size), '\0');
  std::size_t offset = 0;
  while (offset < result.size()) {
    const auto count = ::pread(fd, result.data() + offset, result.size() - offset, static_cast<off_t>(offset));
    if (count < 0 && errno == EINTR) { continue; }
    if (count <= 0) { return error(ErrorCode::Io, "Cannot read complete original Mainline identity"); }
    offset += static_cast<std::size_t>(count);
  }
  return result;
}
} // namespace

struct OriginalMainlineIdentity::Impl {
  OriginalMainlineIdentitySnapshot source;
  struct stat profile{}, pluginsStamp{}, directoryStamp{}, fileStamp{};
  detail::Descriptor plugins{-1}, directory{-1}, file{-1};
};
OriginalMainlineIdentity::OriginalMainlineIdentity(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OriginalMainlineIdentity::OriginalMainlineIdentity(OriginalMainlineIdentity&&) noexcept = default;
OriginalMainlineIdentity& OriginalMainlineIdentity::operator=(OriginalMainlineIdentity&&) noexcept = default;
OriginalMainlineIdentity::~OriginalMainlineIdentity() = default;
const OriginalMainlineIdentitySnapshot& OriginalMainlineIdentity::snapshot() const noexcept {
  static const OriginalMainlineIdentitySnapshot empty{};
  return impl_ ? impl_->source : empty;
}
std::expected<OriginalMainlineIdentity, Error> OriginalMainlineIdentity::load(
    const ProfileLease& lease, std::string pluginDirectory, bool restoreIdentity) {
  try {
    auto binding = originalPluginBinding("mldht", std::move(pluginDirectory));
    if (!binding) { return error(ErrorCode::UnsafePath, "Exact original Mainline plugin basename required"); }
    auto held = profile(lease); if (!held) { return std::unexpected(held.error()); }
    auto impl = std::make_unique<Impl>(); impl->profile = *held;
    impl->source.pluginDirectory = std::move(binding->directoryName);
    if (!restoreIdentity) {
      impl->source.state = OriginalMainlineIdentityState::Disabled;
      return OriginalMainlineIdentity(std::move(impl));
    }
    auto plugins = directory(lease.directoryHandle(), "plugins", impl->pluginsStamp);
    if (!plugins) { return std::unexpected(plugins.error()); }
    impl->plugins = std::move(*plugins);
    if (impl->plugins.get() >= 0) {
      auto plugin = directory(impl->plugins.get(), impl->source.pluginDirectory.c_str(), impl->directoryStamp);
      if (!plugin) { return std::unexpected(plugin.error()); }
      impl->directory = std::move(*plugin);
    }
    if (impl->directory.get() >= 0) {
      impl->file = detail::Descriptor(::openat(impl->directory.get(), Leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
      if (impl->file.get() < 0 && errno != ENOENT) {
        return error(errno == ELOOP ? ErrorCode::UnsafePath : ErrorCode::Io,
            "Cannot open original Mainline identity safely");
      }
      if (impl->file.get() >= 0) {
        auto& stamp = impl->fileStamp;
        if (::fstat(impl->file.get(), &stamp) != 0 || !sameFile(stamp, stamp)) {
          return error(ErrorCode::UnsafePath, "Original Mainline identity must be an owned unshared regular file");
        }
        if (stamp.st_size < 0 || static_cast<std::uint64_t>(stamp.st_size) > MaximumBytes) {
          return error(ErrorCode::Unsupported, "Original Mainline identity exceeds its bounded format");
        }
        auto bytes = contents(impl->file.get(), stamp); if (!bytes) { return std::unexpected(bytes.error()); }
        auto parsed = biglybtc::plugins::mldht::readOriginalIdentity(std::move(*bytes));
        if (!parsed) { return error(ErrorCode::InvalidFormat, "Original Mainline identity cannot be imported unchanged"); }
        impl->source.state = OriginalMainlineIdentityState::Imported;
        impl->source.root = parsed->key; impl->source.originalBytes = std::move(parsed->originalBytes);
      }
    }
    OriginalMainlineIdentity result(std::move(impl));
    auto current = result.validate(lease); if (!current) { return std::unexpected(current.error()); }
    return result;
  } catch (const std::bad_alloc&) { return error(ErrorCode::Io, "Cannot allocate original Mainline identity snapshot"); }
}
std::expected<void, Error> OriginalMainlineIdentity::validate(const ProfileLease& lease) const {
  try {
    if (!impl_) { return error(ErrorCode::Conflict, "Original Mainline identity snapshot has retired"); }
    auto held = profile(lease); if (!held) { return std::unexpected(held.error()); }
    if (!sameDirectory(impl_->profile, *held)) { return error(ErrorCode::Conflict, "Original Mainline identity profile changed"); }
    if (impl_->source.state == OriginalMainlineIdentityState::Disabled) { return {}; }
    const auto missing = [](int fd, const char* name) -> std::expected<void, Error> {
      auto value = absent(fd, name); if (!value) { return std::unexpected(value.error()); }
      if (!*value) { return error(ErrorCode::Conflict, "Original Mainline identity source appeared after capture"); }
      return {};
    };
    if (impl_->plugins.get() < 0) { return missing(lease.directoryHandle(), "plugins"); }
    if (!currentDirectory(lease.directoryHandle(), "plugins", impl_->plugins.get(), impl_->pluginsStamp)) {
      return error(ErrorCode::Conflict, "Original Mainline plugins directory changed");
    }
    if (impl_->directory.get() < 0) { return missing(impl_->plugins.get(), impl_->source.pluginDirectory.c_str()); }
    if (!currentDirectory(impl_->plugins.get(), impl_->source.pluginDirectory.c_str(), impl_->directory.get(), impl_->directoryStamp)) {
      return error(ErrorCode::Conflict, "Original Mainline plugin directory changed");
    }
    if (impl_->file.get() < 0) { return missing(impl_->directory.get(), Leaf); }
    struct stat file{}, named{};
    if (::fstat(impl_->file.get(), &file) != 0 || ::fstatat(impl_->directory.get(), Leaf, &named, AT_SYMLINK_NOFOLLOW) != 0
        || !sameFile(impl_->fileStamp, file) || !sameFile(file, named)) {
      return error(ErrorCode::Conflict, "Original Mainline identity file changed");
    }
    auto bytes = contents(impl_->file.get(), file); if (!bytes) { return std::unexpected(bytes.error()); }
    if (*bytes != impl_->source.originalBytes || ::fstat(impl_->file.get(), &file) != 0
        || ::fstatat(impl_->directory.get(), Leaf, &named, AT_SYMLINK_NOFOLLOW) != 0
        || !sameFile(impl_->fileStamp, file) || !sameFile(file, named)
        || !currentDirectory(lease.directoryHandle(), "plugins", impl_->plugins.get(), impl_->pluginsStamp)
        || !currentDirectory(impl_->plugins.get(), impl_->source.pluginDirectory.c_str(), impl_->directory.get(), impl_->directoryStamp)) {
      return error(ErrorCode::Conflict, "Original Mainline identity changed while validating");
    }
    return {};
  } catch (const std::bad_alloc&) { return error(ErrorCode::Io, "Cannot validate original Mainline identity snapshot"); }
}
std::expected<OriginalMainlineIdentitySlot, Error> OriginalMainlineIdentity::select(
    std::span<const MainlineIdentityKey> active) const {
  if (!impl_ || !impl_->source.root || impl_->source.state != OriginalMainlineIdentityState::Imported) {
    return error(ErrorCode::Missing, "No imported original Mainline root identity");
  }
  const auto selected = biglybtc::plugins::mldht::selectOriginalIdentity(*impl_->source.root, active);
  if (!selected) { return error(ErrorCode::Unsupported, "Original Mainline active identity bound exceeded"); }
  return OriginalMainlineIdentitySlot{selected->key, selected->index};
}
std::expected<OriginalMainlineIdentity, Error> OriginalMainlineIdentity::createMissing(
    const ProfileLease& lease, std::string pluginDirectory, bool restoreIdentity,
    void (*observer)(OriginalMainlineIdentityCreateStage)) {
  // This is not the original malformed/restore=false replacement branch.
  if (!restoreIdentity) { return error(ErrorCode::Unsupported, "Original Mainline replacement is not enabled"); }
  try {
    auto ownership = lease.retain(); if (!ownership) { return std::unexpected(ownership.error()); }
    const ProfileLease& held = **ownership;
    auto source = load(held, std::move(pluginDirectory), true);
    if (!source) { return std::unexpected(source.error()); }
    if (source->snapshot().state != OriginalMainlineIdentityState::Missing) {
      return error(ErrorCode::Conflict, "Original Mainline first creation requires an absent primary");
    }
    const auto& captured = *source->impl_;
    if (captured.plugins.get() < 0 || captured.directory.get() < 0) {
      return error(ErrorCode::Missing, "Original Mainline first creation requires existing plugin directories");
    }
    if ((captured.profile.st_mode | captured.pluginsStamp.st_mode | captured.directoryStamp.st_mode) & 0022) {
      return error(ErrorCode::UnsafePath, "Original Mainline creation directories must not allow other writers");
    }
    const int parent = captured.directory.get();
    constexpr auto Pending = "baseID.config.BiglyBTC-pending";
    const auto chainCurrent = [&]() -> std::expected<void, Error> {
      auto retained = held.retain(); if (!retained) { return std::unexpected(retained.error()); }
      auto currentProfile = profile(held); if (!currentProfile) { return std::unexpected(currentProfile.error()); }
      if (!sameDirectory(captured.profile, *currentProfile)
          || !currentDirectory(held.directoryHandle(), "plugins", captured.plugins.get(), captured.pluginsStamp)
          || !currentDirectory(captured.plugins.get(), captured.source.pluginDirectory.c_str(), parent, captured.directoryStamp)) {
        return error(ErrorCode::Conflict, "Original Mainline creation directory chain changed");
      }
      return {};
    };
    auto current = chainCurrent(); if (!current) { return std::unexpected(current.error()); }
    current = source->validate(held); if (!current) { return std::unexpected(current.error()); }
    detail::Descriptor pending(::openat(parent, Pending, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
    detail::Descriptor created{-1};
    std::string bytes;
    struct stat approved{};
    if (pending.get() < 0) {
      if (errno != ENOENT) {
        return error(errno == ELOOP || errno == ENOTDIR ? ErrorCode::UnsafePath : ErrorCode::Io,
            "Cannot open original Mainline pending root safely");
      }
      MainlineIdentityKey key{};
      std::size_t offset = 0;
      while (offset < key.size()) {
        const auto count = ::getrandom(key.data()+offset, key.size()-offset, GRND_NONBLOCK);
        if (count < 0 && errno == EINTR) { continue; }
        if (count <= 0) { return error(ErrorCode::Io, "Cannot generate original Mainline root"); }
        offset += static_cast<std::size_t>(count);
      }
      bytes = biglybtc::plugins::mldht::encodeNewIdentity(key);
      current = source->validate(held); if (!current) { return std::unexpected(current.error()); }
      current = chainCurrent(); if (!current) { return std::unexpected(current.error()); }
      auto written = detail::writeExclusiveOwned(parent, Pending, bytes, 0600);
      if (!written) { return std::unexpected(written.error()); }
      created = std::move(*written);
      if (::fstat(created.get(), &approved) != 0 || !sameFile(approved, approved)) {
        return error(ErrorCode::Conflict, "Created original Mainline pending root changed");
      }
      // Normalize only this exclusively created inode, never a recovery file.
      if ((approved.st_mode & 07777) != 0600 && ::fchmod(created.get(), 0600) != 0) {
        return error(ErrorCode::Io, "Cannot set private original Mainline pending mode");
      }
      if (::fsync(created.get()) != 0 || ::fstat(created.get(), &approved) != 0) {
        return error(ErrorCode::Io, "Cannot synchronize original Mainline pending root");
      }
      // The shared writer retains O_WRONLY authority; read via a separate FD
      // only after proving it is the SAME created inode and complete metadata.
      pending = detail::Descriptor(::openat(parent, Pending, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
      struct stat readStamp{};
      if (pending.get() < 0 || ::fstat(pending.get(), &readStamp) != 0 || !sameFile(approved, readStamp)) {
        return error(ErrorCode::Conflict, "Created original Mainline pending pathname changed");
      }
    } else {
      if (::fstat(pending.get(), &approved) != 0 || !sameFile(approved, approved)
          || (approved.st_mode & 07777) != 0600 || approved.st_size != 41) {
        return error(ErrorCode::UnsafePath, "Recovery requires an owned private canonical pending root");
      }
      auto recovered = contents(pending.get(), approved); if (!recovered) { return std::unexpected(recovered.error()); }
      auto parsed = biglybtc::plugins::mldht::readOriginalIdentity(*recovered);
      if (!parsed || biglybtc::plugins::mldht::encodeNewIdentity(parsed->key) != *recovered) {
        return error(ErrorCode::InvalidFormat, "Original Mainline pending root is not canonical");
      }
      bytes = std::move(*recovered); // Zero roots are valid; no entropy replacement.
    }
    const auto pendingCurrent = [&]() -> std::expected<void, Error> {
      struct stat held{}, named{}, written{};
      if (::fstat(pending.get(), &held) != 0 || ::fstatat(parent, Pending, &named, AT_SYMLINK_NOFOLLOW) != 0
          || !sameFile(approved, held) || !sameFile(held, named) || (held.st_mode & 07777) != 0600
          || held.st_size != 41 || (created.get() >= 0 && (::fstat(created.get(), &written) != 0 || !sameFile(held, written)))) {
        return error(ErrorCode::Conflict, "Original Mainline pending authority changed");
      }
      auto read = contents(pending.get(), held); if (!read) { return std::unexpected(read.error()); }
      if (*read != bytes || ::fstat(pending.get(), &held) != 0 || !sameFile(approved, held)
          || ::fstatat(parent, Pending, &named, AT_SYMLINK_NOFOLLOW) != 0 || !sameFile(held, named)) {
        return error(ErrorCode::Conflict, "Original Mainline pending content changed");
      }
      return chainCurrent();
    };
    current = pendingCurrent(); if (!current) { return std::unexpected(current.error()); }
    if (::fsync(pending.get()) != 0 || ::fsync(parent) != 0) {
      return error(ErrorCode::Io, "Cannot synchronize recoverable original Mainline pending root");
    }
    if (observer) { observer(OriginalMainlineIdentityCreateStage::PendingSynced); }
    current = source->validate(held); if (!current) { return std::unexpected(current.error()); }
    current = pendingCurrent(); if (!current) { return std::unexpected(current.error()); }
    if (::renameat2(parent, Pending, parent, Leaf, RENAME_NOREPLACE) != 0) {
      return error(errno == EEXIST || errno == ENOTEMPTY ? ErrorCode::Conflict
          : errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP ? ErrorCode::Unsupported : ErrorCode::Io,
          "Original Mainline publication refused without replacement; pending retained");
    }
    if (observer) { observer(OriginalMainlineIdentityCreateStage::PrimaryPublished); }
    current = chainCurrent(); if (!current) { return std::unexpected(current.error()); }
    if (::fsync(parent) != 0) {
      return error(ErrorCode::Io, "Original Mainline primary published but directory synchronization failed");
    }
    struct stat published{}, named{}, final{};
    // Our rename may change ctime. Do not refresh authority just from metadata:
    // reread the exact41 bytes, with full stable FD/name stamps on both sides.
    if (::fstat(pending.get(), &published) != 0 || !sameFile(published, published)
        || published.st_dev != approved.st_dev || published.st_ino != approved.st_ino
        || published.st_mode != approved.st_mode || published.st_uid != approved.st_uid || published.st_gid != approved.st_gid
        || published.st_size != 41 || published.st_mtim.tv_sec != approved.st_mtim.tv_sec
        || published.st_mtim.tv_nsec != approved.st_mtim.tv_nsec
        || ::fstatat(parent, Leaf, &named, AT_SYMLINK_NOFOLLOW) != 0 || !sameFile(published, named)) {
      return error(ErrorCode::Conflict, "Published original Mainline root lost its owned inode");
    }
    auto publishedBytes = contents(pending.get(), published); if (!publishedBytes) { return std::unexpected(publishedBytes.error()); }
    if (*publishedBytes != bytes || ::fstat(pending.get(), &final) != 0 || !sameFile(published, final)
        || ::fstatat(parent, Leaf, &named, AT_SYMLINK_NOFOLLOW) != 0 || !sameFile(final, named)) {
      return error(ErrorCode::Conflict, "Published original Mainline content changed");
    }
    auto reloaded = load(held, captured.source.pluginDirectory, true);
    if (!reloaded) { return std::unexpected(reloaded.error()); }
    if (reloaded->snapshot().state != OriginalMainlineIdentityState::Imported
        || reloaded->snapshot().originalBytes != bytes || !sameFile(final, reloaded->impl_->fileStamp)
        || !sameDirectory(captured.pluginsStamp, reloaded->impl_->pluginsStamp)
        || !sameDirectory(captured.directoryStamp, reloaded->impl_->directoryStamp)) {
      return error(ErrorCode::Conflict, "Reloaded original Mainline root differs from its publication");
    }
    current = chainCurrent(); if (!current) { return std::unexpected(current.error()); }
    current = reloaded->validate(held); if (!current) { return std::unexpected(current.error()); }
    return reloaded;
  } catch (const std::bad_alloc&) {
    return error(ErrorCode::Io, "Cannot allocate original Mainline first creation; pending or primary may remain");
  }
}
} // namespace biglybtc::storage
