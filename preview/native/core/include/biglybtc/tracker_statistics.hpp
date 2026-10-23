#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace biglybtc::core {
inline constexpr std::size_t MaximumTrackerRows=256;
enum class TrackerPolicy { DisabledByIsolation, Permitted };
enum class TrackerReadState { Unavailable, DisabledByIsolation, Ready };
struct TrackerEndpointFacts {
  std::string localEndpoint;
  int protocol=0; // 1=v1 SHA1, 2=v2 SHA256; zero means no endpoint observation.
  bool enabled=false,updating=false;
  int failures=0,seeders=-1,leechers=-1,downloaded=-1,errorCode=0;
  std::optional<std::int64_t> nextAnnounceSeconds;
};
struct TrackerRow {
  std::string displayUrl; // Origin only: no userinfo, path, query or fragment.
  std::string localEndpoint; // Numeric local listen socket; no hostname lookup.
  std::uint8_t tier=0;
  bool verified=false;
  int protocol=0;
  std::optional<bool> enabled,updating;
  std::optional<int> failures,seeders,leechers,downloaded,errorCode;
  std::optional<std::int64_t> nextAnnounceSeconds;
  bool partial=false;
};
struct TrackerStatistics {
  TrackerReadState state=TrackerReadState::Unavailable;
  std::string torrentHash;
  std::vector<TrackerRow> rows;
  bool truncated=false;
};
// Pure copying projection; missing endpoint and scrape -1 remain unknown.
TrackerRow projectTrackerRow(std::string_view url,std::uint8_t tier,bool verified,
    const std::optional<TrackerEndpointFacts>& endpoint={});
bool validTrackerStatistics(const TrackerStatistics&) noexcept;
} // namespace biglybtc::core
