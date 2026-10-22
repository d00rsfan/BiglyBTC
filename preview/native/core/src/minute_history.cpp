#include "biglybtc/minute_history.hpp"
#include <charconv>
#include <chrono>
#include <limits>

namespace biglybtc::core {
namespace {
bool add(std::int64_t a, std::int64_t b, std::int64_t& result) noexcept {
  if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b)
      || (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) { return false; }
  result = a + b; return true;
}
bool subtract(std::int64_t a, std::int64_t b, std::int64_t& result) noexcept {
  if ((b > 0 && a < std::numeric_limits<std::int64_t>::min() + b)
      || (b < 0 && a > std::numeric_limits<std::int64_t>::max() + b)) { return false; }
  result = a - b; return true;
}
bool identity(MinuteHistoryIdentity value) noexcept {
  return value.applicationRun && value.profileSession && value.engineEpoch;
}
HistoryDate day(std::int64_t milliseconds) noexcept {
  const std::chrono::year_month_day date{std::chrono::floor<std::chrono::days>(
      std::chrono::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds{milliseconds}})};
  return {int(date.year()), unsigned(date.month()), unsigned(date.day())};
}
std::optional<HistoryCounters> counters(const MinuteHistoryInput& input) noexcept {
  if (!input.peer || !input.dht) { return {}; }
  return HistoryCounters{(*input.peer)[0], (*input.peer)[1], (*input.peer)[2], (*input.peer)[3],
                         (*input.dht)[0], (*input.dht)[1]};
}
}

bool MinuteHistoryRecorder::append(MinuteHistoryPlan& plan, HistoryDate date, char marker,
    std::int64_t minute, const HistoryCounters& values) noexcept {
  if (plan.count_ == plan.actions_.size()) { return false; }
  auto& action = plan.actions_[plan.count_];
  action.kind_ = MinuteHistoryActionKind::Append; action.date_ = date;
  char* next = action.bytes_.data(); char* end = next + action.bytes_.size();
  if (marker) {
    *next++ = marker; *next++ = ','; *next++ = '1'; *next++ = ',';
    const auto result = std::to_chars(next, end, minute);
    if (result.ec != std::errc{}) { return false; } next = result.ptr;
  }
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (marker || i) { if (next == end) { return false; } *next++ = ','; }
    const auto result = std::to_chars(next, end, values[i]);
    if (result.ec != std::errc{}) { return false; } next = result.ptr;
  }
  if (next == end) { return false; } *next++ = '\n';
  action.size_ = static_cast<std::size_t>(next - action.bytes_.data()); ++plan.count_; return true;
}
void MinuteHistoryRecorder::close(MinuteHistoryPlan& plan, HistoryDate date) noexcept {
  auto& action = plan.actions_[plan.count_++];
  action.kind_ = MinuteHistoryActionKind::Close; action.date_ = date; action.size_ = 0;
}

std::expected<MinuteHistoryPlan, MinuteHistoryError> MinuteHistoryRecorder::prepare(
    const MinuteHistoryRequest& request) noexcept {
  using Error = MinuteHistoryError;
  if (state_.poisoned) { return std::unexpected(Error::Poisoned); }
  if (pending_) { return std::unexpected(Error::Pending); }
  if (!identity(request.identity)) { return std::unexpected(Error::InvalidIdentity); }
  if (request.event != MinuteHistoryEvent::Start && request.event != MinuteHistoryEvent::Tick
      && request.event != MinuteHistoryEvent::End && request.event != MinuteHistoryEvent::Suspend) {
    return std::unexpected(Error::InvalidEvent);
  }
  if (revision_ == std::numeric_limits<std::uint64_t>::max()) { return std::unexpected(Error::Limit); }
  const bool start = request.event == MinuteHistoryEvent::Start;
  if (start && state_.active) { return std::unexpected(Error::Active); }
  if (!start && !state_.active) { return std::unexpected(Error::Inactive); }
  if (!start && request.identity != state_.identity) { return std::unexpected(Error::WrongIdentity); }
  if (start != request.absoluteBaseline.has_value()) { return std::unexpected(Error::MissingBaseline); }
  const bool suspend = request.event == MinuteHistoryEvent::Suspend;
  if (!suspend && (request.utcMilliseconds < 0 || request.utcMilliseconds > 253402300799999LL)) {
    return std::unexpected(Error::InvalidTime);
  }
  const auto sample = counters(request.input);
  if (start && !sample) { return std::unexpected(Error::UnknownProvider); }
  MinuteHistoryPlan plan(this, revision_); plan.after_ = state_;
  if (suspend || !sample) {
    // VERSION1 has no unknown numeric cell. Close without inventing e/zero;
    // a later known provider requires a fresh explicit session baseline.
    close(plan, *state_.fileDate); plan.providerGap_ = !sample;
    plan.after_.active = false; plan.after_.fileDate.reset();
  } else {
    const auto date = day(request.utcMilliseconds);
    const auto minute = request.utcMilliseconds / 60000;
    if (start) {
      if (!append(plan, date, 's', minute, *request.absoluteBaseline)) { return std::unexpected(Error::Limit); }
      plan.after_.active = true; plan.after_.identity = request.identity; plan.after_.fileDate = date;
      plan.after_.sourceOrigin = *sample; plan.after_.absoluteStart = *request.absoluteBaseline;
      plan.after_.previousRelative = {};
    } else {
      HistoryCounters relative{}, diff{};
      for (std::size_t i = 0; i < relative.size(); ++i) {
        if (!subtract((*sample)[i], state_.sourceOrigin[i], relative[i])
            || !subtract(relative[i], state_.previousRelative[i], diff[i])) { return std::unexpected(Error::Overflow); }
      }
      const bool end = request.event == MinuteHistoryEvent::End;
      if (!append(plan, *state_.fileDate, end ? 'e' : '\0', minute, diff)) { return std::unexpected(Error::Limit); }
      plan.after_.previousRelative = relative;
      if (end) {
        // Even after midnight, source writes e only to the already open old file.
        close(plan, *state_.fileDate); plan.after_.active = false; plan.after_.fileDate.reset();
      } else if (date != *state_.fileDate) {
        close(plan, *state_.fileDate);
        for (std::size_t i = 0; i < relative.size(); ++i) {
          if (!add(state_.absoluteStart[i], relative[i], plan.after_.absoluteStart[i])
              || !add(state_.sourceOrigin[i], relative[i], plan.after_.sourceOrigin[i])) {
            return std::unexpected(Error::Overflow);
          }
        }
        if (!append(plan, date, 's', minute, plan.after_.absoluteStart)) { return std::unexpected(Error::Limit); }
        plan.after_.fileDate = date; plan.after_.previousRelative = {};
      }
    }
  }
  pending_ = true; return plan;
}

std::expected<void, MinuteHistoryError> MinuteHistoryRecorder::finish(
    const MinuteHistoryPlan& plan, MinuteHistoryIoResult result) noexcept {
  if (!pending_ || plan.owner_ != this || plan.revision_ != revision_) {
    return std::unexpected(MinuteHistoryError::StalePlan);
  }
  if (result != MinuteHistoryIoResult::Complete && result != MinuteHistoryIoResult::CancelledBeforeIo
      && result != MinuteHistoryIoResult::Indeterminate) { return std::unexpected(MinuteHistoryError::InvalidEvent); }
  if (result == MinuteHistoryIoResult::Complete) { state_ = plan.after_; }
  else if (result == MinuteHistoryIoResult::Indeterminate) { state_.poisoned = true; state_.active = false; }
  pending_ = false; ++revision_; return {};
}
} // namespace biglybtc::core
