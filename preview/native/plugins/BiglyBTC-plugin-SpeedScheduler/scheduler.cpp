#include "scheduler.hpp"

#include <algorithm>
#include <set>
#include <tuple>

namespace biglybtc::plugins::speed_scheduler {
namespace {
bool runnable(TorrentState state) {
  return state == TorrentState::Waiting || state == TorrentState::Preparing || state == TorrentState::Ready
      || state == TorrentState::Downloading || state == TorrentState::Seeding || state == TorrentState::Queued;
}
void unpause(Tick& result, const Torrent& torrent) {
  if (!runnable(torrent.state)) {
    result.actions.push_back({torrent.id, torrent.paused ? ActionKind::Resume : ActionKind::Restart});
  }
}
}

bool inSpan(const Schedule& schedule, LocalMinute now) {
  return now.sundayBasedDay >= 0 && now.sundayBasedDay < 7 && now.minute >= 0 && now.minute < 1440
      && schedule.days[static_cast<std::size_t>(now.sundayBasedDay)]
      && now.minute >= std::min(schedule.start, schedule.end) && now.minute <= std::max(schedule.start, schedule.end);
}

std::expected<Tick, Error> evaluate(const Configuration& configuration, LocalMinute now,
    std::span<const Torrent> torrents, RateState rates, bool enabled, bool overrideForced) {
  if (configuration.schedules.size() > MaximumSchedules || torrents.size() > MaximumTorrents
      || now.sundayBasedDay < 0 || now.sundayBasedDay >= 7 || now.minute < 0 || now.minute >= 1440
      || configuration.defaultUpload < 0 || configuration.defaultDownload < 0) {
    return std::unexpected(Error{"Invalid or unbounded scheduler input"});
  }
  std::set<std::string_view> identities;
  std::size_t membershipCount = 0, membershipBytes = 0;
  for (const auto& torrent : torrents) {
    if (torrent.id.empty() || torrent.id.size() > 128 || !identities.insert(torrent.id).second
        || torrent.memberships.size() > 1024 || static_cast<int>(torrent.state) < 1 || static_cast<int>(torrent.state) > 9
        || std::ranges::any_of(torrent.memberships, [](const auto& value) { return value.size() > 65536; })) {
      return std::unexpected(Error{"Invalid or ambiguous scheduler torrent identity/state"});
    }
    membershipCount += torrent.memberships.size();
    if (membershipCount > 1000000) { return std::unexpected(Error{"Scheduler membership count exceeds its bound"}); }
    for (const auto& value : torrent.memberships) {
      if (value.size() > 64 * 1024 * 1024 - membershipBytes) { return std::unexpected(Error{"Scheduler membership bytes exceed their bound"}); }
      membershipBytes += value.size();
    }
  }
  Tick result;
  result.nonDefaultUploadSet = rates.nonDefaultUploadSet;
  result.nonDefaultDownloadSet = rates.nonDefaultDownloadSet;
  if (!enabled) {
    // The original disabled polling branch only releases owned pauses. The
    // explicit plugin setEnabled(false) command restores global defaults itself.
    for (const auto& torrent : torrents) { if (torrent.ownedPause) { unpause(result, torrent); } }
    return result;
  }
  int upload = -1, download = -1;
  bool downloadsPaused = false, seedsPaused = false;
  std::set<std::string_view> excluded, onlyDownloads, onlySeeds;
  // Original Schedule.equals omits category, seed-pause and enabled. Retain its
  // first representative without the source's quadratic Vector.contains scan.
  std::set<std::tuple<int, int, int, int, bool, std::array<bool, 7>>> selectedRules;
  std::size_t categoryBytes = 0;
  for (std::size_t i = 0; i < configuration.schedules.size(); ++i) {
    const auto& schedule = configuration.schedules[i];
    if (schedule.start < 0 || schedule.start >= 1440 || schedule.end < 0 || schedule.end >= 1440
        || schedule.upload < 0 || schedule.download < 0 || (schedule.category && schedule.category->size() > 65536)) {
      return std::unexpected(Error{"Invalid original schedule time, rate or category"});
    }
    if (schedule.category) {
      if (schedule.category->size() > 4 * 1024 * 1024 - categoryBytes) { return std::unexpected(Error{"Scheduler category bytes exceed their bound"}); }
      categoryBytes += schedule.category->size();
    }
    if (!schedule.enabled || !inSpan(schedule, now)) { continue; }
    auto selected = [&] {
      if (selectedRules.emplace(schedule.start, schedule.end, schedule.upload, schedule.download,
          schedule.downloadsPaused, schedule.days).second) {
        result.selectedSchedules.push_back(i);
      }
    };
    if (schedule.upload > upload) { upload = schedule.upload; selected(); }
    if (schedule.category) {
      if (schedule.excludeCategory) { excluded.insert(*schedule.category); }
      else if (schedule.onlyCategory) {
        if (schedule.downloadsPaused) { onlyDownloads.insert(*schedule.category); }
        if (schedule.seedsPaused) { onlySeeds.insert(*schedule.category); }
      }
    }
    if (!schedule.onlyCategory || schedule.excludeCategory) {
      downloadsPaused = downloadsPaused || schedule.downloadsPaused;
      seedsPaused = seedsPaused || schedule.seedsPaused;
    }
    if (schedule.download > download) { download = schedule.download; selected(); }
  }
  // Original applies download actions before seed actions, preserving host order.
  for (const bool seeds : {false, true}) {
    const auto expected = seeds ? TorrentState::Seeding : TorrentState::Downloading;
    const auto globallyPaused = seeds ? seedsPaused : downloadsPaused;
    for (const auto& torrent : torrents) {
      if (torrent.complete != seeds || (torrent.forceStarted && !overrideForced)) { continue; }
      const bool active = torrent.state != TorrentState::Stopped && torrent.state != TorrentState::Stopping;
      const bool excludedMember = std::ranges::any_of(torrent.memberships, [&](const auto& value) { return excluded.contains(value); });
      const auto& selectedOnly = seeds ? onlySeeds : onlyDownloads;
      const bool onlyMember = std::ranges::any_of(torrent.memberships, [&](const auto& value) { return selectedOnly.contains(value); });
      if (globallyPaused) {
        if (active && !excludedMember) { result.actions.push_back({torrent.id, ActionKind::Pause}); }
        else if (torrent.state != expected && torrent.ownedPause) {
          if (excludedMember) { unpause(result, torrent); }
          else { result.actions.push_back({torrent.id, ActionKind::SetStopReason}); }
        }
      } else {
        if (torrent.state != expected && torrent.ownedPause) {
          if (!onlyMember) { unpause(result, torrent); }
          else { result.actions.push_back({torrent.id, ActionKind::SetStopReason}); }
        } else if (active && onlyMember) { result.actions.push_back({torrent.id, ActionKind::Pause}); }
      }
    }
  }
  if (upload >= 0) {
    result.nonDefaultUploadSet = true;
    if (rates.effectiveUpload != upload) { result.effectiveUpload = upload; }
  } else if (rates.nonDefaultUploadSet || configuration.defaultsEnforce) {
    result.nonDefaultUploadSet = false;
    if (rates.effectiveUpload != configuration.defaultUpload) { result.effectiveUpload = configuration.defaultUpload; }
  }
  if (download >= 0) {
    result.nonDefaultDownloadSet = true;
    if (rates.globalDownload != download) { result.globalDownload = download; }
  } else if (rates.nonDefaultDownloadSet || configuration.defaultsEnforce) {
    result.nonDefaultDownloadSet = false;
    if (rates.globalDownload != configuration.defaultDownload) { result.globalDownload = configuration.defaultDownload; }
  }
  return result;
}
} // namespace biglybtc::plugins::speed_scheduler
