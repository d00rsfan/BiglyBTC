// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "local_session.hpp"
namespace biglybtc::engine {
// Values only,never a writer/admission or retirement receipt. Zero identifies
// the stopped saved projection;nonzero is this worker's current file owner.
struct CurrentTorrentDetails {TorrentDetails details;std::uint64_t admission=0;};
// No SDK request/poll,cache hydration or payload IO. Current retained policy and
// layout win over saved constructor defaults;stopped members use queryTorrent.
std::expected<CurrentTorrentDetails,storage::Error> queryCurrentTorrent(
    const PersistentSession&,const storage::ProfileLease&,const core::TorrentHash&);
}
