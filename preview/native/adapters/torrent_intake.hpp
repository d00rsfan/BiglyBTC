#pragma once

#include "biglybtc/library.hpp"
#include "biglybtc/file_policy.hpp"
#include "biglybtc/torrent_layout.hpp"
#include "profile.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <memory>
#include <stop_token>

namespace biglybtc::storage {

enum class TorrentIntakeStage { JournalSynced, StatePublished, ArchivePublished, InventoryPublished };

// Linux, serialized profile-owner worker only. The caller borrows one held
// lease; no extra lease/engine/payload allocation is performed. destination is
// the parent save directory; metadata's original name becomes save_file.
// Fixed owned paths: active/<UPPERCASE_HASH>.dat, torrents/<HASH>.torrent.
// Full validation precedes the first journal write. An I/O failure can leave a
// durable pending transaction, never a partial downloads.config row. Do not
// retry blindly: run explicit recovery before further inventory mutations.
// forceStart commits forceStart=1 + Waiting atomically (takes precedence over
// stopped); the optional trailing argument preserves existing source call sites.
std::expected<core::TorrentHash, Error> importTorrent(ProfileLease&,
    const std::filesystem::path& metadata, const std::filesystem::path& destination,
    bool stopped, void (*observer)(TorrentIntakeStage) = nullptr, bool forceStart = false);

// Read-only worker preview of the same validated raw metadata. The sealed value
// retains exact source bytes/inode/stat; no payload/profile changes or engine.
class TorrentIntakeMetadata final {
public:
  const core::TorrentHash& hash() const noexcept;
  const compat::OriginalLayout& layout() const noexcept;
  const std::filesystem::path& source() const noexcept;
private:
  struct Data;
  std::shared_ptr<const Data> data_;
  explicit TorrentIntakeMetadata(std::shared_ptr<const Data>);
  friend std::expected<TorrentIntakeMetadata, Error> inspectTorrentIntake(const std::filesystem::path&, std::stop_token);
  friend std::expected<core::TorrentHash, Error> importTorrentWithFileOptions(ProfileLease&,
      const TorrentIntakeMetadata&, const std::filesystem::path&, bool, std::span<const compat::FilePolicy>,
      void (*)(TorrentIntakeStage), bool, std::stop_token);
};
std::expected<TorrentIntakeMetadata, Error> inspectTorrentIntake(
    const std::filesystem::path&, std::stop_token = {});
// Full original-index vector, including immutable skipped padding entries.
// All skipped => Stopped/no force, with no payload allocation. New journal option
// records retain original priority/storage choices through every recovery stage.
// Existing skipped leaves stay L; absent skipped leaves use source C/X policy.
std::expected<core::TorrentHash, Error> importTorrentWithFileOptions(ProfileLease&,
    const TorrentIntakeMetadata&, const std::filesystem::path& destination, bool stopped,
    std::span<const compat::FilePolicy>, void (*observer)(TorrentIntakeStage) = nullptr,
    bool forceStart = false, std::stop_token = {});

bool hasPendingTorrentIntake(const ProfileLease&) noexcept;

// Startup recovery before loading library/admitting operations. No pending
// transaction => nullopt. Rebuilds the exact append from journal input and
// publishes inventory last. Conflicting user edits/artifacts are preserved and
// reported. No generic primary/backup recovery promotion or payload effects.
std::expected<std::optional<core::TorrentHash>, Error> recoverTorrentIntake(
    ProfileLease&, void (*observer)(TorrentIntakeStage) = nullptr);

} // namespace biglybtc::storage
