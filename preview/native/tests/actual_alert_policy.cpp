#include "actual_alert_policy.hpp"

#include <libtorrent/alert_types.hpp>
#include <libtorrent/session_handle.hpp>

#include <chrono>
#include <exception>
#include <stdexcept>
#include <vector>

namespace biglybtc::test::actual_alert_policy {
namespace lt = libtorrent;
namespace {
using namespace std::chrono_literals;
thread_local State* current = nullptr;
bool target(const lt::session_handle* receiver) noexcept {
  if (!current) { return false; }
  if (!current->receiver) { current->receiver = receiver; }
  return current->receiver == receiver;
}
bool selected(const Batch& batch, hook::Fault fault) noexcept {
  if (fault == hook::Fault::None || batch.unexpectedDroppedBits) { return false; }
  const bool observed = fault == hook::Fault::SessionError;
  return batch.sessionErrors == (observed ? 1u : 0u)
      && batch.droppedAlerts == (observed ? 0u : 1u)
      && batch.droppedGlobal == (fault == hook::Fault::DroppedSessionError ? 1u : 0u)
      && batch.droppedStats == (fault == hook::Fault::DroppedStats ? 1u : 0u)
      && batch.droppedRemoval == (fault == hook::Fault::DroppedRemoval ? 1u : 0u);
}
} // namespace

Scope::Scope(State& state) {
  if (current) { throw std::logic_error("Nested actual-alert policy scope"); }
  current = &state;
}
Scope::~Scope() { current = nullptr; }
Controller::Controller() {
  if (!hook::arm(false, false)) { throw std::logic_error("Causal controller already armed"); }
}
Controller::~Controller() {
  if (!hook::release_all_and_wait(3s) || !hook::finish()) { std::terminate(); }
}
bool arm(State& state, hook::Fault fault) noexcept {
  if (current != &state || fault == hook::Fault::None || state.selected != hook::Fault::None
      || state.pendingPost || state.expectingBatch) { return false; }
  const auto before = hook::snapshot();
  if (before.failed || before.overflow) { return false; }
  state.armAfter = before.count ? before.events[before.count-1].sequence : 0;
  state.selected = fault; state.pendingPost = true;
  hook::fault_on_next_stats(fault);
  return true;
}
void observeStage(engine::LocalStage stage) noexcept {
  if (!current) { return; }
  if (stage == engine::LocalStage::Flushed) { current->flushed = true; }
  if (stage == engine::LocalStage::CompletionSaved) { current->completionSaved = true; }
  if (stage == engine::LocalStage::InventorySaved) { current->inventorySaved = true; }
}

// Only these two exact GNU ELF entry points are wrapped. Both pass the actual
// call unchanged; no fabricated alerts, vector edits, second pop or span seam.
void realPost(lt::session_handle*) asm("__real__ZN10libtorrent14session_handle18post_session_statsEv");
void wrappedPost(lt::session_handle*) asm("__wrap__ZN10libtorrent14session_handle18post_session_statsEv");
void realPop(lt::session_handle*, std::vector<lt::alert*>*) asm("__real__ZN10libtorrent14session_handle10pop_alertsEPSt6vectorIPNS_5alertESaIS3_EE");
void wrappedPop(lt::session_handle*, std::vector<lt::alert*>*) asm("__wrap__ZN10libtorrent14session_handle10pop_alertsEPSt6vectorIPNS_5alertESaIS3_EE");

void wrappedPost(lt::session_handle* receiver) {
  auto* state = target(receiver) ? current : nullptr;
  if (state) { ++state->realPosts; }
  realPost(receiver);
  if (!state || !state->pendingPost) { return; }
  state->pendingPost = false;
  const bool emitted = hook::wait_for(hook::Kind::FaultEmitted, 1, state->armAfter, 2s);
  const bool copied = hook::wait_for(hook::Kind::StatsCopied, 1, state->armAfter, 2s);
  const auto ledger = hook::snapshot();
  for (std::size_t i = 0; i < ledger.count; ++i) {
    const auto& item = ledger.events[i];
    if (item.sequence <= state->armAfter) { continue; }
    if (item.kind == hook::Kind::FaultEmitted && !state->emittedSequence
        && item.detail == static_cast<int>(state->selected)) { state->emittedSequence = item.sequence; }
    if (item.kind == hook::Kind::StatsCopied && item.sequence > state->emittedSequence
        && state->emittedSequence && !state->copiedSequence) { state->copiedSequence = item.sequence; }
  }
  if (!emitted || !copied || !state->emittedSequence || !state->copiedSequence
      || ledger.failed || ledger.overflow) { state->protocolFailed = true; return; }
  // StatsCopied is immediately BEFORE emplace. A real synchronous network
  // query cannot complete until that network handler has finished publishing.
  // It changes no session state, reads no profile, and consumes no alerts.
  try {
    static_cast<void>(receiver->is_paused());
    ++state->barrierCalls; state->expectingBatch = true;
  } catch (...) { state->protocolFailed = true; }
}
void wrappedPop(lt::session_handle* receiver, std::vector<lt::alert*>* batch) {
  auto* state = target(receiver) ? current : nullptr;
  realPop(receiver, batch); // Exactly ONE real consumer, same vector untouched.
  if (!state) { return; }
  ++state->realPops;
  if (batch->size() > 4096 || state->batchCount == state->batches.size()) { state->overflow = true; return; }
  Batch facts; facts.size = batch->size();
  for (const auto* item : *batch) {
    if (!item) { state->protocolFailed = true; continue; }
    if (lt::alert_cast<lt::session_stats_alert>(item)) { ++facts.stats; }
    if (lt::alert_cast<lt::session_error_alert>(item)) { ++facts.sessionErrors; }
    if (const auto* dropped = lt::alert_cast<lt::alerts_dropped_alert>(item)) {
      ++facts.droppedAlerts;
      const std::size_t stats = dropped->dropped_alerts.test(lt::session_stats_alert::alert_type) ? 1 : 0;
      const std::size_t removal = dropped->dropped_alerts.test(lt::torrent_removed_alert::alert_type) ? 1 : 0;
      const std::size_t global = dropped->dropped_alerts.test(lt::session_error_alert::alert_type) ? 1 : 0;
      facts.droppedStats += stats; facts.droppedRemoval += removal; facts.droppedGlobal += global;
      facts.unexpectedDroppedBits += dropped->dropped_alerts.count() - stats - removal - global;
    }
  }
  if (state->expectingBatch) {
    state->expectingBatch = false;
    facts.selectedFaultAndStats = selected(facts, state->selected) && facts.stats == 1;
    if (facts.selectedFaultAndStats) {
      ++state->matchedFaultBatches;
      state->faultBatchBeforeFlushing = !state->reachedFlushing;
    }
    else { state->protocolFailed = true; }
  }
  state->batches[state->batchCount++] = facts;
  // facts own primitive counts only. Every alert pointer remains borrowed by
  // the owner for this normal turn and is never stored by this fixture.
}
} // namespace biglybtc::test::actual_alert_policy
