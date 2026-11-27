// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "biglybtc/profile_backup_schedule.hpp"
#include "profile_backup.hpp"

namespace biglybtc::storage {

struct ProfileBackupStatus {
  std::int64_t attemptUtcMillis = 0;
  std::string failure;
};

std::expected<ProfileBackupStatus, Error> readProfileBackupStatus(
    const ProfileLease&, const core::Document* originalConfiguration = nullptr);

std::expected<void, Error> recordProfileBackupStatus(const ProfileLease&, ProfileBackupStatus);

struct AutomaticProfileBackup {
  core::ProfileBackupScheduleResult schedule;
  std::optional<ProfileBackup> completed;
  bool notify = false;
  std::optional<Error> retentionError;
  std::optional<Error> statusError;
};

std::expected<AutomaticProfileBackup, Error> pollAutomaticProfileBackup(const ProfileLease&,
    core::ProfileBackupClock, std::string_view localDate, std::stop_token = {});

} // namespace biglybtc::storage
