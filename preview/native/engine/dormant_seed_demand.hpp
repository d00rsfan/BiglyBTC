// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection_handle.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace biglybtc::engine::detail {

// The SDK owns this extension on its network thread. The profile worker
// supplies the source-authorized v1 hashes and takes coalesced observations.
// The handshake never supplies add_torrent_params or opens payload storage.
class DormantSeedDemand final : public libtorrent::plugin {
public:
  enum class Transport { Http, Udp };
  struct Request {
    Transport transport;
    std::uint64_t nonce;
    bool operator==(const Request&) const = default;
  };

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

  std::vector<libtorrent::sha1_hash> awaitingTracker() {
    const auto now = Clock::now();
    std::scoped_lock lock(mutex_);
    std::vector<libtorrent::sha1_hash> result;
    for (const auto& [hash, candidate] : candidates_) {
      if (candidate.awaiting && !candidate.request && now < candidate.next) { result.push_back(hash); }
    }
    return result;
  }

  // Subscription seeds use peer demand directly, without a tracker receipt.
  std::vector<libtorrent::sha1_hash> takePeerDemand(std::size_t limit) {
    const auto now = Clock::now();
    std::scoped_lock lock(mutex_);
    std::vector<libtorrent::sha1_hash> result;
    for (auto& [hash, candidate] : candidates_) {
      if (result.size() == limit) { break; }
      if (candidate.awaiting && !candidate.request && now < candidate.next) {
        result.push_back(hash);
        candidate.awaiting = false;
      }
    }
    return result;
  }

  bool awaitingTracker(const libtorrent::sha1_hash& hash) {
    const auto now = Clock::now();
    std::scoped_lock lock(mutex_);
    const auto found = candidates_.find(hash);
    return found != candidates_.end() && found->second.awaiting && !found->second.request && now < found->second.next;
  }

  void beginTracker(const libtorrent::sha1_hash& hash, Request request) {
    const auto now = Clock::now();
    std::scoped_lock lock(mutex_);
    const auto candidate = candidates_.find(hash);
    if (candidate != candidates_.end() && candidate->second.awaiting && !candidate->second.request
        && now < candidate->second.next) {
      candidate->second.request = request;
    }
  }

  void finishTracker(const libtorrent::sha1_hash& hash, Request request,
      std::string_view responseKey, std::int64_t observedNanos) {
    const auto now = Clock::now();
    std::scoped_lock lock(mutex_);
    const auto found = candidates_.find(hash);
    if (found == candidates_.end() || found->second.request != request) { return; }
    auto& candidate = found->second;
    candidate.request.reset();
    candidate.awaiting = false;
    if (observedNanos > 0 && now < candidate.next) {
      candidate.confirmation.emplace(std::string(responseKey), observedNanos, now + std::chrono::seconds(30));
    }
  }

  bool confirmed(const libtorrent::sha1_hash& hash, std::string_view responseKey, std::int64_t observedNanos) {
    const auto now = Clock::now();
    std::scoped_lock lock(mutex_);
    const auto found = candidates_.find(hash);
    if (found == candidates_.end() || !found->second.confirmation) { return false; }
    const auto& receipt = *found->second.confirmation;
    return now < receipt.expires && receipt.responseKey == responseKey && receipt.observedNanos == observedNanos;
  }

  void invalidate(const libtorrent::sha1_hash& hash) {
    std::scoped_lock lock(mutex_);
    if (const auto found = candidates_.find(hash); found != candidates_.end()) {
      found->second = Candidate{found->second.next, false, {}, {}};
    }
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
      found->second = Candidate{now + std::chrono::minutes(5), true, {}, {}};
    }
    return false;
  }

private:
  using Clock = std::chrono::steady_clock;
  struct Confirmation {
    std::string responseKey;
    std::int64_t observedNanos;
    Clock::time_point expires;
  };
  struct Candidate {
    Clock::time_point next = Clock::time_point::min();
    bool awaiting = false;
    std::optional<Request> request;
    std::optional<Confirmation> confirmation;
  };
  std::mutex mutex_;
  std::map<libtorrent::sha1_hash, Candidate> candidates_;
};

} // namespace biglybtc::engine::detail
