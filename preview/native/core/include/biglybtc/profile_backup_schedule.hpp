// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace biglybtc::core {

struct ProfileBackupSchedule {
  bool enabled = false;
  std::int32_t everyDays = 1;
  std::int32_t everyHours = 0;
  std::int32_t startupDelayMinutes = 5;
  std::int64_t lastBackupDay = 0;
  std::int64_t lastBackupHour = 0;
};

struct ProfileBackupClock {
  std::int64_t nowUtcMillis = 0;
  std::int32_t localOffsetMillis = 0;
  std::int64_t lastAttemptUtcMillis = 0;
  bool firstCheck = false;
  bool force = false;
};

enum class ProfileBackupAction { Disabled, Wait, Run };

struct ProfileBackupScheduleResult {
  ProfileBackupAction action = ProfileBackupAction::Disabled;
  std::int64_t delayMillis = 0;
  std::int64_t localDay = 0;
  std::int64_t localHour = 0;
};

ProfileBackupScheduleResult profileBackupSchedule(const ProfileBackupSchedule&, const ProfileBackupClock&) noexcept;

} // namespace biglybtc::core
