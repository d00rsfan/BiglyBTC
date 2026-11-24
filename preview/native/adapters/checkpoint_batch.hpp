#pragma once

#include "torrent_state.hpp"
#include <span>
#include <ctime>

namespace biglybtc::storage {
struct CheckpointState {
  const TorrentStateStore* store;
  TorrentStateFile* state;
  // Reuse an identical primary only if its timestamp already covers every
  // flushed payload file. Other storage layouts keep ordinary replacement.
  std::optional<timespec> latestPayload{};
};
class CheckpointBatch final {
public:
  static std::expected<void,Error> save(const ProfileLease&,std::span<const CheckpointState>);
};
}
