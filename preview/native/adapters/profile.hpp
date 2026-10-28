#pragma once

#include "biglybtc/bencode.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <memory>
#include <string>
#include <string_view>

namespace biglybtc::storage {

enum class ErrorCode { Busy, UnsafePath, Io, InvalidFormat, Conflict, Missing, Unsupported, Cancelled };
struct Error {
  ErrorCode code;
  std::string message;
};

class ProfileLease final {
public:
  static std::expected<ProfileLease, Error> acquire(const std::filesystem::path& profile);
  ProfileLease(const ProfileLease&) = delete;
  ProfileLease& operator=(const ProfileLease&) = delete;
  ProfileLease(ProfileLease&& other) noexcept;
  ProfileLease& operator=(ProfileLease&& other) noexcept;
  ~ProfileLease();
  // Retain this actual Linux open-file-description lock, without reacquiring
  // the profile or borrowing the caller's object lifetime. Closing either token
  // leaves Java/native exclusion in place until the last alias is destroyed.
  std::expected<std::shared_ptr<const ProfileLease>, Error> retain() const;
  int directoryHandle() const { return directory_; }
  bool valid() const { return directory_ >= 0 && lock_ >= 0; }
private:
  ProfileLease(int directory, int lock) : directory_(directory), lock_(lock) {}
  int directory_ = -1;
  int lock_ = -1;
};

enum class RecoverySource { Primary, Backup, Saving };
enum class SaveStage { TemporarySynced, BackupRenamed, PrimaryRenamed };

constexpr core::Limits compatibleConfigLimits() {
  return {.inputBytes = 192 * 1024 * 1024, .stringBytes = 192 * 1024 * 1024,
      .nodes = 2000000, .depth = 128};
}

class ConfigFile final {
public:
  static std::expected<ConfigFile, Error> load(const ProfileLease& lease,
      std::string filename, core::Limits limits = compatibleConfigLimits());
  // First publication only: all three fixed candidate pathnames must be absent.
  // Existing regular/symlink/directory/other entries are Conflict, never followed,
  // opened, repaired or removed. Validate/allocate the full returned Primary
  // snapshot before creating .saving, then exclusively write/fsync owned data.
  // Observer order is TemporarySynced, PrimaryRenamed (no BackupRenamed).
  // Hold the actual created fd and compare its identity/metadata with .saving
  // before Linux RENAME_NOREPLACE; a new primary can never be overwritten.
  // New mode is0600, no backup is invented; returned object supports normal save.
  // No cancellation inside this synchronous transaction. Errors may retain an
  // owned .saving or report an already published Primary after sync failure.
  // Path checks/rename are not a transaction against noncooperating swaps.
  static std::expected<ConfigFile, Error> createMissing(const ProfileLease& lease,
      std::string filename, std::string_view encoded,
      core::Limits limits = compatibleConfigLimits(), void (*observer)(SaveStage) = nullptr);
  core::Document& document() { return document_; }
  const core::Document& document() const { return document_; }
  RecoverySource recoverySource() const { return source_; }
  // The optional observer is for deterministic crash/fault tests and diagnostics.
  std::expected<void, Error> save(const ProfileLease& lease,
      void (*observer)(SaveStage) = nullptr);
private:
  ConfigFile(std::string name, core::Document document, RecoverySource source,
      std::optional<std::string> damagedPrimary, bool primaryMissing,
      std::uint64_t device, std::uint64_t inode, core::Limits limits)
      : filename_(std::move(name)), document_(std::move(document)), source_(source),
        damagedPrimary_(std::move(damagedPrimary)), primaryMissing_(primaryMissing),
        device_(device), inode_(inode), limits_(limits) {}
  std::string filename_;
  core::Document document_;
  RecoverySource source_;
  std::optional<std::string> damagedPrimary_;
  bool primaryMissing_;
  std::uint64_t device_;
  std::uint64_t inode_;
  core::Limits limits_;
};

} // namespace biglybtc::storage
