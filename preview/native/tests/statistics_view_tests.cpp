#include "statistics_view.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
using namespace biglybtc;
std::size_t assertions = 0;
void require(bool condition, std::string_view message) {
  ++assertions;
  if (!condition) { throw std::runtime_error(std::string(message)); }
}
struct ReadArm {
  dev_t device;
  ino_t inode;
  std::stop_source* source;
  std::size_t triggerAfter = 1;
  std::size_t matches = 0;
  std::size_t calls = 0;
  int injectedError = 0;
};
thread_local ReadArm* armedRead = nullptr;
struct Arm final {
  explicit Arm(ReadArm& value) { require(!armedRead, "read gate already armed"); armedRead = &value; }
  ~Arm() { armedRead = nullptr; }
  Arm(const Arm&) = delete;
  Arm& operator=(const Arm&) = delete;
};
} // namespace

extern "C" ssize_t __real_read(int descriptor, void* buffer, size_t count);
extern "C" ssize_t __wrap_read(int descriptor, void* buffer, size_t count) {
  const auto result = __real_read(descriptor, buffer, count);
  auto* armed = armedRead;
  if (armed) {
    ++armed->calls;
    struct stat info{};
    if (result > 0 && ::fstat(descriptor, &info) == 0 && info.st_dev == armed->device && info.st_ino == armed->inode) {
      ++armed->matches;
      if (armed->matches < armed->triggerAfter) { return result; }
      armedRead = nullptr;
      // Actual bytes were read from this exact generated inode. The adapter
      // checks cancellation after each bounded readFile call, before projection.
      if (armed->source) { armed->source->request_stop(); }
      if (armed->injectedError) { errno = armed->injectedError; return -1; }
    }
  }
  return result;
}

namespace {
struct Fd final {
  int value;
  explicit Fd(int descriptor) : value(descriptor) {}
  ~Fd() { if (value >= 0) { ::close(value); } }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
std::string readBytes(const std::filesystem::path& path) {
  Fd file(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  require(file.value >= 0, "unable to read explicit test file");
  std::string output;
  std::array<char, 8192> buffer{};
  for (;;) {
    const auto count = ::read(file.value, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) { continue; }
    require(count >= 0, "explicit test-file read failed");
    if (count == 0) { return output; }
    output.append(buffer.data(), static_cast<std::size_t>(count));
  }
}
struct Stamp {
  dev_t device;
  ino_t inode;
  mode_t mode;
  uid_t uid;
  gid_t gid;
  nlink_t links;
  off_t size;
  timespec modified;
  timespec changed;
  std::string bytes;
};
std::optional<Stamp> stamp(const std::filesystem::path& path) {
  struct stat info{};
  if (::lstat(path.c_str(), &info) != 0) {
    require(errno == ENOENT, "explicit test pathname inspection failed");
    return std::nullopt;
  }
  std::string content;
  if (S_ISREG(info.st_mode)) { content = readBytes(path); }
  else if (S_ISLNK(info.st_mode)) {
    std::array<char, 4096> target{};
    const auto count = ::readlink(path.c_str(), target.data(), target.size());
    require(count >= 0 && static_cast<std::size_t>(count) < target.size(), "explicit test symlink inspection failed");
    content.assign(target.data(), static_cast<std::size_t>(count));
  }
  return Stamp{info.st_dev, info.st_ino, info.st_mode, info.st_uid, info.st_gid, info.st_nlink,
      info.st_size, info.st_mtim, info.st_ctim, std::move(content)};
}
bool sameTime(const timespec& left, const timespec& right) { return left.tv_sec == right.tv_sec && left.tv_nsec == right.tv_nsec; }
bool same(const std::optional<Stamp>& left, const std::optional<Stamp>& right) {
  if (left.has_value() != right.has_value()) { return false; }
  if (!left) { return true; }
  // Reading may update atime. No adapter write, replacement, link or metadata
  // change is allowed; device/inode/type/mode/uid/gid/nlink/size/mtime/ctime stay.
  return left->device == right->device && left->inode == right->inode && left->mode == right->mode
      && left->uid == right->uid && left->gid == right->gid && left->links == right->links && left->size == right->size
      && sameTime(left->modified, right->modified) && sameTime(left->changed, right->changed) && left->bytes == right->bytes;
}
struct Observed {
  std::filesystem::path path;
  std::optional<Stamp> before;
};
void unchanged(const std::vector<Observed>& observed) {
  for (const auto& value : observed) { require(same(value.before, stamp(value.path)), "read changed supplied bytes/identity/metadata"); }
}
struct Area final {
  std::filesystem::path root;
  std::filesystem::path profile;
  std::vector<std::filesystem::path> files;
  std::vector<std::filesystem::path> directories;
  std::optional<storage::ProfileLease> lease;
  Area() {
    try {
      char pattern[] = "/tmp/biglybtc-statistics-view-tests-XXXXXX";
      const auto* created = ::mkdtemp(pattern);
      require(created, "unable to create private generated test root");
      root = created;
      profile = root / "profile";
      directory(profile);
      track(profile / ".azlock");
    } catch (...) { cleanup(); throw; }
  }
  ~Area() { cleanup(); }
  Area(const Area&) = delete;
  Area& operator=(const Area&) = delete;
  void track(const std::filesystem::path& path) {
    if (std::find(files.begin(), files.end(), path) == files.end()) { files.push_back(path); }
  }
  void directory(const std::filesystem::path& path) {
    directories.push_back(path);
    require(::mkdir(path.c_str(), 0700) == 0, "unable to create explicit owned directory");
  }
  void write(const std::filesystem::path& path, std::string_view data) {
    track(path);
    Fd file(::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    require(file.value >= 0, "unable to create exclusive generated input");
    std::size_t offset = 0;
    while (offset < data.size()) {
      const auto count = ::write(file.value, data.data() + offset, data.size() - offset);
      if (count < 0 && errno == EINTR) { continue; }
      require(count > 0, "unable to write generated input");
      offset += static_cast<std::size_t>(count);
    }
    require(::fsync(file.value) == 0, "unable to sync generated input");
  }
  void symlink(const std::filesystem::path& path, const std::filesystem::path& target) {
    track(path);
    require(::symlink(target.c_str(), path.c_str()) == 0, "unable to create explicit owned symlink");
  }
  void hold() {
    auto acquired = storage::ProfileLease::acquire(profile);
    require(acquired.has_value(), "unable to acquire generated profile lease");
    lease.emplace(std::move(*acquired));
  }
  const storage::ProfileLease& held() const { return *lease; }
  std::vector<Observed> observe(std::string_view filename) const {
    std::vector<Observed> result;
    for (const auto* suffix : {"", ".bak", ".saving"}) {
      const auto path = profile / (std::string(filename) + suffix);
      result.push_back({path, stamp(path)});
    }
    for (const auto& path : files) { result.push_back({path, stamp(path)}); }
    for (const auto& path : directories) { result.push_back({path, stamp(path)}); }
    result.push_back({root, stamp(root)});
    return result;
  }
private:
  void cleanup() noexcept {
    lease.reset();
    for (auto it = files.rbegin(); it != files.rend(); ++it) { (void)::unlink(it->c_str()); }
    for (auto it = directories.rbegin(); it != directories.rend(); ++it) { (void)::rmdir(it->c_str()); }
    if (!root.empty()) { (void)::rmdir(root.c_str()); }
  }
};

// Source-derived generated fixtures, not Java equality, live stats, profile
// migration or a writer. The fixed filename is original APP_NAME BiglyBT.
constexpr const char* Filename = "BiglyBT.statistics";
constexpr std::array<std::string_view, 13> Keys{"downloaded", "uploaded", "uptime", "mark_time", "mark_downloaded",
    "mark_uploaded", "mark_uptime", "dht_down", "dht_up", "p_uploaded", "d_uploaded", "p_downloaded", "d_downloaded"};
constexpr std::array<std::int64_t, 13> Values{101, std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::min(),
    1700000000000LL, 17, -9, 3, 0, 999, 42, 55, -1, 777};
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string integer(std::int64_t value) { return 'i' + std::to_string(value) + 'e'; }
std::string map(std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  std::string result = "d";
  for (const auto& [key, value] : fields) { result += bytes(key) + value; }
  return result + 'e';
}
std::string full(std::int64_t downloaded = Values[0]) {
  std::string all = "d";
  for (std::size_t i = 0; i < Keys.size(); ++i) { all += bytes(Keys[i]) + integer(i == 0 ? downloaded : Values[i]); }
  all += bytes("future-field") + bytes(std::string("unknown\0\xff", 9)) + 'e';
  return map({{"all", all}, {"opaque", bytes(std::string("\0\xff\r\n", 4))},
      {"untrusted-path", bytes("/an/inert/nonexistent/statistics/path")}});
}
void loaded(const storage::SavedStatisticsView& view, storage::RecoverySource source, std::string_view raw) {
  require(!view.missing() && view.snapshot && view.recoverySource == source, "loaded statistics presence/source differs");
  require(view.snapshot->source && view.snapshot->source->originalBytes() == raw && view.snapshot->source->serialize() == raw,
      "statistics reader lost/rewrote owned raw source bytes");
}
void error(const std::expected<storage::SavedStatisticsView, storage::Error>& result, storage::ErrorCode code) {
  require(!result && result.error().code == code, "statistics read error category differs");
}
void unsafe(Area& area, const std::filesystem::path& path, int kind) {
  if (kind == 0) { area.symlink(path, area.root / "sentinel"); }
  else if (kind == 1) { area.directory(path); }
  else { area.track(path); require(::mkfifo(path.c_str(), 0600) == 0, "unable to create owned FIFO fixture"); }
}
void preserved(const Area& area, const std::vector<Observed>& before, int descriptor) {
  unchanged(before);
  require(area.held().valid() && area.held().directoryHandle() == descriptor, "reader consumed/replaced borrowed held lease");
}
std::string atSize(std::size_t total) {
  std::size_t payload = total;
  for (int i = 0; i < 20; ++i) {
    const auto prefix = "d6:opaque" + std::to_string(payload) + ':';
    const auto next = total - prefix.size() - 1;
    if (next == payload) { return prefix + std::string(payload, 'x') + 'e'; }
    payload = next;
  }
  throw std::runtime_error("unable to construct exact byte-boundary fixture");
}
std::string nodes(std::size_t count) {
  std::string result = "d7:unknownl";
  for (std::size_t i = 3; i < count; ++i) { result += "i0e"; }
  return result + "ee"; // root + byte key + list + integer nodes
}
std::string depth(std::size_t deepest) {
  return "d7:unknown" + std::string(deepest, 'l') + std::string(deepest, 'e') + 'e';
}
} // namespace

int main() {
  std::string active; std::size_t cases = 0;
  try {
    const auto run = [&](std::string name, auto body) { active = std::move(name); body(); ++cases; };
    run("all three observed absent means Missing with no fabricated snapshot", [&] {
      Area area; area.hold(); const auto before = area.observe(Filename); const int fd = area.held().directoryHandle();
      const auto result = storage::readSavedStatisticsView(area.held());
      require(result && result->missing() && !result->snapshot && !result->recoverySource, "absence became saved zeros or loaded empty");
      preserved(area, before, fd);
    });
    run("thirteen raw signed fields plus unknown binary source are owned unchanged", [&] {
      Area area; const auto raw = full(); area.write(area.profile / Filename, raw);
      area.write(area.root / "sentinel", "unrelated generated bytes"); area.hold(); const auto before = area.observe(Filename); const int fd = area.held().directoryHandle();
      const auto result = storage::readSavedStatisticsView(area.held()); require(result.has_value(), "full fixture read failed");
      loaded(*result, storage::RecoverySource::Primary, raw); require(result->snapshot->hasAll, "present all dictionary lost");
      for (std::size_t i = 0; i < Values.size(); ++i) {
        require(result->snapshot->values[i].value == Values[i] && !result->snapshot->values[i].invalidType, "raw signed field narrowed/defaulted/reordered");
      }
      preserved(area, before, fd);
    });
    for (const bool hasAll : {false, true}) { run("present empty document differs from Missing " + std::to_string(hasAll), [&] {
      Area area; const auto raw = hasAll ? std::string("d3:alldee") : std::string("de"); area.write(area.profile / Filename, raw); area.hold();
      const auto before = area.observe(Filename); const auto result = storage::readSavedStatisticsView(area.held());
      require(result.has_value(), "empty dictionary was rejected"); loaded(*result, storage::RecoverySource::Primary, raw);
      require(result->snapshot->hasAll == hasAll, "all presence was fabricated");
      for (const auto& value : result->snapshot->values) { require(!value.value && !value.invalidType, "missing scalar became observed zero/type error"); }
      preserved(area, before, area.held().directoryHandle());
    }); }
    run("wrong individual scalar type stays a Primary fact and does not recover", [&] {
      Area area; const auto raw = map({{"all", map({{"downloaded", bytes("wrong")}, {"uploaded", integer(-17)}})}});
      area.write(area.profile / Filename, raw); area.write(area.profile / "BiglyBT.statistics.bak", full(456)); area.hold(); const auto before = area.observe(Filename);
      const auto result = storage::readSavedStatisticsView(area.held()); require(result.has_value(), "wrong scalar poisoned complete dictionary"); loaded(*result, storage::RecoverySource::Primary, raw);
      require(!result->snapshot->values[0].value && result->snapshot->values[0].invalidType
          && result->snapshot->values[1].value == -17, "wrong scalar diagnostics/raw valid peer field changed");
      preserved(area, before, area.held().directoryHandle());
    });
    struct RecoveryCase { int primary; int backup; int saving; int expected; };
    // 0 absent, 1 valid, 2 malformed; expected1Primary/2Backup/3Saving/4Invalid.
    constexpr std::array<RecoveryCase, 12> recovery{{{1,1,1,1}, {0,1,1,3}, {2,1,1,2}, {2,2,1,3},
        {2,0,1,3}, {0,1,2,2}, {0,2,0,4}, {0,0,2,4}, {2,0,0,4}, {0,2,2,4}, {2,2,2,4}, {0,1,0,2}}};
    for (std::size_t row = 0; row < recovery.size(); ++row) { run("source recovery candidate order " + std::to_string(row), [&] {
      Area area; const auto value = recovery[row]; const int kinds[]{value.primary, value.backup, value.saving};
      const char* names[]{Filename, "BiglyBT.statistics.bak", "BiglyBT.statistics.saving"};
      for (int i = 0; i < 3; ++i) { if (kinds[i]) { area.write(area.profile / names[i], kinds[i] == 1 ? full(1000 + i) : "malformed"); } }
      area.hold(); const auto before = area.observe(Filename); const int fd = area.held().directoryHandle(); const auto result = storage::readSavedStatisticsView(area.held());
      if (value.expected == 4) { error(result, storage::ErrorCode::InvalidFormat); }
      else {
        const int selected = value.expected == 1 ? 0 : value.expected == 2 ? 1 : 2;
        require(result.has_value(), "valid recovery fixture was rejected");
        loaded(*result, selected == 0 ? storage::RecoverySource::Primary : selected == 1 ? storage::RecoverySource::Backup : storage::RecoverySource::Saving, full(1000 + selected));
        require(result->snapshot->values[0].value == 1000 + selected, "recovery projection selected another candidate's counters");
      }
      preserved(area, before, fd);
    }); }
    for (const auto& bad : {std::string{}, std::string("invalid"), std::string("le"), std::string("i7e"), std::string("3:abc"), std::string("d1:ai1e1:ai2ee")}) {
      run("malformed Primary permits complete Backup " + std::to_string(bad.size()), [&] {
        Area area; area.write(area.profile / Filename, bad); area.write(area.profile / "BiglyBT.statistics.bak", full(987)); area.hold(); const auto before = area.observe(Filename);
        const auto result = storage::readSavedStatisticsView(area.held()); require(result.has_value(), "document failure did not permit recovery"); loaded(*result, storage::RecoverySource::Backup, full(987));
        preserved(area, before, area.held().directoryHandle());
      });
    }
    for (const auto& badAll : {std::string("le"), integer(0), bytes("bad")}) { for (const bool primary : {false, true}) {
      run("wrong all is semantic failure without fallback " + std::to_string(badAll.size()) + '/' + std::to_string(primary), [&] {
        Area area; area.write(area.profile / (primary ? Filename : "BiglyBT.statistics.saving"), map({{"all", badAll}}));
        area.write(area.profile / "BiglyBT.statistics.bak", full(909)); area.hold(); const auto before = area.observe(Filename);
        error(storage::readSavedStatisticsView(area.held()), storage::ErrorCode::InvalidFormat);
        preserved(area, before, area.held().directoryHandle());
      });
    } }
    for (const bool empty : {false, true}) { for (int kind = 0; kind < 3; ++kind) { run("valid Primary ignores unsafe unused recovery " + std::to_string(empty) + '/' + std::to_string(kind), [&] {
      Area area; const auto raw = empty ? std::string("de") : full(); area.write(area.profile / Filename, raw); area.write(area.root / "sentinel", "ignored recovery target");
      unsafe(area, area.profile / "BiglyBT.statistics.saving", kind); unsafe(area, area.profile / "BiglyBT.statistics.bak", kind);
      area.hold(); const auto before = area.observe(Filename); const auto result = storage::readSavedStatisticsView(area.held());
      require(result.has_value(), "unused unsafe recovery overrode valid Primary"); loaded(*result, storage::RecoverySource::Primary, raw);
      preserved(area, before, area.held().directoryHandle());
    }); } }
    for (int stage = 0; stage < 3; ++stage) { for (int kind = 0; kind < 3; ++kind) { run("unsafe visited candidate is fatal " + std::to_string(stage) + '/' + std::to_string(kind), [&] {
      Area area; area.write(area.root / "sentinel", full(222));
      const char* target = stage == 0 ? Filename : stage == 1 ? "BiglyBT.statistics.saving" : "BiglyBT.statistics.bak";
      if (stage == 2) { area.write(area.profile / Filename, "malformed"); area.write(area.profile / "BiglyBT.statistics.saving", full(333)); }
      else { area.write(area.profile / "BiglyBT.statistics.bak", full(444)); }
      unsafe(area, area.profile / target, kind); area.hold(); const auto before = area.observe(Filename);
      error(storage::readSavedStatisticsView(area.held()), storage::ErrorCode::UnsafePath);
      preserved(area, before, area.held().directoryHandle());
    }); } }
    run("preferred valid recovery ignores later unsafe candidate", [&] {
      Area area; area.write(area.profile / Filename, "malformed"); area.write(area.profile / "BiglyBT.statistics.bak", full(211)); area.symlink(area.profile / "BiglyBT.statistics.saving", area.root / "not-created"); area.hold();
      const auto before = area.observe(Filename); const auto result = storage::readSavedStatisticsView(area.held()); require(result.has_value(), "later unsafe leaf overrode valid Backup"); loaded(*result, storage::RecoverySource::Backup, full(211)); preserved(area, before, area.held().directoryHandle());
    });
    for (int stage = 0; stage < 3; ++stage) { run("oversized visited candidate never falls back " + std::to_string(stage), [&] {
      Area area; const char* name = stage == 0 ? Filename : stage == 1 ? "BiglyBT.statistics.saving" : "BiglyBT.statistics.bak";
      if (stage == 2) { area.write(area.profile / Filename, "malformed"); area.write(area.profile / "BiglyBT.statistics.saving", full(333)); }
      else { area.write(area.profile / "BiglyBT.statistics.bak", full(444)); }
      area.write(area.profile / name, atSize(core::Limits{}.inputBytes + 1)); area.hold(); const auto before = area.observe(Filename);
      error(storage::readSavedStatisticsView(area.held()), storage::ErrorCode::InvalidFormat); preserved(area, before, area.held().directoryHandle());
    }); }
    for (const bool limitNodes : {false, true}) { run("bounded document parser limit permits older bounded recovery " + std::to_string(limitNodes), [&] {
      Area area; area.write(area.profile / Filename, limitNodes ? nodes(100001) : depth(65)); area.write(area.profile / "BiglyBT.statistics.bak", full(311)); area.hold(); const auto before = area.observe(Filename);
      const auto result = storage::readSavedStatisticsView(area.held()); require(result.has_value(), "parser whole bound did not permit recovery"); loaded(*result, storage::RecoverySource::Backup, full(311)); preserved(area, before, area.held().directoryHandle());
    }); }
    for (int bound = 0; bound < 3; ++bound) { run("ordinary whole bound is inclusive " + std::to_string(bound), [&] {
      Area area; const auto raw = bound == 0 ? atSize(core::Limits{}.inputBytes) : bound == 1 ? nodes(100000) : depth(64);
      area.write(area.profile / Filename, raw); area.hold(); const auto before = area.observe(Filename); const auto result = storage::readSavedStatisticsView(area.held());
      require(result.has_value(), "exact whole document bound was rejected"); loaded(*result, storage::RecoverySource::Primary, raw); require(!result->snapshot->hasAll, "unknown bounded fields fabricated all"); preserved(area, before, area.held().directoryHandle());
    }); }
    run("invalid lease fails safely and pre-stop wins before any read", [&] {
      Area area; area.write(area.profile / Filename, full()); area.hold(); auto moved = std::move(*area.lease); const auto before = area.observe(Filename);
      error(storage::readSavedStatisticsView(area.held()), storage::ErrorCode::UnsafePath);
      std::stop_source stop; stop.request_stop(); ReadArm gate{0,0,&stop};
      { Arm armed(gate); error(storage::readSavedStatisticsView(area.held(), stop.get_token()), storage::ErrorCode::Cancelled); }
      require(gate.calls == 0 && moved.valid(), "prestop touched a file or invalid read consumed live lease"); unchanged(before);
    });
    for (int stage = 0; stage < 3; ++stage) { run("cancellation after actual matched candidate read discards result " + std::to_string(stage), [&] {
      Area area; const char* name = stage == 0 ? Filename : stage == 1 ? "BiglyBT.statistics.saving" : "BiglyBT.statistics.bak";
      if (stage == 2) { area.write(area.profile / Filename, "malformed"); }
      area.write(area.profile / name, full()); area.hold(); const auto before = area.observe(Filename); const auto target = stamp(area.profile / name); std::stop_source stop;
      ReadArm gate{target->device,target->inode,&stop};
      { Arm armed(gate); error(storage::readSavedStatisticsView(area.held(), stop.get_token()), storage::ErrorCode::Cancelled); }
      require(gate.matches == 1 && stop.stop_requested(), "cancellation did not follow a real read of exact generated inode"); preserved(area, before, area.held().directoryHandle());
    }); }
    run("actual matched read error is fatal instead of silently recovering", [&] {
      Area area; area.write(area.profile / Filename, full()); area.write(area.profile / "BiglyBT.statistics.bak", full(300)); area.hold(); const auto before = area.observe(Filename); const auto target = stamp(area.profile / Filename);
      ReadArm gate{target->device,target->inode,nullptr,1,0,0,EIO};
      { Arm armed(gate); error(storage::readSavedStatisticsView(area.held()), storage::ErrorCode::Io); }
      require(gate.matches == 1, "read-error injection missed exact owned inode"); preserved(area, before, area.held().directoryHandle());
    });
    run("snapshot outlives caller generated profile and held lease", [&] {
      std::optional<storage::SavedStatisticsView> retained;
      { Area area; area.write(area.profile / Filename, full()); area.hold(); auto value = storage::readSavedStatisticsView(area.held()); require(value.has_value(), "owned lifetime read failed"); retained.emplace(std::move(*value)); }
      loaded(*retained, storage::RecoverySource::Primary, full()); require(retained->snapshot->values[1].value == Values[1], "owned lifetime field lost");
    });
    run("fresh read after explicit replacement leaves previous snapshot independent", [&] {
      Area area; area.write(area.profile / Filename, full(100)); area.hold(); const auto old = storage::readSavedStatisticsView(area.held()); require(old.has_value(), "initial read failed");
      require(::unlink((area.profile / Filename).c_str()) == 0, "unable to replace explicit generated input"); area.write(area.profile / Filename, full(200)); const auto before = area.observe(Filename);
      const auto current = storage::readSavedStatisticsView(area.held()); require(current.has_value(), "fresh read failed"); loaded(*old, storage::RecoverySource::Primary, full(100)); loaded(*current, storage::RecoverySource::Primary, full(200));
      require(old->snapshot->source.get() != current->snapshot->source.get(), "two reads borrowed a shared mutable document"); preserved(area, before, area.held().directoryHandle());
    });
    run("only exact original fixed leaf is read and ConfigFile config rule stays closed", [&] {
      Area area; area.write(area.profile / "biglybt.statistics", full(123)); area.write(area.profile / "BiglyBT.statistics.config", full(456)); area.hold(); const auto before = area.observe(Filename);
      const auto missing = storage::readSavedStatisticsView(area.held()); require(missing && missing->missing(), "reader used lowercase/appended config alias");
      const auto generic = storage::ConfigFile::load(area.held(), Filename, core::Limits{});
      require(!generic && generic.error().code == storage::ErrorCode::UnsafePath, "statistics reader widened ConfigFile filename validator"); preserved(area, before, area.held().directoryHandle());
    });
    std::cout << "PASS " << cases << " saved statistics reader cases; " << assertions << " assertions\n"; return 0;
  } catch (const std::exception& failure) { std::cerr << "FAIL " << active << ": " << failure.what() << '\n'; return 1; }
}
