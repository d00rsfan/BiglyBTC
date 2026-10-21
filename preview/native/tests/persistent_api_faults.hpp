#pragma once

#include "biglybtc/engine_statistics.hpp"

#include <libtorrent/torrent_handle.hpp>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>

namespace biglybtc::test::accounting_fault {

enum class Mode {
  Pass, HoldIdleResponse, DropStats, DropRemoval, ThrowPost,
  MissingMetric, DuplicateMetric, GaugeMetric, NegativeIndex, CollidingIndex, OutsideIndex,
  Regression, NegativeCounter, PayloadExceedsCombined, EmptySpan
};
enum class EventKind {
  Pop, HeldPop, RealPost, PostThrow, ActualStats, DeliveredStats, DroppedStats,
  Remove, Removed, DroppedRemoval, Metrics, MappingFault, SyntheticSpan
};
struct Event {
  EventKind kind{};
  std::size_t posts = 0;
  std::size_t realPops = 0;
};
// Test-only and owner-thread confined. Copies only weak handles/scalar values;
// no alert pointer or counter span is retained. Fixed records never grow.
struct State {
  Mode mode = Mode::Pass;
  const void* receiver = nullptr;
  std::array<Event, 4096> events{};
  std::size_t eventCount = 0;
  bool overflow = false;
  std::size_t realPosts = 0, realPops = 0, popCalls = 0, metricCalls = 0;
  std::size_t actualStats = 0, deliveredStats = 0, droppedStats = 0;
  std::size_t removals = 0, deliveredRemovals = 0, droppedRemovals = 0;
  std::size_t mappingFaults = 0, syntheticSpans = 0, postThrows = 0;
  std::array<int, 4> realIndices{{-1, -1, -1, -1}};
  std::optional<core::EngineCounters> lastActualCounters;
  libtorrent::torrent_handle removing; // Actual copied weak identity only.
  bool holdNextPost = false, oldPending = false, extraPostBeforeOld = false;
  std::size_t heldTurns = 0, totalHeldTurns = 0, oldDeliveredEvent = 0;
  std::array<std::int64_t, 4096> synthetic{};
  void record(EventKind) noexcept;
};
class Scope final {
public:
  explicit Scope(State&);
  ~Scope();
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};

// Optional reusable app-test gate. Register before the first owned transfer;
// binds the first owner post API entry's receiver AND thread, then arm while idle.
// Other receivers/threads pass through. The controller never invokes an owner.
// Declare after the Bridge (or release with an earlier RAII guard) so release
// precedes every Bridge/engine join, including assertion/exception unwinding.
// One registration covers one owner's lifetime only; do not carry it over an
// owner teardown/recreation where an allocator could reuse the same address.
class CallGate final {
public:
  enum class Point { Post, Pop };
  CallGate();
  ~CallGate();
  CallGate(const CallGate&) = delete;
  CallGate& operator=(const CallGate&) = delete;
  bool bound() const;
  bool entered() const;
  bool waitEntered(std::chrono::milliseconds);
  bool arm(Point);
  void release() noexcept;
  // Called only by these test wrappers, serialized by their private mutex.
  static void visit(Point, const void*);
private:
  const void* receiver_ = nullptr;
  std::thread::id worker_{};
  Point point_ = Point::Pop;
  bool armed_ = false, entered_ = false, released_ = true;
  std::size_t waiting_ = 0;
  std::condition_variable condition_;
};

} // namespace biglybtc::test::accounting_fault
