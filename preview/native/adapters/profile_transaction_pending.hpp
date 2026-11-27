// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "state_file_io.hpp"
#include <array>

namespace biglybtc::storage::detail {

namespace intakeJournal {
inline constexpr const char* Journal = "BiglyBTC.torrent-import";
inline constexpr const char* JournalSaving = "BiglyBTC.torrent-import.saving";
inline constexpr const char* InventorySaving = "BiglyBTC.torrent-import.inventory";
inline constexpr const char* BackupSaving = "BiglyBTC.torrent-import.backup";
inline constexpr const char* AcquisitionIntent = "BiglyBTC.subscription-acquire";
inline constexpr const char* AcquisitionIntentSaving = "BiglyBTC.subscription-acquire.saving";
inline constexpr const char* AcquisitionIntentBackup = "BiglyBTC.subscription-acquire.bak";
}

namespace archiveJournal {
inline constexpr char Journal[] = "BiglyBTC.archive-restore";
inline constexpr char JournalTemp[] = "BiglyBTC.archive-restore.saving";
inline constexpr char CatalogTemp[] = "BiglyBTC.archive-restore.catalog";
inline constexpr char InventoryTemp[] = "BiglyBTC.archive-restore.inventory";
inline constexpr char TagsTemp[] = "BiglyBTC.archive-restore.tags";
}

// ConfigFile accepts non-hidden *.config names. The legacy names may still be
// present in an existing profile and require the same recovery fence.
inline const std::array<std::string,3> CategoryJournalNames{
    "BiglyBTC.category-delete.config", "BiglyBTC.category-delete.config.bak", "BiglyBTC.category-delete.config.saving"};
inline const std::array<std::string,3> LegacyCategoryJournalNames{
    ".BiglyBTC.category-delete", ".BiglyBTC.category-delete.bak", ".BiglyBTC.category-delete.saving"};

bool categoryJournalPending(StateHandle) noexcept;

} // namespace biglybtc::storage::detail

namespace biglybtc::storage {
bool hasPendingTorrentIntake(const ProfileLease&) noexcept;
bool hasPendingCategoryDeletion(const ProfileLease&) noexcept;
}
