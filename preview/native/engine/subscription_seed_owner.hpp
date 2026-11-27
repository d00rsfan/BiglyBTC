// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "dormant_seed_demand.hpp"
#include "subscription_seed_runtime.hpp"
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>

#include <memory>

namespace libtorrent { struct session; }

namespace biglybtc::engine::detail {

class MetadataDiskPolicy;
class OriginalDiskFiles;

struct PreparedSubscriptionTorrent {
  libtorrent::add_torrent_params parameters;
  std::shared_ptr<OriginalDiskFiles> files;
};

// Prepare on the filesystem worker. The session must use the same disk policy.
std::optional<PreparedSubscriptionTorrent> prepareSubscriptionTorrent(
    const libtorrent::sha1_hash&,const SubscriptionSeedSource&,
    const std::filesystem::path& storageSlot,MetadataDiskPolicy&);

class SubscriptionSeedOwner {
public:
  static bool supported() noexcept;
  // Worker-only. The network extension records inbound demand without opening
  // files or adopting the first connection. A peer can retry after checking.
  void refresh(libtorrent::session&, MetadataDiskPolicy&, std::span<const SubscriptionSeedHash>,
      const SubscriptionSeedResolver&);
  void withdraw(libtorrent::session&, const libtorrent::info_hash_t&);
  void clear(libtorrent::session&);
  bool listening() const noexcept { return !candidates_.empty(); }
  std::span<const libtorrent::sha1_hash> candidates() const noexcept { return candidates_; }

private:
  using Clock = std::chrono::steady_clock;
  struct Active {
    libtorrent::torrent_handle handle;
    SubscriptionSeedSource source;
    Clock::time_point expires;
    Clock::time_point revalidateAt;
    bool ready = false;
  };
  std::shared_ptr<DormantSeedDemand> demand_;
  std::vector<libtorrent::sha1_hash> candidates_;
  std::map<libtorrent::sha1_hash, Active> active_;
  std::uint64_t nextStorageSlot_ = 0;
};

} // namespace biglybtc::engine::detail
