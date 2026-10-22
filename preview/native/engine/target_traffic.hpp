#pragma once

#include "local_session.hpp"

#include <libtorrent/torrent_status.hpp>

#include <cstring>
#include <optional>

namespace biglybtc::engine::detail {

// Pure fixed-size copy from the status already used for legacy progress. Never
// query another handle/session, substitute global counters or synthesize idle.
inline std::optional<LocalTargetTraffic> targetTraffic(const core::TorrentHash& target,
    const libtorrent::torrent_status& status) noexcept {
  if (status.errc) { return std::nullopt; }
  const auto bytes = target.bytes();
  const bool matches = (bytes.size() == 20 && status.info_hashes.has_v1()
          && std::memcmp(bytes.data(), status.info_hashes.v1.data(), 20) == 0)
      || (bytes.size() == 32 && status.info_hashes.has_v2()
          && std::memcmp(bytes.data(), status.info_hashes.v2.data(), 32) == 0);
  if (!matches) { return std::nullopt; }
  const LocalTargetTraffic result{target, status.download_rate, status.upload_rate,
      status.download_payload_rate, status.upload_payload_rate};
  if (!core::validTargetTraffic(result)) { return std::nullopt; }
  return result;
}

} // namespace biglybtc::engine::detail
