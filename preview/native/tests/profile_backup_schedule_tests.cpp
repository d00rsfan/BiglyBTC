// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/profile_backup_schedule.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace biglybtc::core;
constexpr std::int64_t Minute = 60000;
constexpr std::int64_t Hour = 60 * Minute;
constexpr std::int64_t Day = 24 * Hour;
constexpr std::int64_t Date = 20000;
constexpr std::int64_t Morning = Date * Day + 5 * Hour;

void check(bool valid, const char* message) {
  if (!valid) { throw std::runtime_error(message); }
}

void verifyStartup() {
  ProfileBackupSchedule config;
  ProfileBackupClock clock{.nowUtcMillis = Morning, .firstCheck = true, .force = true};
  check(profileBackupSchedule(config, clock).action == ProfileBackupAction::Disabled,
      "Disabled backup remains disabled for a forced request");
  config.enabled = true;
  clock.force = false;
  auto initial = profileBackupSchedule(config, clock);
  check(initial.action == ProfileBackupAction::Wait && initial.delayMillis == 5 * Minute, "Use the original startup delay");
  config.startupDelayMinutes = 0;
  check(profileBackupSchedule(config, clock).delayMillis == Minute, "Use at least one minute at startup");
  clock.force = true;
  const auto forced = profileBackupSchedule(config, clock);
  check(forced.action == ProfileBackupAction::Run && forced.localDay == Date && forced.localHour == Date * 24 + 5,
      "A forced request can run at startup and records both local counters");
}

void verifyHourly() {
  ProfileBackupSchedule config{.enabled = true, .everyDays = -1, .everyHours = 2, .lastBackupHour = Date * 24 + 6};
  ProfileBackupClock clock{.nowUtcMillis = Morning + 15 * Minute, .localOffsetMillis = static_cast<std::int32_t>(2 * Hour)};
  const auto wait = profileBackupSchedule(config, clock);
  check(wait.action == ProfileBackupAction::Wait && wait.delayMillis == 45 * Minute && wait.localHour == Date * 24 + 7,
      "Schedule an hourly backup on the original local-hour boundary");
  clock.nowUtcMillis += 45 * Minute;
  check(profileBackupSchedule(config, clock).action == ProfileBackupAction::Run, "An hourly backup is due at the next saved interval");
  clock.lastAttemptUtcMillis = clock.nowUtcMillis - 9 * Minute;
  auto cooldown = profileBackupSchedule(config, clock);
  check(cooldown.action == ProfileBackupAction::Wait && cooldown.delayMillis == 10 * Minute,
      "An hourly retry uses the whole original cooldown");
  clock.lastAttemptUtcMillis -= Minute;
  check(profileBackupSchedule(config, clock).action == ProfileBackupAction::Run, "The hourly cooldown boundary is inclusive");
}

void verifyDaily() {
  ProfileBackupSchedule config{.enabled = true, .everyDays = 2, .everyHours = 1, .lastBackupDay = Date};
  ProfileBackupClock clock{.nowUtcMillis = Morning};
  auto wait = profileBackupSchedule(config, clock);
  check(wait.action == ProfileBackupAction::Wait && wait.delayMillis == 43 * Hour,
      "Daily scheduling takes precedence when the saved day interval is positive");
  config.lastBackupDay = Date + 90;
  check(profileBackupSchedule(config, clock).delayMillis == wait.delayMillis, "A future saved counter clamps to today");
  config.lastBackupDay = Date - 2;
  clock.lastAttemptUtcMillis = Morning - 3 * Hour;
  wait = profileBackupSchedule(config, clock);
  check(wait.action == ProfileBackupAction::Wait && wait.delayMillis == 4 * Hour,
      "Daily retries wait for the original four-hour cooldown");
  clock.lastAttemptUtcMillis -= Hour;
  check(profileBackupSchedule(config, clock).action == ProfileBackupAction::Run, "The daily cooldown boundary is inclusive");

  config.everyDays = std::numeric_limits<std::int32_t>::max();
  config.lastBackupDay = std::numeric_limits<std::int64_t>::max();
  clock.nowUtcMillis = std::numeric_limits<std::int64_t>::max();
  clock.localOffsetMillis = static_cast<std::int32_t>(Hour);
  wait = profileBackupSchedule(config, clock);
  check(wait.action == ProfileBackupAction::Wait && wait.delayMillis > Day, "Keep an extreme saved interval representable");
}
} // namespace

int main() try {
  verifyStartup();
  verifyHourly();
  verifyDaily();
  std::cout << "Original automatic backup startup, hour and day schedule cases passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
