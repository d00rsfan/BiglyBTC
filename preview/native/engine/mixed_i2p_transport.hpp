// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mixed_i2p_source.hpp"
#include <libtorrent/fwd.hpp>
#include <mutex>
namespace libtorrent {struct BiglybtcMixedI2pTransportPolicyV1;}
namespace biglybtc::engine {
// A borrowed, pinned transport endpoint on the sole Public SDK session. It
// cannot create a torrent or storage. The Native source owner supplies policy.
class MixedI2pTransport final {
public:
  std::expected<void,storage::Error> configure(const libtorrent::BiglybtcMixedI2pTransportPolicyV1&);
  std::expected<void,storage::Error> admit(const MixedManagedSource&,std::uint64_t generation,std::uint64_t route);
  std::expected<void,storage::Error> revoke(const MixedManagedSource&,std::uint64_t generation,std::uint64_t route);
  std::expected<void,storage::Error> withdraw(std::uint64_t generation);
private:
  friend class PersistentSession;
  struct State {
    std::mutex mutex;
    const libtorrent::session_handle* owner=nullptr;
    std::shared_ptr<void> pin;
    std::shared_ptr<const detail::MixedManagedOrigin> origin;
  };
  explicit MixedI2pTransport(std::shared_ptr<State> state):state_(std::move(state)){}
  std::shared_ptr<State> state_;
  void retire()noexcept;
};
}
