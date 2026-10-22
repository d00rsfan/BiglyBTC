#include "minute_history_writer.hpp"

#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {
using namespace biglybtc;
namespace fs = std::filesystem;
std::size_t assertions = 0, cases = 0;
void check(bool value, std::string_view message) {
  ++assertions; if (!value) { throw std::runtime_error(std::string(message)); }
}
struct Fd {
  int value = -1;
  explicit Fd(int fd) : value(fd) {}
  ~Fd() { if (value >= 0) { (void)::close(value); } }
  Fd(const Fd&) = delete; Fd& operator=(const Fd&) = delete;
};
std::string read(const fs::path& path) {
  Fd fd(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  check(fd.value >= 0, "fixture read open failed");
  std::string result; std::array<char, 8192> buffer{};
  for (;;) {
    const auto n = ::read(fd.value, buffer.data(), buffer.size());
    if (n < 0 && errno == EINTR) { continue; }
    check(n >= 0, "fixture read failed"); if (!n) { return result; }
    result.append(buffer.data(), static_cast<std::size_t>(n));
  }
}
struct Stamp {
  struct stat info{};
  std::string bytes;
};
Stamp stamp(const fs::path& path) {
  Stamp result; check(::lstat(path.c_str(), &result.info) == 0, "fixture stamp failed");
  if (S_ISREG(result.info.st_mode)) { result.bytes = read(path); }
  return result;
}
bool same(const Stamp& a, const Stamp& b) {
  return a.info.st_dev == b.info.st_dev && a.info.st_ino == b.info.st_ino
      && a.info.st_mode == b.info.st_mode && a.info.st_size == b.info.st_size
      && a.info.st_nlink == b.info.st_nlink && a.info.st_uid == b.info.st_uid
      && a.info.st_mtim.tv_sec == b.info.st_mtim.tv_sec && a.info.st_mtim.tv_nsec == b.info.st_mtim.tv_nsec
      && a.info.st_ctim.tv_sec == b.info.st_ctim.tv_sec && a.info.st_ctim.tv_nsec == b.info.st_ctim.tv_nsec
      && a.bytes == b.bytes;
}
bool absent(const fs::path& path) {
  struct stat info{}; return ::lstat(path.c_str(), &info) < 0 && errno == ENOENT;
}
struct Area {
  fs::path root, profile;
  std::optional<storage::ProfileLease> lease;
  Area() {
    char pattern[] = "/tmp/biglybtc-minute-writer-XXXXXX";
    const char* path = ::mkdtemp(pattern); check(path != nullptr, "owned fixture root failed"); root = path;
    profile = root / "profile"; check(::mkdir(profile.c_str(), 0700) == 0, "fixture profile mkdir failed");
    auto value = storage::ProfileLease::acquire(profile); check(value.has_value(), "fixture held lease failed");
    lease.emplace(std::move(*value));
  }
  ~Area() {
    lease.reset();
    // Only this unique owned tree is removed; filesystem removal does not follow
    // symlink entries. Every external sentinel is also under this owned root.
    if (!root.empty()) { std::error_code ec; fs::remove_all(root, ec); }
  }
  Area(const Area&) = delete; Area& operator=(const Area&) = delete;
  fs::path month() const { return profile / "stats/2024/01"; }
  fs::path day() const { return month() / "01.dat"; }
  void tree() { check(fs::create_directories(month()), "fixture tree creation failed"); }
  void write(const fs::path& path, std::string_view bytes) {
    Fd fd(::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    check(fd.value >= 0, "fixture exclusive file failed");
    std::size_t at = 0;
    while (at < bytes.size()) {
      const auto n = ::write(fd.value, bytes.data() + at, bytes.size() - at);
      if (n < 0 && errno == EINTR) { continue; }
      check(n > 0, "fixture write failed"); at += static_cast<std::size_t>(n);
    }
    check(::fsync(fd.value) == 0, "fixture fsync failed");
  }
};
constexpr std::int64_t Jan1 = 1704067200000LL;
constexpr core::MinuteHistoryIdentity Identity{1, 2, 3};
constexpr core::HistoryCounters Raw{10, 20, 30, 40, 50, 60};
constexpr std::string_view Header = "s,1,28401120,100,200,300,400,500,600\n";
core::MinuteHistoryRequest request(core::MinuteHistoryEvent event, std::int64_t now = Jan1,
    core::HistoryCounters raw = Raw) {
  return {event, Identity, now, {std::array{raw[0], raw[1], raw[2], raw[3]}, std::array{raw[4], raw[5]}},
      event == core::MinuteHistoryEvent::Start ? std::optional{core::HistoryCounters{100, 200, 300, 400, 500, 600}} : std::nullopt};
}
void success(const std::expected<void, storage::Error>& result) {
  check(result.has_value(), result ? "unexpected success" : result.error().message);
}
void error(const std::expected<void, storage::Error>& result, storage::ErrorCode code) {
  check(!result && result.error().code == code, "writer error code mismatch");
}
void start(Area& area, core::MinuteHistoryRecorder& recorder, std::int64_t time = Jan1) {
  success(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Start, time)));
}
enum class Fault { ShortThenError, FileSync, DirectorySync, StopOnOpen, StopAfterWrite, ReplaceAfterWrite, ReplaceMonthAfterWrite, FailNewHeader };
struct Hook {
  Fault fault;
  dev_t device = 0; ino_t inode = 0;
  std::stop_source* stop = nullptr;
  std::string name{}, replacement{}, moved{};
  bool fired = false, mutationOk = true;
  int writes = 0;
};
thread_local Hook* active = nullptr;
struct Arm {
  explicit Arm(Hook& hook) { check(!active, "test hook already armed"); active = &hook; }
  ~Arm() { active = nullptr; }
  Arm(const Arm&) = delete; Arm& operator=(const Arm&) = delete;
};
bool matches(int fd, const Hook& hook) {
  struct stat info{}; return ::fstat(fd, &info) == 0 && info.st_dev == hook.device && info.st_ino == hook.inode;
}
Hook fileHook(Fault fault, const fs::path& path) {
  const auto before = stamp(path); Hook result{fault}; result.device = before.info.st_dev; result.inode = before.info.st_ino; return result;
}
} // namespace

extern "C" ssize_t __real_write(int, const void*, size_t);
extern "C" int __real_fsync(int);
extern "C" int __real_openat(int, const char*, int, ...);
extern "C" ssize_t __wrap_write(int fd, const void* bytes, size_t count) {
  Hook* hook = active;
  if (!hook || !matches(fd, *hook)) { return __real_write(fd, bytes, count); }
  ++hook->writes;
  if (hook->fault == Fault::ShortThenError) {
    hook->fired = true;
    if (hook->writes == 1) { return __real_write(fd, bytes, count > 3 ? 3 : count); }
    errno = EIO; return -1;
  }
  const auto result = __real_write(fd, bytes, count);
  if (result <= 0 || hook->fired) { return result; }
  if (hook->fault == Fault::StopAfterWrite) {
    hook->fired = true; hook->stop->request_stop();
  } else if (hook->fault == Fault::ReplaceAfterWrite) {
    hook->fired = true;
    hook->mutationOk = ::rename(hook->name.c_str(), hook->moved.c_str()) == 0
        && ::symlink(hook->replacement.c_str(), hook->name.c_str()) == 0;
  } else if (hook->fault == Fault::ReplaceMonthAfterWrite) {
    hook->fired = true;
    hook->mutationOk = ::rename(hook->name.c_str(), hook->moved.c_str()) == 0
        && ::mkdir(hook->name.c_str(), 0700) == 0;
  }
  return result;
}
extern "C" int __wrap_fsync(int fd) {
  auto* hook = active;
  if (hook && !hook->fired && (hook->fault == Fault::FileSync || hook->fault == Fault::DirectorySync)
      && matches(fd, *hook)) {
    hook->fired = true; errno = EIO; return -1;
  }
  return __real_fsync(fd);
}
extern "C" int __wrap_openat(int parent, const char* name, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list args; va_start(args, flags); mode = static_cast<mode_t>(va_arg(args, int)); va_end(args);
  }
  auto* hook = active;
  if (hook && !hook->fired && hook->fault == Fault::FailNewHeader && name && hook->name == name
      && (flags & O_CREAT) && matches(parent, *hook)) {
    hook->fired = true; errno = EIO; return -1;
  }
  const int fd = (flags & O_CREAT) ? __real_openat(parent, name, flags, mode) : __real_openat(parent, name, flags);
  if (hook && !hook->fired && hook->fault == Fault::StopOnOpen && fd >= 0 && matches(fd, *hook)) {
    hook->fired = true; hook->stop->request_stop();
  }
  return fd;
}

int main() {
  std::string current;
  try {
    const auto run = [&](std::string name, auto body) { current = std::move(name); body(); ++cases; };
    run("create-exact-private-path-six-independent-fields", [] {
      Area area; core::MinuteHistoryRecorder recorder; const int leaseFd = area.lease->directoryHandle(); start(area, recorder);
      check(read(area.day()) == Header && recorder.state().active, "created header/state mismatch");
      for (const auto& path : {area.profile / "stats", area.profile / "stats/2024", area.month()}) {
        const auto info = stamp(path); check(S_ISDIR(info.info.st_mode) && (info.info.st_mode & 0777) == 0700, "new directory not private");
      }
      const auto file = stamp(area.day()); check((file.info.st_mode & 0777) == 0600 && file.info.st_nlink == 1, "new file mode/link wrong");
      success(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Tick, Jan1 + 60000, {11,22,33,44,55,66})));
      success(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::End, Jan1 + 120000, {13,26,39,52,65,78})));
      check(read(area.day()) == std::string(Header) + "1,2,3,4,5,6\ne,1,28401122,2,4,6,8,10,12\n", "exact independent delta/end mapping wrong");
      auto parsed = core::readSavedHistoryDay(read(area.day()), {2024,1,1});
      check(parsed && parsed->totals == core::HistoryCounters{3,6,9,12,15,18} && !recorder.state().active, "actual reader disagrees");
      check(leaseFd == area.lease->directoryHandle() && ::fcntl(leaseFd, F_GETFD) >= 0, "borrowed lease consumed");
      check(absent(area.day().string() + ".bak") && absent(area.day().string() + ".saving"), "invented config recovery paths");
    });
    run("append-preserves-opaque-valid-record-bytes-mode-and-neighbors", [] {
      Area area; area.tree();
      const std::string old = "s,+01,28401120,+000,0,0,0,0,0\r\n\r\n+01,-02,3,4,5,6\r\n";
      area.write(area.day(), old); check(::chmod(area.day().c_str(), 0640) == 0, "fixture chmod failed");
      area.write(area.month() / "cache.dat", "opaque cache bytes"); area.write(area.month() / "02.dat", "unrelated malformed bytes");
      const auto cache = stamp(area.month() / "cache.dat"), neighbor = stamp(area.month() / "02.dat"), before = stamp(area.day());
      core::MinuteHistoryRecorder recorder; start(area, recorder); const auto after = stamp(area.day());
      check(after.bytes == old + std::string(Header) && after.info.st_mode == before.info.st_mode
          && after.info.st_ino == before.info.st_ino, "old original bytes/mode/inode changed");
      check(same(cache, stamp(area.month() / "cache.dat")) && same(neighbor, stamp(area.month() / "02.dat")), "unrelated paths changed");
    });
    run("unknown-start-no-created-paths-observed-zero-records", [] {
      Area area; core::MinuteHistoryRecorder recorder; auto value = request(core::MinuteHistoryEvent::Start);
      value.input.dht.reset(); error(storage::writeMinuteHistory(*area.lease, recorder, value), storage::ErrorCode::Unsupported);
      check(absent(area.profile / "stats") && !recorder.state().active, "unknown DHT invented files or zeros");
      value.input = {std::array<std::int64_t,4>{}, std::array<std::int64_t,2>{}}; value.absoluteBaseline = core::HistoryCounters{};
      success(storage::writeMinuteHistory(*area.lease, recorder, value));
      value = request(core::MinuteHistoryEvent::Tick, Jan1 + 60000, {}); success(storage::writeMinuteHistory(*area.lease, recorder, value));
      auto day = core::readSavedHistoryDay(read(area.day()), {2024,1,1});
      check(day && day->totals == core::HistoryCounters{} && day->observations == 1, "actual known zero lost");
    });
    run("malformed-unsupported-or-unterminated-tail-never-repaired", [] {
      for (const auto text : {std::string_view{"garbage\n"}, std::string_view{"s,2,28401120,0,0,0,0,0,0\n"},
          std::string_view{"s,1,28401120,0,0,0,0,0,0"}, std::string_view{"s,1,28401120,0,0,0,0,0,0\n1,2,3,4,5,bad\n"}}) {
        Area area; area.tree(); area.write(area.day(), text); const auto before = stamp(area.day()); core::MinuteHistoryRecorder recorder;
        error(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Start)),
            text.starts_with("s,2") ? storage::ErrorCode::Unsupported : storage::ErrorCode::InvalidFormat);
        check(same(before, stamp(area.day())) && recorder.state() == core::MinuteHistoryState{}, "refused file changed or plan leaked");
      }
    });
    run("bounded-existing-bytes-records-and-resulting-record-count", [] {
      for (int kind = 0; kind < 3; ++kind) {
        Area area; area.tree(); std::string bytes;
        if (!kind) { bytes.assign(core::SavedHistoryMaximumBytes + 1, '\n'); }
        else { for (std::size_t i = 0; i < core::SavedHistoryMaximumRecords + (kind == 1 ? 1U : 0U); ++i) { bytes += "0,0,0,0,0,0\n"; } }
        area.write(area.day(), bytes); const auto before = stamp(area.day()); core::MinuteHistoryRecorder recorder;
        error(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Start)), storage::ErrorCode::InvalidFormat);
        check(same(before, stamp(area.day())) && !recorder.state().poisoned, "prewrite limit changed bytes/state");
      }
    });
    run("unsafe-components-special-files-and-external-targets-refused", [] {
      for (int level = 0; level < 4; ++level) {
        for (int kind = 0; kind < 3; ++kind) {
          Area area; area.write(area.root / "sentinel", Header); const auto target = stamp(area.root / "sentinel");
          const std::array<fs::path,4> paths{area.profile / "stats", area.profile / "stats/2024", area.month(), area.day()};
          for (int i = 0; i < level; ++i) { check(::mkdir(paths[static_cast<std::size_t>(i)].c_str(), 0700) == 0, "fixture parent failed"); }
          const auto& path = paths[static_cast<std::size_t>(level)];
          if (!kind) { check(::symlink((area.root / "sentinel").c_str(), path.c_str()) == 0, "fixture symlink failed"); }
          else if (kind == 1) {
            if (level == 3) { check(::mkdir(path.c_str(), 0700) == 0, "fixture leaf dir failed"); }
            else { area.write(path, Header); }
          } else { check(::mkfifo(path.c_str(), 0600) == 0, "fixture FIFO failed"); }
          const auto before = stamp(path); core::MinuteHistoryRecorder recorder;
          error(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Start)), storage::ErrorCode::UnsafePath);
          check(same(target, stamp(area.root / "sentinel")) && same(before, stamp(path)) && !recorder.state().active, "unsafe entry/target changed");
        }
      }
    });
    run("hardlink-refused-with-both-names-intact", [] {
      Area area; area.tree(); area.write(area.root / "sentinel", Header);
      check(::link((area.root / "sentinel").c_str(), area.day().c_str()) == 0, "fixture hardlink failed");
      const auto before = stamp(area.root / "sentinel"); core::MinuteHistoryRecorder recorder;
      error(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Start)), storage::ErrorCode::UnsafePath);
      check(same(before, stamp(area.root / "sentinel")) && same(before, stamp(area.day())), "hardlink target changed");
    });
    run("prestop-invalid-lease-and-beforewrite-stop-do-not-advance", [] {
      Area area; core::MinuteHistoryRecorder recorder; std::stop_source stop; stop.request_stop();
      error(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Start), stop.get_token()), storage::ErrorCode::Cancelled);
      check(absent(area.profile / "stats"), "prestop created directories");
      auto owned = std::move(*area.lease);
      error(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Start)), storage::ErrorCode::UnsafePath);
      area.lease.emplace(std::move(owned)); area.tree(); area.write(area.day(), Header); const auto before = stamp(area.day());
      stop = std::stop_source{}; auto hook = fileHook(Fault::StopOnOpen, area.day()); hook.stop = &stop;
      { Arm arm(hook); error(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Start), stop.get_token()), storage::ErrorCode::Cancelled); }
      check(hook.fired && same(before, stamp(area.day())) && recorder.state() == core::MinuteHistoryState{}, "beforewrite stop advanced state/bytes");
      start(area, recorder); check(read(area.day()) == std::string(Header) + std::string(Header), "clean cancellation left pending plan");
    });
    run("stop-after-actual-write-returns-real-success-not-cancelled", [] {
      Area area; core::MinuteHistoryRecorder recorder; start(area, recorder); std::stop_source stop;
      auto hook = fileHook(Fault::StopAfterWrite, area.day()); hook.stop = &stop;
      { Arm arm(hook); success(storage::writeMinuteHistory(*area.lease, recorder,
          request(core::MinuteHistoryEvent::Tick, Jan1 + 60000, {11,22,33,44,55,66}), stop.get_token())); }
      check(hook.fired && stop.stop_requested() && recorder.state().previousRelative == core::HistoryCounters{1,2,3,4,5,6}
          && !recorder.state().poisoned && read(area.day()).ends_with("1,2,3,4,5,6\n"), "committed write hidden by cancellation");
    });
    run("partial-write-keeps-prefix-poisons-and-never-retries", [] {
      Area area; core::MinuteHistoryRecorder recorder; start(area, recorder); auto hook = fileHook(Fault::ShortThenError, area.day());
      { Arm arm(hook); error(storage::writeMinuteHistory(*area.lease, recorder,
          request(core::MinuteHistoryEvent::Tick, Jan1 + 60000, {11,22,33,44,55,66})), storage::ErrorCode::Io); }
      check(hook.fired && hook.writes == 2 && read(area.day()) == std::string(Header) + "1,2", "real partial prefix not retained");
      check(recorder.state().poisoned && !recorder.state().active, "partial writer not poisoned");
      const auto before = stamp(area.day()); error(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Start)), storage::ErrorCode::Conflict);
      check(same(before, stamp(area.day())), "poisoned writer retried");
    });
    run("file-fsync-error-keeps-actual-record-but-poisons-state", [] {
      Area area; core::MinuteHistoryRecorder recorder; start(area, recorder); const auto oldState = recorder.state();
      auto hook = fileHook(Fault::FileSync, area.day());
      { Arm arm(hook); error(storage::writeMinuteHistory(*area.lease, recorder,
          request(core::MinuteHistoryEvent::Tick, Jan1 + 60000, {11,22,33,44,55,66})), storage::ErrorCode::Io); }
      check(hook.fired && read(area.day()).ends_with("1,2,3,4,5,6\n") && recorder.state().poisoned
          && recorder.state().previousRelative == oldState.previousRelative, "sync failure advanced certified state or erased real bytes");
    });
    run("creation-parent-sync-failure-is-not-success-or-rollback", [] {
      Area area; area.tree(); core::MinuteHistoryRecorder recorder; auto hook = fileHook(Fault::DirectorySync, area.month());
      { Arm arm(hook); error(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Start)), storage::ErrorCode::Io); }
      check(hook.fired && read(area.day()) == Header && recorder.state().poisoned, "creation sync error erased committed bytes or certified success");
    });
    run("real-postwrite-file-and-directory-swap-detected", [] {
      for (const auto fault : {Fault::ReplaceAfterWrite, Fault::ReplaceMonthAfterWrite}) {
        Area area; core::MinuteHistoryRecorder recorder; start(area, recorder); area.write(area.root / "sentinel", Header);
        const auto target = stamp(area.root / "sentinel"); auto hook = fileHook(fault, area.day());
        hook.name = (fault == Fault::ReplaceAfterWrite ? area.day() : area.month()).string();
        hook.moved = hook.name + ".moved"; hook.replacement = (area.root / "sentinel").string();
        { Arm arm(hook); error(storage::writeMinuteHistory(*area.lease, recorder,
            request(core::MinuteHistoryEvent::Tick, Jan1 + 60000, {11,22,33,44,55,66})), storage::ErrorCode::Conflict); }
        check(hook.fired && hook.mutationOk && recorder.state().poisoned && same(target, stamp(area.root / "sentinel")), "swap not exercised or touched target");
        const auto written = fault == Fault::ReplaceAfterWrite ? fs::path(hook.moved) : fs::path(hook.moved) / "01.dat";
        check(read(written) == std::string(Header) + "1,2,3,4,5,6\n", "actual old inode append was erased");
      }
    });
    run("rollover-spill-new-baseline-and-later-end-stay-original-shaped", [] {
      Area area; core::MinuteHistoryRecorder recorder; start(area, recorder, Jan1 - 60000);
      success(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Tick, Jan1, {11,22,33,44,55,66})));
      const auto oldFile = area.profile / "stats/2023/12/31.dat";
      check(read(oldFile) == "s,1,28401119,100,200,300,400,500,600\n1,2,3,4,5,6\n", "rollover old-file spill wrong");
      check(read(area.day()) == "s,1,28401120,101,202,303,404,505,606\n", "rollover double counted or wrong new baseline");
      const auto oldStamp = stamp(oldFile);
      success(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::End, Jan1 + 60000, {12,24,36,48,60,72})));
      check(read(area.day()).ends_with("e,1,28401121,1,2,3,4,5,6\n") && same(oldStamp, stamp(oldFile)), "later e redirected/reopened old file");
    });
    run("rollover-later-file-failure-keeps-old-prefix-poisons-no-retry", [] {
      Area area; area.tree(); core::MinuteHistoryRecorder recorder; start(area, recorder, Jan1 - 60000);
      auto hook = fileHook(Fault::FailNewHeader, area.month()); hook.name = "01.dat";
      storage::MinuteHistoryWriteReceipt receipt{99};
      { Arm arm(hook); error(storage::writeMinuteHistory(*area.lease, recorder,
          request(core::MinuteHistoryEvent::Tick, Jan1, {11,22,33,44,55,66}), {}, &receipt), storage::ErrorCode::Io); }
      check(receipt.appendedActions == 0, "failed rollover batch advertised its already appended old-file prefix");
      const auto oldFile = area.profile / "stats/2023/12/31.dat";
      check(hook.fired && read(oldFile).ends_with("1,2,3,4,5,6\n") && absent(area.day()) && recorder.state().poisoned, "two-file failure was misreported or rolled back");
    });
    run("close-only-gap-never-reads-or-fabricates-terminal-data", [] {
      Area area; core::MinuteHistoryRecorder recorder; start(area, recorder); const auto before = stamp(area.day());
      auto value = request(core::MinuteHistoryEvent::End, Jan1 + 60000); value.input.dht.reset();
      success(storage::writeMinuteHistory(*area.lease, recorder, value));
      check(same(before, stamp(area.day())) && !recorder.state().active && !recorder.state().poisoned, "unknown end fabricated or rewrote bytes");
      check(::unlink(area.day().c_str()) == 0, "fixture removal failed"); start(area, recorder);
      success(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Suspend, -1)));
      check(read(area.day()) == Header && !recorder.state().active, "suspend invented e");
    });
    run("successful-write-receipts-exclude-close-only-cancel-and-uncertain-append", [] {
      Area area; core::MinuteHistoryRecorder recorder; storage::MinuteHistoryWriteReceipt receipt{99};
      success(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Start), {}, &receipt));
      check(receipt.appendedActions == 1 && read(area.day()) == Header, "start receipt missing actual header append");
      success(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Suspend, -1), {}, &receipt));
      check(receipt.appendedActions == 0 && read(area.day()) == Header, "close-only success advertised changed bytes");
      std::stop_source stop; stop.request_stop(); receipt.appendedActions = 99;
      error(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Start), stop.get_token(), &receipt), storage::ErrorCode::Cancelled);
      check(receipt.appendedActions == 0, "cancelled plan retained a stale success receipt");
      success(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Start), {}, &receipt));
      auto hook = fileHook(Fault::FileSync, area.day()); receipt.appendedActions = 99;
      { Arm arm(hook); error(storage::writeMinuteHistory(*area.lease, recorder,
          request(core::MinuteHistoryEvent::Tick, Jan1 + 60000, {11,22,33,44,55,66}), {}, &receipt), storage::ErrorCode::Io); }
      check(hook.fired && recorder.state().poisoned && read(area.day()).ends_with("1,2,3,4,5,6\n")
          && receipt.appendedActions == 0, "uncertain real append published a successful receipt");
    });
    run("rollover-and-end-success-count-actual-appends-once-per-plan", [] {
      Area area; core::MinuteHistoryRecorder recorder; storage::MinuteHistoryWriteReceipt receipt;
      success(storage::writeMinuteHistory(*area.lease, recorder,
          request(core::MinuteHistoryEvent::Start, Jan1 - 60000), {}, &receipt));
      check(receipt.appendedActions == 1, "start append count wrong");
      success(storage::writeMinuteHistory(*area.lease, recorder,
          request(core::MinuteHistoryEvent::Tick, Jan1, {11,22,33,44,55,66}), {}, &receipt));
      check(receipt.appendedActions == 2, "rollover must report both real appended records in one receipt");
      success(storage::writeMinuteHistory(*area.lease, recorder,
          request(core::MinuteHistoryEvent::End, Jan1 + 60000, {13,26,39,52,65,78}), {}, &receipt));
      check(receipt.appendedActions == 1 && !recorder.state().active, "end append receipt includes non-IO close action");
    });
    run("signed-adjustments-and-full-width-baseline-remain-exact", [] {
      Area area; core::MinuteHistoryRecorder recorder; auto value = request(core::MinuteHistoryEvent::Start);
      value.absoluteBaseline = core::HistoryCounters{std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(),0,-1,1,0};
      success(storage::writeMinuteHistory(*area.lease, recorder, value));
      success(storage::writeMinuteHistory(*area.lease, recorder, request(core::MinuteHistoryEvent::Tick, Jan1 + 60000, {8,17,26,35,44,53})));
      check(read(area.day()) == "s,1,28401120,-9223372036854775808,9223372036854775807,0,-1,1,0\n-2,-3,-4,-5,-6,-7\n", "signed width/adjustment changed");
    });
    std::cout << "minute history writer cases=" << cases << " assertions=" << assertions << '\n'; return 0;
  } catch (const std::exception& error) {
    std::cerr << "case=" << current << ": " << error.what() << '\n'; return 1;
  }
}
