#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace biglybtc::plugins::speed_scheduler {

inline constexpr std::string_view RuntimeId = "SpeedScheduler";
inline constexpr std::string_view DisplayName = "Speed Scheduler";
inline constexpr std::size_t MaximumSchedules = 10000;
inline constexpr std::size_t MaximumTorrents = 100000;

struct Error { std::string message; };
struct Schedule {
  std::array<bool, 7> days{}; // Sunday = 0, original Calendar convention.
  int start = 8 * 60;
  int end = 17 * 60;
  int upload = 0; // Original KiB/s fields; zero is unlimited.
  int download = 0;
  bool downloadsPaused = false;
  bool seedsPaused = false;
  bool enabled = true;
  bool excludeCategory = false;
  bool onlyCategory = false;
  std::optional<std::string> category{"Uncategorized"};
  bool operator==(const Schedule&) const = default;
};
struct Configuration {
  int defaultUpload = 0;
  int defaultDownload = 0;
  bool defaultsEnforce = true;
  std::vector<Schedule> schedules;
  bool operator==(const Configuration&) const = default;
};
struct LocalMinute { int sundayBasedDay; int minute; };
bool inSpan(const Schedule&, LocalMinute);

enum class TorrentState { Waiting = 1, Preparing, Ready, Downloading, Seeding, Stopping, Stopped, Error, Queued };
struct Torrent {
  std::string id;
  bool complete = false;
  TorrentState state = TorrentState::Waiting;
  bool forceStarted = false;
  bool paused = false;
  bool ownedPause = false;
  // A host supplies either original category (default Uncategorized) or manual
  // tag names, according to the existing use.tags.not.cats configuration.
  std::vector<std::string> memberships{};
};
enum class ActionKind { Pause, Resume, Restart, SetStopReason };
struct Action {
  std::string id;
  ActionKind kind;
  // Pause records ownership and reason "Speed Scheduler" after host success;
  // Resume/Restart removes only this plugin's ownership after host success.
  bool operator==(const Action&) const = default;
};
struct RateState {
  int effectiveUpload = 0;
  int globalDownload = 0;
  bool nonDefaultUploadSet = true; // Original thread restores defaults on first tick.
  bool nonDefaultDownloadSet = true;
};
struct Tick {
  std::optional<int> effectiveUpload;
  std::optional<int> globalDownload;
  bool nonDefaultUploadSet = false;
  bool nonDefaultDownloadSet = false;
  std::vector<std::size_t> selectedSchedules;
  std::vector<Action> actions;
};
std::expected<Tick, Error> evaluate(const Configuration&, LocalMinute,
    std::span<const Torrent>, RateState, bool enabled = true, bool overrideForced = false);

// Read-only XML import: the original source remains available byte for byte.
struct OriginalXml { Configuration configuration; std::string bytes; };
std::expected<OriginalXml, Error> readOriginalXml(std::string input);

} // namespace biglybtc::plugins::speed_scheduler
