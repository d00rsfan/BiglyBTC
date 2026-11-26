// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection_handle.hpp>

#include <chrono>
#include <map>
#include <mutex>
#include <span>
#include <vector>

namespace biglybtc::engine::detail {

// The SDK owns this extension on its network thread. The profile worker
// supplies the source-authorized v1 hashes and takes coalesced observations.
// The handshake never supplies add_torrent_params or opens payload storage.
class DormantSeedDemand final : public libtorrent::plugin {
public:
  libtorrent::feature_flags_t implemented_features() override { return unknown_torrent_feature; }

  void replace(std::span<const libtorrent::sha1_hash> hashes) {
    std::scoped_lock lock(mutex_);
    std::map<libtorrent::sha1_hash, Candidate> next;
    for (const auto& hash : hashes) {
      const auto previous = candidates_.find(hash);
      next.try_emplace(hash, previous == candidates_.end() ? Candidate{} : previous->second);
    }
    candidates_.swap(next);
  }

  std::vector<libtorrent::sha1_hash> take() {
    std::scoped_lock lock(mutex_);
    std::vector<libtorrent::sha1_hash> result;
    for (const auto& [hash, candidate] : candidates_) {
      if (candidate.pending) { result.push_back(hash); }
    }
    for (const auto& hash : result) { candidates_.at(hash).pending = false; }
    return result;
  }

  bool on_unknown_torrent(const libtorrent::info_hash_t& hash,
      const libtorrent::peer_connection_handle& peer, libtorrent::add_torrent_params&) override {
    if (!hash.has_v1() || peer.is_outgoing()) { return false; }
    const auto address = peer.remote().address();
    if (address.is_unspecified() || address.is_multicast()) { return false; }
    const auto now = Clock::now();
    std::scoped_lock lock(mutex_);
    const auto found = candidates_.find(hash.v1);
    if (found != candidates_.end() && now >= found->second.next) {
      found->second.pending = true;
      found->second.next = now + std::chrono::minutes(5);
    }
    return false;
  }

private:
  using Clock = std::chrono::steady_clock;
  struct Candidate {
    Clock::time_point next = Clock::time_point::min();
    bool pending = false;
  };
  std::mutex mutex_;
  std::map<libtorrent::sha1_hash, Candidate> candidates_;
};

} // namespace biglybtc::engine::detail
