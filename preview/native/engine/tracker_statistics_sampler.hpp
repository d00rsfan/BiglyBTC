#pragma once
#include "biglybtc/tracker_statistics.hpp"
#include <libtorrent/torrent_handle.hpp>
namespace biglybtc::engine {
// Owner worker only. Does not announce, scrape, change settings, consume alerts
// or start a session. Disabled isolation policy is checked before any LT call.
core::TrackerStatistics sampleTrackerStatistics(const libtorrent::torrent_handle&,
    core::TrackerPolicy) noexcept;
} // namespace biglybtc::engine
