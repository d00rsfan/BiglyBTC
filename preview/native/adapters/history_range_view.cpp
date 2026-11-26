#include "history_range_view.hpp"
#include "state_directory_listing.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <new>
#include <utility>

namespace biglybtc::storage {
namespace {
using namespace detail;
using Records = std::optional<std::vector<core::SavedHistoryRecord>>;
std::unexpected<Error> failure(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}
std::unexpected<Error> cancelled() { return failure(ErrorCode::Cancelled, "Saved history range read cancelled"); }
std::unexpected<Error> coreFailure(core::SavedHistoryError code) {
  switch (code) {
    case core::SavedHistoryError::Cancelled: return cancelled();
    case core::SavedHistoryError::Memory: return failure(ErrorCode::Io, "Insufficient memory for saved history range");
    case core::SavedHistoryError::UnsupportedVersion: return failure(ErrorCode::Unsupported, "Saved history version is unsupported");
    default: return failure(ErrorCode::InvalidFormat, "Saved history range is invalid, excessive or overflowing");
  }
}
std::string digits(unsigned value, std::size_t width) {
  std::string text(width, '0');
  for (std::size_t i = width; i > 0; --i) { text[i - 1] = static_cast<char>('0' + value % 10); value /= 10; }
  return text;
}
std::chrono::sys_days dayPoint(core::HistoryDate date) {
  return std::chrono::sys_days{std::chrono::year{date.year} / std::chrono::month{date.month} / std::chrono::day{date.day}};
}
core::HistoryDate dateAt(std::chrono::sys_days point) {
  const std::chrono::year_month_day date{point};
  return {static_cast<int>(date.year()), static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day())};
}
std::expected<void, Error> rootUnchanged(const ProfileLease& lease, StateHandle root, const StateStamp& before) {
  const auto after = stateStamp(root);
  if (!lease.valid() || lease.directoryHandle() != root || !after || !sameStateDirectory(before, *after)) {
    return failure(ErrorCode::Conflict, "Held history profile changed while reading");
  }
  return {};
}

std::expected<std::optional<StateDescriptor>, Error> directory(StateHandle parent, const std::string& name,
    StateStamp& opened, bool missingAllowed) {
  const auto before = stateStampAt(parent, name);
  if (!before) { return std::unexpected(before.error()); }
  if (!*before) {
    if (missingAllowed) { return std::nullopt; }
    return failure(ErrorCode::Conflict, "Saved history directory disappeared");
  }
  if (!(*before)->directory || !(*before)->owned) {
    return failure(ErrorCode::UnsafePath, "Saved history directory must be owned and physical");
  }
  auto fd = openDirectoryAt(parent, name);
  if (!fd) {
    if (fd.error().code == ErrorCode::Missing) {
      return failure(ErrorCode::Conflict, "Saved history directory disappeared");
    }
    return std::unexpected(fd.error());
  }
  const auto held = stateStamp(fd->get());
  const auto named = stateStampAt(parent, name);
  if (!held || !named || !*named
      || !sameStateDirectory(**before, *held) || !sameStateDirectory(*held, **named)) {
    return failure(ErrorCode::Conflict, "Saved history directory changed while opening");
  }
  opened = *held;
  return std::optional<StateDescriptor>{std::move(*fd)};
}
std::expected<Records, Error> readDay(const ProfileLease& lease, StateHandle root, const StateStamp& rootBefore,
    core::HistoryDate date, std::stop_token stop) {
  const std::array<std::string, 3> names{"stats", digits(static_cast<unsigned>(date.year), 4), digits(date.month, 2)};
  std::array<StateDescriptor, 3> directories;
  std::array<StateStamp, 3> opened{};
  std::size_t count = 0;
  const auto unchanged = [&]() -> std::expected<void, Error> {
    auto parent = root;
    for (std::size_t i = 0; i < count; ++i) {
      const auto held = stateStamp(directories[i].get());
      const auto named = stateStampAt(parent, names[i]);
      if (!held || !named || !*named ||
          !sameStateDirectory(opened[i], *held) || !sameStateDirectory(opened[i], **named)) {
        return failure(ErrorCode::Conflict, "Saved history directory changed while reading");
      }
      parent = directories[i].get();
    }
    return rootUnchanged(lease, root, rootBefore);
  };
  const auto missing = [&]() -> std::expected<Records, Error> {
    if (stop.stop_requested()) { return cancelled(); }
    auto checked = unchanged(); if (!checked) { return std::unexpected(std::move(checked.error())); }
    return Records{};
  };
  auto parent = root;
  for (std::size_t i = 0; i < directories.size(); ++i) {
    if (stop.stop_requested()) { return cancelled(); }
    auto next = directory(parent, names[i], opened[i], true);
    if (stop.stop_requested()) { return cancelled(); }
    if (!next) { return std::unexpected(std::move(next.error())); }
    if (!*next) { return missing(); }
    directories[i] = std::move(**next); ++count; parent = directories[i].get();
  }
  const auto name = digits(date.day, 2) + ".dat";
  if (stop.stop_requested()) { return cancelled(); }
  const auto namedBefore = stateStampAt(parent, name);
  if (!namedBefore) { return std::unexpected(namedBefore.error()); }
  if (!*namedBefore) { return missing(); }
  if (!(*namedBefore)->regular || !(*namedBefore)->owned) {
    return failure(ErrorCode::UnsafePath, "Saved history day must be an owned regular file");
  }
  auto file = openStateFile(parent, name);
  if (!file) {
    if (file.error().code == ErrorCode::Missing) {
      return failure(ErrorCode::Conflict, "Saved history file disappeared while opening");
    }
    return std::unexpected(file.error());
  }
  const auto before = stateStamp(file->get());
  if (!before || !sameStateSnapshot(**namedBefore, *before)) {
    return failure(ErrorCode::Conflict, "Saved history day changed while opening");
  }
  if (before->size < 0 || static_cast<std::uint64_t>(before->size) > core::SavedHistoryMaximumBytes) {
    return failure(ErrorCode::InvalidFormat, "Saved history day exceeds the input limit");
  }
  std::string bytes(static_cast<std::size_t>(before->size), '\0');
  for (std::size_t offset = 0; offset < bytes.size();) {
    if (stop.stop_requested()) { return cancelled(); }
    const auto got = readStateSequential(file->get(), bytes.data() + offset, bytes.size() - offset);
    if (stop.stop_requested()) { return cancelled(); }
    if (got <= 0) { return failure(ErrorCode::Io, "Saved history day read was incomplete"); }
    offset += static_cast<std::size_t>(got);
  }
  if (stop.stop_requested()) { return cancelled(); }
  auto records = core::readSavedHistoryRecords(bytes, stop);
  if (stop.stop_requested()) { return cancelled(); }
  if (!records) { return coreFailure(records.error()); }
  const auto heldAfter = stateStamp(file->get());
  const auto namedAfter = stateStampAt(parent, name);
  if (!heldAfter || !namedAfter || !*namedAfter
      || !sameStateSnapshot(*before, *heldAfter) || !sameStateSnapshot(*before, **namedAfter)) {
    return failure(ErrorCode::Conflict, "Saved history day changed while reading");
  }
  auto checked = unchanged(); if (!checked) { return std::unexpected(std::move(checked.error())); }
  if (stop.stop_requested()) { return cancelled(); }
  return Records{std::move(*records)};
}
std::optional<unsigned> number(std::string_view text, std::size_t width, unsigned lower, unsigned upper) {
  if (text.size() != width) { return {}; }
  unsigned value = 0;
  for (const char digit : text) {
    if (digit < '0' || digit > '9') { return {}; }
    value = value * 10 + static_cast<unsigned>(digit - '0');
  }
  if (value < lower || value > upper) { return {}; }
  return value;
}
// Only three levels are possible: stats/year/month. Stack depth and retained
// directory descriptors are fixed even if an adversary supplies many entries.
std::expected<void, Error> inventory(StateHandle parent, const std::string& name, unsigned level,
    int year, unsigned month, std::size_t& entries, std::optional<std::int64_t>& first, std::stop_token stop) {
  if (stop.stop_requested()) { return cancelled(); }
  StateStamp before{};
  auto opened = directory(parent, name, before, level == 0);
  if (!opened) { return std::unexpected(std::move(opened.error())); }
  if (!*opened) { return {}; }
  const auto fd = (**opened).get();
  const auto names = listStateDirectoryNames(fd, core::SavedHistoryRangeMaximumFiles - entries + 2, stop);
  if (!names) {
    if (names.error().code == ErrorCode::Unsupported) {
      return failure(ErrorCode::InvalidFormat, "Saved history inventory exceeds the entry limit");
    }
    return std::unexpected(names.error());
  }
  for (const auto& leaf : *names) {
    if (stop.stop_requested()) { return cancelled(); }
    if (leaf == "." || leaf == "..") { continue; }
    if (entries == core::SavedHistoryRangeMaximumFiles) {
      return failure(ErrorCode::InvalidFormat, "Saved history inventory exceeds the entry limit");
    }
    ++entries;
    const auto& component = leaf.native();
    if (!std::ranges::all_of(component, [](auto ch) { return static_cast<std::uint32_t>(ch) <= 127; })) { continue; }
    std::string child;
    child.reserve(component.size());
    for (const auto ch : component) { child.push_back(static_cast<char>(ch)); }
    if (level == 0) {
      auto value = number(child, 4, 1970, 9999); if (!value) { continue; }
      auto result = inventory(fd, child, 1, static_cast<int>(*value), 0, entries, first, stop);
      if (!result) { return result; }
    } else if (level == 1) {
      auto value = number(child, 2, 1, 12); if (!value) { continue; }
      auto result = inventory(fd, child, 2, year, *value, entries, first, stop);
      if (!result) { return result; }
    } else {
      if (child.size() != 6 || child.substr(2) != ".dat") { continue; }
      auto value = number(std::string_view(child).substr(0, 2), 2, 1, 31);
      if (!value || !core::validHistoryDate({year, month, *value})) { continue; }
      const auto file = stateStampAt(fd, leaf);
      if (!file || !*file) {
        return failure(ErrorCode::Conflict, "Saved history date disappeared during inventory");
      }
      if (!(*file)->regular || !(*file)->owned) {
        return failure(ErrorCode::UnsafePath, "Saved history date must be an owned regular file");
      }
      const auto point = std::chrono::duration_cast<std::chrono::milliseconds>(
          dayPoint({year, month, *value}).time_since_epoch()).count();
      if (!first || point < *first) { first = point; }
    }
  }
  const auto held = stateStamp(fd);
  const auto named = stateStampAt(parent, name);
  if (!held || !named || !*named || !sameStateDirectory(before, *held) || !sameStateDirectory(before, **named)
      || !sameStateTimestamp(before.modified, held->modified) || !sameStateTimestamp(before.modified, (*named)->modified)
      || !sameStateTimestamp(before.changed, held->changed) || !sameStateTimestamp(before.changed, (*named)->changed)) {
    return failure(ErrorCode::Conflict, "Saved history directory changed during inventory");
  }
  return {};
}
} // namespace

std::expected<core::SavedHistoryRange, Error> readSavedHistoryRangeView(
    const ProfileLease& lease, core::HistoryRangeRequest request, std::stop_token stop) {
  try {
    if (stop.stop_requested()) { return cancelled(); }
    if (!lease.valid()) { return failure(ErrorCode::UnsafePath, "A held profile lease is required for saved history"); }
    auto plan = core::planSavedHistoryRange(request);
    if (!plan) { return coreFailure(plan.error()); }
    const auto root = lease.directoryHandle();
    const auto rootBefore = stateStamp(root);
    if (!rootBefore || !rootBefore->directory || !rootBefore->owned) {
      return failure(ErrorCode::UnsafePath, "Held history profile is not an owned directory");
    }
    if (request.period == core::HistoryPeriod::All) {
      std::size_t entries = 0; std::optional<std::int64_t> first;
      auto scanned = inventory(root, "stats", 0, 0, 0, entries, first, stop);
      if (stop.stop_requested()) { return cancelled(); }
      if (!scanned) { return std::unexpected(std::move(scanned.error())); }
      auto checked = rootUnchanged(lease, root, *rootBefore);
      if (!checked) { return std::unexpected(std::move(checked.error())); }
      if (stop.stop_requested()) { return cancelled(); }
      plan = core::planSavedHistoryRange(request, first);
      if (!plan) { return coreFailure(plan.error()); }
    }
    auto accumulator = core::SavedHistoryRangeAccumulator::create(*plan);
    if (!accumulator) { return coreFailure(accumulator.error()); }
    const auto first = dayPoint(plan->firstFile), last = dayPoint(plan->lastFile);
    for (auto point = first; point <= last; point += std::chrono::days{1}) {
      if (stop.stop_requested()) { return cancelled(); }
      const auto date = dateAt(point);
      auto records = readDay(lease, root, *rootBefore, date, stop);
      if (stop.stop_requested()) { return cancelled(); }
      if (!records) { return std::unexpected(std::move(records.error())); }
      auto added = accumulator->append(date, std::move(*records), stop);
      if (!added) { return coreFailure(added.error()); }
    }
    auto checked = rootUnchanged(lease, root, *rootBefore);
    if (!checked) { return std::unexpected(std::move(checked.error())); }
    if (stop.stop_requested()) { return cancelled(); }
    auto result = std::move(*accumulator).finish();
    if (!result) { return coreFailure(result.error()); }
    if (stop.stop_requested()) { return cancelled(); }
    return std::move(*result);
  } catch (const std::bad_alloc&) {
    return stop.stop_requested() ? cancelled() : failure(ErrorCode::Io, "Insufficient memory for saved history range");
  }
}

} // namespace biglybtc::storage
