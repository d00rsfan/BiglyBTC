#pragma once

#include "biglybtc/library.hpp"
#include "profile.hpp"

#include <libtorrent/add_torrent_params.hpp>
#include <cstdint>
#include <memory>
#include <span>

namespace biglybtc::engine {
struct NetworkPolicy;
namespace detail {

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
      const storage::ProfileLease&,const core::TorrentHash&,const NetworkPolicy&);
  ManagedStorage(ManagedStorage&&) noexcept;
  ManagedStorage& operator=(ManagedStorage&&) noexcept;
  ManagedStorage(const ManagedStorage&) = delete;
  ManagedStorage& operator=(const ManagedStorage&) = delete;
  ~ManagedStorage();
  libtorrent::add_torrent_params parameters() const;
  const ManagedQueueMetadata& queueMetadata() const;
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
