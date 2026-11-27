// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "embedded_tracker.hpp"
#include <array>
#include <chrono>
#include <map>
#include <string_view>
namespace biglybtc::tracker::detail {
struct WirePolicy {
  unsigned interval=120,minInterval=120,scrapeInterval=240,maxPeers=100;
  bool compact=true,sendPeerIds=true,keyEnabled=true,fullScrape=true;
};
struct WireRequest {
  bool announce=false,fullScrape=false,compact=false,noPeerId=false,udp=false;
  std::vector<std::string> hashes;
  std::string peerId,event,key;
  std::optional<std::string> ip;
  std::string authorization;
  std::uint16_t port=0;
  std::int64_t uploaded=0,downloaded=0,left=0,numWant=-1;
};
std::expected<WireRequest,std::string> parseTrackerRequest(std::string_view raw);
struct WirePeer {
  std::string id,key;std::array<unsigned char,4> address{};std::uint16_t port=0;
  std::int64_t uploaded=0,downloaded=0,left=0;
  bool completed=false;
  std::chrono::steady_clock::time_point seen;
};
struct WireTorrent {
  bool serving=false;
  TrackerRuntimeStats stats;
  std::map<std::string,WirePeer> peers;
};
struct UdpAnnounceValues {
  std::uint32_t interval,leechers,seeds;
  std::string peers;
};
struct WireResponse {
  std::string body;
  std::optional<std::string> accountingHash;
  std::optional<UdpAnnounceValues> udpAnnounce{};
  std::map<std::string,std::array<std::uint32_t,3>> udpScrape{};
  std::string udpFailure{};
};
enum class UdpAction : std::uint32_t { Connect=0, Announce=1, Scrape=2, Error=3 };
struct UdpRequest {
  UdpAction action;
  std::uint32_t transaction;
  std::uint64_t connection;
  WireRequest wire;
};
std::optional<UdpRequest> parseUdpTrackerRequest(std::string_view raw);
std::string udpConnectResponse(std::uint32_t transaction,std::uint64_t connection);
std::string udpTrackerResponse(const UdpRequest&,const WireResponse&);
// Pure values only. The concrete service supplies its authenticated registry
// and actual socket address; this helper grants no listener/profile authority.
WireResponse trackerResponse(const WireRequest&,std::array<unsigned char,4>,
    std::map<std::string,WireTorrent>&,const WirePolicy&,
    std::chrono::steady_clock::time_point);
std::string trackerFailure(std::string_view);
std::string trackerHttpResponse(std::string_view body);
} // namespace biglybtc::tracker::detail
