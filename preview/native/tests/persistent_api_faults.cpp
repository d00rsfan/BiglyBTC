#include "persistent_api_faults.hpp"

#include <libtorrent/alert_types.hpp>
#include <libtorrent/session_handle.hpp>
#include <libtorrent/session_stats.hpp>

#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace biglybtc::test::accounting_fault {
namespace lt = libtorrent;
namespace {
thread_local State* active = nullptr;
std::mutex gateMutex;
CallGate* activeGate = nullptr;
constexpr std::array<std::string_view, 4> Names{
    "net.recv_bytes", "net.sent_bytes", "net.recv_payload_bytes", "net.sent_payload_bytes"};
State* target(const lt::session_handle* receiver) noexcept {
  if (!active) { return nullptr; }
  if (!active->receiver) { active->receiver = receiver; }
  return active->receiver == receiver ? active : nullptr;
}
bool counterMode(Mode mode) {
  return mode == Mode::Regression || mode == Mode::NegativeCounter
      || mode == Mode::PayloadExceedsCombined || mode == Mode::EmptySpan;
}
} // namespace

// Exact explicit receiver/argument/return ABI, pinned GNU ELF symbols. Use C++
// linkage plus assembler labels (avoids C-linkage non-C return-type warnings).
void realPost(lt::session_handle*) asm("__real__ZN10libtorrent14session_handle18post_session_statsEv");
void wrappedPost(lt::session_handle*) asm("__wrap__ZN10libtorrent14session_handle18post_session_statsEv");
void realPop(lt::session_handle*, std::vector<lt::alert*>*) asm("__real__ZN10libtorrent14session_handle10pop_alertsEPSt6vectorIPNS_5alertESaIS3_EE");
void wrappedPop(lt::session_handle*, std::vector<lt::alert*>*) asm("__wrap__ZN10libtorrent14session_handle10pop_alertsEPSt6vectorIPNS_5alertESaIS3_EE");
void realRemove(lt::session_handle*, const lt::torrent_handle&, lt::remove_flags_t) asm("__real__ZN10libtorrent14session_handle14remove_torrentERKNS_14torrent_handleENS_5flags13bitfield_flagIhNS_16remove_flags_tagEvEE");
void wrappedRemove(lt::session_handle*, const lt::torrent_handle&, lt::remove_flags_t) asm("__wrap__ZN10libtorrent14session_handle14remove_torrentERKNS_14torrent_handleENS_5flags13bitfield_flagIhNS_16remove_flags_tagEvEE");
std::vector<lt::stats_metric> realMetrics() asm("__real__ZN10libtorrent21session_stats_metricsEv");
std::vector<lt::stats_metric> wrappedMetrics() asm("__wrap__ZN10libtorrent21session_stats_metricsEv");
lt::span<const std::int64_t> realCounters(const lt::session_stats_alert*) asm("__real__ZNK10libtorrent19session_stats_alert8countersEv");
lt::span<const std::int64_t> wrappedCounters(const lt::session_stats_alert*) asm("__wrap__ZNK10libtorrent19session_stats_alert8countersEv");

void State::record(EventKind kind) noexcept {
  if (eventCount == events.size()) { overflow = true; return; }
  events[eventCount++] = {kind, realPosts, realPops};
}
Scope::Scope(State& state) {
  if (active) { throw std::logic_error("Nested persistent accounting fault scope"); }
  active = &state;
}
Scope::~Scope() { active = nullptr; }

CallGate::CallGate() {
  std::lock_guard lock(gateMutex);
  if (activeGate) { throw std::logic_error("Nested persistent API call gate"); }
  activeGate = this;
}
CallGate::~CallGate() {
  std::unique_lock lock(gateMutex);
  armed_ = false; released_ = true; condition_.notify_all();
  condition_.wait(lock, [&] { return waiting_ == 0; });
  if (activeGate == this) { activeGate = nullptr; }
}
bool CallGate::bound() const { std::lock_guard lock(gateMutex); return receiver_ != nullptr; }
bool CallGate::entered() const { std::lock_guard lock(gateMutex); return entered_; }
bool CallGate::waitEntered(std::chrono::milliseconds timeout) {
  std::unique_lock lock(gateMutex);
  return condition_.wait_for(lock, timeout, [&] { return entered_; });
}
bool CallGate::arm(Point point) {
  std::lock_guard lock(gateMutex);
  if (!receiver_ || waiting_ != 0 || armed_) { return false; }
  point_ = point; entered_ = false; released_ = false; armed_ = true; return true;
}
void CallGate::release() noexcept {
  std::lock_guard lock(gateMutex); armed_ = false; released_ = true; condition_.notify_all();
}
void CallGate::visit(Point point, const void* receiver) {
  std::unique_lock lock(gateMutex); auto* gate = activeGate;
  if (!gate) { return; }
  if (!gate->receiver_ && point == Point::Post) {
    gate->receiver_ = receiver; gate->worker_ = std::this_thread::get_id();
  }
  if (!gate->armed_ || gate->point_ != point || gate->receiver_ != receiver
      || gate->worker_ != std::this_thread::get_id()) { return; }
  gate->armed_ = false; gate->entered_ = true; ++gate->waiting_; gate->condition_.notify_all();
  gate->condition_.wait(lock, [&] { return gate->released_; });
  --gate->waiting_; gate->condition_.notify_all();
}

void wrappedPost(lt::session_handle* receiver) {
  CallGate::visit(CallGate::Point::Post, receiver);
  auto* state = target(receiver);
  if (!state) { realPost(receiver); return; }
  if (state->mode == Mode::ThrowPost) {
    ++state->postThrows; state->record(EventKind::PostThrow);
    throw std::runtime_error("Test-only attributable stats-post failure");
  }
  if (state->oldPending) { state->extraPostBeforeOld = true; }
  ++state->realPosts; state->record(EventKind::RealPost);
  if (state->mode == Mode::HoldIdleResponse && state->holdNextPost) {
    state->holdNextPost = false; state->oldPending = true; state->heldTurns = 3;
  }
  realPost(receiver);
}

void wrappedPop(lt::session_handle* receiver, std::vector<lt::alert*>* pending) {
  CallGate::visit(CallGate::Point::Pop, receiver);
  auto* state = target(receiver);
  if (!state) { realPop(receiver, pending); return; }
  ++state->popCalls; state->record(EventKind::Pop);
  if (state->mode == Mode::HoldIdleResponse && state->heldTurns != 0) {
    --state->heldTurns; ++state->totalHeldTurns; pending->clear(); state->record(EventKind::HeldPop);
    return; // No real pop, no borrowed pointer retained; LT owns its queued response.
  }
  realPop(receiver, pending); ++state->realPops;
  // Inspect and filter this SAME batch only. The next real pop may invalidate
  // every pointer. Do not save one or call an owner/observer from this wrapper.
  std::erase_if(*pending, [&](lt::alert* alert) {
    if (!alert) { return false; }
    if (auto* stats = lt::alert_cast<lt::session_stats_alert>(alert)) {
      ++state->actualStats; state->record(EventKind::ActualStats);
      const auto values = realCounters(stats); // Deliberately bypass synthetic accessor seam.
      if (std::all_of(state->realIndices.begin(), state->realIndices.end(),
          [&](int index) { return index >= 0 && index < values.size(); })) {
        state->lastActualCounters = core::EngineCounters{values[state->realIndices[0]], values[state->realIndices[1]],
            values[state->realIndices[2]], values[state->realIndices[3]]};
      }
      if (state->mode == Mode::DropStats) {
        ++state->droppedStats; state->record(EventKind::DroppedStats); return true;
      }
      ++state->deliveredStats; state->record(EventKind::DeliveredStats);
      if (state->oldPending) { state->oldPending = false; state->oldDeliveredEvent = state->eventCount; }
    }
    if (auto* removed = lt::alert_cast<lt::torrent_removed_alert>(alert);
        removed && removed->handle == state->removing) {
      if (state->mode == Mode::DropRemoval) {
        ++state->droppedRemovals; state->record(EventKind::DroppedRemoval); return true;
      }
      ++state->deliveredRemovals; state->record(EventKind::Removed);
    }
    return false;
  });
}

void wrappedRemove(lt::session_handle* receiver, const lt::torrent_handle& handle, lt::remove_flags_t flags) {
  if (auto* state = target(receiver)) {
    state->removing = handle; ++state->removals; state->record(EventKind::Remove);
  }
  realRemove(receiver, handle, flags);
}

std::vector<lt::stats_metric> wrappedMetrics() {
  auto values = realMetrics(); auto* state = active;
  if (!state) { return values; }
  ++state->metricCalls; state->record(EventKind::Metrics);
  for (std::size_t i = 0; i < Names.size(); ++i) {
    for (const auto& metric : values) {
      if (metric.name && std::string_view(metric.name) == Names[i]) { state->realIndices[i] = metric.value_index; }
    }
  }
  const auto found = std::find_if(values.begin(), values.end(), [](const auto& metric) {
    return metric.name && std::string_view(metric.name) == Names[0];
  });
  if (found == values.end()) { return values; } // Positive-control assertions will fail.
  bool changed = true;
  switch (state->mode) {
    case Mode::MissingMetric: values.erase(found); break;
    case Mode::DuplicateMetric: { const auto copy = *found; values.push_back(copy); break; }
    case Mode::GaugeMetric: found->type = lt::metric_type_t::gauge; break;
    case Mode::NegativeIndex: found->value_index = -1; break;
    case Mode::CollidingIndex: found->value_index = state->realIndices[1]; break;
    case Mode::OutsideIndex: found->value_index = std::numeric_limits<int>::max(); break;
    default: changed = false; break;
  }
  if (changed) { ++state->mappingFaults; state->record(EventKind::MappingFault); }
  return values; // Only our owned returned vector changed, never LT's alert memory.
}

lt::span<const std::int64_t> wrappedCounters(const lt::session_stats_alert* alert) {
  const auto values = realCounters(alert); auto* state = active;
  if (!state || !counterMode(state->mode)) { return values; }
  ++state->syntheticSpans; state->record(EventKind::SyntheticSpan);
  if (state->mode == Mode::EmptySpan) { return {}; }
  if (values.size() < 0 || static_cast<std::size_t>(values.size()) > state->synthetic.size()
      || !std::all_of(state->realIndices.begin(), state->realIndices.end(),
          [&](int index) { return index >= 0 && index < values.size(); })) {
    state->overflow = true; return {};
  }
  std::copy(values.begin(), values.end(), state->synthetic.begin());
  const auto& i = state->realIndices;
  if (state->mode == Mode::Regression) {
    for (const auto at : i) { state->synthetic[static_cast<std::size_t>(at)] = 0; }
  } else if (state->mode == Mode::NegativeCounter) { state->synthetic[static_cast<std::size_t>(i[0])] = -1; }
  else { state->synthetic[static_cast<std::size_t>(i[0])] = 0; state->synthetic[static_cast<std::size_t>(i[2])] = 1; }
  // This view is explicit synthetic policy input. It outlives the immediate
  // retain call; actual alert bytes and lastActualCounters remain unchanged.
  return {state->synthetic.data(), values.size()};
}

} // namespace biglybtc::test::accounting_fault
