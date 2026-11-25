// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_transaction_pending.hpp"

namespace biglybtc::storage {
namespace {
bool pending(detail::StateHandle profile,const auto& leaves) noexcept {
  try {
    for(const auto& leaf:leaves){
      const auto found=detail::stateStampAt(profile,std::filesystem::path(leaf));
      if(!found||*found){return true;}
    }
    return false;
  }catch(...){return true;}
}
}

bool hasPendingTorrentIntake(const ProfileLease& lease) noexcept {
  using namespace detail::intakeJournal;
  const std::array names{Journal,JournalSaving,InventorySaving,BackupSaving,AcquisitionIntent,
      AcquisitionIntentSaving,AcquisitionIntentBackup};
  return !lease.valid()||pending(lease.directoryHandle(),names);
}

bool detail::categoryJournalPending(StateHandle root) noexcept {
  return pending(root,CategoryJournalNames)||pending(root,LegacyCategoryJournalNames);
}

bool hasPendingCategoryDeletion(const ProfileLease& lease) noexcept {
  return !lease.valid()||detail::categoryJournalPending(lease.directoryHandle());
}

} // namespace biglybtc::storage
