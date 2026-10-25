#include "biglybtc/queue_file_priority.hpp"
#include "biglybtc/queue_policy.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace biglybtc;
using Stats = core::QueueFilePriorityStats;
using Error = core::QueueFilePriorityError;
constexpr auto Min = std::numeric_limits<std::int32_t>::min();
constexpr auto Max = std::numeric_limits<std::int32_t>::max();
void check(bool okay, const char* message) { if (!okay) { throw std::runtime_error(message); } }
compat::OriginalLayout geometry(std::initializer_list<std::int64_t> lengths) {
  compat::OriginalLayout result{"owned", true, 4, 0, {}};
  for (const auto bytes : lengths) {
    result.files.push_back({std::to_string(result.files.size()), result.bytes, bytes, false});
    result.bytes += bytes;
  }
  return result;
}
Stats observe(const compat::OriginalLayout& layout, const std::vector<compat::FilePolicy>& policy,
    std::initializer_list<std::uint8_t> bits) {
  const auto result = core::queueFilePriorityStatistics(layout, policy, {bits.begin(), bits.size()});
  check(result.has_value(), "Valid literal original-index observation rejected"); return *result;
}
void originalMaxIncompleteAndSkippedRules() {
  auto layout = geometry({4, 4, 0, 4});
  const std::vector<compat::FilePolicy> policy{{false, 5}, {false, -2}, {false, 9}, {true, 100}};
  check(observe(layout, policy, {1, 0, 0}) == Stats{9, -2, 12, -2},
      "Completed high/empty priorities must not outrank actual incomplete files");
  check(observe(layout, policy, {1, 1, 0}) == Stats{9, Min, 12, 0},
      "No selected incomplete file retains the original MIN sentinel");
  auto skipped = policy; for (auto& item : skipped) { item.skipped = true; }
  check(observe(layout, skipped, {0, 0, 0}) == Stats{Min, Min, 0, 0}, "All DND is distinct from normal priority zero");
  layout.files[1].padding = true;
  check(observe(layout, policy, {1, 0, 0}) == Stats{9, -2, 12, -2},
      "A padding label must not invent a skipped policy or erase an original slot");
}
void boundariesAndJavaSignedWrap() {
  check(observe(geometry({}), {}, {}) == Stats{Min, Min, 0, 0}, "Empty original set sentinel differs");
  check(observe(geometry({0}), {{false, 4}}, {}) == Stats{4, Min, 4, 0}, "Real empty file is complete without a piece");
  check(observe(geometry({4, 4}), {{false, Max}, {false, Max}}, {0, 0}) == Stats{Max, Max, -2, -2},
      "Original priority sums use signed32 modulo arithmetic");
  check(observe(geometry({4, 4}), {{false, Min}, {false, -1}}, {1, 0}) == Stats{-1, -1, Max, -1},
      "Negative source priorities preserve exact max/signed-wrap semantics");
  check(observe(geometry({2, 4, 2}), {{false, 1}, {false, 2}, {false, 3}}, {1, 0}) == Stats{3, 3, 6, 5},
      "A file spanning a verified and missing piece is incomplete; exact boundary file stays complete");
}
void badInputsProduceNoInventedObservation() {
  auto layout = geometry({4, 4}); const std::array<compat::FilePolicy, 2> policy{};
  const std::array<std::uint8_t, 2> bits{1, 0};
  auto result = core::queueFilePriorityStatistics(layout, policy, {});
  check(!result && result.error() == Error::Pieces, "Missing bitmap must remain unavailable");
  result = core::queueFilePriorityStatistics(layout, std::span<const compat::FilePolicy>{}, bits);
  check(!result && result.error() == Error::Policy, "Sparse policy cannot be silently defaulted by an observer");
  layout.files[1].offset = 5;
  result = core::queueFilePriorityStatistics(layout, policy, bits);
  check(!result && result.error() == Error::Geometry, "A shifted original index must fail without a prefix result");
  layout = geometry({4, 4}); layout.files[0].bytes = std::numeric_limits<std::int64_t>::max();
  check(!core::queueFilePriorityStatistics(layout, policy, bits), "Overflowing geometry must not enter piece arithmetic");
  layout = geometry({4, 4}); const std::array<std::uint8_t, 2> bad{1, 2};
  result = core::queueFilePriorityStatistics(layout, policy, bad);
  check(!result && result.error() == Error::Pieces, "Unverified byte values cannot become verified pieces");
}
void actualObservationChangesOrdinaryQueueChoice() {
  const auto layout = geometry({4, 4});
  const auto a = observe(layout, {{false, 9}, {false, 0}}, {1, 0});
  const auto b = observe(layout, {{false, 1}, {false, 1}}, {0, 0});
  std::array<core::QueueTorrentFacts, 2> facts{};
  for (std::size_t i = 0; i < facts.size(); ++i) {
    auto& fact = facts[i]; fact.token = i + 1; fact.position = static_cast<std::uint32_t>(i + 1);
    fact.shareRatio = 0; fact.secondsDownloading = fact.secondsSeeding = 0;
    fact.downloadBytesPerSecond = fact.uploadBytesPerSecond = 0;
    fact.sizeBytes = fact.remainingBytes = 8;
  }
  facts[0].filePriority.emplace(a.begin(), a.end()); facts[1].filePriority.emplace(b.begin(), b.end());
  core::QueuePolicy policy; policy.maximumDownloads = policy.maximumActive = 1; policy.minimumDownloads = 0;
  policy.downloadOrder = core::QueueDownloadOrder::FilePriority;
  core::QueueContext context; context.uploadBytesPerSecond = context.downloadBytesPerSecond = 0;
  auto plan = core::evaluateQueue(policy, facts, context); check(plan.has_value(), "Startup queue plan rejected");
  check(plan->decisions[0].action == core::QueueAction::Run && plan->decisions[1].action == core::QueueAction::Queue,
      "Original startup interval must preserve position before custom sorting");
  context.nowMillis = 30000;
  plan = core::evaluateQueue(policy, facts, context); check(plan.has_value(), "Actual observation queue plan rejected");
  check(plan->decisions[1].action == core::QueueAction::Run && plan->decisions[0].action == core::QueueAction::Queue,
      "Incomplete priority must outrank saved position and completed-only high priority");
  facts[1].manuallyPaused = true;
  plan = core::evaluateQueue(policy, facts, context); check(plan.has_value(), "Manual hold plan rejected");
  check(plan->decisions[0].action == core::QueueAction::Run && plan->decisions[1].action == core::QueueAction::Preserve,
      "Priority observations must never override a manual hold");
}
} // namespace
int main() {
  try {
    originalMaxIncompleteAndSkippedRules(); boundariesAndJavaSignedWrap();
    badInputsProduceNoInventedObservation(); actualObservationChangesOrdinaryQueueChoice();
    std::cout << "Queue original file-priority observation rules and queue integration passed\n"; return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
