#include "history_range_view.hpp"

#include "file_io.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <dirent.h>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace biglybtc::storage {
namespace {
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
bool sameDirectory(const struct stat& a, const struct stat& b) {
  return S_ISDIR(a.st_mode) && S_ISDIR(b.st_mode) && a.st_dev == b.st_dev && a.st_ino == b.st_ino
      && a.st_uid == b.st_uid;
}
bool sameTimes(const struct stat& a, const struct stat& b) {
  return a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec
      && a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
bool sameFile(const struct stat& a, const struct stat& b) {
  return S_ISREG(a.st_mode) && S_ISREG(b.st_mode) && a.st_dev == b.st_dev && a.st_ino == b.st_ino
      && a.st_uid == b.st_uid && a.st_mode == b.st_mode && a.st_nlink == b.st_nlink
      && a.st_size == b.st_size && sameTimes(a, b);
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
std::expected<void, Error> rootUnchanged(const ProfileLease& lease, int root, const struct stat& before) {
  struct stat after{};
  if (!lease.valid() || lease.directoryHandle() != root || ::fstat(root, &after) != 0 || !sameDirectory(before, after)) {
    return failure(ErrorCode::Conflict, "Held history profile changed while reading");
  }
  return {};
}
// Open only a previously selected literal/date component. No special entry is
// opened first for inspection, and O_DIRECTORY|O_NOFOLLOW prevents blocking on a
// replaced FIFO or following a replaced symlink between inspection and open.
std::expected<std::optional<detail::Descriptor>, Error> directory(int parent, const std::string& name,
    struct stat& opened, bool missingAllowed) {
  struct stat named{};
  if (::fstatat(parent, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0) {
    if (errno == ENOENT && missingAllowed) { return std::nullopt; }
    return failure(errno == ENOENT ? ErrorCode::Conflict : ErrorCode::Io, "Cannot inspect saved history directory");
  }
  if (!S_ISDIR(named.st_mode) || named.st_uid != ::geteuid()) {
    return failure(ErrorCode::UnsafePath, "Saved history directory must be owned and physical");
  }
  detail::Descriptor fd(::openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (fd.get() < 0) {
    return failure(errno == ELOOP || errno == ENOTDIR ? ErrorCode::UnsafePath
        : errno == ENOENT ? ErrorCode::Conflict : ErrorCode::Io, "Cannot open saved history directory safely");
  }
  struct stat current{};
  if (::fstat(fd.get(), &opened) != 0 || ::fstatat(parent, name.c_str(), &current, AT_SYMLINK_NOFOLLOW) != 0
      || !sameDirectory(named, opened) || !sameDirectory(opened, current)) {
    return failure(ErrorCode::Conflict, "Saved history directory changed while opening");
  }
  return std::optional<detail::Descriptor>{std::move(fd)};
}
std::expected<Records, Error> readDay(const ProfileLease& lease, int root, const struct stat& rootBefore,
    core::HistoryDate date, std::stop_token stop) {
  const std::array<std::string, 3> names{"stats", digits(static_cast<unsigned>(date.year), 4), digits(date.month, 2)};
  std::array<detail::Descriptor, 3> directories{detail::Descriptor{-1}, detail::Descriptor{-1}, detail::Descriptor{-1}};
  std::array<struct stat, 3> opened{};
  std::size_t count = 0;
  const auto unchanged = [&]() -> std::expected<void, Error> {
    int parent = root;
    for (std::size_t i = 0; i < count; ++i) {
      struct stat named{}, held{};
      if (::fstat(directories[i].get(), &held) != 0
          || ::fstatat(parent, names[i].c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0
          || !sameDirectory(opened[i], held) || !sameDirectory(opened[i], named)) {
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
  int parent = root;
  for (std::size_t i = 0; i < directories.size(); ++i) {
    if (stop.stop_requested()) { return cancelled(); }
    auto next = directory(parent, names[i], opened[i], true);
    if (stop.stop_requested()) { return cancelled(); }
    if (!next) { return std::unexpected(std::move(next.error())); }
    if (!*next) { return missing(); }
    directories[i] = std::move(**next); ++count; parent = directories[i].get();
  }
  const auto name = digits(date.day, 2) + ".dat";
  struct stat namedBefore{};
  if (stop.stop_requested()) { return cancelled(); }
  if (::fstatat(parent, name.c_str(), &namedBefore, AT_SYMLINK_NOFOLLOW) != 0) {
    if (errno == ENOENT) { return missing(); }
    return failure(ErrorCode::Io, "Cannot inspect saved history day file");
  }
  if (!S_ISREG(namedBefore.st_mode) || namedBefore.st_uid != ::geteuid()) {
    return failure(ErrorCode::UnsafePath, "Saved history day must be an owned regular file");
  }
  detail::Descriptor fd(::openat(parent, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
  if (fd.get() < 0) {
    return failure(errno == ELOOP ? ErrorCode::UnsafePath : errno == ENOENT ? ErrorCode::Conflict : ErrorCode::Io,
        "Cannot open saved history day safely");
  }
  struct stat before{};
  if (::fstat(fd.get(), &before) != 0 || !sameFile(namedBefore, before)) {
    return failure(ErrorCode::Conflict, "Saved history day changed while opening");
  }
  if (before.st_size < 0 || static_cast<std::uint64_t>(before.st_size) > core::SavedHistoryMaximumBytes) {
    return failure(ErrorCode::InvalidFormat, "Saved history day exceeds the input limit");
  }
  std::string bytes(static_cast<std::size_t>(before.st_size), '\0');
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    if (stop.stop_requested()) { return cancelled(); }
    const auto got = ::read(fd.get(), bytes.data() + offset, bytes.size() - offset);
    if (stop.stop_requested()) { return cancelled(); }
    if (got < 0 && errno == EINTR) { continue; }
    if (got <= 0) { return failure(ErrorCode::Io, "Saved history day read was incomplete"); }
    offset += static_cast<std::size_t>(got);
  }
  if (stop.stop_requested()) { return cancelled(); }
  auto records = core::readSavedHistoryRecords(bytes, stop);
  if (stop.stop_requested()) { return cancelled(); }
  if (!records) { return coreFailure(records.error()); }
  struct stat heldAfter{}, namedAfter{};
  if (::fstat(fd.get(), &heldAfter) != 0 || ::fstatat(parent, name.c_str(), &namedAfter, AT_SYMLINK_NOFOLLOW) != 0
      || !sameFile(before, heldAfter) || !sameFile(before, namedAfter)) {
    return failure(ErrorCode::Conflict, "Saved history day changed while reading");
  }
  auto checked = unchanged(); if (!checked) { return std::unexpected(std::move(checked.error())); }
  if (stop.stop_requested()) { return cancelled(); }
  return Records{std::move(*records)};
}
class Entries final {
public:
  explicit Entries(DIR* value) : value_(value) {}
  ~Entries() { if (value_) { (void)::closedir(value_); } }
  Entries(const Entries&) = delete;
  Entries& operator=(const Entries&) = delete;
  DIR* get() const { return value_; }
private:
  DIR* value_;
};
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
std::expected<void, Error> inventory(int parent, const std::string& name, unsigned level,
    int year, unsigned month, std::size_t& entries, std::optional<std::int64_t>& first, std::stop_token stop) {
  if (stop.stop_requested()) { return cancelled(); }
  struct stat before{};
  auto opened = directory(parent, name, before, level == 0);
  if (!opened) { return std::unexpected(std::move(opened.error())); }
  if (!*opened) { return {}; }
  const int fd = (**opened).get();
  detail::Descriptor scan(::openat(fd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (scan.get() < 0) { return failure(ErrorCode::Io, "Cannot enumerate saved history directory"); }
  DIR* raw = ::fdopendir(scan.get());
  if (!raw) { return failure(ErrorCode::Io, "Cannot enumerate saved history directory"); }
  (void)scan.release(); Entries stream(raw);
  for (;;) {
    if (stop.stop_requested()) { return cancelled(); }
    errno = 0; const auto* next = ::readdir(stream.get()); const int readError = errno;
    if (stop.stop_requested()) { return cancelled(); }
    if (!next) {
      if (readError != 0) { return failure(ErrorCode::Io, "Saved history inventory read failed"); }
      break;
    }
    const std::string child(next->d_name);
    if (child == "." || child == "..") { continue; }
    if (entries == core::SavedHistoryRangeMaximumFiles) {
      return failure(ErrorCode::InvalidFormat, "Saved history inventory exceeds the entry limit");
    }
    ++entries;
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
      struct stat file{};
      if (::fstatat(fd, child.c_str(), &file, AT_SYMLINK_NOFOLLOW) != 0) {
        return failure(ErrorCode::Conflict, "Saved history date disappeared during inventory");
      }
      if (!S_ISREG(file.st_mode) || file.st_uid != ::geteuid()) {
        return failure(ErrorCode::UnsafePath, "Saved history date must be an owned regular file");
      }
      const auto point = std::chrono::duration_cast<std::chrono::milliseconds>(
          dayPoint({year, month, *value}).time_since_epoch()).count();
      if (!first || point < *first) { first = point; }
    }
  }
  struct stat held{}, named{};
  if (::fstat(fd, &held) != 0 || ::fstatat(parent, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0
      || !sameDirectory(before, held) || !sameDirectory(before, named)
      || !sameTimes(before, held) || !sameTimes(before, named)) {
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
    const int root = lease.directoryHandle(); struct stat rootBefore{};
    if (::fstat(root, &rootBefore) != 0 || !S_ISDIR(rootBefore.st_mode) || rootBefore.st_uid != ::geteuid()) {
      return failure(ErrorCode::UnsafePath, "Held history profile is not an owned directory");
    }
    if (request.period == core::HistoryPeriod::All) {
      std::size_t entries = 0; std::optional<std::int64_t> first;
      auto scanned = inventory(root, "stats", 0, 0, 0, entries, first, stop);
      if (stop.stop_requested()) { return cancelled(); }
      if (!scanned) { return std::unexpected(std::move(scanned.error())); }
      auto checked = rootUnchanged(lease, root, rootBefore);
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
      auto records = readDay(lease, root, rootBefore, date, stop);
      if (stop.stop_requested()) { return cancelled(); }
      if (!records) { return std::unexpected(std::move(records.error())); }
      auto added = accumulator->append(date, std::move(*records), stop);
      if (!added) { return coreFailure(added.error()); }
    }
    auto checked = rootUnchanged(lease, root, rootBefore);
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
