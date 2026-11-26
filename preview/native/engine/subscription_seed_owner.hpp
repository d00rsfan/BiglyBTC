// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "dormant_seed_demand.hpp"
#include "subscription_seed_runtime.hpp"
#include <libtorrent/torrent_handle.hpp>

#include <memory>

namespace libtorrent { struct session; }

namespace biglybtc::engine::detail {

class SubscriptionSeedOwner {
public:
  static bool supported() noexcept;
  // Worker-only. The network extension records inbound demand without opening
  // files or adopting the first connection. A peer can retry after checking.
  void refresh(libtorrent::session&, std::span<const SubscriptionSeedHash>,
      const SubscriptionSeedResolver&);
  void withdraw(libtorrent::session&, const libtorrent::info_hash_t&);
  void clear(libtorrent::session&);
  bool listening() const noexcept { return !candidates_.empty(); }

private:
  using Clock = std::chrono::steady_clock;
  struct Active {
    libtorrent::torrent_handle handle;
    std::string metainfo;
    Clock::time_point expires;
    Clock::time_point revalidateAt;
    bool ready = false;
  };
  std::shared_ptr<DormantSeedDemand> demand_;
  std::vector<libtorrent::sha1_hash> candidates_;
  std::map<libtorrent::sha1_hash, Active> active_;
};

} // namespace biglybtc::engine::detail
