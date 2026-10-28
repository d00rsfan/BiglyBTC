#pragma once
#include "biglybtc/library.hpp"
#include "../services/mainline_scrape.hpp"
#include <cstdint>
#include <string>
#include <string_view>
namespace biglybtc::engine {
// Passive UI/service value only. No LT header, socket, lease or admission.
struct MainlineScrapeEstimate {
  core::TorrentHash hash;
  std::string scope;
  std::uint64_t round=0;
  std::int64_t startedWallMillis=0;
  services::mainline_scrape::Summary counts;
  // Exact constructor fields from DHTScrapeResult, never a tracker digest.
  static constexpr std::string_view SourceProtocol="dht",SourceHost="mldht",SourceFile="scrape";
};
} // namespace biglybtc::engine
