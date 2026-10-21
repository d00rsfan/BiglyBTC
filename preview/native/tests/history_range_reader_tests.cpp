#include "history_range_view.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc;
using I = std::int64_t;
std::size_t assertions = 0, cases = 0;
void check(bool value, std::string_view message) {
  ++assertions; if (!value) { throw std::runtime_error(std::string(message)); }
}
struct Fd {
  int value;
  explicit Fd(int fd) : value(fd) {}
  ~Fd() { if (value >= 0) { (void)::close(value); } }
  Fd(const Fd&) = delete; Fd& operator=(const Fd&) = delete;
};
std::string bytes(const std::filesystem::path& path) {
  Fd fd(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)); check(fd.value >= 0, "fixture read open");
  std::string result; std::array<char, 4096> block{};
  for (;;) {
    const auto count = ::read(fd.value, block.data(), block.size());
    if (count < 0 && errno == EINTR) { continue; }
    check(count >= 0, "fixture read"); if (!count) { return result; }
    result.append(block.data(), static_cast<std::size_t>(count));
  }
}
struct Stamp { struct stat info; std::string data; };
Stamp stamp(const std::filesystem::path& path) {
  struct stat info{}; check(::lstat(path.c_str(), &info) == 0, "fixture stamp");
  return {info, S_ISREG(info.st_mode) ? bytes(path) : std::string{}};
}
bool same(const Stamp& a, const Stamp& b) {
  return a.info.st_dev == b.info.st_dev && a.info.st_ino == b.info.st_ino
      && a.info.st_size == b.info.st_size && a.info.st_mode == b.info.st_mode && a.info.st_nlink == b.info.st_nlink
      && a.info.st_mtim.tv_sec == b.info.st_mtim.tv_sec && a.info.st_mtim.tv_nsec == b.info.st_mtim.tv_nsec
      && a.info.st_ctim.tv_sec == b.info.st_ctim.tv_sec && a.info.st_ctim.tv_nsec == b.info.st_ctim.tv_nsec && a.data == b.data;
}
struct Area {
  std::filesystem::path root, profile;
  std::vector<std::filesystem::path> files, directories;
  std::optional<storage::ProfileLease> lease;
  Area() {
    char pattern[] = "/tmp/biglybtc-history-range-tests-XXXXXX";
    const char* made = ::mkdtemp(pattern); check(made != nullptr, "generated root"); root = made;
    try { profile = root / "profile"; directory(profile); files.push_back(profile / ".azlock"); }
    catch (...) { cleanup(); throw; }
  }
  ~Area() { cleanup(); }
  Area(const Area&) = delete; Area& operator=(const Area&) = delete;
  void directory(const std::filesystem::path& path) {
    struct stat info{};
    if (::lstat(path.c_str(), &info) == 0) { check(S_ISDIR(info.st_mode), "physical fixture directory"); return; }
    check(errno == ENOENT, "fixture parent inspection"); directories.push_back(path); check(::mkdir(path.c_str(), 0700) == 0, "fixture mkdir");
  }
  void write(const std::filesystem::path& path, std::string_view data) {
    files.push_back(path); Fd fd(::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    check(fd.value >= 0, "exclusive fixture write"); std::size_t offset = 0;
    while (offset < data.size()) {
      const auto count = ::write(fd.value, data.data() + offset, data.size() - offset);
      if (count < 0 && errno == EINTR) { continue; }
      check(count > 0, "fixture write"); offset += static_cast<std::size_t>(count);
    }
  }
  void link(const std::filesystem::path& path, const std::filesystem::path& target) {
    files.push_back(path); check(::symlink(target.c_str(), path.c_str()) == 0, "fixture symlink");
  }
  void tree() { directory(profile / "stats"); directory(profile / "stats/1970"); directory(month()); }
  void hold() { auto value = storage::ProfileLease::acquire(profile); check(value.has_value(), "generated lease"); lease.emplace(std::move(*value)); }
  std::filesystem::path month() const { return profile / "stats/1970/01"; }
  std::filesystem::path day() const { return month() / "02.dat"; }
private:
  void cleanup() noexcept {
    lease.reset();
    for (auto it = files.rbegin(); it != files.rend(); ++it) { (void)::unlink(it->c_str()); }
    for (auto it = directories.rbegin(); it != directories.rend(); ++it) { (void)::rmdir(it->c_str()); }
    if (!root.empty()) { (void)::rmdir(root.c_str()); }
  }
};
constexpr I Day = 86400000;
constexpr core::HistoryRangeRequest Hour{core::HistoryPeriod::Hour, 0, Day + 3600000};
constexpr std::string_view Valid = "s,1,1440,100,200,300,400,500,600\n1,2,3,4,5,6\n";
using Result = std::expected<core::SavedHistoryRange, storage::Error>;
void error(const Result& value, storage::ErrorCode expected) {
  check(!value && value.error().code == expected, "range error mismatch");
}
enum class ReadAction { Stop, Io, StopIo, Replace, Truncate, ReplaceMonth };
struct ReadArm {
  dev_t device; ino_t inode; ReadAction action; std::stop_source* stop;
  const char* path = nullptr; const char* moved = nullptr;
  bool fired = false, mutationOk = true;
};
thread_local ReadArm* activeRead = nullptr;
struct Arm {
  explicit Arm(ReadArm& value) { check(!activeRead, "read arm occupied"); activeRead = &value; }
  ~Arm() { activeRead = nullptr; }
  Arm(const Arm&) = delete; Arm& operator=(const Arm&) = delete;
};
enum class ScanAction { Flood, Stop, Io, Replace };
struct ScanArm {
  dev_t device; ino_t inode; ScanAction action; std::size_t repeats = 0;
  std::stop_source* stop = nullptr; const char* path = nullptr; const char* moved = nullptr;
  bool fired = false, mutationOk = true;
  struct dirent fake{};
};
thread_local ScanArm* activeScan = nullptr;
struct ScanScope {
  explicit ScanScope(ScanArm& value) { check(!activeScan, "scan arm occupied"); activeScan = &value; }
  ~ScanScope() { activeScan = nullptr; }
  ScanScope(const ScanScope&) = delete; ScanScope& operator=(const ScanScope&) = delete;
};
} // namespace
extern "C" ssize_t __real_read(int, void*, size_t);
extern "C" ssize_t __wrap_read(int fd, void* buffer, size_t count) {
  const auto value = __real_read(fd, buffer, count); auto* arm = activeRead; struct stat info{};
  if (!arm || value <= 0 || ::fstat(fd, &info) != 0 || info.st_dev != arm->device || info.st_ino != arm->inode) { return value; }
  activeRead = nullptr; arm->fired = true;
  if (arm->stop) { arm->stop->request_stop(); }
  if (arm->action == ReadAction::Replace) {
    arm->mutationOk = ::rename(arm->path, arm->moved) == 0;
    if (arm->mutationOk) {
      const int out = ::open(arm->path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
      arm->mutationOk = out >= 0;
      if (out >= 0) {
        const auto written = ::write(out, Valid.data(), Valid.size());
        arm->mutationOk = written >= 0 && static_cast<std::size_t>(written) == Valid.size(); (void)::close(out);
      }
    }
  } else if (arm->action == ReadAction::Truncate) {
    const int out = ::open(arm->path, O_WRONLY | O_NOFOLLOW | O_CLOEXEC); arm->mutationOk = out >= 0;
    if (out >= 0) { arm->mutationOk = ::ftruncate(out, 0) == 0; (void)::close(out); }
  } else if (arm->action == ReadAction::ReplaceMonth) {
    arm->mutationOk = ::rename(arm->path, arm->moved) == 0 && ::mkdir(arm->path, 0700) == 0;
  }
  if (arm->action == ReadAction::Io || arm->action == ReadAction::StopIo) { errno = EIO; return -1; }
  return value;
}
extern "C" struct dirent* __real_readdir(DIR*);
extern "C" struct dirent* __wrap_readdir(DIR* directory) {
  auto* arm = activeScan; struct stat info{};
  if (!arm || ::fstat(::dirfd(directory), &info) != 0 || info.st_dev != arm->device || info.st_ino != arm->inode) {
    return __real_readdir(directory);
  }
  if (arm->action == ScanAction::Flood && arm->repeats > 0) {
    --arm->repeats; arm->fired = true; std::memcpy(arm->fake.d_name, "junk", 5); return &arm->fake;
  }
  const auto result = __real_readdir(directory);
  if (!arm->fired) {
    arm->fired = true;
    if (arm->stop) { arm->stop->request_stop(); }
    if (arm->action == ScanAction::Io) { errno = EIO; return nullptr; }
    if (arm->action == ScanAction::Replace) {
      arm->mutationOk = ::rename(arm->path, arm->moved) == 0 && ::mkdir(arm->path, 0700) == 0;
    }
  }
  return result;
}

int main() {
  std::string current;
  try {
    const auto run = [&](std::string name, auto body) { current = std::move(name); body(); ++cases; };
    run("missing-paths-remain-unknown-with-no-side-effects", [] {
      for (int depth = 0; depth < 4; ++depth) {
        Area area; if (depth > 0) { area.directory(area.profile / "stats"); }
        if (depth > 1) { area.directory(area.profile / "stats/1970"); }
        if (depth > 2) { area.directory(area.month()); } area.hold(); const auto before = stamp(area.profile);
        const auto value = storage::readSavedHistoryRangeView(*area.lease, Hour);
        check(value && value->presentFiles == 0 && value->missingFiles == 1 && !value->totals, "missing coverage");
        check(value->buckets.size() == 60, "hour minute resolution");
        for (const auto& bucket : value->buckets) { check(!bucket.totals && bucket.observations == 0, "missing bucket invented zero"); }
        check(same(before, stamp(area.profile)), "missing read wrote profile");
      }
    });
    run("present-owning-values-and-no-unrelated-reads-or-writes", [] {
      Area area; area.tree(); area.write(area.day(), Valid); area.write(area.month() / "cache.dat", "garbage");
      area.write(area.month() / "03.dat", "garbage"); area.hold();
      const auto before = stamp(area.day()), cache = stamp(area.month() / "cache.dat"), neighbor = stamp(area.month() / "03.dat");
      auto value = storage::readSavedHistoryRangeView(*area.lease, Hour);
      check(value && value->totals == core::HistoryCounters{1,2,3,4,5,6} && value->observations == 1, "six counters mapping");
      check(value->buckets[1].totals == value->totals && value->presentFiles == 1 && value->missingFiles == 0, "minute placement");
      check(same(before, stamp(area.day())) && same(cache, stamp(area.month() / "cache.dat")) && same(neighbor, stamp(area.month() / "03.dat")), "query altered unrelated bytes");
      auto copy = *value; auto moved = std::move(*value); area.lease.reset();
      check(copy.totals == moved.totals && copy.buckets[1].totals == moved.totals, "result depended on lease/input");
    });
    run("present-empty-header-and-observed-zero-remain-distinct", [] {
      for (const auto data : {std::string_view{}, std::string_view{"s,1,1440,1,2,3,4,5,6\n"},
          std::string_view{"s,1,1440,0,0,0,0,0,0\n0,0,0,0,0,0\n"}}) {
        Area area; area.tree(); area.write(area.day(), data); area.hold();
        const auto value = storage::readSavedHistoryRangeView(*area.lease, Hour);
        check(value && value->presentFiles == 1 && value->missingFiles == 0, "present became missing");
        if (data.ends_with("\n0,0,0,0,0,0\n")) {
          check(value->totals == core::HistoryCounters{} && value->observations == 1, "observed zero lost");
        } else { check(!value->totals && value->observations == 0, "unobserved invented zero"); }
      }
    });
    run("inclusive-whole-end-is-not-summed-from-buckets", [] {
      Area area; area.tree(); area.write(area.day(), "s,1,1499,0,0,0,0,0,0\n1,-2,3,-4,5,-6\n"); area.hold();
      const auto value = storage::readSavedHistoryRangeView(*area.lease, Hour);
      check(value && value->totals == core::HistoryCounters{1,-2,3,-4,5,-6} && value->observations == 1, "inclusive endpoint omitted");
      for (const auto& bucket : value->buckets) { check(!bucket.totals, "exclusive bucket included endpoint"); }
    });
    run("filename-bucket-membership-remains-independent", [] {
      Area area; area.tree(); area.write(area.month() / "03.dat", Valid); area.hold();
      const auto value = storage::readSavedHistoryRangeView(*area.lease, {core::HistoryPeriod::Day, 0, 2 * Day});
      check(value && value->totals == core::HistoryCounters{1,2,3,4,5,6}, "whole query ignored enumerated spill record");
      for (const auto& bucket : value->buckets) { check(!bucket.totals, "filename spill crossed per-bucket date membership"); }
    });
    run("invalid-request-and-moved-lease-reject-before-path-use", [] {
      Area area; area.hold();
      for (const auto query : {core::HistoryRangeRequest{core::HistoryPeriod::Hour, 1, Day},
          core::HistoryRangeRequest{static_cast<core::HistoryPeriod>(99), 0, Day},
          core::HistoryRangeRequest{core::HistoryPeriod::Hour, 0, -1},
          core::HistoryRangeRequest{core::HistoryPeriod::All, -1, Day},
          core::HistoryRangeRequest{core::HistoryPeriod::Year, 0, Day}}) {
        error(storage::readSavedHistoryRangeView(*area.lease, query), storage::ErrorCode::InvalidFormat);
      }
      auto owner = std::move(*area.lease); error(storage::readSavedHistoryRangeView(*area.lease, Hour), storage::ErrorCode::UnsafePath);
      check(storage::readSavedHistoryRangeView(owner, Hour).has_value(), "owned moved lease failed");
    });
    run("unsafe-selected-paths-never-followed-or-blocked", [] {
      for (int level = 0; level < 4; ++level) {
        for (int kind = 0; kind < 3; ++kind) {
          Area area; area.write(area.root / "sentinel", Valid); const auto sentinel = stamp(area.root / "sentinel");
          const std::array<std::filesystem::path, 4> paths{area.profile / "stats", area.profile / "stats/1970", area.month(), area.day()};
          for (int parent = 0; parent < level; ++parent) { area.directory(paths[static_cast<std::size_t>(parent)]); }
          const auto& path = paths[static_cast<std::size_t>(level)];
          if (kind == 0) { area.link(path, area.root / "sentinel"); }
          else if (kind == 1 && level == 3) { area.directory(path); }
          else if (kind == 1) { area.write(path, ""); }
          else { area.files.push_back(path); check(::mkfifo(path.c_str(), 0600) == 0, "fixture FIFO"); }
          area.hold(); error(storage::readSavedHistoryRangeView(*area.lease, Hour), storage::ErrorCode::UnsafePath);
          check(same(sentinel, stamp(area.root / "sentinel")), "followed unsafe target");
        }
      }
    });
    run("strict-record-version-and-size-errors-return-no-prefix", [] {
      for (const auto& [data, expected] : std::array<std::pair<std::string, storage::ErrorCode>, 6>{{
          {"garbage", storage::ErrorCode::InvalidFormat}, {"s,2,1440,0,0,0,0,0,0", storage::ErrorCode::Unsupported},
          {"s,1,1440,0,0,0,0,0,0\n9223372036854775808,0,0,0,0,0", storage::ErrorCode::InvalidFormat},
          {"s,1,1440,0,0,0,0,0,0\n9223372036854775807,0,0,0,0,0\n1,0,0,0,0,0\n", storage::ErrorCode::InvalidFormat},
          {std::string(513, 'x'), storage::ErrorCode::InvalidFormat},
          {std::string(core::SavedHistoryMaximumBytes + 1, '\n'), storage::ErrorCode::InvalidFormat}}}) {
        Area area; area.tree(); area.write(area.day(), data); area.hold(); const auto before = stamp(area.day());
        error(storage::readSavedHistoryRangeView(*area.lease, Hour), expected); check(same(before, stamp(area.day())), "invalid data rewritten");
      }
      Area area; area.tree(); area.write(area.day(), Valid); area.write(area.month() / "03.dat", "bad"); area.hold();
      error(storage::readSavedHistoryRangeView(*area.lease, {core::HistoryPeriod::Day, 0, 2 * Day}), storage::ErrorCode::InvalidFormat);
    });
    run("cancellation-and-io-after-genuine-selected-read", [] {
      for (const auto action : {ReadAction::Stop, ReadAction::Io, ReadAction::StopIo}) {
        Area area; area.tree(); area.write(area.day(), Valid); area.hold(); const auto before = stamp(area.day());
        std::stop_source stop; ReadArm state{before.info.st_dev, before.info.st_ino, action, action == ReadAction::Io ? nullptr : &stop};
        { Arm arm(state); error(storage::readSavedHistoryRangeView(*area.lease, Hour, stop.get_token()),
            action == ReadAction::Io ? storage::ErrorCode::Io : storage::ErrorCode::Cancelled); }
        check(state.fired && same(before, stamp(area.day())), "real read gate absent or modified input");
      }
      Area area; area.hold(); std::stop_source stop; stop.request_stop();
      error(storage::readSavedHistoryRangeView(*area.lease, Hour, stop.get_token()), storage::ErrorCode::Cancelled);
    });
    run("fd-path-replacement-truncation-and-directory-replacement", [] {
      for (const auto action : {ReadAction::Replace, ReadAction::Truncate, ReadAction::ReplaceMonth}) {
        Area area; area.tree(); area.write(area.day(), Valid); area.hold(); const auto before = stamp(area.day());
        const auto path = (action == ReadAction::ReplaceMonth ? area.month() : area.day()).string(), moved = path + ".owned-moved";
        if (action == ReadAction::ReplaceMonth) { area.directories.push_back(moved); area.files.push_back(std::filesystem::path(moved) / "02.dat"); }
        else if (action == ReadAction::Replace) { area.files.push_back(moved); }
        ReadArm state{before.info.st_dev, before.info.st_ino, action, nullptr, path.c_str(), moved.c_str()};
        { Arm arm(state); error(storage::readSavedHistoryRangeView(*area.lease, Hour), storage::ErrorCode::Conflict); }
        check(state.fired && state.mutationOk, "real mutation gate failed");
      }
    });
    run("all-earliest-filename-unknown-empty-and-inert-nondates", [] {
      Area area; area.tree(); area.write(area.day(), ""); area.write(area.month() / "07.dat", "s,1,8640,0,0,0,0,0,0\n1,2,3,4,5,6\n");
      area.link(area.profile / "stats/not-a-year", area.root / "absent");
      area.link(area.month() / "cache.dat", area.root / "absent"); area.write(area.month() / "99.dat", "bad");
      area.write(area.month() / "7.dat", "bad"); area.hold();
      const auto value = storage::readSavedHistoryRangeView(*area.lease, {core::HistoryPeriod::All, 0, 7 * Day});
      check(value && value->plan.startMillis == 0 && value->presentFiles == 2 && value->missingFiles == 6, "All date inventory/coverage");
      check(value->totals == core::HistoryCounters{1,2,3,4,5,6} && value->observations == 1, "All parse or inert-name handling");
    });
    run("all-relevant-symlink-special-and-future-paths-reject", [] {
      for (int level = 0; level < 4; ++level) {
        Area area; const std::array<std::filesystem::path, 4> paths{area.profile / "stats", area.profile / "stats/1970", area.month(), area.day()};
        for (int parent = 0; parent < level; ++parent) { area.directory(paths[static_cast<std::size_t>(parent)]); }
        area.link(paths[static_cast<std::size_t>(level)], area.root / "absent"); area.hold();
        error(storage::readSavedHistoryRangeView(*area.lease, {core::HistoryPeriod::All, 0, 7 * Day}), storage::ErrorCode::UnsafePath);
      }
      Area area; area.tree(); area.write(area.month() / "09.dat", ""); area.hold();
      error(storage::readSavedHistoryRangeView(*area.lease, {core::HistoryPeriod::All, 0, 7 * Day}), storage::ErrorCode::InvalidFormat);
    });
    run("all-inventory-entry-bound-is-exact-and-ignores-no-counts", [] {
      for (const auto extras : {core::SavedHistoryRangeMaximumFiles, core::SavedHistoryRangeMaximumFiles + 1}) {
        Area area; area.directory(area.profile / "stats"); area.hold(); struct stat info{};
        check(::stat((area.profile / "stats").c_str(), &info) == 0, "scan stat");
        ScanArm state{info.st_dev, info.st_ino, ScanAction::Flood, extras};
        Result value;
        { ScanScope arm(state); value = storage::readSavedHistoryRangeView(*area.lease, {core::HistoryPeriod::All, 0, 7 * Day + 1}); }
        check(state.fired, "inventory boundary gate inactive");
        if (extras == core::SavedHistoryRangeMaximumFiles) { check(value && !value->totals, "exact inventory cap rejected"); }
        else { error(value, storage::ErrorCode::InvalidFormat); }
      }
    });
    run("inventory-cancel-io-and-directory-swap-fail-whole-query", [] {
      for (const auto action : {ScanAction::Stop, ScanAction::Io, ScanAction::Replace}) {
        Area area; area.tree(); area.write(area.day(), Valid); area.hold(); const auto path = area.month().string(), moved = path + ".moved";
        if (action == ScanAction::Replace) { area.directories.push_back(moved); area.files.push_back(std::filesystem::path(moved) / "02.dat"); }
        struct stat info{}; check(::stat(area.month().c_str(), &info) == 0, "scan target stat"); std::stop_source stop;
        ScanArm state{info.st_dev, info.st_ino, action, 0, action == ScanAction::Stop ? &stop : nullptr, path.c_str(), moved.c_str()};
        { ScanScope arm(state); error(storage::readSavedHistoryRangeView(*area.lease, {core::HistoryPeriod::All, 0, 7 * Day}, stop.get_token()),
            action == ScanAction::Stop ? storage::ErrorCode::Cancelled : action == ScanAction::Io ? storage::ErrorCode::Io : storage::ErrorCode::Conflict); }
        check(state.fired && state.mutationOk, "inventory exact-fd fault inactive");
      }
    });
    run("middle-file-cancel-and-unsafe-path-never-publish-earlier-prefix", [] {
      for (int kind = 0; kind < 2; ++kind) {
        Area area; area.tree(); area.write(area.day(), Valid);
        if (kind == 0) { area.write(area.month() / "03.dat", Valid); }
        else { area.link(area.month() / "03.dat", area.day()); } area.hold();
        const auto query = core::HistoryRangeRequest{core::HistoryPeriod::Day, 0, 2 * Day};
        if (kind == 0) {
          const auto target = stamp(area.month() / "03.dat"); std::stop_source stop;
          ReadArm state{target.info.st_dev, target.info.st_ino, ReadAction::Stop, &stop};
          { Arm arm(state); error(storage::readSavedHistoryRangeView(*area.lease, query, stop.get_token()), storage::ErrorCode::Cancelled); }
          check(state.fired, "middle-file cancel did not read second file");
        } else { error(storage::readSavedHistoryRangeView(*area.lease, query), storage::ErrorCode::UnsafePath); }
      }
    });
    run("all-oversized-date-range-fails-before-any-file-prefix", [] {
      Area area; area.tree(); area.write(area.day(), Valid); area.hold();
      error(storage::readSavedHistoryRangeView(*area.lease, {core::HistoryPeriod::All, 0, 200LL * 365 * Day}), storage::ErrorCode::InvalidFormat);
    });
    std::cout << "PASS " << cases << " saved-history range reader families; " << assertions
              << " assertions; generated held profiles, bounded inventories, no prefix/writes/cache\n";
    return 0;
  } catch (const std::exception& exception) { std::cerr << "FAIL " << current << ": " << exception.what() << '\n'; return 1; }
}
