// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mixed_i2p_source.hpp"
#include "pending_metadata_source.hpp"
#include <libtorrent/fwd.hpp>
#include <mutex>
#include <atomic>
namespace libtorrent {struct BiglybtcMixedI2pTransportPolicyV1;}
namespace biglybtc::engine {
// A borrowed, pinned transport endpoint on the sole Public SDK session. It
// cannot create a torrent or storage. The Native source owner supplies policy.
class MixedI2pTransport final {
public:
  std::expected<void,storage::Error> configure(const libtorrent::BiglybtcMixedI2pTransportPolicyV1&);
  std::expected<void,storage::Error> admit(const MixedManagedSource&,std::uint64_t generation,std::uint64_t route);
  std::expected<void,storage::Error> revoke(const MixedManagedSource&,std::uint64_t generation,std::uint64_t route);
  // Before no-ti add: register a distinct real pending entry and authorize only
  // metadata. Generation/route is source-owned; engine admission stays sealed.
  std::expected<void,storage::Error> admitMetadata(const PendingMetadataSource&,std::uint64_t generation,std::uint64_t route);
  std::expected<void,storage::Error> revokeMetadata(const PendingMetadataSource&,std::uint64_t generation,std::uint64_t route);
  std::expected<void,storage::Error> discoveredPeer(const MixedManagedSource&,std::uint64_t generation,std::uint64_t route,const std::array<std::uint8_t,32>&,std::uint64_t operation);
  std::expected<void,storage::Error> discoveredPeer(const PendingMetadataSource&,std::uint64_t generation,std::uint64_t route,const std::array<std::uint8_t,32>&,std::uint64_t operation);
  std::expected<void,storage::Error> withdraw(std::uint64_t generation);
private:
  friend class PersistentSession;
  struct State {
    std::mutex mutex;
    const libtorrent::session_handle* owner=nullptr;
    std::shared_ptr<void> pin;
    std::shared_ptr<const detail::MixedManagedOrigin> origin;
    // Sticky provenance of Accepted configure on this retained qualified owner.
    // Queued old ACCEPT failures can drain after transport withdrawal.
    std::atomic_bool configured=false;
  };
  explicit MixedI2pTransport(std::shared_ptr<State> state):state_(std::move(state)){}
  std::shared_ptr<State> state_;
  void retire()noexcept;
};
}
