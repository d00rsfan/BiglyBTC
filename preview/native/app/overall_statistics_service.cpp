#include "overall_statistics_service.hpp"

#include <limits>
#include <bit>
#include <new>
#include <string_view>
#include <thread>
#include <utility>

namespace biglybtc::app {
namespace {
bool valid(OverallOwnerIdentity identity) noexcept {
  return identity.applicationRun && identity.profileSession && identity.engineEpoch;
}
bool valid(OverallSampleQuality value) noexcept {
  return value == OverallSampleQuality::Observed || value == OverallSampleQuality::SettledCheckpoint
      || value == OverallSampleQuality::ClosedFinal;
}
bool valid(core::OverallPeerCounters value) noexcept {
  constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
  return value.dataReceived >= 0 && value.protocolReceived >= 0 && value.dataSent >= 0 && value.protocolSent >= 0
      && value.dataReceived <= maximum - value.protocolReceived && value.dataSent <= maximum - value.protocolSent;
}
bool regressed(core::OverallPeerCounters before, core::OverallPeerCounters after) noexcept {
  return after.dataReceived < before.dataReceived || after.protocolReceived < before.protocolReceived
      || after.dataSent < before.dataSent || after.protocolSent < before.protocolSent;
}
storage::Error preparationError(core::OverallDocumentError error) {
  switch (error) {
    case core::OverallDocumentError::Memory:
      return {storage::ErrorCode::Io, "Insufficient memory while preparing overall statistics"};
    case core::OverallDocumentError::Limit:
      return {storage::ErrorCode::Unsupported, "Overall statistics exceed ordinary document limits"};
    case core::OverallDocumentError::InvalidAll:
      return {storage::ErrorCode::InvalidFormat, "Overall statistics 'all' must be a dictionary"};
    case core::OverallDocumentError::InvalidDocument:
      return {storage::ErrorCode::InvalidFormat, "Invalid overall statistics document"};
  }
  return {storage::ErrorCode::InvalidFormat, "Unsupported overall statistics document"};
}
} // namespace

struct OverallStatisticsService::Impl {
  const storage::ProfileLease& lease;
  const OverallOwnerIdentity identity;
  const std::thread::id thread = std::this_thread::get_id();
  storage::OverallStatisticsView baseline;
  core::OverallStatistics reducer;
  std::optional<OverallOwnerSample> sample;
  std::optional<core::OverallValues> pending;
  std::optional<core::OverallDocument> document;
  std::optional<storage::Error> error;
  bool blocked = false;
  bool recovery = false;
  bool uncertain = false;
  bool finalApplied = false;
  bool changed = false;

  Impl(const storage::ProfileLease& held, OverallOwnerIdentity id, storage::OverallStatisticsView loaded,
      std::int64_t startMillis, std::int64_t loadMillis)
      : lease(held), identity(id), baseline(std::move(loaded)),
        reducer(baseline.snapshot ? baseline.snapshot->values : core::OverallValues{}, {}, startMillis, loadMillis) {
    recovery = baseline.recoverySource && baseline.recoverySource != storage::RecoverySource::Primary;
    if (recovery) {
      blocked = true;
      error.emplace(storage::Error{storage::ErrorCode::Conflict,
          "Recovered overall statistics must be resolved before writing"});
    }
  }
  bool onThread() const noexcept { return thread == std::this_thread::get_id(); }
  void stage() noexcept {
    changed = true;
    // Before a write attempt, a mark can replace an unattempted tick intent so
    // only the complete marked document is written. Once blocked it is frozen.
    if (!blocked) { pending = reducer.values(); document.reset(); }
  }
  OverallServiceError fail(storage::Error failure) noexcept {
    blocked = true;
    if (!error) { error.emplace(std::move(failure)); }
    return OverallServiceError::Storage;
  }
};

OverallStatisticsService::OverallStatisticsService(std::unique_ptr<Impl> value) noexcept : impl_(std::move(value)) {}
OverallStatisticsService::~OverallStatisticsService() = default;
std::expected<std::unique_ptr<OverallStatisticsService>, storage::Error> OverallStatisticsService::open(
    const storage::ProfileLease& lease, OverallOwnerIdentity identity,
    std::int64_t startMillis, std::int64_t loadMillis, std::stop_token stop) {
  try {
    if (!valid(identity)) { return std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Invalid overall owner identity"}); }
    auto loaded = storage::readOverallStatisticsView(lease, stop);
    if (!loaded) { return std::unexpected(std::move(loaded.error())); }
    if (stop.stop_requested()) {
      return std::unexpected(storage::Error{storage::ErrorCode::Cancelled, "Overall statistics service opening cancelled"});
    }
    auto impl = std::make_unique<Impl>(lease, identity, std::move(*loaded), startMillis, loadMillis);
    return std::unique_ptr<OverallStatisticsService>(new OverallStatisticsService(std::move(impl)));
  } catch (const std::bad_alloc&) {
    return std::unexpected(storage::Error{storage::ErrorCode::Io, "Insufficient memory for overall statistics service"});
  }
}
std::expected<void, OverallServiceError> OverallStatisticsService::capture(OverallOwnerSample incoming) noexcept {
  auto& state = *impl_;
  if (!state.onThread()) { return std::unexpected(OverallServiceError::WrongThread); }
  if (incoming.identity != state.identity || !incoming.sequence || !valid(incoming.quality) || !valid(incoming.absolute)) {
    return std::unexpected(OverallServiceError::InvalidInput);
  }
  if (state.uncertain) { return std::unexpected(OverallServiceError::Uncertified); }
  if (state.sample) {
    const auto& old = *state.sample;
    if (incoming == old && incoming.quality == OverallSampleQuality::ClosedFinal) { return {}; }
    if (old.quality == OverallSampleQuality::ClosedFinal) { return std::unexpected(OverallServiceError::Closed); }
    const bool promotion = incoming.sequence == old.sequence && incoming.absolute == old.absolute
        && static_cast<int>(incoming.quality) > static_cast<int>(old.quality);
    if (incoming.sequence < old.sequence || (incoming.sequence == old.sequence && !promotion)) {
      return std::unexpected(OverallServiceError::StaleSample);
    }
    if (regressed(old.absolute, incoming.absolute)) { return std::unexpected(OverallServiceError::CounterRegression); }
  }
  state.sample = incoming;
  return {};
}
std::expected<void, OverallServiceError> OverallStatisticsService::invalidate(OverallOwnerIdentity identity) noexcept {
  auto& state = *impl_;
  if (!state.onThread()) { return std::unexpected(OverallServiceError::WrongThread); }
  if (identity != state.identity) { return std::unexpected(OverallServiceError::InvalidInput); }
  state.uncertain = true;
  return {};
}
std::expected<core::OverallUpdate, OverallServiceError> OverallStatisticsService::tick(
    std::int64_t nowMillis, core::OverallDhtInput dht, bool force) {
  auto& state = *impl_;
  if (!state.onThread()) { return std::unexpected(OverallServiceError::WrongThread); }
  if (state.uncertain) { return std::unexpected(OverallServiceError::Uncertified); }
  if (!state.sample) { return std::unexpected(OverallServiceError::NoObservation); }
  if (state.sample->quality == OverallSampleQuality::ClosedFinal && (!force || state.finalApplied)) {
    return std::unexpected(OverallServiceError::Closed);
  }
  const auto before = state.reducer.values();
  auto updated = state.reducer.update(state.sample->absolute, dht, nowMillis, force);
  if (state.reducer.values() != before) { state.changed = true; }
  if (!updated) { return std::unexpected(OverallServiceError::Domain); }
  if (state.sample->quality == OverallSampleQuality::ClosedFinal
      && updated->disposition != core::OverallUpdateDisposition::ClockBackwards) { state.finalApplied = true; }
  if (updated->save) { state.stage(); }
  return std::move(*updated);
}
std::expected<OverallFlushDisposition, OverallServiceError> OverallStatisticsService::flush(std::stop_token stop) {
  auto& state = *impl_;
  if (!state.onThread()) { return std::unexpected(OverallServiceError::WrongThread); }
  if (state.blocked) { return std::unexpected(OverallServiceError::WriteBlocked); }
  if (stop.stop_requested()) { return std::unexpected(OverallServiceError::Cancelled); }
  if (!state.pending) { return OverallFlushDisposition::NothingPending; }
  try {
    if (!state.document) {
      auto prepared = core::prepareOverallDocument(state.baseline.missing() ? std::string_view{"de"}
          : std::string_view{state.baseline.snapshot->source->originalBytes()}, *state.pending);
      if (!prepared) { return std::unexpected(state.fail(preparationError(prepared.error()))); }
      state.document.emplace(std::move(*prepared));
    }
    auto saved = storage::saveOverallStatisticsView(state.lease, state.baseline, *state.pending, stop);
    if (!saved) {
      if (saved.error().code == storage::ErrorCode::Cancelled) { return std::unexpected(OverallServiceError::Cancelled); }
      return std::unexpected(state.fail(std::move(saved.error())));
    }
    // The adapter independently prepares the same frozen absolute document.
    // No fallible work is required after its successful synchronous transaction.
    state.changed = state.reducer.values() != *state.pending;
    state.baseline = std::move(saved->current);
    const auto disposition = saved->disposition == storage::OverallSaveDisposition::Committed
        ? OverallFlushDisposition::Committed : OverallFlushDisposition::Unchanged;
    state.document.reset(); state.pending.reset();
    return disposition;
  } catch (const std::bad_alloc&) {
    // Fixed pending values remain even if preparing its owned document failed.
    state.blocked = true;
    return std::unexpected(OverallServiceError::Memory);
  }
}
std::expected<OverallFlushDisposition, OverallServiceError> OverallStatisticsService::setMark(
    std::int64_t nowMillis, std::stop_token stop) {
  auto& state = *impl_;
  if (!state.onThread()) { return std::unexpected(OverallServiceError::WrongThread); }
  if (stop.stop_requested()) { return std::unexpected(OverallServiceError::Cancelled); }
  if (state.uncertain) { return std::unexpected(OverallServiceError::Uncertified); }
  if (state.sample && state.sample->quality == OverallSampleQuality::ClosedFinal) { return std::unexpected(OverallServiceError::Closed); }
  state.reducer.setMark(nowMillis); state.stage();
  return flush(stop);
}
std::expected<OverallFlushDisposition, OverallServiceError> OverallStatisticsService::clearMark(std::stop_token stop) {
  auto& state = *impl_;
  if (!state.onThread()) { return std::unexpected(OverallServiceError::WrongThread); }
  if (stop.stop_requested()) { return std::unexpected(OverallServiceError::Cancelled); }
  if (state.uncertain) { return std::unexpected(OverallServiceError::Uncertified); }
  if (state.sample && state.sample->quality == OverallSampleQuality::ClosedFinal) { return std::unexpected(OverallServiceError::Closed); }
  state.reducer.clearMark(); state.stage();
  return flush(stop);
}
std::expected<core::OverallScalar, OverallServiceError> OverallStatisticsService::querySince(core::OverallSince query) noexcept {
  auto& state = *impl_;
  if (!state.onThread()) { return std::unexpected(OverallServiceError::WrongThread); }
  const auto before = state.reducer.values();
  auto value = state.reducer.querySince(query);
  if (state.reducer.values() != before) { state.changed = true; }
  if (!value) { return std::unexpected(OverallServiceError::Domain); }
  return *value;
}
std::expected<OverallFlushDisposition, OverallServiceError> OverallStatisticsService::saveCurrent(std::stop_token stop) {
  auto& state = *impl_;
  if (!state.onThread()) { return std::unexpected(OverallServiceError::WrongThread); }
  if (stop.stop_requested()) { return std::unexpected(OverallServiceError::Cancelled); }
  state.stage();
  return flush(stop);
}
const core::OverallValues& OverallStatisticsService::values() const noexcept { return impl_->reducer.values(); }
core::OverallObservation OverallStatisticsService::observation() const noexcept { return impl_->reducer.observe(); }
core::OverallValues OverallStatisticsService::projectedValues() const noexcept {
  auto values = impl_->reducer.values();
  if (!impl_->sample) { return values; }
  const auto projected = impl_->reducer.coreStatistics(impl_->sample->absolute, true);
  using S = core::SavedStatistic;
  constexpr std::array<S, 4> fields{S::ProtocolUploaded, S::DataUploaded, S::ProtocolDownloaded, S::DataDownloaded};
  std::array<std::uint64_t, 4> deltas{};
  for (std::size_t i = 0; i < fields.size(); ++i) {
    auto& stored = values[static_cast<std::size_t>(fields[i])];
    // Recover each raw counter delta before applying the source update clamp.
    // Snapshot[] is deliberately stale after the source ClockGap branch.
    deltas[i] = static_cast<std::uint64_t>((*projected)[i]) - static_cast<std::uint64_t>(stored);
    stored = (*projected)[i] < 0 ? 0 : (*projected)[i];
  }
  for (std::size_t i = 0; i < 2; ++i) {
    auto& total = values[static_cast<std::size_t>(i ? S::Downloaded : S::Uploaded)];
    total = std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(total) + deltas[i * 2] + deltas[i * 2 + 1]);
    if (total < 0) { total = 0; }
  }
  return values;
}
const std::optional<OverallOwnerSample>& OverallStatisticsService::latestSample() const noexcept { return impl_->sample; }
const storage::OverallStatisticsView& OverallStatisticsService::baseline() const noexcept { return impl_->baseline; }
bool OverallStatisticsService::dirty() const noexcept { return impl_->changed; }
bool OverallStatisticsService::writesBlocked() const noexcept { return impl_->blocked; }
bool OverallStatisticsService::recoveredReadOnly() const noexcept { return impl_->recovery; }
bool OverallStatisticsService::uncertified() const noexcept { return impl_->uncertain; }
bool OverallStatisticsService::finalCountersApplied() const noexcept { return impl_->finalApplied; }
const storage::Error* OverallStatisticsService::lastStorageError() const noexcept { return impl_->error ? &*impl_->error : nullptr; }
const core::OverallDocument* OverallStatisticsService::pendingDocument() const noexcept { return impl_->document ? &*impl_->document : nullptr; }
const core::OverallValues* OverallStatisticsService::pendingValues() const noexcept { return impl_->pending ? &*impl_->pending : nullptr; }
} // namespace biglybtc::app
