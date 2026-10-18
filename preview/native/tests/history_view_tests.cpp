#include "history_view.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc;
std::size_t assertions = 0, cases = 0;
void check(bool condition, std::string_view message) {
  ++assertions; if (!condition) { throw std::runtime_error(std::string(message)); }
}
struct Fd {
  int value;
  explicit Fd(int fd) : value(fd) {}
  ~Fd() { if (value >= 0) { (void)::close(value); } }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
std::string read(const std::filesystem::path& path) {
  Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)); check(fd.value >= 0, "explicit read open failed");
  std::string bytes; std::array<char, 4096> buffer{};
  for (;;) {
    const auto count = ::read(fd.value, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) { continue; }
    check(count >= 0, "explicit read failed"); if (count == 0) { return bytes; }
    bytes.append(buffer.data(), static_cast<std::size_t>(count));
  }
}
struct Stamp { dev_t device; ino_t inode; off_t size; mode_t mode; nlink_t links; timespec modified; timespec changed; std::string bytes; };
Stamp stamp(const std::filesystem::path& path) {
  struct stat info{}; check(::lstat(path.c_str(), &info) == 0, "explicit stamp failed");
  return {info.st_dev, info.st_ino, info.st_size, info.st_mode, info.st_nlink, info.st_mtim, info.st_ctim,
      S_ISREG(info.st_mode) ? read(path) : std::string{}};
}
bool same(const Stamp& a, const Stamp& b) {
  return a.device == b.device && a.inode == b.inode && a.size == b.size && a.mode == b.mode && a.links == b.links
      && a.modified.tv_sec == b.modified.tv_sec && a.modified.tv_nsec == b.modified.tv_nsec
      && a.changed.tv_sec == b.changed.tv_sec && a.changed.tv_nsec == b.changed.tv_nsec && a.bytes == b.bytes;
}
struct Area {
  std::filesystem::path root, profile;
  std::vector<std::filesystem::path> files, dirs;
  std::optional<storage::ProfileLease> lease;
  Area() {
    char pattern[] = "/tmp/biglybtc-history-view-tests-XXXXXX";
    const char* made = ::mkdtemp(pattern); check(made, "cannot create generated root"); root = made;
    try { profile = root / "profile"; directory(profile); files.push_back(profile / ".azlock"); }
    catch (...) { cleanup(); throw; }
  }
  ~Area() { cleanup(); }
  Area(const Area&) = delete;
  Area& operator=(const Area&) = delete;
  void directory(const std::filesystem::path& path) { dirs.push_back(path); check(::mkdir(path.c_str(), 0700) == 0, "mkdir failed"); }
  void write(const std::filesystem::path& path, std::string_view bytes) {
    files.push_back(path); Fd fd(::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    check(fd.value >= 0, "exclusive fixture open failed");
    std::size_t offset = 0;
    while (offset < bytes.size()) {
      const auto count = ::write(fd.value, bytes.data() + offset, bytes.size() - offset);
      if (count < 0 && errno == EINTR) { continue; }
      check(count > 0, "fixture write failed"); offset += static_cast<std::size_t>(count);
    }
    check(::fsync(fd.value) == 0, "fixture fsync failed");
  }
  void link(const std::filesystem::path& name, const std::filesystem::path& target) {
    files.push_back(name); check(::symlink(target.c_str(), name.c_str()) == 0, "fixture symlink failed");
  }
  void hold() { auto value = storage::ProfileLease::acquire(profile); check(value.has_value(), "generated lease failed"); lease.emplace(std::move(*value)); }
  std::filesystem::path month() const { return profile / "stats/1970/01"; }
  std::filesystem::path day() const { return month() / "02.dat"; }
  void tree() { directory(profile / "stats"); directory(profile / "stats/1970"); directory(month()); }
private:
  void cleanup() noexcept {
    lease.reset(); for (auto it = files.rbegin(); it != files.rend(); ++it) { (void)::unlink(it->c_str()); }
    for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) { (void)::rmdir(it->c_str()); }
    if (!root.empty()) { (void)::rmdir(root.c_str()); }
  }
};
constexpr core::HistoryDate Date{1970, 1, 2};
constexpr std::string_view Valid = "s,1,1440,100,200,300,400,500,600\n1,2,3,4,5,6\n";
void error(const std::expected<storage::SavedHistoryDayView, storage::Error>& result, storage::ErrorCode code) {
  check(!result && result.error().code == code, "history reader error mismatch");
}
void loaded(const std::expected<storage::SavedHistoryDayView, storage::Error>& result) {
  check(result && !result->missing() && result->snapshot && result->snapshot->date == Date, "history not loaded");
  check(result->snapshot->totals == core::HistoryCounters{1, 2, 3, 4, 5, 6}
      && result->snapshot->hours[0] == result->snapshot->totals && result->snapshot->observations == 1, "history counter mapping wrong");
}
enum class Action { Stop, Io, StopAndIo, ReplaceFile, Truncate, ReplaceMonth };
struct ReadArm {
  dev_t device; ino_t inode; Action action; std::stop_source* source;
  const char* path; const char* moved; std::string_view replacement;
  bool fired = false; bool mutationOk = true;
};
thread_local ReadArm* activeRead = nullptr;
struct Arm {
  explicit Arm(ReadArm& value) { check(!activeRead, "read gate already armed"); activeRead = &value; }
  ~Arm() { activeRead = nullptr; }
  Arm(const Arm&) = delete; Arm& operator=(const Arm&) = delete;
};
} // namespace

extern "C" ssize_t __real_read(int descriptor, void* buffer, size_t count);
extern "C" ssize_t __wrap_read(int descriptor, void* buffer, size_t count) {
  const auto result = __real_read(descriptor, buffer, count); auto* arm = activeRead;
  struct stat info{};
  if (arm && result > 0 && ::fstat(descriptor, &info) == 0 && info.st_dev == arm->device && info.st_ino == arm->inode) {
    activeRead = nullptr; arm->fired = true;
    if (arm->source) { arm->source->request_stop(); }
    if (arm->action == Action::ReplaceFile) {
      arm->mutationOk = ::rename(arm->path, arm->moved) == 0;
      if (arm->mutationOk) {
        const int fd = ::open(arm->path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        arm->mutationOk = fd >= 0;
        if (fd >= 0) {
          const auto written = ::write(fd, arm->replacement.data(), arm->replacement.size());
          arm->mutationOk = written >= 0 && static_cast<std::size_t>(written) == arm->replacement.size();
          (void)::close(fd);
        }
      }
    } else if (arm->action == Action::Truncate) {
      const int fd = ::open(arm->path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
      arm->mutationOk = fd >= 0 && ::ftruncate(fd, 0) == 0; if (fd >= 0) { (void)::close(fd); }
    } else if (arm->action == Action::ReplaceMonth) {
      arm->mutationOk = ::rename(arm->path, arm->moved) == 0 && ::mkdir(arm->path, 0700) == 0;
    }
    if (arm->action == Action::Io || arm->action == Action::StopAndIo) { errno = EIO; return -1; }
  }
  return result;
}

int main() {
  std::string current;
  try {
    const auto run = [&](std::string name, auto body) { current = std::move(name); body(); ++cases; };
    run("each-missing-component-does-not-create-paths", [] {
      for (int missing = 0; missing < 4; ++missing) {
        Area area; area.hold();
        if (missing > 0) { area.directory(area.profile / "stats"); }
        if (missing > 1) { area.directory(area.profile / "stats/1970"); }
        if (missing > 2) { area.directory(area.month()); }
        const auto rootStamp = stamp(area.profile); const int fd = area.lease->directoryHandle();
        const auto result = storage::readSavedHistoryDayView(*area.lease, Date);
        check(result && result->missing(), "missing component fabricated a snapshot");
        check(same(rootStamp, stamp(area.profile)) && area.lease->directoryHandle() == fd && ::fcntl(fd, F_GETFD) >= 0, "reader changed/consumed profile lease");
      }
    });
    run("loaded-owning-snapshot-and-exact-no-write", [] {
      Area area; area.tree(); area.write(area.day(), Valid); area.hold();
      area.write(area.month() / "cache.dat", "unrelated cache bytes"); area.write(area.month() / "03.dat", "malformed neighboring day");
      const auto before = stamp(area.day()), cache = stamp(area.month() / "cache.dat"), neighbor = stamp(area.month() / "03.dat");
      auto result = storage::readSavedHistoryDayView(*area.lease, Date); loaded(result);
      check(same(before, stamp(area.day())) && same(cache, stamp(area.month() / "cache.dat")) && same(neighbor, stamp(area.month() / "03.dat")), "reader changed selected or unrelated content");
      auto copy = *result; auto moved = std::move(*result); area.lease.reset();
      check(copy.snapshot->totals == moved.snapshot->totals && copy.snapshot->totals == core::HistoryCounters{1, 2, 3, 4, 5, 6}, "snapshot depended on lease/input lifetime");
    });
    run("empty-and-header-only-file-are-present-unknown", [] {
      for (const auto text : {std::string_view{}, std::string_view{"s,1,1440,1,2,3,4,5,6\n"}}) {
        Area area; area.tree(); area.write(area.day(), text); area.hold();
        const auto result = storage::readSavedHistoryDayView(*area.lease, Date);
        check(result && !result->missing() && !result->snapshot->totals && result->snapshot->observations == 0, "empty/header file became missing/zero");
      }
    });
    run("no-backup-or-saving-recovery", [] {
      Area area; area.tree(); area.write(area.day().string() + ".bak", Valid); area.write(area.day().string() + ".saving", Valid); area.hold();
      auto result = storage::readSavedHistoryDayView(*area.lease, Date); check(result && result->missing(), "invented day-file recovery");
    });
    run("date-and-held-lease-validation", [] {
      Area area; area.tree(); area.write(area.day(), Valid); area.hold();
      for (const auto date : {core::HistoryDate{1969, 1, 1}, core::HistoryDate{2024, 2, 30}, core::HistoryDate{1970, 257, 1}}) {
        error(storage::readSavedHistoryDayView(*area.lease, date), storage::ErrorCode::InvalidFormat);
      }
      auto owned = std::move(*area.lease); check(!area.lease->valid(), "moved lease unexpectedly valid");
      error(storage::readSavedHistoryDayView(*area.lease, Date), storage::ErrorCode::UnsafePath);
      loaded(storage::readSavedHistoryDayView(owned, Date));
    });
    run("date-leaves-are-exact-zero-padded", [] {
      Area area; area.directory(area.profile / "stats"); area.directory(area.profile / "stats/9999");
      area.directory(area.profile / "stats/9999/12"); area.write(area.profile / "stats/9999/12/31.dat", ""); area.hold();
      const auto result = storage::readSavedHistoryDayView(*area.lease, {9999, 12, 31});
      check(result && !result->missing() && result->snapshot->date == core::HistoryDate{9999, 12, 31}, "bounded year/month/day formatted incorrectly");
    });
    run("unsafe-directories-and-leaf-never-followed-or-blocked", [] {
      for (int level = 0; level < 4; ++level) {
        for (int kind = 0; kind < 3; ++kind) {
          Area area; area.write(area.root / "sentinel", Valid); const auto sentinel = stamp(area.root / "sentinel");
          const std::array<std::filesystem::path, 4> paths{area.profile / "stats", area.profile / "stats/1970", area.month(), area.day()};
          for (int parent = 0; parent < level; ++parent) { area.directory(paths[static_cast<std::size_t>(parent)]); }
          const auto& path = paths[static_cast<std::size_t>(level)];
          if (kind == 0) {
            auto target = area.root / "redirect";
            if (level < 3) {
              area.directory(target);
              for (int remaining = level + 1; remaining < 3; ++remaining) {
                target /= paths[static_cast<std::size_t>(remaining)].filename(); area.directory(target);
              }
              area.write(target / "02.dat", Valid);
              // Reset to the redirect root; if O_NOFOLLOW were omitted this
              // complete directory tree would let the reader load valid data.
              area.link(path, area.root / "redirect");
            } else { area.link(path, area.root / "sentinel"); }
          }
          else if (kind == 1) {
            if (level == 3) { area.directory(path); } else { area.write(path, Valid); }
          } else { area.files.push_back(path); check(::mkfifo(path.c_str(), 0600) == 0, "mkfifo failed"); }
          area.hold(); error(storage::readSavedHistoryDayView(*area.lease, Date), storage::ErrorCode::UnsafePath);
          check(same(sentinel, stamp(area.root / "sentinel")), "unsafe target was modified");
        }
      }
    });
    run("invalid-version-malformed-and-overflow-map-explicitly", [] {
      for (const auto& [text, code] : std::array<std::pair<std::string_view, storage::ErrorCode>, 4>{{
          {"garbage", storage::ErrorCode::InvalidFormat}, {"s,2,1440,0,0,0,0,0,0", storage::ErrorCode::Unsupported},
          {"s,1,1440,0,0,0,0,0,0\n9223372036854775808,0,0,0,0,0", storage::ErrorCode::InvalidFormat},
          {"s,1,1440,0,0,0,0,0,0\n1,2,3,4,5,bad", storage::ErrorCode::InvalidFormat}}}) {
        Area area; area.tree(); area.write(area.day(), text); area.hold(); const auto before = stamp(area.day());
        error(storage::readSavedHistoryDayView(*area.lease, Date), code); check(same(before, stamp(area.day())), "invalid read rewrote data");
      }
    });
    run("bounded-input-line-record-limits", [] {
      for (int kind = 0; kind < 3; ++kind) {
        Area area; area.tree(); std::string text;
        if (kind == 0) { text.assign(core::SavedHistoryMaximumBytes + 1, '\n'); }
        if (kind == 1) { text.assign(core::SavedHistoryMaximumLineBytes + 1, '0'); }
        if (kind == 2) { for (std::size_t i = 0; i <= core::SavedHistoryMaximumRecords; ++i) { text += "0,0,0,0,0,0\n"; } }
        area.write(area.day(), text); area.hold(); error(storage::readSavedHistoryDayView(*area.lease, Date), storage::ErrorCode::InvalidFormat);
      }
    });
    run("pre-cancel-and-stop-after-exact-real-read", [] {
      for (const auto action : {Action::Stop, Action::Io, Action::StopAndIo}) {
        Area area; area.tree(); area.write(area.day(), Valid); area.hold(); const auto before = stamp(area.day());
        std::stop_source stop; struct stat info{}; check(::stat(area.day().c_str(), &info) == 0, "read arm stat failed");
        ReadArm state{info.st_dev, info.st_ino, action, action == Action::Io ? nullptr : &stop, nullptr, nullptr, {}};
        { Arm arm(state); error(storage::readSavedHistoryDayView(*area.lease, Date, stop.get_token()),
            action == Action::Io ? storage::ErrorCode::Io : storage::ErrorCode::Cancelled); }
        check(state.fired && same(before, stamp(area.day())), "read gate did not observe unchanged real target");
      }
      Area area; area.hold(); std::stop_source stop; stop.request_stop();
      error(storage::readSavedHistoryDayView(*area.lease, Date, stop.get_token()), storage::ErrorCode::Cancelled);
    });
    run("replace-and-truncate-during-real-read-fail", [] {
      for (const auto action : {Action::ReplaceFile, Action::Truncate, Action::ReplaceMonth}) {
        Area area; area.tree(); area.write(area.day(), Valid); area.hold(); struct stat info{};
        check(::stat(area.day().c_str(), &info) == 0, "read arm stat failed");
        const auto path = action == Action::ReplaceMonth ? area.month().string() : area.day().string();
        const auto moved = path + ".owned-moved";
        if (action == Action::ReplaceMonth) {
          area.dirs.push_back(moved); area.files.push_back(std::filesystem::path(moved) / "02.dat");
        } else if (action == Action::ReplaceFile) { area.files.push_back(moved); }
        ReadArm state{info.st_dev, info.st_ino, action, nullptr, path.c_str(), moved.c_str(), Valid};
        { Arm arm(state); error(storage::readSavedHistoryDayView(*area.lease, Date), storage::ErrorCode::Conflict); }
        check(state.fired && state.mutationOk, "replacement gate did not mutate exact generated target");
      }
    });
    std::cout << "PASS " << cases << " saved-history reader families; " << assertions
              << " assertions; generated held profiles only, no scans/writes/live history claims\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL " << current << ": " << e.what() << '\n'; return 1; }
}
