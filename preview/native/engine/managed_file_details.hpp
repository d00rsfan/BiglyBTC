// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "local_session.hpp"
namespace biglybtc::engine {
// Values only,never a writer/admission or retirement receipt. Zero identifies
// the stopped saved projection;nonzero is this worker's current file owner.
struct CurrentTorrentFileQuery {
  bool progress = false;
  bool fileStatistics = false;
  bool verifiedWantedPieces = false;
  bool rateLimits = false;
};
struct SelectedTorrentProgress {
  std::int64_t size;
  std::int64_t remaining;
};
struct CurrentTorrentDetails {
  TorrentDetails details;
  std::uint64_t admission=0;
  std::optional<SelectedTorrentProgress> progress{};
  std::optional<std::int64_t> verifiedWantedBytes{};
};
// Current retained policy and layout take precedence. A progress request also
// samples verified pieces from a settled admission. Stopped members use the
// saved file policy. Neither query hydrates a cache or reads payload bytes.
std::expected<CurrentTorrentDetails,storage::Error> queryCurrentTorrent(
    const PersistentSession&,const storage::ProfileLease&,const core::TorrentHash&, CurrentTorrentFileQuery = {});
}
