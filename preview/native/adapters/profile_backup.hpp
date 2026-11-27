// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"
#include <stop_token>

namespace biglybtc::storage {

struct ProfileBackup {
  std::filesystem::path directory;
  std::uint64_t files = 0;
  std::uint64_t bytes = 0;
};

std::expected<ProfileBackup, Error> backupSavedProfile(const ProfileLease&,
    const std::filesystem::path& destination, std::string_view localDate, std::stop_token = {});

std::expected<std::filesystem::path, Error> restoreProfileBackup(const ProfileLease& current,
    const std::filesystem::path& backup, const std::filesystem::path& destination, std::stop_token = {});

std::expected<void, Error> stageProfileRestore(const ProfileLease& current,
    const std::filesystem::path& backup, std::stop_token = {});

std::expected<bool, Error> applyPendingProfileRestore(const ProfileLease&, std::stop_token = {});

std::expected<std::size_t, Error> rotateSavedProfileBackups(const ProfileLease&, const ProfileBackup& completed,
    std::uint32_t retain, std::stop_token = {});

} // namespace biglybtc::storage
