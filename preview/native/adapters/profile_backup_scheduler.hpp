// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "biglybtc/profile_backup_schedule.hpp"
#include "profile_backup.hpp"
#include <functional>

namespace biglybtc::storage {

struct ProfileBackupStatus {
  std::int64_t attemptUtcMillis = 0;
  std::string failure;
};

std::expected<ProfileBackupStatus, Error> readProfileBackupStatus(
    const ProfileLease&, const core::Document* originalConfiguration = nullptr);

std::expected<void, Error> recordProfileBackupStatus(const ProfileLease&, ProfileBackupStatus);

// Put the last known Java-compatible metadata into a private copy of the configuration.
std::expected<void, Error> mergeProfileBackupImage(const ProfileLease&, core::Document& configuration);

// Merge native backup metadata only after the runtime has released its configuration sources.
std::expected<void, Error> synchronizeProfileBackupWithJava(const ProfileLease&);

struct AutomaticProfileBackup {
  core::ProfileBackupScheduleResult schedule;
  std::optional<ProfileBackup> completed;
  bool notify = false;
  std::optional<Error> retentionError;
  std::optional<Error> statusError;
};

// A due turn captures accepted transfer observations from the profile worker.
using ProfileBackupStatisticsSource = std::function<std::vector<ProfileBackupTransferStatistics>()>;
std::expected<AutomaticProfileBackup, Error> pollAutomaticProfileBackup(const ProfileLease&,
    core::ProfileBackupClock, std::string_view localDate, std::stop_token = {},
    const ProfileBackupStatisticsSource& statistics = {});

} // namespace biglybtc::storage
