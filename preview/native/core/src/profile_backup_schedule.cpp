// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/profile_backup_schedule.hpp"

#include <algorithm>
#include <limits>

namespace biglybtc::core {
namespace {
constexpr std::int64_t Minute = 60000;
constexpr std::int64_t Hour = 60 * Minute;
constexpr std::int64_t Day = 24 * Hour;

std::int64_t localMillis(const ProfileBackupClock& clock) noexcept {
  const auto localOffset = clock.localOffsetMillis;
  const auto utc = clock.nowUtcMillis;
  if (localOffset > 0 && utc > std::numeric_limits<std::int64_t>::max() - localOffset) {
    return std::numeric_limits<std::int64_t>::max();
  }
  if (localOffset < 0 && utc < std::numeric_limits<std::int64_t>::min() - localOffset) {
    return std::numeric_limits<std::int64_t>::min();
  }
  return utc + localOffset;
}
} // namespace

ProfileBackupScheduleResult profileBackupSchedule(const ProfileBackupSchedule& schedule,
    const ProfileBackupClock& clock) noexcept {
  if (!schedule.enabled) { return {}; }
  const auto local = localMillis(clock);
  ProfileBackupScheduleResult result{ProfileBackupAction::Wait, 0, local / Day, local / Hour};
  if (clock.force) {
    result.action = ProfileBackupAction::Run;
    return result;
  }
  if (clock.firstCheck) {
    result.delayMillis = std::max(1, schedule.startupDelayMinutes) * Minute;
    return result;
  }

  const bool daily = schedule.everyDays > 0;
  const auto period = daily ? Day : Hour;
  const auto index = daily ? result.localDay : result.localHour;
  const auto last = std::min(index, daily ? schedule.lastBackupDay : schedule.lastBackupHour);
  const auto interval = daily ? schedule.everyDays : std::max(1, schedule.everyHours);
  // Compare bucket indices before converting a saved interval to milliseconds.
  const auto threshold = index - interval;
  const auto untilDue = last < threshold ? 0 : (last + interval - index) * period - local % period;
  if (untilDue > 0) {
    result.delayMillis = std::max(Minute, untilDue);
    return result;
  }
  const auto cooldown = daily ? 4 * Hour : 10 * Minute;
  if (clock.nowUtcMillis >= std::numeric_limits<std::int64_t>::min() + cooldown
      && clock.lastAttemptUtcMillis <= clock.nowUtcMillis - cooldown) {
    result.action = ProfileBackupAction::Run;
  } else {
    result.delayMillis = cooldown;
  }
  return result;
}

} // namespace biglybtc::core
