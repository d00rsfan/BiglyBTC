#pragma once

#include "biglybtc/library.hpp"
#include "profile.hpp"
#include "original_torrent_identity.hpp"

#include <libtorrent/add_torrent_params.hpp>
#include <cstdint>
#include <memory>
#include <filesystem>
#include <vector>
#include <span>

namespace biglybtc::engine {
struct NetworkPolicy;
namespace detail {

class OriginalDiskFiles;
struct ManagedPayloadCounterBridge;

inline constexpr std::size_t MaximumManagedPayloadClaims=65536;
struct ManagedPayloadClaim {
  std::filesystem::path absolutePath;
  // inode0 means a reserved future path. Known inode claims never depend on
  // file size/mtime, which change during ordinary owned transfer.
  std::uint64_t device=0,inode=0;
};

struct ManagedQueueMetadata {
  std::int64_t position = 1, downloaded = 0, uploaded = 0, discarded = 0, hashFailed = 0;
  std::int64_t secondsDownloading = 0, secondsSeeding = 0;
  std::optional<std::int32_t> minimumShareRatio, maximumShareRatio;
};

// Owns authenticated payload descriptors/state for ONE managed admission. The
// caller retains the same profile lease for preparation, engine use and save.
class ManagedStorage final {
public:
  static std::expected<ManagedStorage,storage::Error> prepare(
      const storage::ProfileLease&,const core::TorrentHash&,const NetworkPolicy&,
      std::span<const ManagedPayloadClaim> claimed={},
      std::span<const storage::OriginalTorrentIdentity> retainedIdentities={},
      std::shared_ptr<const ManagedPayloadCounterBridge> swarmBridge={});
  ManagedStorage(ManagedStorage&&) noexcept;
  ManagedStorage& operator=(ManagedStorage&&) noexcept;
  ManagedStorage(const ManagedStorage&) = delete;
  ManagedStorage& operator=(const ManagedStorage&) = delete;
  ~ManagedStorage();
  libtorrent::add_torrent_params parameters() const;
  const ManagedQueueMetadata& queueMetadata() const;
  std::shared_ptr<OriginalDiskFiles> mappedFiles() const;
  std::int64_t originalBytes() const noexcept;
  bool originalProgressRequiresPieces() const noexcept;
  std::expected<std::int64_t,storage::Error> verifiedOriginalBytes(std::span<const std::uint8_t>) const;
  std::span<const ManagedPayloadClaim> claims() const noexcept;
  const storage::OriginalTorrentIdentity& identity() const;
  // Only after this exact handle's save-resume flush + storage weak-expiration
  // removal fence. Totals are this admission's actual payload byte deltas;
  // they are checked and added to the captured saved cumulative counters.
  // allComplete must be actual is_seeding, never selected-files is_finished.
  std::expected<void,storage::Error> checkpoint(const storage::ProfileLease&,
      std::span<const std::uint8_t> verifiedPieces,bool allComplete,
      std::int64_t downloaded,std::int64_t uploaded,
      std::int64_t secondsDownloading = 0, std::int64_t secondsSeeding = 0);
private:
  struct Impl;
  explicit ManagedStorage(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};

} // namespace detail
} // namespace biglybtc::engine
