#include "column_layout_view.hpp"

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
      char pattern[] = "/tmp/biglybtc-column-layout-view-tests-XXXXXX";
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

// Generated inputs and source-derived policy expectations only. These tests
// qualify the lease-bound read adapter, not original Java execution, effective
// registered-column settings, profile migration, UI application or a writer.
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string integer(std::int64_t value) { return 'i' + std::to_string(value) + 'e'; }
std::string map(std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  std::string result = "d";
  for (const auto& [key, value] : fields) { result += bytes(key) + value; }
  return result + 'e';
}
std::string settings(std::string_view marker, std::int64_t width = 311) {
  return map({{"Table.MyTorrents", map({{"Column.name", "l" + integer(1) + integer(7) + integer(width) + "e"},
      {"SortColumns", "l" + bytes("name") + "e"}})}, {"marker", bytes(marker)},
      {"opaque", bytes(std::string("\0\xff", 2))}});
}
void loaded(const storage::ColumnLayoutsView& view, storage::RecoverySource source, std::string_view raw) {
  require(!view.missing() && view.snapshot.has_value() && view.recoverySource == source,
      "loaded layout lost source or presence");
  require(view.snapshot->source && view.snapshot->source->originalBytes() == raw,
      "loaded layout raw source differs");
  require(view.snapshot->source->serialize() == raw, "read projection rewrote source bytes/order");
}
void named(const storage::ColumnLayoutsView& view, std::int64_t width = 311) {
  require(view.snapshot && view.snapshot->incomplete.columns.size() == 1, "generated named column disappeared");
  const auto& column = view.snapshot->incomplete.columns.front();
  require(column.id.bytes == "name" && column.width.value && column.width.value->raw == width,
      "source requested width/column identity was altered");
}
void error(const std::expected<storage::ColumnLayoutsView, storage::Error>& result, storage::ErrorCode code) {
  require(!result && result.error().code == code, "layout adapter error category differs");
}
void unsafe(Area& area, const std::filesystem::path& path, int kind) {
  if (kind == 0) { area.symlink(path, area.root / "sentinel"); }
  else if (kind == 1) { area.directory(path); }
  else {
    area.track(path);
    require(::mkfifo(path.c_str(), 0600) == 0, "unable to create owned nonblocking FIFO fixture");
  }
}
void proveUnchanged(const Area& area, const std::vector<Observed>& before, int descriptor) {
  unchanged(before);
  require(area.held().valid() && area.held().directoryHandle() == descriptor,
      "reader consumed or replaced borrowed lease");
}
std::string atSize(std::size_t total) {
  std::size_t payload = total;
  for (int attempt = 0; attempt < 20; ++attempt) {
    const auto prefix = "d6:opaque" + std::to_string(payload) + ':';
    require(total > prefix.size(), "bad fixed-size input fixture");
    const auto next = total - prefix.size() - 1;
    if (next == payload) { return prefix + std::string(payload, 'x') + 'e'; }
    payload = next;
  }
  throw std::runtime_error("unable to size generated opaque document");
}
std::string nodeCount(std::size_t nodes) {
  require(nodes >= 3, "bad node-count fixture");
  std::string raw = "d7:unknownl";
  raw.reserve(nodes * 3 + 20);
  for (std::size_t i = 3; i < nodes; ++i) { raw += "i0e"; }
  return raw + "ee"; // Root + key + list + the supplied number of integer nodes.
}
std::string depth(std::size_t deepest) {
  return "d7:unknown" + std::string(deepest, 'l') + std::string(deepest, 'e') + 'e';
}
} // namespace

int main() {
  std::string active;
  std::size_t cases = 0;
  try {
    const auto run = [&](std::string name, auto body) { active = std::move(name); body(); ++cases; };
    run("missing is distinct from loaded empty", [&] {
      Area area; area.hold();
      const auto before = area.observe("tables.config");
      auto missing = storage::readColumnLayoutsView(area.held());
      require(missing && missing->missing() && !missing->snapshot && !missing->recoverySource,
          "absent layout was fabricated or failed");
      proveUnchanged(area, before, area.held().directoryHandle());
      area.write(area.profile / "tables.config", "de");
      const auto supplied = area.observe("tables.config");
      auto empty = storage::readColumnLayoutsView(area.held());
      require(empty.has_value(), "valid empty dictionary failed");
      loaded(*empty, storage::RecoverySource::Primary, "de");
      require(empty->snapshot->incomplete.presence == core::ColumnLayoutPresence::Missing
          && empty->snapshot->complete.presence == core::ColumnLayoutPresence::Missing,
          "absent tables were confused with absent file");
      require(empty->snapshot->issues.empty(), "empty document fabricated diagnostics/default columns");
      proveUnchanged(area, supplied, area.held().directoryHandle());
    });
    run("loaded named data and raw opaque bytes", [&] {
      Area area; const auto raw = settings("primary");
      area.write(area.profile / "tables.config", raw); area.hold();
      const auto before = area.observe("tables.config"); const int descriptor = area.held().directoryHandle();
      auto view = storage::readColumnLayoutsView(area.held()); require(view.has_value(), "valid layout failed");
      loaded(*view, storage::RecoverySource::Primary, raw); named(*view);
      require(view->snapshot->source->find("opaque")->bytes == std::string_view("\0\xff", 2), "opaque bytes changed");
      require(view->snapshot->incomplete.origin == core::ColumnLayoutOrigin::ExactTable
          && view->snapshot->complete.origin == core::ColumnLayoutOrigin::Absent, "table origin differs");
      proveUnchanged(area, before, descriptor);
    });
    run("user defaults are separate per pane", [&] {
      Area area;
      const auto raw = map({{"TableDefault.MyTorrents", map({{"Column.name", "li1ei0ei321ee"}})},
          {"TableDefault.MySeeders", "de"}, {"Table.MyTorrents.big", map({{"Column.no-alias", "le"}})}});
      area.write(area.profile / "tables.config", raw); area.hold(); const auto before = area.observe("tables.config");
      auto view = storage::readColumnLayoutsView(area.held()); require(view.has_value(), "default-only layout failed");
      loaded(*view, storage::RecoverySource::Primary, raw); named(*view, 321);
      require(view->snapshot->incomplete.origin == core::ColumnLayoutOrigin::UserDefault
          && view->snapshot->complete.origin == core::ColumnLayoutOrigin::UserDefault, "user-default source differs");
      require(view->snapshot->complete.presence == core::ColumnLayoutPresence::Present
          && view->snapshot->complete.columns.empty(), "empty complete default became missing");
      proveUnchanged(area, before, area.held().directoryHandle());
    });
    run("malformed selected facts remain loaded", [&] {
      Area area;
      const auto raw = map({{"Table.MyTorrents", integer(9)}, {"TableDefault.MyTorrents", "de"},
          {"Table.MySeeders", map({{"Column.plugin", bytes("wrong list")}, {"SortColumns", "li0ee"}})}});
      area.write(area.profile / "tables.config", raw); area.hold(); const auto before = area.observe("tables.config");
      auto view = storage::readColumnLayoutsView(area.held()); require(view.has_value(), "diagnosed facts became file failure");
      loaded(*view, storage::RecoverySource::Primary, raw);
      require(view->snapshot->incomplete.origin == core::ColumnLayoutOrigin::ExactTable
          && view->snapshot->incomplete.presence == core::ColumnLayoutPresence::WrongType, "wrong exact type used default");
      require(view->snapshot->complete.columns.size() == 1 && !view->snapshot->issues.empty(), "malformed column was discarded");
      require(view->snapshot->complete.sortSource == core::ColumnLayoutSortSource::SortColumns
          && view->snapshot->complete.sort.size() == 1
          && view->snapshot->complete.sort.front().id.presence == core::ColumnLayoutPresence::WrongType,
          "malformed sort member was dropped or defaulted");
      proveUnchanged(area, before, area.held().directoryHandle());
    });
    run("explicit empty exact table blocks user default", [&] {
      Area area; const auto raw = map({{"Table.MyTorrents", "de"},
          {"TableDefault.MyTorrents", map({{"Column.name", "li1ei0ei400ee"}})}});
      area.write(area.profile / "tables.config", raw); area.hold(); const auto before = area.observe("tables.config");
      auto view = storage::readColumnLayoutsView(area.held()); require(view.has_value(), "empty exact table failed");
      loaded(*view, storage::RecoverySource::Primary, raw);
      require(view->snapshot->incomplete.origin == core::ColumnLayoutOrigin::ExactTable
          && view->snapshot->incomplete.columns.empty(), "default replaced explicit empty table");
      proveUnchanged(area, before, area.held().directoryHandle());
    });

    struct RecoveryCase { const char* name; const char* primary; const char* backup; const char* saving; storage::RecoverySource source; const char* selected; };
    const RecoveryCase recovery[] = {
      {"valid primary wins", "P", "B", "S", storage::RecoverySource::Primary, "P"},
      {"absent primary prefers saving", nullptr, "B", "S", storage::RecoverySource::Saving, "S"},
      {"absent primary backup only", nullptr, "B", nullptr, storage::RecoverySource::Backup, "B"},
      {"absent primary bad saving then backup", nullptr, "B", "bad", storage::RecoverySource::Backup, "B"},
      {"bad primary prefers backup", "bad", "B", "S", storage::RecoverySource::Backup, "B"},
      {"bad primary bad backup then saving", "bad", "bad", "S", storage::RecoverySource::Saving, "S"},
      {"bad primary backup only", "bad", "B", nullptr, storage::RecoverySource::Backup, "B"},
      {"bad primary missing backup then saving", "bad", nullptr, "S", storage::RecoverySource::Saving, "S"},
    };
    for (const auto& item : recovery) { run(item.name, [&] {
      Area area;
      const auto provide = [&](const char* token, const char* suffix) {
        if (token) { area.write(area.profile / (std::string("tables.config") + suffix),
            std::string_view(token) == "bad" ? std::string("not bencoded") : settings(token)); }
      };
      provide(item.primary, ""); provide(item.backup, ".bak"); provide(item.saving, ".saving");
      area.hold(); const auto before = area.observe("tables.config"); const int descriptor = area.held().directoryHandle();
      auto view = storage::readColumnLayoutsView(area.held()); require(view.has_value(), "recovery selection failed");
      loaded(*view, item.source, settings(item.selected)); named(*view); proveUnchanged(area, before, descriptor);
    }); }
    struct InvalidCase { const char* name; const char* primary; const char* backup; const char* saving; };
    const InvalidCase invalid[] = {
      {"absent primary malformed backup is not missing", nullptr, "broken", nullptr},
      {"absent primary malformed saving is not missing", nullptr, nullptr, "broken"},
      {"absent primary both malformed is not missing", nullptr, "broken", "le"},
      {"malformed primary without recovery", "broken", nullptr, nullptr},
      {"empty primary without recovery", "", nullptr, nullptr},
      {"non-dictionary primary without recovery", "le", nullptr, nullptr},
      {"trailing bytes primary without recovery", "dex", nullptr, nullptr},
    };
    for (const auto& item : invalid) { run(item.name, [&] {
      Area area;
      for (const auto& [data, suffix] : {std::pair{item.primary, ""}, std::pair{item.backup, ".bak"}, std::pair{item.saving, ".saving"}}) {
        if (data) { area.write(area.profile / (std::string("tables.config") + suffix), data); }
      }
      area.hold(); const auto before = area.observe("tables.config");
      error(storage::readColumnLayoutsView(area.held()), storage::ErrorCode::InvalidFormat);
      proveUnchanged(area, before, area.held().directoryHandle());
    }); }

    for (int kind = 0; kind < 3; ++kind) {
      for (int position = 0; position < 3; ++position) { run("unsafe active candidate " + std::to_string(kind) + '/' + std::to_string(position), [&] {
        Area area; area.write(area.root / "sentinel", "external-owned-sentinel");
        if (position == 0) {
          unsafe(area, area.profile / "tables.config", kind);
          area.write(area.profile / "tables.config.bak", settings("ignored backup"));
          area.write(area.profile / "tables.config.saving", settings("ignored saving"));
        } else if (position == 1) {
          unsafe(area, area.profile / "tables.config.saving", kind);
          area.write(area.profile / "tables.config.bak", settings("ignored backup"));
        } else {
          area.write(area.profile / "tables.config", "broken");
          unsafe(area, area.profile / "tables.config.bak", kind);
          area.write(area.profile / "tables.config.saving", settings("ignored saving"));
        }
        area.hold(); const auto before = area.observe("tables.config");
        error(storage::readColumnLayoutsView(area.held()), storage::ErrorCode::UnsafePath);
        proveUnchanged(area, before, area.held().directoryHandle());
      }); }
      run("valid primary ignores unsafe recovery " + std::to_string(kind), [&] {
        Area area; area.write(area.root / "sentinel", "unchanged sentinel"); const auto raw = settings("usable primary");
        area.write(area.profile / "tables.config", raw);
        unsafe(area, area.profile / "tables.config.bak", kind); unsafe(area, area.profile / "tables.config.saving", kind);
        area.hold(); const auto before = area.observe("tables.config");
        auto view = storage::readColumnLayoutsView(area.held()); require(view.has_value(), "unused unsafe recovery poisoned valid primary");
        loaded(*view, storage::RecoverySource::Primary, raw); proveUnchanged(area, before, area.held().directoryHandle());
      });
    }

    for (int kind = 0; kind < 3; ++kind) { for (int extra = 0; extra < 2; ++extra) {
      run("fixed ordinary document ceiling " + std::to_string(kind) + '/' + std::to_string(extra), [&] {
        Area area;
        const auto raw = kind == 0 ? atSize(core::Limits{}.inputBytes + static_cast<std::size_t>(extra))
            : kind == 1 ? nodeCount(core::Limits{}.nodes + static_cast<std::size_t>(extra))
            : depth(core::Limits{}.depth + static_cast<std::size_t>(extra));
        area.write(area.profile / "tables.config", raw); area.hold();
        const auto before = area.observe("tables.config");
        // All over-limit inputs are valid under the unchanged generic192MiB,
        // 2M-node/depth128 loader. The adapter must use ordinary caps instead.
        if (extra) {
          auto generic = storage::ConfigFile::load(area.held(), "tables.config");
          require(generic.has_value(), "generated over-ordinary-cap document is not valid under generic loader");
          error(storage::readColumnLayoutsView(area.held()), storage::ErrorCode::InvalidFormat);
        } else {
          auto view = storage::readColumnLayoutsView(area.held()); require(view.has_value(), "exact ordinary ceiling rejected");
          loaded(*view, storage::RecoverySource::Primary, raw);
        }
        proveUnchanged(area, before, area.held().directoryHandle());
      });
    } }
    for (int cap = 0; cap < 4; ++cap) { run("pure projection limit maps Unsupported " + std::to_string(cap), [&] {
      Area area; std::string table = "d";
      if (cap == 0) {
        for (int i = 0; i < 1025; ++i) { table += bytes("Column.p" + std::to_string(i)) + "le"; }
      } else if (cap == 1) {
        table += bytes("SortColumns") + "l";
        for (int i = 0; i < 33; ++i) { table += bytes("name"); }
        table += 'e';
      } else if (cap == 2) { table += bytes("Column." + std::string(257, 'x')) + "le"; }
      else {
        for (int i = 0; i < 410; ++i) {
          table += bytes("Column.p" + std::to_string(i)) + 'l';
          for (int slot = 0; slot < 10; ++slot) { table += bytes("wrong type"); }
          table += 'e';
        }
      }
      const auto raw = map({{"Table.MyTorrents", table + 'e'}});
      area.write(area.profile / "tables.config", raw); area.hold(); const auto before = area.observe("tables.config");
      auto generic = storage::ConfigFile::load(area.held(), "tables.config", core::Limits{});
      require(generic.has_value(), "projection-cap fixture failed ordinary raw loader");
      error(storage::readColumnLayoutsView(area.held()), storage::ErrorCode::Unsupported);
      proveUnchanged(area, before, area.held().directoryHandle());
    }); }
    run("no global-library-path lookup", [&] {
      Area area; area.write(area.root / "sentinel", "not valid config and never a table source");
      for (const auto* name : {"biglybt.config", "downloads.config", "tracker.config", "sharing.config", "cached-payload"}) {
        area.symlink(area.profile / name, area.root / "sentinel");
      }
      area.hold(); const auto before = area.observe("tables.config"); const auto sentinel = stamp(area.root / "sentinel");
      std::stop_source source; ReadArm gate{sentinel->device, sentinel->inode, &source};
      std::expected<storage::ColumnLayoutsView, storage::Error> view = std::unexpected(storage::Error{});
      { Arm armed(gate); view = storage::readColumnLayoutsView(area.held()); }
      require(view && view->missing() && gate.matches == 0 && !source.stop_requested(), "reader accessed a nonallowlisted source");
      proveUnchanged(area, before, area.held().directoryHandle());
    });
    run("pre-cancel touches no config read", [&] {
      Area area; area.write(area.profile / "tables.config", settings("not read")); area.hold();
      const auto before = area.observe("tables.config"); const auto input = stamp(area.profile / "tables.config");
      std::stop_source source; source.request_stop(); ReadArm gate{input->device, input->inode, &source};
      { Arm armed(gate); error(storage::readColumnLayoutsView(area.held(), source.get_token()), storage::ErrorCode::Cancelled); }
      require(gate.calls == 0, "pre-cancel performed a read"); proveUnchanged(area, before, area.held().directoryHandle());
    });
    for (bool cancel : {false, true}) { run(cancel ? "pre-cancel before moved-from lease validation" : "invalid moved-from lease rejected", [&] {
      Area area; area.hold(); const auto before = area.observe("tables.config");
      auto live = std::move(*area.lease); require(live.valid() && !area.held().valid(), "invalid-lease fixture failed");
      std::stop_source source; if (cancel) { source.request_stop(); }
      error(storage::readColumnLayoutsView(area.held(), source.get_token()),
          cancel ? storage::ErrorCode::Cancelled : storage::ErrorCode::UnsafePath);
      unchanged(before); require(live.valid(), "reader affected independently moved lease");
    }); }
    for (int selection = 0; selection < 3; ++selection) { run("stop after real selected read " + std::to_string(selection), [&] {
      Area area; const auto raw = settings("cancelled loaded result");
      const char* name = selection == 0 ? "tables.config" : selection == 1 ? "tables.config.bak" : "tables.config.saving";
      area.write(area.profile / name, raw);
      if (selection == 1) { area.write(area.profile / "tables.config", "broken"); }
      area.hold(); const auto before = area.observe("tables.config"); const auto input = stamp(area.profile / name);
      std::stop_source source; ReadArm gate{input->device, input->inode, &source};
      { Arm armed(gate); error(storage::readColumnLayoutsView(area.held(), source.get_token()), storage::ErrorCode::Cancelled); }
      require(gate.matches == 1 && gate.calls >= 1 && source.stop_requested(), "real-read cancellation gate never fired");
      proveUnchanged(area, before, area.held().directoryHandle());
    }); }
    run("stop after bad primary covers loader as a whole", [&] {
      Area area; area.write(area.profile / "tables.config", "broken"); area.write(area.profile / "tables.config.bak", settings("discarded"));
      area.hold(); const auto before = area.observe("tables.config"); const auto input = stamp(area.profile / "tables.config");
      std::stop_source source; ReadArm gate{input->device, input->inode, &source};
      { Arm armed(gate); error(storage::readColumnLayoutsView(area.held(), source.get_token()), storage::ErrorCode::Cancelled); }
      require(gate.matches == 1 && source.stop_requested(), "primary read did not request stop");
      // No assertion of interruption inside ConfigFile::load: it may read and
      // parse a recovery candidate before the adapter observes cancellation.
      proveUnchanged(area, before, area.held().directoryHandle());
    });
    run("stop after real absence-confirmation read wins malformed", [&] {
      Area area; area.write(area.profile / "tables.config.bak", "broken"); area.hold();
      const auto before = area.observe("tables.config"); const auto input = stamp(area.profile / "tables.config.bak");
      std::stop_source source; ReadArm gate{input->device, input->inode, &source, 2};
      { Arm armed(gate); error(storage::readColumnLayoutsView(area.held(), source.get_token()), storage::ErrorCode::Cancelled); }
      require(gate.matches == 2 && source.stop_requested(), "second exact-inode absence-confirmation read was not observed");
      proveUnchanged(area, before, area.held().directoryHandle());
    });
    run("owned result survives replacement lease and root destruction", [&] {
      std::optional<storage::ColumnLayoutsView> retained;
      const auto firstRaw = settings("old immutable snapshot", 311);
      {
        Area area; area.write(area.profile / "tables.config", firstRaw); area.hold();
        const auto before = area.observe("tables.config");
        auto first = storage::readColumnLayoutsView(area.held()); require(first.has_value(), "initial owned snapshot failed");
        loaded(*first, storage::RecoverySource::Primary, firstRaw); named(*first, 311);
        proveUnchanged(area, before, area.held().directoryHandle()); retained.emplace(std::move(*first));
        const auto held = area.root / "previous-tables"; area.track(held);
        require(::rename((area.profile / "tables.config").c_str(), held.c_str()) == 0, "test-owned replacement rename failed");
        const auto secondRaw = settings("new immutable snapshot", 622); area.write(area.profile / "tables.config", secondRaw);
        const auto replaced = area.observe("tables.config");
        auto second = storage::readColumnLayoutsView(area.held()); require(second.has_value(), "replacement snapshot failed");
        loaded(*second, storage::RecoverySource::Primary, secondRaw); named(*second, 622);
        loaded(*retained, storage::RecoverySource::Primary, firstRaw); named(*retained, 311);
        require(retained->snapshot->source.get() != second->snapshot->source.get(), "fresh snapshots shared mutable source identity");
        proveUnchanged(area, replaced, area.held().directoryHandle());
        auto copied = *retained; loaded(copied, storage::RecoverySource::Primary, firstRaw); named(copied, 311);
        area.lease.reset();
        loaded(*retained, storage::RecoverySource::Primary, firstRaw);
      }
      loaded(*retained, storage::RecoverySource::Primary, firstRaw); named(*retained, 311);
    });
    run("regular hardlinked config remains read-only", [&] {
      Area area; const auto raw = settings("regular hardlink"); area.write(area.root / "sentinel", raw);
      const auto config = area.profile / "tables.config"; area.track(config);
      require(::link((area.root / "sentinel").c_str(), config.c_str()) == 0, "test-owned config hardlink failed");
      area.hold(); const auto before = area.observe("tables.config"); require(stamp(config)->links == 2, "hardlink fixture differs");
      auto view = storage::readColumnLayoutsView(area.held()); require(view.has_value(), "regular hardlink rejected by read-only policy");
      loaded(*view, storage::RecoverySource::Primary, raw); proveUnchanged(area, before, area.held().directoryHandle());
    });
    std::cout << "PASS " << cases << " column-layout view cases; " << assertions << " assertions\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << "FAIL " << active << ": " << failure.what() << '\n';
    return 1;
  }
}
