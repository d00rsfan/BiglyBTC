#pragma once
#include "biglybtc/file_policy.hpp"

namespace biglybtc::compat {
struct OriginalFileStorageChange {
  // Original ST_LINEAR=1 or ST_REORDER=3;not an SDK storage-mode code.
  std::int32_t target=1;
  std::vector<std::uint8_t> files;
  bool operator==(const OriginalFileStorageChange&)const=default;
};
// Complete known requested Original storage codes1..4 and exact0/1 mask.
// Returns conversion intentions in Original order. Before issuing the next
// intention or changing skipped flags,the owner must certify every selected
// file of the preceding request. Partial conversion is not a rollback receipt.
// No IO,profile writes,skip effects or recovery authority follow from this plan.
std::expected<std::vector<OriginalFileStorageChange>,PolicyError> originalSkipStoragePlan(
    std::span<const std::int32_t> requested,std::span<const std::uint8_t> changed,bool skipped);
}
