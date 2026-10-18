#include "profile.hpp"
#include "file_io.hpp"

#include <cerrno>
#include <cctype>
#include <fcntl.h>
#include <sys/stat.h>
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


bool validName(std::string_view name) {
  if (name.empty() || name.size() > 240 || name.front() == '.' || !name.ends_with(".config")) {
    return false;
  }
  for (const unsigned char value : name) {
    if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9') || value == '.' || value == '-' || value == '_')) {
      return false;
    }
  }
  return true;
}


} // namespace

std::expected<ProfileLease, Error> ProfileLease::acquire(const std::filesystem::path& profile) {
  std::error_code ec;
  const auto canonical = std::filesystem::canonical(profile, ec);
  if (ec) {
    return error(ErrorCode::Missing, "Profile directory is unavailable");
  }
  Descriptor directory(::open(canonical.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (directory.get() < 0) {
    return error(ErrorCode::UnsafePath, "Profile directory cannot be opened safely");
  }
  Descriptor lock(::openat(directory.get(), ".azlock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600));
  if (lock.get() < 0) {
    return error(errno == ELOOP ? ErrorCode::UnsafePath : ErrorCode::Io, "Profile lock cannot be opened safely");
  }
  struct stat info{};
  if (::fstat(lock.get(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_uid != ::geteuid()) {
    return error(ErrorCode::UnsafePath, "Profile lock must be an owned regular file");
  }
  struct flock request{};
  request.l_type = F_WRLCK;
  request.l_whence = SEEK_SET;
  // Linux OFD locks conflict with Java POSIX locks, without close-any-fd release.
  if (::fcntl(lock.get(), F_OFD_SETLK, &request) != 0) {
    if (errno == EAGAIN || errno == EACCES) {
      return error(ErrorCode::Busy, "Profile is already in use");
    }
    return error(ErrorCode::Unsupported, "Compatible profile locking is unavailable");
  }
  return ProfileLease(directory.release(), lock.release());
}

ProfileLease::ProfileLease(ProfileLease&& other) noexcept
    : directory_(std::exchange(other.directory_, -1)), lock_(std::exchange(other.lock_, -1)) {}

ProfileLease& ProfileLease::operator=(ProfileLease&& other) noexcept {
  if (this != &other) {
    if (lock_ >= 0) { ::close(lock_); }
    if (directory_ >= 0) { ::close(directory_); }
    directory_ = std::exchange(other.directory_, -1);
    lock_ = std::exchange(other.lock_, -1);
  }
  return *this;
}

ProfileLease::~ProfileLease() {
  if (lock_ >= 0) { ::close(lock_); }
  if (directory_ >= 0) { ::close(directory_); }
}

std::expected<ConfigFile, Error> ConfigFile::load(const ProfileLease& lease, std::string filename, core::Limits limits) {
  if (!lease.valid() || !validName(filename)) {
    return error(ErrorCode::UnsafePath, "Held profile lease and safe configuration filename required");
  }
  struct stat directory{};
  if (::fstat(lease.directoryHandle(), &directory) != 0) {
    return error(ErrorCode::Io, "Unable to identify profile directory");
  }
  auto primary = readFile(lease.directoryHandle(), filename, limits.inputBytes);
  if (!primary) {
    return std::unexpected(primary.error());
  }
  const bool missing = !primary->has_value();
  if (!missing) {
    auto parsed = core::Document::parse(**primary, limits);
    if (parsed && parsed->root().type == core::Node::Type::Dictionary) {
      return ConfigFile(std::move(filename), std::move(*parsed), RecoverySource::Primary,
          std::nullopt, false, directory.st_dev, directory.st_ino, limits);
    }
  }
  const std::string candidates[] = {missing ? ".saving" : ".bak", missing ? ".bak" : ".saving"};
  for (const auto& suffix : candidates) {
    auto candidate = readFile(lease.directoryHandle(), filename + suffix, limits.inputBytes);
    if (!candidate) {
      return std::unexpected(candidate.error());
    }
    if (!candidate->has_value()) {
      continue;
    }
    auto parsed = core::Document::parse(std::move(**candidate), limits);
    if (parsed && parsed->root().type == core::Node::Type::Dictionary) {
      return ConfigFile(std::move(filename), std::move(*parsed),
          suffix == ".bak" ? RecoverySource::Backup : RecoverySource::Saving,
          std::move(*primary), missing, directory.st_dev, directory.st_ino, limits);
    }
  }
  return error(missing ? ErrorCode::Missing : ErrorCode::InvalidFormat, "No complete compatible configuration could be read");
}

std::expected<void, Error> ConfigFile::save(const ProfileLease& lease, void (*observer)(SaveStage)) {
  struct stat directory{};
  if (!lease.valid() || ::fstat(lease.directoryHandle(), &directory) != 0
      || directory.st_dev != device_ || directory.st_ino != inode_) {
    return error(ErrorCode::UnsafePath, "Configuration belongs to a different or released profile lease");
  }
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
  const auto data = document_.serialize();
  auto parsed = core::Document::parse(data, limits_);
  if (!parsed) {
    return error(ErrorCode::InvalidFormat, "Edited configuration exceeds the selected limits");
  }
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
  if (observer) { observer(SaveStage::TemporarySynced); }
  if (source_ == RecoverySource::Primary) {
    const auto backup = filename_ + ".bak";
    if (::renameat(lease.directoryHandle(), filename_.c_str(), lease.directoryHandle(), backup.c_str()) != 0
        || ::fsync(lease.directoryHandle()) != 0) {
      return error(ErrorCode::Io, "Backup transition failed; saving file retained for recovery");
    }
    if (observer) { observer(SaveStage::BackupRenamed); }
  }
  if (::renameat(lease.directoryHandle(), saving.c_str(), lease.directoryHandle(), filename_.c_str()) != 0) {
    return error(ErrorCode::Io, "Configuration replacement failed; recovery files retained");
  }
  if (observer) { observer(SaveStage::PrimaryRenamed); }
  if (::fsync(lease.directoryHandle()) != 0) {
    return error(ErrorCode::Io, "Directory synchronization failed; configuration replacement already occurred");
  }
  document_ = std::move(*parsed);
  source_ = RecoverySource::Primary;
  damagedPrimary_.reset();
  primaryMissing_ = false;
  return {};
}

} // namespace biglybtc::storage
