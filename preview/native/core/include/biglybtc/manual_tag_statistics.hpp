#pragma once
#include "biglybtc/saved_download_tags.hpp"
#include "biglybtc/target_traffic.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace biglybtc::core {
inline constexpr std::size_t MaximumManualTagRows = 4096;
inline constexpr std::size_t MaximumManualTagLibraryHashes = 100000;
enum class ManualTagError { InvalidSource, InvalidLibrary, InvalidTraffic, Limit, Memory };
struct ManualTagRow {
  std::optional<std::int64_t> uid;
  std::optional<std::string> rawName;
  bool nameMayRequireLocalization = false, ambiguousIdentity = false, membershipKnown = false;
  std::optional<std::int64_t> rawVisibility;
  bool visibilityDefaulted = true, visibilityMalformed = false;
  std::size_t sourceIndex = 0, resolvedCount = 0, unresolvedCount = 0;
  std::vector<TorrentHash> resolvedHashes;
};
class ManualTagMembership final {
public:
  std::uint64_t session() const noexcept { return session_; }
  std::uint64_t libraryRevision() const noexcept { return libraryRevision_; }
  std::uint64_t tagRevision() const noexcept { return tagRevision_; }
  bool manualTypePresent() const noexcept { return source_->manualTypePresent; }
  std::span<const ManualTagRow> rows() const noexcept { return rows_; }
  bool contains(const TorrentHash&) const;
private:
  friend std::expected<std::shared_ptr<const ManualTagMembership>, ManualTagError> resolveManualTagMembership(
      std::uint64_t, std::uint64_t, std::uint64_t, const SavedDownloadTags&, std::span<const TorrentHash>);
  ManualTagMembership() = default;
  std::uint64_t session_ = 0, libraryRevision_ = 0, tagRevision_ = 0;
  std::shared_ptr<const SavedDownloadTags> source_;
  std::vector<std::string> libraryKeys_;
  std::vector<ManualTagRow> rows_;
};
// Run once per explicit tag/library revision, not per rate sample. Reparse the
// owned original tag bytes rather than trust edited public projection fields.
// The producer supplies its actual immutable current-library hash inventory;
// this pure join establishes native-library resolution, not a Java live manager.
std::expected<std::shared_ptr<const ManualTagMembership>, ManualTagError> resolveManualTagMembership(
    std::uint64_t session, std::uint64_t libraryRevision, std::uint64_t tagRevision,
    const SavedDownloadTags&, std::span<const TorrentHash> currentLibrary);

enum class ManualTagTrafficState { Unavailable, Active, Idle };
struct ManualTagTraffic {
  ManualTagTrafficState state = ManualTagTrafficState::Unavailable;
  std::optional<TargetTraffic> target;
  // Only the owner may assert complete coverage of the current native engine.
  // Missing progress, command completion and saved state do not authorize it.
  bool completeOwnedCoverage = false;
  bool current = true;
};
enum class ManualTagRateState { Unknown, UnresolvedMembership, AmbiguousIdentity,
  ObservedActive, ObservedIdle, ObservedOtherTagZero, LastObserved };
struct ManualTagRate {
  std::optional<std::int64_t> downloadRate, uploadRate;
  ManualTagRateState state = ManualTagRateState::Unknown;
  bool operator==(const ManualTagRate&) const = default;
};
bool validManualTagTraffic(const ManualTagTraffic&) noexcept;
// No session totals, counter subtraction, rates inferred from missing members,
// cross-tag total or history. At most one actual target observation is consumed.
std::expected<std::vector<ManualTagRate>, ManualTagError> projectManualTagRates(
    const ManualTagMembership&, const ManualTagTraffic&);
} // namespace biglybtc::core
