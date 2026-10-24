#pragma once

#include "biglybtc/bencode.hpp"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/settings_pack.hpp>

#include <expected>
#include <string>
#include <vector>

namespace biglybtc::engine {

enum class NetworkMode { IsolatedLoopback, Normal };
enum class NetworkPolicyErrorCode { InvalidConfiguration, Unsupported, Limit, Memory };
struct NetworkPolicyError {
  NetworkPolicyErrorCode code;
  // Configuration key / bounded field name only. Never contains credentials or URLs.
  std::string field;
};
enum class NetworkProxyKind { None, Socks4, Socks5, Http };
struct NetworkProxy {
  NetworkProxyKind kind = NetworkProxyKind::None;
  std::string host, username, password;
  int port = 0;
  bool peers = false, trackers = false, remoteDns = false;
};
struct NetworkPolicyOptions {
  // Public networking is an explicit caller choice, never inferred from config.
  NetworkMode mode = NetworkMode::IsolatedLoopback;
  // No equivalent original BEP14 setting; opt in explicitly.
  bool localDiscovery = false;
  // Requires both caller permission and original UPnP/NAT-PMP enable settings.
  bool portMapping = false;
};
struct NetworkPolicy {
  NetworkMode mode = NetworkMode::IsolatedLoopback;
  bool incomingTcp = true, outgoingTcp = true;
  bool incomingUtp = false, outgoingUtp = false;
  bool httpTrackers = false, udpTrackers = false;
  bool dht = false, pex = false, lsd = false, webSeeds = false;
  bool upnp = false, natpmp = false, ipv6 = false, requireEncryption = false;
  // Numeric addresses only. Empty original Bind IP becomes explicit wildcards;
  // isolated mode always binds 127.0.0.1:0. No DNS/interface enumeration here.
  std::vector<std::string> bindAddresses{"127.0.0.1"};
  int listenPort = 0;
  NetworkProxy proxy;
  // 0 means unlimited. Positive values are exact configured KiB/s * 1024.
  int uploadBytesPerSecond = 0, downloadBytesPerSecond = 0;
  int seedingUploadBytesPerSecond = 0;
  bool seedingOnlyUpload = false;
};
struct TorrentNetworkSelection {
  bool publicNetwork = true, tor = false, i2p = false;
  bool dhtBackup = true, webSeeds = true;
  // Additional conservative restriction; ti->priv() is always enforced too.
  bool privateTorrent = false;
};

// nullptr is a missing config and selects pinned original defaults. The input is
// borrowed read-only; unknown keys, original bytes and edit state remain untouched.
// Known malformed/routing-incompatible settings fail instead of enabling fallback.
std::expected<NetworkPolicy, NetworkPolicyError> readNetworkPolicy(
    const core::Document* configuration, NetworkPolicyOptions options = {});

// These pure operations create values only: no sessions, DNS, sockets, IO or probes.
// Seeding-only is whole-session mode chosen by the owner, not per-torrent seeding.
std::expected<libtorrent::settings_pack, NetworkPolicyError> makeNetworkSettings(
    const NetworkPolicy&, bool seedingOnly = false);

// Call on decoded params AFTER resume/torrent sources are merged, BEFORE add.
// Explicit override flags prevent removed metadata/resume sources reappearing.
// No mutation on failure; unrelated params/flags remain unchanged. Subsequent
// tracker/webseed mutations must go through the same policy boundary.
std::expected<void, NetworkPolicyError> applyTorrentNetworkPolicy(
    const NetworkPolicy&, const TorrentNetworkSelection&, libtorrent::add_torrent_params&);

} // namespace biglybtc::engine
