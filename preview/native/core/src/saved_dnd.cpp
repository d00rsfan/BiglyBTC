#include "biglybtc/saved_dnd.hpp"
#include "biglybtc/bencode.hpp"

#include <limits>

namespace biglybtc::core {
namespace {
// Java source: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f,
// DownloadManagerState.ATTRIBUTE_DEFAULTS and DownloadManagerStatsImpl selected
// getters. This native admission policy excludes inconsistent saved counters.
static_assert(std::numeric_limits<float>::is_iec559
    && std::numeric_limits<float>::radix == 2
    && std::numeric_limits<float>::digits == 24 && sizeof(float) == 4);

void issue(std::uint32_t& bits, SavedDndIssue value) noexcept {
  bits |= static_cast<std::uint32_t>(value);
}
void defaulted(std::uint32_t& bits, SavedDndDefault value) noexcept {
  bits |= static_cast<std::uint32_t>(value);
}
bool dictionary(const Node& node) noexcept {
  if (node.type != Node::Type::Dictionary || node.children.size() % 2 != 0) { return false; }
  for (std::size_t i = 0; i < node.children.size(); i += 2) {
    if (node.children[i].type != Node::Type::Bytes) { return false; }
  }
  return true;
}
void integer(const Node& node, std::string_view key, bool& present,
    std::optional<std::int64_t>& value, std::uint32_t& diagnostics,
    SavedDndIssue typeIssue) noexcept {
  const auto* found = node.find(key);
  present = found != nullptr;
  if (!found) { return; }
  if (found->type != Node::Type::Integer) { issue(diagnostics, typeIssue); return; }
  value = found->integer;
}
std::optional<std::int64_t> subtract(std::int64_t left, std::int64_t right) noexcept {
  if ((right > 0 && left < std::numeric_limits<std::int64_t>::min() + right)
      || (right < 0 && left > std::numeric_limits<std::int64_t>::max() + right)) { return std::nullopt; }
  return left - right;
}
} // namespace

SavedDndFacts readSavedDndFacts(const Node* attributes) noexcept {
  SavedDndFacts result;
  result.attributesPresent = attributes != nullptr;
  if (!attributes) { issue(result.diagnostics, SavedDndIssue::AttributesMissing); return result; }
  result.attributesDictionary = dictionary(*attributes);
  if (!result.attributesDictionary) { issue(result.diagnostics, SavedDndIssue::AttributesType); return result; }
  integer(*attributes, "skipfss", result.skippedBytesPresent, result.skippedBytes,
      result.diagnostics, SavedDndIssue::SkippedBytesType);
  integer(*attributes, "skipbdl", result.skippedDownloadedBytesPresent, result.skippedDownloadedBytes,
      result.diagnostics, SavedDndIssue::SkippedDownloadedBytesType);
  if (!result.skippedBytesPresent) { defaulted(result.defaults, SavedDndDefault::SkippedBytes); }
  if (!result.skippedDownloadedBytesPresent) { defaulted(result.defaults, SavedDndDefault::SkippedDownloadedBytes); }
  const auto* parameters = attributes->find("parameters");
  result.parametersPresent = parameters != nullptr;
  if (!parameters) { issue(result.diagnostics, SavedDndIssue::FlagsMissing); return result; }
  result.parametersDictionary = dictionary(*parameters);
  if (!result.parametersDictionary) { issue(result.diagnostics, SavedDndIssue::ParametersType); return result; }
  integer(*parameters, "dndflags", result.dndFlagsPresent, result.dndFlags,
      result.diagnostics, SavedDndIssue::DndFlagsType);
  if (!result.dndFlagsPresent) { issue(result.diagnostics, SavedDndIssue::FlagsMissing); }
  return result;
}

SavedDndProgress projectSavedDndProgress(std::optional<std::int64_t> cachedBytes,
    std::optional<std::int64_t> completedBytes, const SavedDndFacts& facts) noexcept {
  SavedDndProgress result;
  result.defaults = facts.defaults;
  result.diagnostics = facts.diagnostics;
  const bool totalKnown = cachedBytes && *cachedBytes > 0;
  if (!totalKnown) { issue(result.diagnostics, SavedDndIssue::TotalUnresolved); }
  bool completedKnown = false;
  if (!completedBytes) { issue(result.diagnostics, SavedDndIssue::CompletedMissing); }
  else if (*completedBytes < 0 || (totalKnown && *completedBytes > *cachedBytes)) {
    issue(result.diagnostics, SavedDndIssue::CompletedRange);
  } else { completedKnown = totalKnown; }

  if (!facts.attributesPresent || !facts.attributesDictionary) {
    issue(result.diagnostics, facts.attributesPresent ? SavedDndIssue::AttributesType : SavedDndIssue::AttributesMissing);
    return result;
  }
  if (facts.parametersPresent && facts.parametersDictionary && facts.dndFlagsPresent && facts.dndFlags) {
    const auto flags = static_cast<std::uint64_t>(*facts.dndFlags);
    result.cachedHasDnd = (flags & 1U) != 0;
    result.cachedCompleteNoDnd = (flags & 2U) != 0;
    if (*result.cachedHasDnd) {
      result.controllerNoDiskCompleteExcludingDnd = *result.cachedCompleteNoDnd;
      result.controllerNoDiskCompleteIncludingDnd = false;
    } else if (completedKnown) {
      const bool complete = *completedBytes == *cachedBytes;
      result.controllerNoDiskCompleteExcludingDnd = complete;
      result.controllerNoDiskCompleteIncludingDnd = complete;
    }
  } else { issue(result.diagnostics, SavedDndIssue::FlagsMissing); }

  if (!totalKnown) { return result; }
  if (!facts.skippedBytesPresent || !facts.skippedBytes || *facts.skippedBytes < 0) {
    // Missing skipfss is the original -1/recalculation boundary, never zero.
    issue(result.diagnostics, SavedDndIssue::SkippedUnresolved);
    return result;
  }
  const auto skipped = *facts.skippedBytes;
  if (skipped > *cachedBytes) { issue(result.diagnostics, SavedDndIssue::SkippedRange); return result; }
  const auto selected = subtract(*cachedBytes, skipped);
  if (!selected) { issue(result.diagnostics, SavedDndIssue::ArithmeticOverflow); return result; }
  result.selectedBytes = *selected;
  if (*selected == 0) { result.percentThousandths = 1000; }

  std::optional<std::int64_t> skippedDownloaded;
  if (facts.skippedDownloadedBytesPresent) { skippedDownloaded = facts.skippedDownloadedBytes; }
  else if (hasSavedDndDefault(facts.defaults, SavedDndDefault::SkippedDownloadedBytes)) { skippedDownloaded = 0; }
  if (!skippedDownloaded || *skippedDownloaded < 0 || *skippedDownloaded > skipped) {
    issue(result.diagnostics, SavedDndIssue::SkippedDownloadedRange);
    return result;
  }
  if (!completedKnown) { return result; }
  if (*completedBytes < *skippedDownloaded) {
    issue(result.diagnostics, SavedDndIssue::CountersInconsistent);
    return result;
  }
  const auto selectedCompleted = subtract(*completedBytes, *skippedDownloaded);
  if (!selectedCompleted) { issue(result.diagnostics, SavedDndIssue::ArithmeticOverflow); return result; }
  if (*selectedCompleted > *selected) { issue(result.diagnostics, SavedDndIssue::CountersInconsistent); return result; }

  // Keep the source subtraction order after validating its admitted domain.
  const auto fullRemaining = subtract(*cachedBytes, *completedBytes);
  const auto skippedRemaining = subtract(skipped, *skippedDownloaded);
  if (!fullRemaining || !skippedRemaining) { issue(result.diagnostics, SavedDndIssue::ArithmeticOverflow); return result; }
  const auto remaining = subtract(*fullRemaining, *skippedRemaining);
  if (!remaining) { issue(result.diagnostics, SavedDndIssue::ArithmeticOverflow); return result; }
  result.selectedRemaining = *remaining;
  if (*selected == 0) { return result; }
  const auto selectedDone = subtract(*selected, *remaining);
  if (!selectedDone) { issue(result.diagnostics, SavedDndIssue::ArithmeticOverflow); return result; }

  // Separate binary32 operations match Java's long-to-float conversions,
  // division, multiplication and truncation. Admission proves 0..1000 before
  // the integer conversion, avoiding C++ out-of-range float-to-int behavior.
  const float numerator = static_cast<float>(*selectedDone);
  const float denominator = static_cast<float>(*selected);
  const float fraction = numerator / denominator;
  const float scaled = 1000.0F * fraction;
  auto percent = static_cast<std::int32_t>(scaled);
  if (percent == 1000 && *remaining > 0) { percent = 999; }
  else if (percent < 0) { percent = 0; }
  result.percentThousandths = percent;
  return result;
}

} // namespace biglybtc::core
