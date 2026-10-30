// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "network_policy.hpp"
#include "profile.hpp"
#include "biglybtc/library.hpp"
namespace biglybtc::services::i2p::provider {class ProfileSource;}
namespace biglybtc::engine {
// A separate explicit owner path. No caller bool changes the existing Public
// policy reader or its Tor/I2P refusal. Only an actual profile source mints it.
class OwnedI2pPolicy final {
public:
  bool matches(const storage::ProfileLease&,const core::TorrentHash&)const noexcept;
  std::expected<void,NetworkPolicyError> apply(const TorrentNetworkSelection&,libtorrent::add_torrent_params&)const;
  libtorrent::settings_pack settings()const;
private:
  friend class services::i2p::provider::ProfileSource;
  OwnedI2pPolicy(std::shared_ptr<const storage::ProfileLease> lease,core::TorrentHash hash):lease_(std::move(lease)),hash_(std::move(hash)){}
  std::shared_ptr<const storage::ProfileLease> lease_;
  core::TorrentHash hash_;
};
}
