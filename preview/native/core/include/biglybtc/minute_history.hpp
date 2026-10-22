#pragma once
#include "biglybtc/saved_history.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace biglybtc::core {

struct MinuteHistoryIdentity {
  std::uint64_t applicationRun = 0, profileSession = 0, engineEpoch = 0;
  bool operator==(const MinuteHistoryIdentity&) const = default;
};
// Actual independent domains. Missing provider is unknown, never numeric zero.
struct MinuteHistoryInput {
  // Protocol sent, payload sent, protocol received, payload received.
  std::optional<std::array<std::int64_t, 4>> peer;
  // DHT sent, DHT received; never derived from peer combined/payload counters.
  std::optional<std::array<std::int64_t, 2>> dht;
};
enum class MinuteHistoryEvent { Start, Tick, End, Suspend };
struct MinuteHistoryRequest {
  MinuteHistoryEvent event = MinuteHistoryEvent::Tick;
  MinuteHistoryIdentity identity;
  std::int64_t utcMilliseconds = 0;
  MinuteHistoryInput input;
  // Supplied by the independently qualified Overall boundary, Start only.
  std::optional<HistoryCounters> absoluteBaseline;
};
enum class MinuteHistoryError {
  InvalidIdentity, InvalidEvent, InvalidTime, MissingBaseline, UnknownProvider,
  Active, Inactive, WrongIdentity, Pending, StalePlan, Poisoned, Overflow, Limit
};
enum class MinuteHistoryIoResult { Complete, CancelledBeforeIo, Indeterminate };
enum class MinuteHistoryActionKind { Append, Close };
class MinuteHistoryAction {
public:
  MinuteHistoryActionKind kind() const noexcept { return kind_; }
  HistoryDate date() const noexcept { return date_; }
  std::string_view bytes() const noexcept { return {bytes_.data(), size_}; }
private:
  friend class MinuteHistoryRecorder;
  MinuteHistoryActionKind kind_ = MinuteHistoryActionKind::Close;
  HistoryDate date_;
  std::array<char, 192> bytes_{};
  std::size_t size_ = 0;
};
struct MinuteHistoryState {
  bool active = false, poisoned = false;
  MinuteHistoryIdentity identity;
  std::optional<HistoryDate> fileDate;
  HistoryCounters sourceOrigin{}, absoluteStart{}, previousRelative{};
  bool operator==(const MinuteHistoryState&) const = default;
};
class MinuteHistoryRecorder;
class MinuteHistoryPlan {
public:
  std::span<const MinuteHistoryAction> actions() const noexcept { return {actions_.data(), count_}; }
  bool providerGap() const noexcept { return providerGap_; }
private:
  friend class MinuteHistoryRecorder;
  MinuteHistoryPlan(const MinuteHistoryRecorder* owner, std::uint64_t revision) noexcept
      : owner_(owner), revision_(revision) {}
  const MinuteHistoryRecorder* owner_;
  std::uint64_t revision_;
  std::array<MinuteHistoryAction, 3> actions_{};
  std::size_t count_ = 0;
  bool providerGap_ = false;
  MinuteHistoryState after_;
};

// Worker-confined, Qt/IO/timer free. Plan bytes remain owned by the copied Plan.
// One outstanding plan; no semantic state advances before Complete. A cancelled
// plan is invalidated without advancing counters. Any ambiguous/partial IO
// poisons this recorder: no blind append retry or fabricated terminal record.
class MinuteHistoryRecorder final {
public:
  MinuteHistoryRecorder() = default;
  MinuteHistoryRecorder(const MinuteHistoryRecorder&) = delete;
  MinuteHistoryRecorder& operator=(const MinuteHistoryRecorder&) = delete;
  MinuteHistoryRecorder(MinuteHistoryRecorder&&) = delete;
  MinuteHistoryRecorder& operator=(MinuteHistoryRecorder&&) = delete;
  std::expected<MinuteHistoryPlan, MinuteHistoryError> prepare(const MinuteHistoryRequest&) noexcept;
  std::expected<void, MinuteHistoryError> finish(const MinuteHistoryPlan&, MinuteHistoryIoResult) noexcept;
  MinuteHistoryState state() const noexcept { return state_; }
private:
  bool append(MinuteHistoryPlan&, HistoryDate, char marker, std::int64_t minute,
              const HistoryCounters&) noexcept;
  void close(MinuteHistoryPlan&, HistoryDate) noexcept;
  MinuteHistoryState state_;
  std::uint64_t revision_ = 0;
  bool pending_ = false;
};
} // namespace biglybtc::core
