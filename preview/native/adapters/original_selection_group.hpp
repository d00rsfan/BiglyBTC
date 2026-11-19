#pragma once
#include "file_operations.hpp"
#include "linear_to_compact.hpp"
#include "original_storage_conversion.hpp"

namespace biglybtc::storage {
enum class OriginalSelectionGroupStage {
  SavingSynced,JournalSynced,ResumePublished,FileConverted,PoliciesPublished,Completed,HandoffJournalSynced,HandoffReleased
};
// Stopped serialized worker only. Complete mapping/control/payload preflight
// precedes the durable group record. C acknowledgements precede X,then flags.
// false delegates unchanged identity-layout requests to the existing v1/v2
// transaction. A failure after journal publication requires stopped recovery.
std::expected<bool,Error> convertOriginalSelectionGroup(const ProfileLease&,
    const FileOperationRequest&,void(*)(OriginalSelectionGroupStage)=nullptr,
    void(*)(OriginalStorageConversionStage)=nullptr);
std::expected<bool,Error> isOriginalSelectionGroupPending(const ProfileLease&);
std::expected<OriginalCompactionResult,Error> recoverOriginalSelectionGroup(
    const ProfileLease&,void(*)(OriginalSelectionGroupStage)=nullptr,
    void(*)(OriginalStorageConversionStage)=nullptr);
}
