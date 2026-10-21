#pragma once
#include "persistent_session.hpp"
#include <libtorrent/aux_/biglybtc_causal_test.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace biglybtc::test::actual_alert_policy {
namespace hook = libtorrent::biglybtc_causal;
struct Batch {
  std::size_t size = 0;
  std::size_t stats = 0;
  std::size_t sessionErrors = 0;
  std::size_t droppedAlerts = 0;
  std::size_t unexpectedDroppedBits = 0;
  std::size_t droppedStats = 0;
  std::size_t droppedRemoval = 0;
  std::size_t droppedGlobal = 0;
  bool selectedFaultAndStats = false;
};
struct State {
  const void* receiver = nullptr; // Identity only; never dereferenced after a call.
  std::array<Batch, 512> batches{};
  std::size_t batchCount = 0;
  std::size_t realPosts = 0;
  std::size_t realPops = 0;
  std::size_t barrierCalls = 0;
  std::size_t matchedFaultBatches = 0;
  hook::Fault selected = hook::Fault::None;
  std::uint64_t armAfter = 0;
  std::uint64_t emittedSequence = 0;
  std::uint64_t copiedSequence = 0;
  bool pendingPost = false;
  bool expectingBatch = false;
  bool protocolFailed = false;
  bool overflow = false;
  bool armedFromPositiveIncompletePayload = false;
  std::int64_t payloadAtArm = 0;
  std::optional<core::EngineStatistics> observedBeforeFault;
  bool reachedFlushing = false;
  bool faultBatchBeforeFlushing = false;
  bool flushed = false;
  bool completionSaved = false;
  bool inventorySaved = false;
};
// Worker-thread confined, one target owner. It outlives that owner's teardown.
class Scope final {
public:
  explicit Scope(State&);
  ~Scope();
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};
// Construct AFTER the lazy owner: every unwind disarms/releases before its join.
// Callback holds are disabled; this is not the socket/disk causal cohort.
class Controller final {
public:
  Controller();
  ~Controller();
  Controller(const Controller&) = delete;
  Controller& operator=(const Controller&) = delete;
};
bool arm(State&, hook::Fault) noexcept;
void observeStage(engine::LocalStage) noexcept;
} // namespace biglybtc::test::actual_alert_policy
