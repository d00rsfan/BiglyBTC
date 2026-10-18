#include "saved_service_view.hpp"

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
  std::size_t matches = 0;
  std::size_t calls = 0;
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
      armedRead = nullptr;
      // The actual qualified reader has returned bytes. Cancellation is checked
      // after ConfigFile::load as a whole, not by interrupting an in-flight read.
      if (armed->source) { armed->source->request_stop(); }
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
      char pattern[] = "/tmp/biglybtc-saved-service-view-tests-XXXXXX";
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
    for (const auto& path : {profile / ".azlock", profile, root / "sentinel", profile / "shares"}) { result.push_back({path, stamp(path)}); }
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
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string map(std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  std::string output = "d";
  for (const auto& [key, value] : fields) { output += bytes(key) + value; }
  return output + 'e';
}
std::string config(std::string_view key, std::string_view marker, std::string records = "le") {
  return map({{key, std::move(records)}, {"marker", bytes(marker)}, {"unknown", bytes(std::string("\0\xff", 2))}});
}
template<class View>
void loaded(const View& view, storage::RecoverySource source, std::string_view raw) {
  require(!view.missing() && view.snapshot && view.recoverySource == source, "saved-service source/presence differs");
  require(view.snapshot->source && view.snapshot->source->serialize() == raw, "loaded source bytes/order changed");
}
} // namespace

int main() {
  std::string active;
  std::size_t cases = 0;
  try {
    const auto run = [&](std::string name, auto body) { active = std::move(name); body(); ++cases; };
    const auto services = [&](auto body) {
      body(&storage::readSavedHostingView, std::string("tracker.config"), std::string("torrents"));
      body(&storage::readSavedSharesView, std::string("sharing.config"), std::string("resources"));
    };
    services([&](auto read, const std::string& filename, const std::string& key) {
      run(filename + " absent/empty/list-absent", [&] {
        Area area;
        area.hold();
        const auto before = area.observe(filename);
        auto missing = read(area.held(), {});
        require(missing && missing->missing() && !missing->snapshot && !missing->recoverySource, "absent service was fabricated or failed");
        unchanged(before);
        const auto raw = config(key, "empty");
        area.write(area.profile / filename, raw);
        const auto supplied = area.observe(filename);
        auto empty = read(area.held(), {});
        require(empty.has_value(), "valid empty config failed");
        loaded(*empty, storage::RecoverySource::Primary, raw);
        require(empty->snapshot->listPresent && empty->snapshot->records.empty(), "valid empty list was marked missing");
        unchanged(supplied);
      });
      run(filename + " valid root missing list", [&] {
        Area area;
        area.write(area.profile / filename, "de");
        area.hold();
        const auto before = area.observe(filename);
        auto view = read(area.held(), {});
        require(view && !view->missing() && view->snapshot && !view->snapshot->listPresent, "missing list was confused with missing file");
        require(!view->snapshot->issues.empty(), "missing list diagnostic disappeared");
        loaded(*view, storage::RecoverySource::Primary, "de");
        unchanged(before);
      });
      struct RecoveryCase { const char* name; const char* primary; const char* backup; const char* saving; storage::RecoverySource source; const char* selected; };
      const RecoveryCase recovery[]{
          {"primary wins", "valid", "valid", "valid", storage::RecoverySource::Primary, "primary"},
          {"absent primary prefers saving", nullptr, "valid", "valid", storage::RecoverySource::Saving, "saving"},
          {"damaged primary prefers backup", "bad", "valid", "valid", storage::RecoverySource::Backup, "backup"},
          {"damaged primary fallback saving", "bad", nullptr, "valid", storage::RecoverySource::Saving, "saving"},
          {"damaged primary skips bad backup", "bad", "bad", "valid", storage::RecoverySource::Saving, "saving"},
          {"absent primary fallback backup", nullptr, "valid", nullptr, storage::RecoverySource::Backup, "backup"},
          {"absent primary skips bad saving", nullptr, "valid", "bad", storage::RecoverySource::Backup, "backup"}};
      for (const auto& row : recovery) {
        run(filename + ' ' + row.name, [&] {
          Area area;
          const auto add = [&](const char* state, const char* suffix, const char* marker) {
            if (state) { area.write(area.profile / (filename + suffix), std::string_view(state) == "valid" ? config(key, marker) : "damaged"); }
          };
          add(row.primary, "", "primary"); add(row.backup, ".bak", "backup"); add(row.saving, ".saving", "saving");
          area.write(area.root / "sentinel", "external synthetic sentinel");
          area.hold();
          const auto before = area.observe(filename);
          auto view = read(area.held(), {});
          require(view.has_value(), "valid recovery candidate failed");
          loaded(*view, row.source, config(key, row.selected));
          unchanged(before);
        });
      }
      for (const auto* suffix : {".bak", ".saving"}) {
        run(filename + " absent primary malformed " + suffix, [&] {
          Area area;
          area.write(area.profile / (filename + suffix), "damaged");
          area.hold();
          const auto before = area.observe(filename);
          auto view = read(area.held(), {});
          require(!view && view.error().code == storage::ErrorCode::InvalidFormat, "malformed recovery candidate was reported missing");
          unchanged(before);
        });
      }
      run(filename + " valid primary ignores unsafe candidates", [&] {
        Area area;
        const auto raw = config(key, "primary");
        area.write(area.profile / filename, raw);
        area.write(area.root / "sentinel", "synthetic outside-profile sentinel");
        area.symlink(area.profile / (filename + ".bak"), area.root / "sentinel");
        area.directory(area.profile / (filename + ".saving"));
        area.hold();
        const auto before = area.observe(filename);
        auto view = read(area.held(), {});
        require(view.has_value(), "valid primary unnecessarily read unsafe recovery input");
        loaded(*view, storage::RecoverySource::Primary, raw);
        unchanged(before);
      });
      run(filename + " valid saving ignores unsafe later backup", [&] {
        Area area;
        const auto raw = config(key, "saving");
        area.write(area.profile / (filename + ".saving"), raw);
        area.write(area.root / "sentinel", "synthetic sentinel");
        area.symlink(area.profile / (filename + ".bak"), area.root / "sentinel");
        area.hold();
        const auto before = area.observe(filename);
        auto view = read(area.held(), {});
        require(view.has_value(), "saving-first recovery unnecessarily read later unsafe backup");
        loaded(*view, storage::RecoverySource::Saving, raw);
        unchanged(before);
      });
      run(filename + " unsafe primary never falls back", [&] {
        Area area;
        area.write(area.root / "sentinel", "synthetic sentinel");
        area.symlink(area.profile / filename, area.root / "sentinel");
        area.write(area.profile / (filename + ".bak"), config(key, "backup"));
        area.hold();
        const auto before = area.observe(filename);
        auto view = read(area.held(), {});
        require(!view && view.error().code == storage::ErrorCode::UnsafePath, "unsafe primary was followed or bypassed");
        unchanged(before);
      });
      run(filename + " FIFO is rejected without blocking", [&] {
        Area area;
        area.track(area.profile / filename);
        require(::mkfifo((area.profile / filename).c_str(), 0600) == 0, "unable to create exact owned FIFO");
        area.hold();
        const auto before = area.observe(filename);
        auto view = read(area.held(), {});
        require(!view && view.error().code == storage::ErrorCode::UnsafePath, "nonregular config was accepted");
        unchanged(before);
      });
      run(filename + " wrong root and list are not empty", [&] {
        for (const auto& raw : {std::string("le"), map({{key, "i1e"}})}) {
          Area area;
          area.write(area.profile / filename, raw);
          area.hold();
          const auto before = area.observe(filename);
          auto view = read(area.held(), {});
          require(!view && view.error().code == storage::ErrorCode::InvalidFormat, "wrong shape was displayed as empty");
          unchanged(before);
        }
      });
      run(filename + " wrong list primary does not switch to backup", [&] {
        Area area;
        area.write(area.profile / filename, map({{key, "i1e"}}));
        area.write(area.profile / (filename + ".bak"), config(key, "backup"));
        area.hold();
        const auto before = area.observe(filename);
        auto view = read(area.held(), {});
        require(!view && view.error().code == storage::ErrorCode::InvalidFormat, "projection error selected a different saved document");
        unchanged(before);
      });
      run(filename + " fixed small input and projection bounds", [&] {
        Area area;
        const auto path = area.profile / filename;
        area.write(path, "de");
        {
          Fd file(::open(path.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW));
          require(file.value >= 0 && ::ftruncate(file.value, static_cast<off_t>(core::Limits{}.inputBytes + 1)) == 0, "unable to prepare exact oversized input");
        }
        area.hold();
        const auto before = area.observe(filename);
        auto view = read(area.held(), {});
        require(!view && view.error().code == storage::ErrorCode::InvalidFormat, "saved view used permissive192MiB config limit");
        unchanged(before);
        Area rows;
        std::string list = "l";
        for (int i = 0; i <= 10000; ++i) { list += "de"; }
        list += 'e';
        rows.write(rows.profile / filename, config(key, "over-row-cap", std::move(list)));
        rows.hold();
        const auto rowBefore = rows.observe(filename);
        auto bounded = read(rows.held(), {});
        require(!bounded && bounded.error().code == storage::ErrorCode::Unsupported, "projection limit mapping differs");
        unchanged(rowBefore);
      });
      run(filename + " borrowed lease and moved-from rejection", [&] {
        Area area;
        area.write(area.profile / filename, config(key, "held"));
        area.hold();
        auto conflict = storage::ProfileLease::acquire(area.profile);
        require(!conflict && conflict.error().code == storage::ErrorCode::Busy, "test lease does not hold compatible exclusion");
        auto view = read(area.held(), {});
        require(view.has_value(), "adapter attempted to reacquire held lease");
        auto valid = std::move(*area.lease);
        require(valid.valid() && !area.held().valid(), "moved lease setup failed");
        const auto before = area.observe(filename);
        auto invalid = read(area.held(), {});
        require(!invalid && invalid.error().code == storage::ErrorCode::UnsafePath, "invalid lease was read");
        std::stop_source cancelled;
        cancelled.request_stop();
        auto stopped = read(area.held(), cancelled.get_token());
        require(!stopped && stopped.error().code == storage::ErrorCode::Cancelled, "prestop did not win before invalid lease access");
        unchanged(before);
      });
      run(filename + " deterministic pre/post-read cancellation", [&] {
        Area area;
        const auto raw = config(key, "cancelled");
        area.write(area.profile / filename, raw);
        area.hold();
        const auto before = area.observe(filename);
        const auto identity = stamp(area.profile / filename);
        require(identity.has_value(), "cancellation target is absent");
        std::stop_source pre;
        pre.request_stop();
        ReadArm preArm{identity->device, identity->inode, &pre};
        {
          Arm gate(preArm);
          auto result = read(area.held(), pre.get_token());
          require(!result && result.error().code == storage::ErrorCode::Cancelled, "prestop was ignored");
        }
        require(preArm.calls == 0 && preArm.matches == 0, "prestop performed config reads");
        std::stop_source post;
        ReadArm postArm{identity->device, identity->inode, &post};
        {
          Arm gate(postArm);
          auto result = read(area.held(), post.get_token());
          require(!result && result.error().code == storage::ErrorCode::Cancelled, "loaded data was published after post-read stop");
        }
        require(post.stop_requested() && postArm.matches == 1 && postArm.calls == 1, "post-read cancellation did not cross actual exact-inode read");
        unchanged(before);
      });
      run(filename + " replacement yields fresh owned results", [&] {
        Area area;
        const auto oldRaw = config(key, "old");
        const auto newRaw = config(key, "new");
        area.write(area.profile / filename, oldRaw);
        area.hold();
        auto old = read(area.held(), {});
        require(old.has_value(), "initial saved view failed");
        const auto original = stamp(area.profile / filename);
        const auto replacement = area.profile / (filename + ".replacement");
        area.write(replacement, newRaw);
        require(::rename(replacement.c_str(), (area.profile / filename).c_str()) == 0, "explicit owned config replacement failed");
        const auto current = stamp(area.profile / filename);
        require(original && current && original->inode != current->inode, "replacement did not change exact inode");
        const auto before = area.observe(filename);
        auto fresh = read(area.held(), {});
        require(fresh.has_value(), "fresh saved view failed");
        loaded(*old, storage::RecoverySource::Primary, oldRaw);
        loaded(*fresh, storage::RecoverySource::Primary, newRaw);
        require(old->snapshot->source.get() != fresh->snapshot->source.get(), "refresh reused mutable backing object");
        unchanged(before);
      });
      run(filename + " serialized paths remain inert and links unchanged", [&] {
        Area area;
        const auto sentinel = area.root / "sentinel";
        area.write(sentinel, "not-a-torrent; synthetic external sentinel");
        const auto personal = bytes("SYNTHETIC-PUBLIC-KEY-NOT-REAL");
        const auto record = filename == "tracker.config"
            ? map({{"hash", bytes(std::string(20, 'H'))}, {"status", "i2e"}, {"passive", "i1e"}, {"torrent_file", bytes(sentinel.string())}})
            : map({{"type", "i1e"}, {"file", bytes(sentinel.string())}, {"ihash", bytes(std::string(16, 'F'))}, {"ifile", bytes(sentinel.string())}, {"per_key", personal}});
        const auto raw = config(key, "inert", "l" + record + 'e');
        area.write(area.profile / filename, raw);
        area.hold();
        const auto before = area.observe(filename);
        auto view = read(area.held(), {});
        require(view && view->snapshot && view->snapshot->records.size() == 1, "inert saved path caused a cache/provider dependency");
        loaded(*view, storage::RecoverySource::Primary, raw);
        unchanged(before);
        require(!stamp(area.profile / "shares"), "read-only projection created share cache directory");
        Area linked;
        linked.write(linked.root / "sentinel", config(key, "hardlink"));
        linked.track(linked.profile / filename);
        require(::link((linked.root / "sentinel").c_str(), (linked.profile / filename).c_str()) == 0, "owned hardlink setup failed");
        linked.hold();
        const auto linkedBefore = linked.observe(filename);
        auto linkedView = read(linked.held(), {});
        require(linkedView.has_value(), "qualified read-only regular hardlink was rejected");
        require(stamp(linked.profile / filename)->links == 2, "hardlink count changed");
        unchanged(linkedBefore);
      });
    });
    std::cout << "PASS saved-service-view " << cases << " cases / " << assertions
        << " assertions; generated owned profiles, read-only source selection and byte/inode preservation; zero Java equality credit\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL saved-service-view " << active << ": " << error.what() << '\n';
    return 1;
  }
}
