// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <QVariantMap>
#include <optional>
namespace biglybtc::ui {
// Passive presentation of actual announce or authenticated scrape observations.
// Missing counts remain unavailable; queued state is not an announce failure.
struct TrackerStatusFacts {
 bool live=false,queued=false,paused=false,checking=false,configured=true;
 int outcome=0,peers=-1;QString error;
 std::optional<int> scrapeSeeds,scrapePeers;bool scrapeFailed=false;
};
QVariantMap presentTrackerStatus(const TrackerStatusFacts&);
}
