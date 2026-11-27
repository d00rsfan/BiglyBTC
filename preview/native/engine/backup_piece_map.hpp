// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <libtorrent/torrent_status.hpp>

#include <cstdint>
#include <span>

namespace biglybtc::engine::detail {

inline bool backupPieceMapAvailable(const libtorrent::torrent_status& status) {
  using Status = libtorrent::torrent_status;
  return !status.errc&&!status.pieces.empty()&&
      (status.state==Status::downloading||status.state==Status::finished||status.state==Status::seeding);
}

inline bool backupPieceMapStillPresent(const libtorrent::torrent_status& status,std::span<const std::uint8_t> pieces) {
  if (!backupPieceMapAvailable(status)||static_cast<std::size_t>(status.pieces.size())!=pieces.size()) {
    return false;
  }
  for (int index=0;index<status.pieces.size();++index) {
    if (pieces[static_cast<std::size_t>(index)]&&!status.pieces[libtorrent::piece_index_t{index}]) { return false; }
  }
  return true;
}

} // namespace biglybtc::engine::detail
