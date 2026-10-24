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
  // Only after this exact handle's save-resume flush + storage weak-expiration
  // removal fence. Totals are this admission's actual payload byte deltas;
  // they are checked and added to the captured saved cumulative counters.
  // allComplete must be actual is_seeding, never selected-files is_finished.
  std::expected<void,storage::Error> checkpoint(const storage::ProfileLease&,
      std::span<const std::uint8_t> verifiedPieces,bool allComplete,
      std::int64_t downloaded,std::int64_t uploaded);
private:
  struct Impl;
  explicit ManagedStorage(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};

} // namespace detail
} // namespace biglybtc::engine
