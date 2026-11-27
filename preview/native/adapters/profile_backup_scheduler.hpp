// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "biglybtc/profile_backup_schedule.hpp"
#include "profile_backup.hpp"

namespace biglybtc::storage {

struct AutomaticProfileBackup {
  core::ProfileBackupScheduleResult schedule;
  std::optional<ProfileBackup> completed;
  bool notify = false;
};

std::expected<AutomaticProfileBackup, Error> pollAutomaticProfileBackup(const ProfileLease&,
    core::ProfileBackupClock, std::string_view localDate, std::stop_token = {});

} // namespace biglybtc::storage
