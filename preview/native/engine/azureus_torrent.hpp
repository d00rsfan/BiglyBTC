// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "original_torrent_identity.hpp"
#include "biglybtc/shared_udp_witness.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <vector>
namespace biglybtc::engine {
inline constexpr std::size_t MaximumAzureusTorrentTargets=8192,MaximumAzureusPeerHints=256;
struct AzureusTorrentTarget {
  core::TorrentHash hash;
  storage::OriginalFullInfoHashes full;
  std::uint64_t admission=0;
  std::uint16_t tcpPort=0,udpPort=0;
  bool seeding=false,crypto=false,trackerHealthy=false;
  std::optional<std::uint32_t> swarmSize;
  std::uint32_t selection=0;
  std::shared_ptr<std::atomic<bool>> live;
  std::uint64_t scrapeSequence=0;
  bool operator==(const AzureusTorrentTarget&)const=default;
};
struct AzureusPeer {
  core::SharedUdpEndpoint tcp;
  std::uint16_t udpPort=0;
  bool crypto=false,seeding=false;
  bool operator==(const AzureusPeer&)const=default;
};
struct AzureusPeerBatch {
  core::TorrentHash hash;std::uint64_t admission=0;
  std::shared_ptr<std::atomic<bool>> live;
  std::vector<AzureusPeer> peers;
};
struct AzureusTorrentObservation {
  std::uint64_t lookups=0,publications=0,withdrawals=0,discoveredPeers=0,deliveredPeers=0,contactSaves=0;
  std::size_t eligible=0,pending=0,readyNetworks=0;
  bool closed=false;std::string state;
  bool operator==(const AzureusTorrentObservation&)const=default;
};
std::string encodeAzureusPeer(std::uint16_t tcpPort,std::uint16_t udpPort,std::uint16_t dhtPort,bool crypto);
std::optional<AzureusPeer> decodeAzureusPeer(std::string_view,std::uint8_t flags,
    core::SharedUdpEndpoint originator,bool ownedLoopback=false);
// Serialized profile-worker-only memo. Every check samples named file identities;
// only unchanged documents are reused. No payload, network or completion grant.
class AzureusTorrentSources final {
public:
  static std::expected<std::unique_ptr<AzureusTorrentSources>,storage::Error> create(const storage::ProfileLease&);
  ~AzureusTorrentSources();
  std::expected<bool,storage::Error> check(const AzureusTorrentTarget&,bool withdrawing=false);
  std::size_t parses()const noexcept;
private:
  struct Impl;explicit AzureusTorrentSources(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}
