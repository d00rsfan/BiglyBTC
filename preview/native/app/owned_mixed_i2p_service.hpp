// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owned_i2p_service.hpp"
#include <functional>
#include <memory>
namespace biglybtc::engine {class MixedManagedSource;class MixedI2pTransport;class PendingMetadataSource;struct PendingMagnet;}
namespace biglybtc::app {
// Host-worker affine adapter. Owns only original mixed key0/I2CP/Bridge source
// authority; the borrowed Public engine keeps the sole handle/disk/checkpoint.
class OwnedMixedI2pService final {
public:
  struct Observation {
    core::TorrentHash hash;
    bool active=false,unavailable=false,sourceChanged=false,joined=false,metadataOnly=false;
    std::uint64_t admission=0,generation=0,routeNonce=0;
    std::string failure;
  };
  static bool available()noexcept;
  static std::expected<std::unique_ptr<OwnedMixedI2pService>,storage::Error> create(
      const storage::ProfileLease&,I2pServiceOptions,
      std::shared_ptr<engine::MixedI2pTransport>);
  std::expected<void,storage::Error> prepareMagnet(const engine::PendingMagnet&);
  std::expected<void,storage::Error> bindMetadata(std::shared_ptr<const engine::PendingMetadataSource>);
  std::expected<void,storage::Error> attachMetadata(std::shared_ptr<const engine::PendingMetadataSource>);
  std::expected<void,storage::Error> removeMetadata(const core::TorrentHash&);
  std::expected<void,storage::Error> publishPendingMagnet(const core::TorrentHash&,
      const std::function<std::expected<void,storage::Error>()>&);
  ~OwnedMixedI2pService();
  OwnedMixedI2pService(const OwnedMixedI2pService&)=delete;
  // External I/O only; these never wait on Qt/provider execution. bind is
  // called INSIDE the filesystem-only publication creating real engine storage.
  std::expected<void,storage::Error> prepare(const core::TorrentHash&);
  std::expected<void,storage::Error> bind(std::shared_ptr<const engine::MixedManagedSource>);
  std::expected<void,storage::Error> publish(const core::TorrentHash&,
      const std::function<std::expected<void,storage::Error>()>&);
  std::expected<void,storage::Error> publishPositions(
      const std::function<std::expected<void,storage::Error>()>&);
  bool sourceCurrent()const noexcept; // Atomic/static anchor state only.
  void revoke()noexcept; // Source only; NO Qt/SDK dispatch or wait.
  // Outside ALL source publication gates, after actual Public handle install.
  // attach enqueues a route; only a later active observation proves admission.
  std::expected<void,storage::Error> attach(std::shared_ptr<const engine::MixedManagedSource>);
  std::expected<void,storage::Error> suspend(const core::TorrentHash&);
  // Revoke route then release source child only; no Public pause/save/remove.
  std::expected<void,storage::Error> remove(const core::TorrentHash&);
  std::vector<Observation> observations()const;
  // Withdraw mixed routes and join the private Qt/source graph only. Does not
  // retire or checkpoint the borrowed Public session or any of its torrents.
  std::expected<void,storage::Error> close();
private:
  struct Impl;explicit OwnedMixedI2pService(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}
