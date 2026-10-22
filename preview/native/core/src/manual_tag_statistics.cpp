#include "biglybtc/manual_tag_statistics.hpp"

#include <algorithm>
#include <new>

namespace biglybtc::core {
bool ManualTagMembership::contains(const TorrentHash& hash) const {
  const auto key = hash.hex();
  return std::binary_search(libraryKeys_.begin(), libraryKeys_.end(), key);
}
std::expected<std::shared_ptr<const ManualTagMembership>, ManualTagError> resolveManualTagMembership(
    std::uint64_t session, std::uint64_t libraryRevision, std::uint64_t tagRevision,
    const SavedDownloadTags& saved, std::span<const TorrentHash> currentLibrary) {
  if (!session || !libraryRevision || !tagRevision || !saved.source) { return std::unexpected(ManualTagError::InvalidSource); }
  if (currentLibrary.size() > MaximumManualTagLibraryHashes) { return std::unexpected(ManualTagError::Limit); }
  try {
    auto parsed = readSavedDownloadTags(saved.source->originalBytes());
    if (!parsed) { return std::unexpected(parsed.error() == SavedTagError::Memory ? ManualTagError::Memory
        : parsed.error() == SavedTagError::Limit ? ManualTagError::Limit : ManualTagError::InvalidSource); }
    if (parsed->tags.size() > MaximumManualTagRows) { return std::unexpected(ManualTagError::Limit); }
    auto value = std::shared_ptr<ManualTagMembership>(new ManualTagMembership);
    value->session_ = session; value->libraryRevision_ = libraryRevision; value->tagRevision_ = tagRevision;
    value->source_ = std::make_shared<const SavedDownloadTags>(std::move(*parsed));
    value->libraryKeys_.reserve(currentLibrary.size());
    for (const auto& hash : currentLibrary) {
      if (hash.bytes().size() != 20 && hash.bytes().size() != 32) { return std::unexpected(ManualTagError::InvalidLibrary); }
      value->libraryKeys_.push_back(hash.hex());
    }
    std::sort(value->libraryKeys_.begin(), value->libraryKeys_.end());
    if (std::adjacent_find(value->libraryKeys_.begin(), value->libraryKeys_.end()) != value->libraryKeys_.end()) {
      return std::unexpected(ManualTagError::InvalidLibrary);
    }
    value->rows_.reserve(value->source_->tags.size());
    for (const auto& tag : value->source_->tags) {
      if (!tag.visible) { continue; } // Original TagStatsView excludes hidden tags.
      ManualTagRow row;
      row.rawVisibility=tag.rawVisibility;row.visibilityDefaulted=tag.visibilityDefaulted;
      row.visibilityMalformed=tag.visibilityMalformed;
      row.uid = tag.uid; row.rawName = tag.rawName; row.sourceIndex = tag.sourceIndex;
      row.nameMayRequireLocalization = tag.nameMayRequireLocalization;
      row.ambiguousIdentity = !tag.uid || tag.duplicateId;
      row.membershipKnown = !row.ambiguousIdentity && tag.membersPresent && !tag.membersMalformed;
      row.resolvedHashes.reserve(tag.uniqueHashes.size());
      for (const auto& hash : tag.uniqueHashes) {
        if (value->contains(hash)) { row.resolvedHashes.push_back(hash); }
        else { ++row.unresolvedCount; }
      }
      for (const auto& member : tag.members) { if (!member.hash) { ++row.unresolvedCount; } }
      row.resolvedCount = row.resolvedHashes.size();
      value->rows_.push_back(std::move(row));
    }
    return std::shared_ptr<const ManualTagMembership>(std::move(value));
  } catch (const std::bad_alloc&) { return std::unexpected(ManualTagError::Memory); }
}
bool validManualTagTraffic(const ManualTagTraffic& value) noexcept {
  switch (value.state) {
    case ManualTagTrafficState::Unavailable: return !value.target && !value.completeOwnedCoverage;
    case ManualTagTrafficState::Idle: return !value.target;
    case ManualTagTrafficState::Active:
      return value.target && validTargetTraffic(*value.target);
  }
  return false;
}
std::expected<std::vector<ManualTagRate>, ManualTagError> projectManualTagRates(
    const ManualTagMembership& membership, const ManualTagTraffic& traffic) {
  if (!validManualTagTraffic(traffic)) { return std::unexpected(ManualTagError::InvalidTraffic); }
  if (membership.rows().size() > MaximumManualTagRows) { return std::unexpected(ManualTagError::Limit); }
  try {
    if (traffic.target && !membership.contains(traffic.target->hash)) { return std::unexpected(ManualTagError::InvalidTraffic); }
    std::vector<ManualTagRate> result(membership.rows().size());
    for (std::size_t i = 0; i < result.size(); ++i) {
      const auto& row = membership.rows()[i]; auto& rate = result[i];
      if (row.ambiguousIdentity) { rate.state = ManualTagRateState::AmbiguousIdentity; continue; }
      if (!row.membershipKnown || row.unresolvedCount) { rate.state = ManualTagRateState::UnresolvedMembership; continue; }
      if (traffic.state == ManualTagTrafficState::Unavailable) { continue; }
      if (traffic.state == ManualTagTrafficState::Idle) {
        if (!traffic.completeOwnedCoverage) { continue; }
        rate = {0, 0, traffic.current ? ManualTagRateState::ObservedIdle : ManualTagRateState::LastObserved};
        continue;
      }
      const auto present = std::find(row.resolvedHashes.begin(), row.resolvedHashes.end(), traffic.target->hash)
          != row.resolvedHashes.end();
      if (present && (traffic.completeOwnedCoverage || row.resolvedCount == 1)) {
        rate = {traffic.target->downloadRate, traffic.target->uploadRate,
            traffic.current ? ManualTagRateState::ObservedActive : ManualTagRateState::LastObserved};
      } else if (!present && traffic.completeOwnedCoverage) {
        rate = {0, 0, traffic.current ? ManualTagRateState::ObservedOtherTagZero : ManualTagRateState::LastObserved};
      }
    }
    return result;
  } catch (const std::bad_alloc&) { return std::unexpected(ManualTagError::Memory); }
}
} // namespace biglybtc::core
