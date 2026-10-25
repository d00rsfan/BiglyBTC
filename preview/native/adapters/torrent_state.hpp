#pragma once

#include "biglybtc/library.hpp"
#include "encoded_document.hpp"
#include "profile.hpp"

namespace biglybtc::storage {

enum class StateSaveStage { TemporarySynced, BackupSynced, PrimaryRenamed };

class TorrentStateFile final {
public:
  TorrentStateFile(TorrentStateFile&&) noexcept = default;
  TorrentStateFile& operator=(TorrentStateFile&&) noexcept = default;
  TorrentStateFile(const TorrentStateFile&) = delete;
  TorrentStateFile& operator=(const TorrentStateFile&) = delete;
  const core::Document& document() const { return document_.document(); }
  const core::TorrentHash& hash() const { return hash_; }
  bool recoveredBackup() const { return backup_; }
  bool prepared() const { return prepared_; }
  std::expected<void, Error> replaceSection(std::string_view key, std::string encoded);
private:
  friend class TorrentStateStore;
  TorrentStateFile(core::TorrentHash hash, EncodedDocument document,
      std::optional<std::string> original, bool backup, std::uint64_t device, std::uint64_t inode)
      : hash_(std::move(hash)), document_(std::move(document)), original_(std::move(original)),
        backup_(backup), device_(device), inode_(inode) {}
  core::TorrentHash hash_;
  EncodedDocument document_;
  std::optional<std::string> original_;
  bool backup_;
  bool prepared_ = false;
  bool attributesOnly_ = false;
  std::optional<std::string> attributeCache_;
  std::uint64_t device_;
  std::uint64_t inode_;
};

class TorrentStateStore final {
public:
  static std::expected<TorrentStateStore, Error> open(const ProfileLease& lease);
  TorrentStateStore(TorrentStateStore&& other) noexcept;
  TorrentStateStore& operator=(TorrentStateStore&& other) noexcept;
  TorrentStateStore(const TorrentStateStore&) = delete;
  TorrentStateStore& operator=(const TorrentStateStore&) = delete;
  ~TorrentStateStore();
  std::expected<TorrentStateFile, Error> load(const ProfileLease& lease, const core::TorrentHash& hash) const;
  // Read-only authoritative cached-attribute projection. Saving this snapshot
  // changes only attributes, retaining primary resume/info bytes. No payload IO.
  std::expected<TorrentStateFile, Error> loadAttributes(const ProfileLease&, const core::TorrentHash&) const;
  std::expected<void, Error> saveAttributes(const ProfileLease&, TorrentStateFile&, std::string encodedAttributes) const;
  // Hydrate/remove this entry from the global cache, then durably mark all its
  // pieces for recheck before permitting payload allocation/writes.
  std::expected<TorrentStateFile, Error> beginUpdate(const ProfileLease& lease, const core::TorrentHash& hash) const;
  std::expected<void, Error> save(const ProfileLease& lease, TorrentStateFile& state,
      void (*observer)(StateSaveStage) = nullptr) const;
private:
  TorrentStateStore(int active, std::uint64_t device, std::uint64_t inode,
      std::uint64_t activeDevice, std::uint64_t activeInode)
      : active_(active), device_(device), inode_(inode), activeDevice_(activeDevice), activeInode_(activeInode) {}
  std::expected<void, Error> validate(const ProfileLease& lease) const;
  std::expected<void, Error> saveInternal(TorrentStateFile& state,
      void (*observer)(StateSaveStage)) const;
  std::expected<void, Error> removeCachedEntry(TorrentStateFile& state) const;
  int active_;
  std::uint64_t device_;
  std::uint64_t inode_;
  std::uint64_t activeDevice_;
  std::uint64_t activeInode_;
};

} // namespace biglybtc::storage
