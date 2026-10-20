#include "history_month_view.hpp"
#include "biglybtc/saved_history_week.hpp"

#include <algorithm>
#include <cstdarg>

#include <array>
#include <bitset>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc;
using I = std::int64_t;
constexpr core::HistoryDate End{1970, 1, 30};
std::size_t assertions = 0, cases = 0, retainedQueries = 0, retainedScalars = 0;
void check(bool condition, std::string_view message) {
  ++assertions; if (!condition) { throw std::runtime_error(std::string(message)); }
}
struct Fd {
  int value;
  explicit Fd(int fd) : value(fd) {}
  ~Fd() { if (value >= 0) { (void)::close(value); } }
  Fd(const Fd&) = delete; Fd& operator=(const Fd&) = delete;
};
std::string bytes(const std::filesystem::path& path) {
  Fd file(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  check(file.value >= 0, "cannot read explicit fixture leaf");
  std::string result; std::array<char, 4096> buffer{};
  for (;;) {
    const auto count = ::read(file.value, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) { continue; }
    check(count >= 0, "explicit fixture read failed"); if (count == 0) { return result; }
    result.append(buffer.data(), static_cast<std::size_t>(count));
  }
}
struct Stamp {
  dev_t device; ino_t inode; off_t size; mode_t mode; nlink_t links;
  timespec modified; timespec changed; std::string data;
};
Stamp stamp(const std::filesystem::path& path) {
  struct stat info{}; check(::lstat(path.c_str(), &info) == 0, "cannot stamp explicit path");
  return {info.st_dev, info.st_ino, info.st_size, info.st_mode, info.st_nlink, info.st_mtim, info.st_ctim,
      S_ISREG(info.st_mode) ? bytes(path) : std::string{}};
}
bool same(const Stamp& a, const Stamp& b) {
  return a.device == b.device && a.inode == b.inode && a.size == b.size && a.mode == b.mode && a.links == b.links
      && a.modified.tv_sec == b.modified.tv_sec && a.modified.tv_nsec == b.modified.tv_nsec
      && a.changed.tv_sec == b.changed.tv_sec && a.changed.tv_nsec == b.changed.tv_nsec && a.data == b.data;
}
std::string digits(unsigned value, std::size_t width) {
  auto result = std::to_string(value); check(result.size() <= width, "fixture date exceeds width");
  return std::string(width - result.size(), '0') + result;
}
std::filesystem::path relative(core::HistoryDate date) {
  return std::filesystem::path("stats") / digits(static_cast<unsigned>(date.year), 4)
      / digits(date.month, 2) / (digits(date.day, 2) + ".dat");
}
std::string counters(core::HistoryCounters values) {
  std::string result;
  for (const auto value : values) { if (!result.empty()) { result += ','; } result += std::to_string(value); }
  return result;
}
std::string header(core::HistoryDate date) {
  const auto day = std::chrono::sys_days{std::chrono::year{date.year}
      / std::chrono::month{date.month} / std::chrono::day{date.day}};
  const I minute = static_cast<I>(day.time_since_epoch().count()) * 1440;
  // Epoch-day s0 is deliberately inactive in the original reader. s1 yields a
  // genuine observation at00:02; later days use their positive midnight anchor.
  return "s,1," + std::to_string(minute == 0 ? 1 : minute) + ",100,200,300,400,500,600\n";
}
std::string text(core::HistoryDate date, core::HistoryCounters values = {1, 2, 3, 4, 5, 6}) {
  return header(date) + counters(values) + '\n';
}
std::array<core::HistoryDate, 30> dates(core::HistoryDate end = End) {
  auto result = core::savedHistoryMonthDates(end); check(result.has_value(), "invalid test interval"); return *result;
}
struct Area {
  std::filesystem::path root, profile;
  std::vector<std::filesystem::path> files, directories;
  std::optional<storage::ProfileLease> lease;
  Area() {
    char pattern[] = "/tmp/biglybtc-history-month-tests-XXXXXX";
    const char* made = ::mkdtemp(pattern); check(made != nullptr, "cannot create generated root"); root = made;
    try { profile = root / "profile"; directory(profile); files.push_back(profile / ".azlock"); }
    catch (...) { cleanup(); throw; }
  }
  ~Area() { cleanup(); }
  Area(const Area&) = delete; Area& operator=(const Area&) = delete;
  void directory(const std::filesystem::path& path) {
    struct stat info{};
    if (::lstat(path.c_str(), &info) == 0) { check(S_ISDIR(info.st_mode), "fixture parent is not a physical directory"); return; }
    check(errno == ENOENT, "fixture parent inspection failed");
    directories.push_back(path); check(::mkdir(path.c_str(), 0700) == 0, "fixture mkdir failed");
  }
  void write(const std::filesystem::path& path, std::string_view data) {
    files.push_back(path); Fd file(::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    check(file.value >= 0, "exclusive fixture write open failed"); std::size_t offset = 0;
    while (offset < data.size()) {
      const auto count = ::write(file.value, data.data() + offset, data.size() - offset);
      if (count < 0 && errno == EINTR) { continue; }
      check(count > 0, "fixture write failed"); offset += static_cast<std::size_t>(count);
    }
    check(::fsync(file.value) == 0, "fixture fsync failed");
  }
  std::filesystem::path day(core::HistoryDate date) const { return profile / relative(date); }
  void prepare(core::HistoryDate date) {
    directory(profile / "stats"); directory(profile / "stats" / digits(static_cast<unsigned>(date.year), 4));
    directory(day(date).parent_path());
  }
  void writeDay(core::HistoryDate date, std::string_view data) { prepare(date); write(day(date), data); }
  void hold() { auto result = storage::ProfileLease::acquire(profile); check(result.has_value(), "generated profile lease failed"); lease.emplace(std::move(*result)); }
  void populate(core::HistoryDate end = End) { for (const auto date : dates(end)) { writeDay(date, text(date)); } }
private:
  void cleanup() noexcept {
    lease.reset(); for (auto it = files.rbegin(); it != files.rend(); ++it) { (void)::unlink(it->c_str()); }
    for (auto it = directories.rbegin(); it != directories.rend(); ++it) { (void)::rmdir(it->c_str()); }
    if (!root.empty()) { (void)::rmdir(root.c_str()); }
  }
};
struct Identity { dev_t device{}; ino_t inode{}; bool present = false; };
enum class Action { None, Stop, Io, StopAndIo, ReplaceFile };
struct ReadPlan {
  Identity root{};
  std::array<Identity, 30> files{};
  std::array<std::size_t, 64> opened{};
  std::size_t openCount = 0, statsAttempts = 0, lockAttempts = 0;
  bool openOverflow = false;
  std::array<std::size_t, 30> order{};
  std::bitset<30> seen;
  std::size_t count = 0, trigger = 30;
  Action action = Action::None;
  std::stop_source* stop = nullptr;
  const char* changed = nullptr;
  const char* moved = nullptr;
  std::string_view replacement;
  bool fired = false, mutationOk = true;
};
thread_local ReadPlan* activePlan = nullptr;
struct Arm {
  explicit Arm(ReadPlan& plan) { check(activePlan == nullptr, "read hook already active"); activePlan = &plan; }
  ~Arm() { activePlan = nullptr; }
  Arm(const Arm&) = delete; Arm& operator=(const Arm&) = delete;
};
ReadPlan plan(const Area& area, core::HistoryDate end = End) {
  ReadPlan result; const auto requested = dates(end); struct stat root{};
  check(::lstat(area.profile.c_str(), &root) == 0, "cannot identify owned profile"); result.root = {root.st_dev, root.st_ino, true};
  for (std::size_t i = 0; i < requested.size(); ++i) {
    struct stat info{};
    if (::lstat(area.day(requested[i]).c_str(), &info) == 0 && S_ISREG(info.st_mode)) {
      result.files[i] = {info.st_dev, info.st_ino, true};
    }
  }
  return result;
}
void prefix(const ReadPlan& observed, std::size_t count, std::size_t attempts) {
  check(!observed.openOverflow && observed.openCount == count && observed.statsAttempts == attempts,
      "batch opened beyond failure, duplicated/skipped a daily call or did not use the held root");
  check(observed.lockAttempts == 0, "batch attempted to reacquire the held lease");
  for (std::size_t i = 0; i < count; ++i) { check(observed.opened[i] == i, "daily leaf opens were not oldest-first exactly once"); }
}
void error(const std::expected<core::SavedHistoryMonth, storage::Error>& result, storage::ErrorCode code) {
  check(!result && result.error().code == code, "wrong batch error or published partial success");
}
void shape(const core::SavedHistoryMonth& result, core::HistoryDate end = End) {
  const auto wanted = dates(end); check(result.endingDate == end, "ending date changed");
  for (std::size_t i = 0; i < wanted.size(); ++i) {
    check(result.days[i].date == wanted[i], "row dates not chronological/exact");
    if (result.days[i].snapshot) { check(result.days[i].snapshot->date == wanted[i], "inner snapshot date retargeted"); }
  }
}
// Reuse only the original525 retained daily/hour queries, never a new30-day
// source observation or aggregate. Existing fixture bytes remain immutable.
I number(std::string_view field) {
  I value{}; const auto parsed = std::from_chars(field.data(), field.data() + field.size(), value);
  check(parsed.ec == std::errc{} && parsed.ptr == field.data() + field.size(), "invalid frozen oracle number"); return value;
}
struct OracleDay {
  core::HistoryDate date;
  std::bitset<25> seen;
  std::array<core::HistoryCounters, 25> values{};
};
struct OracleCase {
  core::HistoryDate end;
  bool configured = false;
  std::array<OracleDay, 7> days{};
};
void oracle(const std::filesystem::path& fixtures) {
  std::ifstream input(fixtures / "expected.tsv"); check(bool(input), "actual Java expected.tsv is required; no skipped comparison");
  std::map<std::string, OracleCase> definitions;
  std::size_t rows = 0; std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#') { continue; }
    std::vector<std::string_view> fields; std::string_view rest(line);
    for (;;) { const auto tab = rest.find('\t'); fields.push_back(rest.substr(0, tab));
      if (tab == std::string_view::npos) { break; } rest.remove_prefix(tab + 1); }
    check(fields.size() == 15, "oracle TSV must have fifteen fields"); const std::string id(fields[0]);
    check(!id.empty() && id.size() <= 64 && id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos,
        "oracle case ID must be a bounded owned fixture name");
    const auto parsedDate = [&](std::size_t at) {
      const I year = number(fields[at]), month = number(fields[at + 1]), day = number(fields[at + 2]);
      check(year >= 1970 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 && day <= 31, "oracle date out of domain");
      const core::HistoryDate date{static_cast<int>(year), static_cast<unsigned>(month), static_cast<unsigned>(day)};
      check(core::validHistoryDate(date), "oracle calendar date invalid"); return date;
    };
    const auto ending = parsedDate(1), date = parsedDate(5);
    const I index = number(fields[4]), hour = number(fields[8]);
    check(index >= 0 && index < 7 && hour >= -1 && hour <= 23, "oracle day/hour index out of range");
    auto& test = definitions[id];
    if (!test.configured) { test.end = ending; test.configured = true; }
    check(test.end == ending, "oracle case changed its ending date");
    auto& selected = test.days[static_cast<std::size_t>(index)]; const auto slot = static_cast<std::size_t>(hour + 1);
    check(!selected.seen.test(slot), "duplicate original Java query row");
    if (selected.seen.any()) { check(selected.date == date, "oracle member date changed"); }
    selected.date = date; selected.seen.set(slot);
    for (std::size_t field = 0; field < 6; ++field) { selected.values[slot][field] = number(fields[field + 9]); }
    ++rows;
  }
  check(input.eof() && definitions.size() == 3 && rows == 525, "actual frozen Java corpus incomplete");
  for (const auto& [id, test] : definitions) {
    Area area; const auto original = core::savedHistoryWeekDates(test.end); check(original.has_value(), "retained week range invalid"); const auto wanted = *original;
    const auto end = test.end.year == 1970 && test.end.month == 1 && test.end.day < 30 ? End : test.end; const auto full = dates(end);
    std::array<bool, 7> present{}; std::array<std::optional<Stamp>, 7> originals{};
    for (std::size_t i = 0; i < test.days.size(); ++i) {
      check(test.days[i].seen.all() && test.days[i].date == wanted[i], "oracle did not cover exactly seven chronological daily/hour slots");
      const auto path = fixtures / "cases" / id / relative(test.days[i].date); struct stat info{};
      if (::lstat(path.c_str(), &info) == 0) {
        check(S_ISREG(info.st_mode), "oracle input must be a physical regular file");
        originals[i] = stamp(path); present[i] = true; area.writeDay(test.days[i].date, originals[i]->data);
      } else { check(errno == ENOENT, "cannot inspect exact oracle member path"); }
    }
    area.hold(); auto result = storage::readSavedHistoryMonthView(*area.lease, end);
    check(result.has_value(), "valid actual Java corpus failed native daily batch"); shape(*result, end);
    for (std::size_t i = 0; i < full.size(); ++i) {
      if (std::find(wanted.begin(), wanted.end(), full[i]) == wanted.end()) { check(!result->days[i].snapshot, "unconfigured extra date invented a snapshot"); }
    }
    for (std::size_t i = 0; i < test.days.size(); ++i) {
      const auto match = std::find(full.begin(), full.end(), test.days[i].date); check(match != full.end(), "retained member outside30-date result");
      const auto slotIndex = static_cast<std::size_t>(match - full.begin()); const auto& actual = result->days[slotIndex].snapshot;
      check(actual.has_value() == present[i], "source file presence changed by batch");
      for (std::size_t slot = 0; slot < 25; ++slot) {
        const auto values = !actual ? std::optional<core::HistoryCounters>{}
            : slot == 0 ? actual->totals : actual->hours[slot - 1];
        for (std::size_t component = 0; component < 6; ++component) {
          // Only this scalar comparison maps native unknown to Java's zero.
          // It grants no presence/count/malformed/whole-period equality credit.
          check((values ? (*values)[component] : 0) == test.days[i].values[slot][component], "retained original independent-day scalar regression differs");
          ++retainedScalars;
        }
        ++retainedQueries;
      }
      if (present[i]) {
        check(same(*originals[i], stamp(fixtures / "cases" / id / relative(test.days[i].date))), "oracle fixture modified");
        check(bytes(area.day(test.days[i].date)) == originals[i]->data, "generated profile input modified");
      }
    }
  }
}
} // namespace

// Observe real libc calls, not replacement history/provider decisions. Empty
// files are included by leaf opens even though they have no positive read().
extern "C" int __real_openat(int directory, const char* name, int flags, ...);
extern "C" int __wrap_openat(int directory, const char* name, int flags, ...) {
  bool hasMode = (flags & O_CREAT) != 0;
#ifdef O_TMPFILE
  hasMode = hasMode || (flags & O_TMPFILE) == O_TMPFILE;
#endif
  mode_t mode = 0;
  if (hasMode) { va_list args; va_start(args, flags); mode = va_arg(args, mode_t); va_end(args); }
  const int result = hasMode ? __real_openat(directory, name, flags, mode) : __real_openat(directory, name, flags);
  const int originalError = errno;
  auto* current = activePlan; if (current == nullptr || name == nullptr) { errno = originalError; return result; }
  struct stat parent{};
  if (::fstat(directory, &parent) == 0 && parent.st_dev == current->root.device && parent.st_ino == current->root.inode) {
    if (std::string_view(name) == "stats") { ++current->statsAttempts; }
    if (std::string_view(name) == ".azlock") { ++current->lockAttempts; }
  }
  struct stat file{};
  if (result >= 0 && ::fstat(result, &file) == 0 && S_ISREG(file.st_mode)) {
    for (std::size_t i = 0; i < current->files.size(); ++i) {
      if (current->files[i].present && file.st_dev == current->files[i].device && file.st_ino == current->files[i].inode) {
        if (current->openCount < current->opened.size()) { current->opened[current->openCount++] = i; }
        else { current->openOverflow = true; }
        break;
      }
    }
  }
  errno = originalError; return result;
}

extern "C" ssize_t __real_read(int descriptor, void* buffer, size_t count);
extern "C" ssize_t __wrap_read(int descriptor, void* buffer, size_t count) {
  const auto result = __real_read(descriptor, buffer, count); auto* current = activePlan;
  if (current == nullptr || result <= 0) { return result; }
  struct stat info{}; if (::fstat(descriptor, &info) != 0) { return result; }
  for (std::size_t i = 0; i < current->files.size(); ++i) {
    const auto& target = current->files[i];
    if (!target.present || current->seen.test(i) || info.st_dev != target.device || info.st_ino != target.inode) { continue; }
    current->seen.set(i); current->order[current->count++] = i;
    if (i != current->trigger) { break; }
    current->fired = true;
    if (current->stop != nullptr) { current->stop->request_stop(); }
    if (current->action == Action::ReplaceFile) {
      current->mutationOk = ::rename(current->changed, current->moved) == 0;
      if (current->mutationOk) {
        const int fd = ::open(current->changed, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        current->mutationOk = fd >= 0; std::size_t offset = 0;
        while (fd >= 0 && offset < current->replacement.size()) {
          const auto written = ::write(fd, current->replacement.data() + offset, current->replacement.size() - offset);
          if (written < 0 && errno == EINTR) { continue; }
          if (written <= 0) { current->mutationOk = false; break; }
          offset += static_cast<std::size_t>(written);
        }
        if (fd >= 0) { (void)::close(fd); }
      }
    }
    if (current->action == Action::Io || current->action == Action::StopAndIo) { errno = EIO; return -1; }
    break;
  }
  return result;
}

int main(int argc, char** argv) {
  std::string current;
  try {
    const auto run = [&](std::string name, auto body) { current = std::move(name); body(); ++cases; };
    run("missing-components-make-thirty-attempts-no-files-no-extra-lease", [] {
      for (int component = 0; component < 4; ++component) {
        Area area; area.hold();
        if (component > 0) { area.directory(area.profile / "stats"); }
        if (component > 1) { area.directory(area.profile / "stats/1970"); }
        if (component > 2) { area.directory(area.profile / "stats/1970/01"); }
        const auto before = stamp(area.profile); const int held = area.lease->directoryHandle(); auto observed = plan(area);
        { Arm arm(observed); const auto result = storage::readSavedHistoryMonthView(*area.lease, End);
          check(result.has_value(), "missing dates should yield a successful fixed batch"); shape(*result);
          for (const auto& row : result->days) { check(!row.snapshot, "missing day fabricated a snapshot"); } }
        prefix(observed, 0, 30);
        check(same(before, stamp(area.profile)) && area.lease->directoryHandle() == held && ::fcntl(held, F_GETFD) >= 0,
            "batch created paths, closed/reacquired caller lease or changed profile metadata");
      }
    });
    run("thirty-oldest-first-real-opens-owned-data-and-byte-metadata-preservation", [] {
      core::SavedHistoryMonth owned;
      {
        Area area; const auto wanted = dates(); std::array<Stamp, 30> before{};
        for (std::size_t i = 0; i < wanted.size(); ++i) {
          const I v = static_cast<I>(i + 1); area.writeDay(wanted[i], text(wanted[i], {v, -v, v * 2, -v * 2, v * 3, -v * 3})); before[i] = stamp(area.day(wanted[i]));
        }
        area.hold(); auto observed = plan(area); const int held = area.lease->directoryHandle();
        { Arm arm(observed); auto result = storage::readSavedHistoryMonthView(*area.lease, End); check(result.has_value(), "thirty files failed"); owned = std::move(*result); }
        prefix(observed, 30, 30); shape(owned);
        for (std::size_t i = 0; i < wanted.size(); ++i) { check(same(before[i], stamp(area.day(wanted[i]))), "input bytes/device/inode/mode/mtime/ctime changed"); }
        check(area.lease->directoryHandle() == held && ::fcntl(held, F_GETFD) >= 0, "borrowed lease consumed");
      }
      auto copy = owned; auto moved = std::move(owned);
      for (std::size_t i = 0; i < 30; ++i) {
        const I v = static_cast<I>(i + 1); const core::HistoryCounters expected{v, -v, v * 2, -v * 2, v * 3, -v * 3};
        check(copy.days[i].snapshot && moved.days[i].snapshot && copy.days[i].snapshot->totals == expected
            && copy.days[i].snapshot->hours[0] == expected && moved.days[i].snapshot->totals == expected,
            "six counters/hours depended on lease, files or moved result lifetime");
      }
    });
    run("missing-empty-header-zero-signed-and-extreme-rows-remain-distinct", [] {
      Area area; const auto wanted = dates(); area.writeDay(wanted[1], ""); area.writeDay(wanted[2], header(wanted[2]));
      area.writeDay(wanted[3], text(wanted[3], {})); area.writeDay(wanted[4], text(wanted[4], {-1, -2, -3, -4, -5, -6}));
      const I low = std::numeric_limits<I>::min(), high = std::numeric_limits<I>::max(); const core::HistoryCounters extreme{low, high, low, high, low, high};
      area.writeDay(wanted[5], text(wanted[5], extreme)); const auto parent = area.day(wanted[6]).parent_path();
      const auto cache = parent / "cache.dat"; area.write(cache, "unrelated malformed cache");
      area.write(area.day(wanted[6]).string() + ".bak", text(wanted[6])); area.write(area.day(wanted[6]).string() + ".saving", text(wanted[6]));
      const auto neighbor = area.day({1970, 1, 31}); area.write(neighbor, "malformed outside interval");
      const auto cacheBefore = stamp(cache), neighborBefore = stamp(neighbor); area.hold(); auto observed = plan(area);
      { Arm arm(observed); const auto result = storage::readSavedHistoryMonthView(*area.lease, End); check(result.has_value(), "mixed batch rejected"); shape(*result);
        check(!result->days[0].snapshot && !result->days[6].snapshot && !result->days[29].snapshot, "missing row recovered/invented data");
        for (const auto i : {1U, 2U}) { check(result->days[i].snapshot && !result->days[i].snapshot->totals && result->days[i].snapshot->observations == 0, "present no-observation row became missing/zero"); }
        check(result->days[3].snapshot->totals == core::HistoryCounters{} && result->days[3].snapshot->observations == 1, "recorded zero became unknown");
        check(result->days[4].snapshot->totals == core::HistoryCounters{-1, -2, -3, -4, -5, -6}, "signed adjustment changed");
        check(result->days[5].snapshot->totals == extreme && result->days[5].snapshot->hours[0] == extreme, "signed extremes narrowed/summed with baseline"); }
      check(!observed.openOverflow && observed.openCount == 5 && observed.statsAttempts == 30 && observed.lockAttempts == 0, "mixed batch daily call count differs");
      for (std::size_t i = 0; i < 5; ++i) { check(observed.opened[i] == i + 1, "empty/present member skipped or order changed"); }
      check(same(cacheBefore, stamp(cache)) && same(neighborBefore, stamp(neighbor)), "unrequested files changed");
    });
    run("exact-paths-across-year-leap-and-upper-domain", [] {
      for (const auto end : {core::HistoryDate{2024, 1, 10}, core::HistoryDate{2024, 3, 1}, core::HistoryDate{9999, 12, 31}}) {
        Area area; area.populate(end); area.hold(); auto observed = plan(area, end);
        { Arm arm(observed); const auto result = storage::readSavedHistoryMonthView(*area.lease, end); check(result.has_value(), "boundary tree rejected"); shape(*result, end);
          for (const auto& row : result->days) { check(row.snapshot && row.snapshot->totals == core::HistoryCounters{1, 2, 3, 4, 5, 6}, "boundary file skipped/retargeted"); } }
        prefix(observed, 30, 30);
      }
    });
    run("invalid-range-moved-lease-and-prestop-do-no-daily-read", [] {
      Area area; area.populate(); area.hold();
      for (const auto end : {core::HistoryDate{1970, 1, 29}, core::HistoryDate{1969, 12, 31}, core::HistoryDate{2024, 2, 30}, core::HistoryDate{10000, 1, 1}, core::HistoryDate{2024, 257, 1}}) {
        auto observed = plan(area); { Arm arm(observed); error(storage::readSavedHistoryMonthView(*area.lease, end), storage::ErrorCode::InvalidFormat); } prefix(observed, 0, 0);
      }
      auto live = std::move(*area.lease); auto invalidLease = plan(area);
      { Arm arm(invalidLease); error(storage::readSavedHistoryMonthView(*area.lease, End), storage::ErrorCode::UnsafePath); } prefix(invalidLease, 0, 0);
      std::stop_source stop; stop.request_stop(); auto observed = plan(area);
      { Arm arm(observed); error(storage::readSavedHistoryMonthView(*area.lease, {0, 0, 0}, stop.get_token()), storage::ErrorCode::Cancelled);
        error(storage::readSavedHistoryMonthView(live, End, stop.get_token()), storage::ErrorCode::Cancelled); } prefix(observed, 0, 0);
    });
    run("unsafe-first-middle-last-leaves-stop-before-later-days", [] {
      for (const std::size_t index : {0U, 14U, 29U}) { for (int kind = 0; kind < 3; ++kind) {
        Area area; area.populate(); const auto wanted = dates(); const auto changed = area.day(wanted[index]); check(::unlink(changed.c_str()) == 0, "cannot replace owned leaf");
        const auto target = area.root / "valid-sentinel"; area.write(target, text(wanted[index])); const auto before = stamp(target);
        if (kind == 0) { check(::symlink(target.c_str(), changed.c_str()) == 0, "symlink fixture failed"); }
        if (kind == 1) { check(::mkfifo(changed.c_str(), 0600) == 0, "FIFO fixture failed"); }
        if (kind == 2) { area.directory(changed); }
        area.hold(); auto observed = plan(area);
        { Arm arm(observed); error(storage::readSavedHistoryMonthView(*area.lease, End), storage::ErrorCode::UnsafePath); }
        prefix(observed, index, index + 1); check(same(before, stamp(target)), "unsafe target was changed");
      } }
    });
    run("unsafe-directory-components-never-follow-or-block", [] {
      for (std::size_t level = 0; level < 3; ++level) { for (int kind = 0; kind < 3; ++kind) {
        Area area; const std::array<std::filesystem::path, 3> components{area.profile / "stats", area.profile / "stats/1970", area.profile / "stats/1970/01"};
        for (std::size_t i = 0; i < level; ++i) { area.directory(components[i]); }
        const auto& bad = components[level];
        if (kind == 0) {
          const auto redirect = area.root / "redirect"; area.directory(redirect); auto child = redirect;
          for (std::size_t i = level + 1; i < components.size(); ++i) { child /= components[i].filename(); area.directory(child); }
          for (const auto date : dates()) { area.write(child / area.day(date).filename(), text(date)); }
          area.files.push_back(bad); check(::symlink(redirect.c_str(), bad.c_str()) == 0, "directory symlink fixture failed");
        } else if (kind == 1) { area.write(bad, "regular file instead of directory"); }
        else { area.files.push_back(bad); check(::mkfifo(bad.c_str(), 0600) == 0, "directory FIFO fixture failed"); }
        area.hold(); auto observed = plan(area);
        { Arm arm(observed); error(storage::readSavedHistoryMonthView(*area.lease, End), storage::ErrorCode::UnsafePath); } prefix(observed, 0, 1);
      } }
    });
    run("first-middle-last-parser-version-overflow-and-limits-no-partial", [] {
      for (const std::size_t index : {0U, 14U, 29U}) { for (int kind = 0; kind < 6; ++kind) {
        Area area; const auto wanted = dates(); std::string invalid;
        if (kind == 0) { invalid = "malformed"; }
        if (kind == 1) { invalid = "s,2,4320,0,0,0,0,0,0\n"; }
        if (kind == 2) { invalid = header(wanted[index]) + "9223372036854775808,0,0,0,0,0\n"; }
        if (kind == 3) { invalid.assign(core::SavedHistoryMaximumBytes + 1, '\n'); }
        if (kind == 4) { invalid.assign(core::SavedHistoryMaximumLineBytes + 1, '0'); }
        if (kind == 5) { for (std::size_t i = 0; i <= core::SavedHistoryMaximumRecords; ++i) { invalid += "0,0,0,0,0,0\n"; } }
        for (std::size_t i = 0; i < 30; ++i) { area.writeDay(wanted[i], i == index ? invalid : text(wanted[i])); }
        const auto before = stamp(area.day(wanted[index])); area.hold(); auto observed = plan(area);
        { Arm arm(observed); error(storage::readSavedHistoryMonthView(*area.lease, End), kind == 1 ? storage::ErrorCode::Unsupported : storage::ErrorCode::InvalidFormat); }
        prefix(observed, index + 1, index + 1); check(same(before, stamp(area.day(wanted[index]))), "invalid file rewritten");
        check(observed.seen.test(index) == (kind != 3), "oversized input was read before rejection or parser fault never saw real bytes");
      } }
    });
    run("stop-after-first-middle-last-real-byte-read-never-publishes-prefix", [] {
      for (const std::size_t index : {0U, 14U, 29U}) {
        Area area; area.populate(); area.hold(); auto observed = plan(area); std::stop_source stop;
        observed.trigger = index; observed.action = Action::Stop; observed.stop = &stop;
        { Arm arm(observed); error(storage::readSavedHistoryMonthView(*area.lease, End, stop.get_token()), storage::ErrorCode::Cancelled); }
        check(observed.fired && stop.stop_requested(), "stop did not follow actual matching input bytes"); prefix(observed, index + 1, index + 1);
      }
    });
    run("first-middle-last-real-EIO-and-cancellation-precedence", [] {
      for (const std::size_t index : {0U, 14U, 29U}) { for (const auto action : {Action::Io, Action::StopAndIo}) {
        Area area; area.populate(); area.hold(); auto observed = plan(area); const auto before = stamp(area.day(dates()[index])); std::stop_source stop;
        observed.trigger = index; observed.action = action; observed.stop = action == Action::Io ? nullptr : &stop;
        { Arm arm(observed); error(storage::readSavedHistoryMonthView(*area.lease, End, stop.get_token()), action == Action::Io ? storage::ErrorCode::Io : storage::ErrorCode::Cancelled); }
        check(observed.fired && same(before, stamp(area.day(dates()[index]))), "fault did not match real bytes or changed input"); prefix(observed, index + 1, index + 1);
      } }
    });
    run("same-byte-first-middle-last-replacement-during-read-conflicts", [] {
      for (const std::size_t index : {0U, 14U, 29U}) {
        Area area; area.populate(); area.hold(); const auto changed = area.day(dates()[index]).string(), moved = changed + ".owned-moved"; area.files.push_back(moved);
        const auto replacement = text(dates()[index]); auto observed = plan(area); observed.trigger = index; observed.action = Action::ReplaceFile;
        observed.changed = changed.c_str(); observed.moved = moved.c_str(); observed.replacement = replacement;
        { Arm arm(observed); error(storage::readSavedHistoryMonthView(*area.lease, End), storage::ErrorCode::Conflict); }
        check(observed.fired && observed.mutationOk && bytes(changed) == bytes(moved), "owned identity swap fixture failed"); prefix(observed, index + 1, index + 1);
      }
    });
    run("completed-earlier-snapshot-owned-not-a-multi-file-transaction", [] {
      Area area; area.populate(); area.hold(); const auto changed = area.day(dates()[0]).string(), moved = changed + ".owned-moved"; area.files.push_back(moved);
      const auto replacement = text(dates()[0], {9, 8, 7, 6, 5, 4}); auto observed = plan(area); observed.trigger = 14; observed.action = Action::ReplaceFile;
      observed.changed = changed.c_str(); observed.moved = moved.c_str(); observed.replacement = replacement;
      { Arm arm(observed); const auto result = storage::readSavedHistoryMonthView(*area.lease, End);
        check(result && result->days[0].snapshot && result->days[0].snapshot->totals == core::HistoryCounters{1, 2, 3, 4, 5, 6}, "completed snapshot aliased later file state"); }
      check(observed.fired && observed.mutationOk && bytes(changed) == replacement, "later-read mutation fixture failed"); prefix(observed, 30, 30);
    });
    run("exact-inherited-one-MiB-input-is-present-unknown", [] {
      static_assert(core::SavedHistoryMaximumBytes == 1024 * 1024);
      Area area; const auto wanted = dates();
      for (std::size_t i = 0; i < 30; ++i) { area.writeDay(wanted[i], i == 14 ? std::string(core::SavedHistoryMaximumBytes, '\n') : text(wanted[i])); }
      area.hold(); auto observed = plan(area);
      { Arm arm(observed); const auto result = storage::readSavedHistoryMonthView(*area.lease, End);
        check(result && result->days[14].snapshot && !result->days[14].snapshot->totals && result->days[14].snapshot->observations == 0, "inherited exact input limit changed"); }
      prefix(observed, 30, 30); check(observed.seen.test(14), "exact-size file was not really read");
    });
    run("reuse-frozen-daily-hour-scalars-only-no-new-thirty-day-equality", [&] {
      check(argc == 2, "pass existing immutable history-week fixture directory as argv[1]"); oracle(argv[1]);
      check(retainedQueries == 525 && retainedScalars == 3150, "retained existing query/scalar regression corpus differs");
    });
    std::cout << "PASS " << cases << " saved-history-month reader families; " << assertions
              << " assertions; held generated profiles only; no aggregate or Java/runtime equality credit\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL " << current << ": " << e.what() << '\n'; return 1; }
}
