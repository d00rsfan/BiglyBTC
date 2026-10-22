#include "overall_statistics_view.hpp"

#include "statistics_view.hpp"

#include <array>
#include <new>
#include <string>
#include <utility>

namespace biglybtc::storage {
namespace {
constexpr const char* Filename = "BiglyBT.statistics";
Error cancelled() { return {ErrorCode::Cancelled, "Overall statistics operation cancelled"}; }
Error conflict() { return {ErrorCode::Conflict, "Overall statistics require an unchanged pristine primary or all-missing baseline"}; }
Error projectionError(core::OverallDocumentError value) {
  switch (value) {
    case core::OverallDocumentError::Limit:
      return {ErrorCode::Unsupported, "Overall statistics exceed ordinary document limits"};
    case core::OverallDocumentError::Memory:
      return {ErrorCode::Io, "Insufficient memory for overall statistics"};
    case core::OverallDocumentError::InvalidAll:
      return {ErrorCode::InvalidFormat, "Overall statistics 'all' must be a dictionary"};
    case core::OverallDocumentError::InvalidDocument:
      return {ErrorCode::InvalidFormat, "Invalid overall statistics dictionary"};
  }
  return {ErrorCode::InvalidFormat, "Unsupported overall statistics projection result"};
}

// A public Document may have been parsed with higher quotas or edited. The
// fresh bounded parse controls this walk; match each supplied child count before
// indexing it. Never read, subtract or order a caller's bytes pointer. Only a
// fresh parser's view is used to calculate an original offset.
bool pristine(const core::Document& input, const core::Document& parsed) {
  constexpr core::Limits limits;
  if (input.originalBytes().size() > limits.inputBytes) { return false; }
  struct Frame { const core::Node* supplied; const core::Node* bounded; std::size_t next; };
  std::array<Frame, limits.depth + 1> frames{};
  std::size_t depth = 0;
  std::size_t count = 0;
  const auto same = [&](const core::Node& supplied, const core::Node& bounded) {
    if (count == limits.nodes) { return false; }
    ++count;
    if (supplied.changed || supplied.origin || supplied.type != bounded.type
        || supplied.begin != bounded.begin || supplied.end != bounded.end
        || supplied.integer != bounded.integer || supplied.children.size() != bounded.children.size()
        || supplied.children.size() > limits.nodes - count) { return false; }
    if (bounded.type == core::Node::Type::Bytes) {
      const auto offset = static_cast<std::size_t>(bounded.bytes.data() - parsed.originalBytes().data());
      return supplied.bytes.size() == bounded.bytes.size()
          && supplied.bytes.data() == input.originalBytes().data() + offset;
    }
    return supplied.bytes.empty();
  };
  if (!same(input.root(), parsed.root())) { return false; }
  frames[0] = {&input.root(), &parsed.root(), 0};
  for (;;) {
    auto& frame = frames[depth];
    if (frame.next == frame.bounded->children.size()) {
      if (depth == 0) { return true; }
      --depth;
      continue;
    }
    if (depth == limits.depth) { return false; }
    const auto index = frame.next++;
    const auto& supplied = frame.supplied->children[index];
    const auto& bounded = frame.bounded->children[index];
    if (!same(supplied, bounded)) { return false; }
    ++depth;
    frames[depth] = {&supplied, &bounded, 0};
  }
}
OverallStatisticsView view(core::OverallDocument document) {
  OverallStatisticsView result;
  result.snapshot = std::make_shared<const core::OverallDocument>(std::move(document));
  result.recoverySource = RecoverySource::Primary;
  return result;
}
} // namespace

std::expected<OverallStatisticsView, Error> readOverallStatisticsView(
    const ProfileLease& lease, std::stop_token stop) {
  try {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    // Reuse the exact qualified read-only candidate order and safe fd reader.
    auto loaded = readSavedStatisticsView(lease, stop);
    if (!loaded) { return std::unexpected(std::move(loaded.error())); }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (loaded->missing()) { return OverallStatisticsView{}; }
    auto projected = core::readOverallDocument(loaded->snapshot->source->originalBytes());
    if (!projected) { return std::unexpected(projectionError(projected.error())); }
    auto result = view(std::move(*projected));
    result.recoverySource = loaded->recoverySource;
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    return result;
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error{ErrorCode::Io, "Insufficient memory while reading overall statistics"});
  }
}

std::expected<OverallStatisticsSave, Error> saveOverallStatisticsView(
    const ProfileLease& lease, const OverallStatisticsView& baseline,
    const core::OverallValues& values, std::stop_token stop, void (*observer)(SaveStage)) {
  try {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!lease.valid()) { return std::unexpected(Error{ErrorCode::UnsafePath, "Held profile lease required for overall statistics"}); }
    if (!baseline.snapshot && baseline.recoverySource) { return std::unexpected(conflict()); }
    if (baseline.snapshot && (baseline.recoverySource != RecoverySource::Primary || !baseline.snapshot->source)) {
      return std::unexpected(conflict());
    }
    if (baseline.missing()) {
      // The old ConfigFile Missing result alone cannot distinguish malformed
      // recovery candidates. This reader confirms all exact candidates absent.
      auto current = readOverallStatisticsView(lease, stop);
      if (!current) { return std::unexpected(std::move(current.error())); }
      if (!current->missing()) { return std::unexpected(conflict()); }
      auto prepared = core::prepareOverallDocument("de", values);
      if (!prepared) { return std::unexpected(projectionError(prepared.error())); }
      auto result = view(std::move(*prepared));
      OverallStatisticsSave success{std::move(result), OverallSaveDisposition::Committed};
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      auto created = ConfigFile::createMissing(lease, Filename,
          success.current.snapshot->source->originalBytes(), core::Limits{}, observer);
      if (!created) { return std::unexpected(std::move(created.error())); }
      return success;
    }

    auto boundedBaseline = core::readOverallDocument(baseline.snapshot->source->originalBytes());
    if (!boundedBaseline) { return std::unexpected(projectionError(boundedBaseline.error())); }
    if (!pristine(*baseline.snapshot->source, *boundedBaseline->source)
        || baseline.snapshot->hasAll != boundedBaseline->hasAll
        || baseline.snapshot->values != boundedBaseline->values
        || baseline.snapshot->presence != boundedBaseline->presence) { return std::unexpected(conflict()); }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto current = ConfigFile::load(lease, Filename, core::Limits{});
    if (!current) { return std::unexpected(std::move(current.error())); }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (current->recoverySource() != RecoverySource::Primary
        || current->document().originalBytes() != boundedBaseline->source->originalBytes()) {
      return std::unexpected(conflict());
    }
    auto prepared = core::prepareOverallDocument(current->document().originalBytes(), values);
    if (!prepared) { return std::unexpected(projectionError(prepared.error())); }
    auto result = view(std::move(*prepared));
    const auto& encoded = result.snapshot->source->originalBytes();
    if (encoded == current->document().originalBytes()) {
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      return OverallStatisticsSave{std::move(result), OverallSaveDisposition::Unchanged};
    }
    const auto* all = result.snapshot->source->find("all");
    // This node belongs to our fresh bounded parse, never to the supplied tree.
    const auto replacement = std::string(std::string_view(encoded).substr(all->begin, all->end - all->begin));
    auto edited = current->document().replaceValue("all", replacement);
    if (!edited) { return std::unexpected(Error{ErrorCode::InvalidFormat, "Unable to prepare bounded overall statistics replacement"}); }
    if (current->document().serialize() != encoded) {
      return std::unexpected(Error{ErrorCode::InvalidFormat, "Prepared overall statistics bytes do not match the transaction document"});
    }
    OverallStatisticsSave success{std::move(result), OverallSaveDisposition::Committed};
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    const auto saved = current->save(lease, observer);
    if (!saved) { return std::unexpected(saved.error()); }
    return success; // No new allocation, cancellation or delta calculation.
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error{ErrorCode::Io,
        "Insufficient memory during overall statistics save; recovery or primary may remain if publication began"});
  }
}
} // namespace biglybtc::storage
