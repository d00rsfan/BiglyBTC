#pragma once

#include "biglybtc/library.hpp"

#include <cstdint>

namespace biglybtc::core {

// One torrent's actual status rates in bytes/second. These combined/payload
// fields are independent of the existing payload-only LocalProgress rates.
// This value conveys no session/epoch/sequence, coverage, idle or final authority.
struct TargetTraffic {
  TorrentHash hash;
  std::int64_t downloadRate = 0;
  std::int64_t uploadRate = 0;
  std::int64_t payloadDownloadRate = 0;
  std::int64_t payloadUploadRate = 0;
  bool operator==(const TargetTraffic&) const = default;
};

inline bool validTargetTraffic(const TargetTraffic& value) noexcept {
  const auto size = value.hash.bytes().size();
  return (size == 20 || size == 32)
      && value.payloadDownloadRate >= 0 && value.payloadUploadRate >= 0
      && value.downloadRate >= value.payloadDownloadRate
      && value.uploadRate >= value.payloadUploadRate;
}

} // namespace biglybtc::core
