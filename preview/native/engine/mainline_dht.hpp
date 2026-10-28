#pragma once

#include "network_policy.hpp"
#include "../adapters/profile.hpp"

#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/socket.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace biglybtc::engine {

struct MainlineDhtOptions {
  int port = 49001;
  bool ipv4 = true, ipv6 = true;
  bool onlyPeerBootstrap = false;
  // Pinned mlDHT engine defaults; an explicit empty vector never falls back.
  std::vector<std::pair<std::string, int>> bootstrapNodes{
      {"dht.transmissionbt.com", 6881}, {"router.bittorrent.com", 6881},
      {"router.utorrent.com", 6881}, {"router.silotis.us", 6881}};
  // Opaque native LT DHT-only state. Legacy contacts are numeric, untrusted
  // bootstrap hints; neither source grants peer or verified-node authority.
  std::string restoredState{};
  std::vector<std::pair<std::string, int>> restoredNodes{};
  bool restoreIdentity = true;
};
struct MainlineDhtPeers {
  libtorrent::sha1_hash hash;
  std::vector<libtorrent::tcp::endpoint> peers;
};
enum class MainlineDhtPhase { Starting, Listening, Failed, Closed };
struct MainlineDhtStatus {
  MainlineDhtPhase phase = MainlineDhtPhase::Starting;
  unsigned short listenPort = 0;
  std::size_t activeHashes = 0;
  // LT API submissions and actual delivered replies/peers, not wire packets.
  std::uint64_t lookups = 0, announces = 0, replies = 0, deliveredPeers = 0;
  bool observationsLost = false;
  // One newly correlated successful PORT-node reply whose endpoint is in the
  // actual active routing table; never addNode registration or a peer count.
  std::uint64_t peerBootstrapProgress = 0;
};

// Worker-confined dedicated session: never owns a profile, torrent or payload.
// Native DHT-only checkpoints are bounded warm hints. TCP/uTP transfers are disabled; LT still
// creates an unused TCP acceptor. Proxied DHT is unsupported without fallback.
class MainlineDhtService final {
public:
  static constexpr std::size_t MaximumActiveHashes = 256;
  static constexpr std::size_t MaximumPeers = 128;
  static constexpr std::size_t MaximumNodes = 64;
  static constexpr std::size_t MaximumStateBytes = 1024 * 1024;
  static constexpr std::size_t MaximumRoutingContacts = 256;
  static constexpr std::size_t MaximumNodeIdentities = 64;
  static std::expected<std::unique_ptr<MainlineDhtService>, storage::Error> create(
      const NetworkPolicy& resolvedPolicy, const MainlineDhtOptions&);
  ~MainlineDhtService();
  MainlineDhtService(const MainlineDhtService&) = delete;
  MainlineDhtService& operator=(const MainlineDhtService&) = delete;

  // Each explicit call re-arms a round; calls before the next poll coalesce.
  std::expected<void, storage::Error> lookup(libtorrent::sha1_hash);
  // This bounded service requires a distinct DHT/transfer port.
  std::expected<void, storage::Error> announce(libtorrent::sha1_hash, unsigned short actualPeerPort);
  std::expected<void, storage::Error> addNode(const std::pair<std::string, int>&);
  // Numeric endpoint supplied by an authenticated current peer PORT event.
  // One bounded lifetime probe per endpoint; no DNS, retry or peer authority.
  // Actual response/routing progress is reported separately in status().
  std::expected<void, storage::Error> addPeerNode(const std::pair<std::string, int>&);
  // LT has no request cancellation: this only revokes result delivery. Reusing
  // a hash cannot distinguish an old reply; peer hints carry no admission proof.
  std::expected<void, storage::Error> forget(libtorrent::sha1_hash);
  std::expected<std::vector<MainlineDhtPeers>, storage::Error> poll();
  // Worker/live-session only; contains no settings, torrents or profile data.
  std::expected<std::string, storage::Error> checkpoint();
  std::expected<void, storage::Error> close();
  MainlineDhtStatus status() const noexcept;

private:
  struct Impl;
  explicit MainlineDhtService(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};

} // namespace biglybtc::engine
