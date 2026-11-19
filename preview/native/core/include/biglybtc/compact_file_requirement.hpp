// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>
namespace biglybtc::compat {
struct OriginalFilePieceSpan {
  std::int32_t first=0,last=0;
  bool skipped=false;
  bool operator==(const OriginalFilePieceSpan&)const=default;
};
enum class CompactFileRequirementError {Geometry,Order,Target};
// Actual TOTorrentFile arithmetic in the supported nonnegative int32 piece
// domain,including a zero-length origin at ordinary BitTorrent piece sizes.
std::expected<OriginalFilePieceSpan,CompactFileRequirementError> originalFilePieceSpan(
    std::int64_t offset,std::int64_t bytes,std::int64_t pieceBytes,bool skipped);
// Pure RDResumeHandler.fileMustExist predicate for an actual COMPACT or
// REORDER_COMPACT target. Inputs describe the complete original ordered file
// set with current effective skipped facts,not an incomplete saved hint list.
// No file existence,source freshness,allocation,delete or write grant follows.
std::expected<bool,CompactFileRequirementError> originalCompactFileMustExist(
    std::span<const OriginalFilePieceSpan>,std::size_t target);
// Same full-census predicate for every index,with one validation and bounded
// O(files log files) work. Values for noncompact slots are not allocation rules.
std::expected<std::vector<std::uint8_t>,CompactFileRequirementError> originalCompactFileRequirements(
    std::span<const OriginalFilePieceSpan>);
}
