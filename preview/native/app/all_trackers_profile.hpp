// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include <biglybtc/library.hpp>
#include <biglybtc/tracker_tag_rules.hpp>
#include <stop_token>
#include <map>

namespace biglybtc::app {
struct AllTrackerOrigin {
  core::tags::tracker::Origin tracker;
  unsigned torrents=0;
  bool operator==(const AllTrackerOrigin&)const=default;
};
struct AllTrackerSubject {
  core::TorrentHash hash;
  std::string name;
  std::vector<core::tags::tracker::Endpoint> trackers;
};
struct AllTrackerLibrary {
  std::uint64_t sourceRevision=0;
  std::vector<AllTrackerOrigin> origins;
  std::vector<AllTrackerSubject> subjects;
};
using AllTrackerMembership=std::map<std::string,bool,std::less<>>;
std::expected<AllTrackerMembership,storage::Error> reconcileAllTrackerSelection(
    const AllTrackerLibrary& current,const AllTrackerLibrary* previous,
    const AllTrackerMembership& prior,std::span<const std::string> selectors,bool propertyChanged);
// Holds the normal profile read turn and validates only the original saved
// inventory and metainfo. Tracker routes and payload files are never opened.
std::expected<AllTrackerLibrary,storage::Error> readAllTrackerLibrary(
    const storage::ProfileLease&,std::stop_token={});
}
