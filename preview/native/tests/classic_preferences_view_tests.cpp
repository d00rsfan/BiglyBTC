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
struct FsyncArm {
  dev_t device;
  ino_t inode;
  bool armed = false;
  std::size_t matches = 0;
};
thread_local FsyncArm* armedFsync = nullptr;
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

extern "C" int __real_fsync(int descriptor);
extern "C" int __wrap_fsync(int descriptor) {
  auto* gate = armedFsync;
  struct stat info{};
  if (gate && gate->armed && ::fstat(descriptor, &info) == 0 && S_ISDIR(info.st_mode)
      && info.st_dev == gate->device && info.st_ino == gate->inode) {
    gate->armed = false;
    ++gate->matches;
    // A test-only injected result on this owned directory after PrimaryRenamed;
    // no assertion of a real disk fault or achieved physical durability.
    errno = EIO;
    return -1;
  }
  return __real_fsync(descriptor);
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
      char pattern[] = "/tmp/biglybtc-classic-preferences-view-tests-XXXXXX";
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

// Generated source-derived policy checks only. No Java manager/profile oracle,
// inode-CAS, path-rebinding authority or actual storage-fault parity is claimed.
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string integer(std::int64_t value) { return 'i' + std::to_string(value) + 'e'; }
std::string dictionary(std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  std::string result = "d";
  for (const auto& [key, value] : fields) { result += bytes(key) + value; }
  return result + 'e';
}
core::Document parse(std::string raw, core::Limits limits = {}) {
  auto result = core::Document::parse(std::move(raw), limits);
  require(result.has_value(), "generated document parse failed");
  return std::move(*result);
}
core::ClassicPreferences preferences(bool alternative = false) {
  core::ClassicPreferences result;
  for (auto* pane : {&result.incomplete, &result.complete}) {
    for (std::size_t i = 0; i < core::ClassicPreferenceIds.size(); ++i) {
      pane->order[i] = core::ClassicPreferenceIds[i];
      pane->columns[i].visible = alternative ? i % 2 == 0 : i < 8;
      if (i) { pane->columns[i].width = (alternative ? 300 : 100) + static_cast<int>(i); }
    }
    pane->sortId = alternative ? "saved-selected-done" : "name";
    pane->ascending = !alternative;
    if (alternative) { std::reverse(pane->order.begin(), pane->order.end()); }
  }
  result.complete.columns[4].visible = false;
  require(core::validateClassicPreferences(result).has_value(), "generated preferences invalid");
  return result;
}
std::string original(std::string_view marker = "original") {
  return dictionary({{"z-plugin", dictionary({{"binary", bytes(std::string("\0\xff\r\n", 4))}})},
      {"Table.MyTorrents", dictionary({{"Column.plugin", "li1ei-2e3:badli1ei2ei3ee4:taile"},
          {"SortColumns", "l" + bytes("plugin") + bytes("name") + "e"}})},
      {"Table.MySeeders", bytes("malformed original table retained")},
      {"marker", bytes(marker)}, {"number", integer(7)}, {"a-tail", "li0e0:e"}});
}
std::string withPreferences(std::string raw, const core::ClassicPreferences& prefs) {
  auto document = parse(std::move(raw));
  require(core::writeClassicPreferences(document, prefs).has_value(), "unable to create preference fixture");
  return document.serialize();
}
void tracks(Area& area) {
  for (const char* suffix : {"", ".bak", ".saving"}) { area.track(area.profile / (std::string("tables.config") + suffix)); }
}
void prepare(Area& area, std::string_view raw) { tracks(area); area.write(area.profile / "tables.config", raw); area.hold(); }
void replace(Area& area, const std::filesystem::path& path, std::string_view raw) {
  require(::unlink(path.c_str()) == 0, "unable to remove owned replacement input");
  area.write(path, raw);
}
template<typename T>
void error(const std::expected<T, storage::Error>& result, storage::ErrorCode code) {
  require(!result && result.error().code == code, "classic preference adapter error category differs");
}
void decoded(const core::Document& document, const core::ClassicPreferences& prefs) {
  require(document.serialize() == document.originalBytes(), "returned baseline is edited rather than freshly parsed");
  const auto result = core::readClassicPreferences(document);
  require(result.has_value() && result->has_value() && **result == prefs, "persisted preferences differ");
}
std::vector<std::pair<std::string, std::string>> opaque(const core::Document& document) {
  std::vector<std::pair<std::string, std::string>> result;
  const auto& children = document.root().children;
  for (std::size_t i = 0; i + 1 < children.size(); i += 2) {
    if (children[i].bytes == "BiglyBTC.ClassicColumns.v1") { continue; }
    result.emplace_back(document.originalBytes().substr(children[i].begin, children[i].end - children[i].begin),
        document.originalBytes().substr(children[i + 1].begin, children[i + 1].end - children[i + 1].begin));
  }
  return result;
}
bool sameRenamedFile(const Stamp& old, const Stamp& now) {
  return old.device == now.device && old.inode == now.inode && old.mode == now.mode
      && old.uid == now.uid && old.gid == now.gid && old.links == now.links
      && old.size == now.size && sameTime(old.modified, now.modified) && old.bytes == now.bytes;
}
struct Mask final {
  mode_t previous = ::umask(0);
  ~Mask() { ::umask(previous); }
};
enum class ObserverAction { Record, Cancel, BlockBackup, BlockPrimary, FailFinalSync };
struct ObserverState {
  Area* area;
  ObserverAction action = ObserverAction::Record;
  storage::SaveStage at = storage::SaveStage::TemporarySynced;
  std::stop_source* stop = nullptr;
  FsyncArm* sync = nullptr;
  std::array<storage::SaveStage, 3> stages{};
  std::size_t count = 0;
};
thread_local ObserverState* observerState = nullptr;
void observe(storage::SaveStage stage) {
  auto* state = observerState;
  require(state && state->count < state->stages.size(), "unexpected save observer stage");
  state->stages[state->count++] = stage;
  if (stage != state->at) { return; }
  if (state->stop) { state->stop->request_stop(); }
  if (state->action == ObserverAction::BlockBackup) { state->area->directory(state->area->profile / "tables.config.bak"); }
  if (state->action == ObserverAction::BlockPrimary) { state->area->directory(state->area->profile / "tables.config"); }
  if (state->action == ObserverAction::FailFinalSync) {
    require(state->sync, "final sync observer missing gate"); state->sync->armed = true;
  }
}
struct Observing final {
  explicit Observing(ObserverState& state) { require(!observerState, "nested observer fixture"); observerState = &state; armedFsync = state.sync; }
  ~Observing() { observerState = nullptr; armedFsync = nullptr; }
};
void allStages(const ObserverState& state) {
  require(state.count == 3 && state.stages == std::array{storage::SaveStage::TemporarySynced,
      storage::SaveStage::BackupRenamed, storage::SaveStage::PrimaryRenamed}, "save stage order differs");
}
std::string atSize(std::size_t total) {
  std::size_t payload = total;
  for (int i = 0; i < 20; ++i) {
    const auto prefix = "d6:opaque" + std::to_string(payload) + ':';
    const auto next = total - prefix.size() - 1;
    if (next == payload) { return prefix + std::string(payload, 'x') + 'e'; }
    payload = next;
  }
  throw std::runtime_error("unable to size generated boundary");
}
} // namespace

int main() {
  std::string active;
  std::size_t cases = 0;
  try {
    const auto run = [&](std::string name, auto body) { active = std::move(name); body(); ++cases; };
    const auto first = preferences(); const auto second = preferences(true);
    run("private read missing and valid owned preferences", [&] {
      Area area; prepare(area, original()); const auto before = area.observe("tables.config");
      auto missing = storage::readColumnLayoutsView(area.held());
      require(missing && missing->snapshot && !missing->nativePreferences, "missing private map became fabricated preferences");
      unchanged(before);
      const auto raw = withPreferences(original(), first); replace(area, area.profile / "tables.config", raw);
      const auto changed = area.observe("tables.config");
      auto ready = storage::readColumnLayoutsView(area.held());
      require(ready && ready->nativePreferences && *ready->nativePreferences == first
          && ready->recoverySource == storage::RecoverySource::Primary, "native preferences missing from held read");
      unchanged(changed); require(ready->snapshot->source->originalBytes() == raw, "private read source changed");
      replace(area, area.profile / "tables.config", original("changed after read"));
      require(*ready->nativePreferences == first && ready->snapshot->source->originalBytes() == raw, "owned read aliases config replacement");
    });
    run("primary save preserves opaque bytes mode and backup identity", [&] {
      Area area; const auto raw = original(); prepare(area, raw);
      require(::chmod((area.profile / "tables.config").c_str(), 0640) == 0, "unable to set owned mode");
      area.write(area.root / "sentinel", "untouched outside config"); const auto sentinel = stamp(area.root / "sentinel");
      const auto old = stamp(area.profile / "tables.config"); auto baseline = parse(raw); ObserverState state{&area};
      std::expected<std::shared_ptr<const core::Document>, storage::Error> saved = std::unexpected(storage::Error{});
      { Mask mask; Observing observer(state); saved = storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first, {}, observe); }
      require(saved.has_value(), "valid primary save failed"); allStages(state); decoded(**saved, first);
      const auto primary = stamp(area.profile / "tables.config"); const auto backup = stamp(area.profile / "tables.config.bak");
      require(primary && backup && primary->inode != old->inode && (primary->mode & 0777) == 0640, "new primary identity/mode differs");
      require(sameRenamedFile(*old, *backup), "backup did not retain old primary inode/mode/bytes");
      require(!stamp(area.profile / "tables.config.saving"), "successful save left a saving file");
      require(primary->bytes == (*saved)->originalBytes() && opaque(baseline) == opaque(**saved), "saved original keys/values/order changed");
      require(same(sentinel, stamp(area.root / "sentinel")), "save touched unrelated sentinel");
    });
    run("replace private map keeps original multisort tails and a second pristine baseline", [&] {
      Area area; const auto raw = withPreferences(original(), first); prepare(area, raw); auto baseline = parse(raw);
      auto saved = storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, second);
      require(saved.has_value(), "private replacement failed"); decoded(**saved, second); require(opaque(baseline) == opaque(**saved), "private replacement altered opaque original data");
      const auto firstSaved = (*saved)->originalBytes(); auto later = storage::saveClassicPreferencesView(area.held(), **saved, storage::RecoverySource::Primary, first);
      require(later.has_value(), "returned pristine baseline failed second save"); decoded(**later, first);
      require((*saved)->originalBytes() == firstSaved && (*saved).get() != (*later).get(), "second save mutated/shared first baseline");
      const auto unchangedAfter = area.observe("tables.config");
      error(storage::saveClassicPreferencesView(area.held(), **saved, storage::RecoverySource::Primary, second), storage::ErrorCode::Conflict);
      unchanged(unchangedAfter);
    });
    run("saved baseline outlives lease and generated profile", [&] {
      std::shared_ptr<const core::Document> retained;
      { Area area; prepare(area, original()); auto baseline = parse(original()); auto saved = storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, second); require(saved.has_value(), "lifetime save failed"); retained = std::move(*saved); }
      require(retained != nullptr, "saved baseline missing after owner teardown"); decoded(*retained, second);
    });
    for (int variant = 0; variant < 3; ++variant) { run("raw byte or presence conflict " + std::to_string(variant), [&] {
      Area area; const auto raw = original(); prepare(area, raw); auto baseline = parse(raw);
      if (variant == 0) { replace(area, area.profile / "tables.config", original("external edit")); }
      else if (variant == 1) { require(::unlink((area.profile / "tables.config").c_str()) == 0, "unable to remove owned primary"); }
      else { replace(area, area.profile / "tables.config", "broken"); }
      const auto before = area.observe("tables.config");
      error(storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first),
          variant == 0 ? storage::ErrorCode::Conflict : variant == 1 ? storage::ErrorCode::Missing : storage::ErrorCode::InvalidFormat);
      unchanged(before);
    }); }
    run("same-byte primary replacement is allowed byte baseline not inode CAS", [&] {
      Area area; const auto raw = original(); prepare(area, raw); auto baseline = parse(raw); const auto old = stamp(area.profile / "tables.config");
      const auto retained = area.root / "old-primary"; area.track(retained);
      require(::rename((area.profile / "tables.config").c_str(), retained.c_str()) == 0, "owned primary rename failed");
      area.write(area.profile / "tables.config", raw); const auto replacement = stamp(area.profile / "tables.config");
      require(replacement->inode != old->inode, "same-byte replacement did not replace inode");
      auto saved = storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first);
      require(saved.has_value(), "same-byte primary replacement was incorrectly rejected");
      require(sameRenamedFile(*replacement, *stamp(area.profile / "tables.config.bak")), "backup was not actual reloaded primary");
      require(readBytes(retained) == raw, "save touched displaced original inode");
    });
    for (int source = 0; source < 2; ++source) { for (bool claimPrimary : {false, true}) { run("recovery never promoted " + std::to_string(source) + '/' + std::to_string(claimPrimary), [&] {
      Area area; tracks(area); const auto raw = withPreferences(original(), first);
      area.write(area.profile / (source == 0 ? "tables.config.bak" : "tables.config.saving"), raw); area.hold();
      auto view = storage::readColumnLayoutsView(area.held()); require(view && view->nativePreferences == first, "valid recovery preferences did not read");
      const auto before = area.observe("tables.config");
      error(storage::saveClassicPreferencesView(area.held(), *view->snapshot->source,
          claimPrimary ? storage::RecoverySource::Primary : *view->recoverySource, second), storage::ErrorCode::Conflict);
      unchanged(before);
    }); } }
    for (int kind = 0; kind < 3; ++kind) { run("private malformed unsupported prefix refuses read and save " + std::to_string(kind), [&] {
      Area area; const auto key = kind == 0 ? "BiglyBTC.ClassicColumns.v1" : kind == 1 ? "BiglyBTC.ClassicColumns.v2" : "BiglyBTC.ClassicColumns";
      const auto raw = dictionary({{"Table.MyTorrents", "de"}, {key, integer(1)}, {"opaque", bytes("retained")}});
      prepare(area, raw); auto baseline = parse(raw); const auto before = area.observe("tables.config");
      const auto code = kind == 0 ? storage::ErrorCode::InvalidFormat : storage::ErrorCode::Unsupported;
      error(storage::readColumnLayoutsView(area.held()), code);
      error(storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first), code);
      unchanged(before);
    }); }
    for (int kind = 0; kind < 3; ++kind) { run("edited inconsistent or wrong-root baseline rejected " + std::to_string(kind), [&] {
      Area area; const auto raw = original(); prepare(area, raw); auto baseline = parse(kind == 2 ? "le" : raw);
      if (kind == 0) { require(baseline.setInteger("number", 8), "unable to edit owned baseline"); }
      if (kind == 1) { const_cast<core::Node*>(baseline.find("number"))->integer = 8; }
      const auto before = area.observe("tables.config");
      error(storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first), storage::ErrorCode::InvalidFormat);
      unchanged(before);
    }); }
    run("invalid requested preferences cannot create recovery files", [&] {
      Area area; prepare(area, original()); auto baseline = parse(original()); auto invalid = first; invalid.incomplete.columns[1].width = 15;
      const auto before = area.observe("tables.config");
      error(storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, invalid), storage::ErrorCode::InvalidFormat);
      unchanged(before);
    });
    run("missing file is not created", [&] {
      Area area; tracks(area); area.hold(); auto baseline = parse(original()); const auto before = area.observe("tables.config");
      error(storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first), storage::ErrorCode::Missing); unchanged(before);
    });
    for (bool cancelled : {false, true}) { run("released lease pre-stop priority " + std::to_string(cancelled), [&] {
      Area area; prepare(area, original()); auto baseline = parse(original()); auto live = std::move(*area.lease); const auto before = area.observe("tables.config");
      std::stop_source stop; if (cancelled) { stop.request_stop(); }
      error(storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first, stop.get_token()),
          cancelled ? storage::ErrorCode::Cancelled : storage::ErrorCode::UnsafePath);
      require(live.valid(), "save consumed independently moved lease"); unchanged(before);
    }); }
    run("other-profile different bytes conflicts without touching either profile", [&] {
      Area one; Area two; prepare(one, original("one")); prepare(two, original("two")); auto baseline = parse(original("one"));
      const auto beforeOne = one.observe("tables.config"); const auto beforeTwo = two.observe("tables.config");
      error(storage::saveClassicPreferencesView(two.held(), baseline, storage::RecoverySource::Primary, first), storage::ErrorCode::Conflict);
      unchanged(beforeOne); unchanged(beforeTwo);
    });
    run("pre-stop performs no config read or observer publication", [&] {
      Area area; prepare(area, original()); auto baseline = parse(original()); const auto input = stamp(area.profile / "tables.config"); const auto before = area.observe("tables.config");
      std::stop_source stop; stop.request_stop(); ReadArm gate{input->device, input->inode, &stop}; ObserverState state{&area};
      { Arm armed(gate); Observing observer(state); error(storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first, stop.get_token(), observe), storage::ErrorCode::Cancelled); }
      require(gate.calls == 0 && state.count == 0, "pre-stop touched config/observer"); unchanged(before);
    });
    run("actual reload read cancellation discards operation before any save", [&] {
      Area area; prepare(area, original()); auto baseline = parse(original()); const auto input = stamp(area.profile / "tables.config"); const auto before = area.observe("tables.config");
      std::stop_source stop; ReadArm gate{input->device, input->inode, &stop}; ObserverState state{&area};
      { Arm armed(gate); Observing observer(state); error(storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first, stop.get_token(), observe), storage::ErrorCode::Cancelled); }
      require(gate.matches == 1 && stop.stop_requested() && state.count == 0, "real reload cancellation gate did not fire before save"); unchanged(before);
    });
    for (const auto stage : {storage::SaveStage::TemporarySynced, storage::SaveStage::BackupRenamed, storage::SaveStage::PrimaryRenamed}) {
      run("observer stop preserves actual successful save " + std::to_string(static_cast<int>(stage)), [&] {
        Area area; prepare(area, original()); auto baseline = parse(original()); std::stop_source stop;
        ObserverState state{&area, ObserverAction::Cancel, stage, &stop};
        std::expected<std::shared_ptr<const core::Document>, storage::Error> saved = std::unexpected(storage::Error{});
        { Observing observer(state); saved = storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, second, stop.get_token(), observe); }
        require(saved.has_value() && stop.stop_requested(), "committed save was misreported as cancellation"); allStages(state); decoded(**saved, second);
        require(readBytes(area.profile / "tables.config") == (*saved)->originalBytes()
            && readBytes(area.profile / "tables.config.bak") == baseline.originalBytes(), "cancel observer changed actual save/backup result");
      });
    }
    for (int kind = 0; kind < 4; ++kind) { run("pre-existing saving preserved " + std::to_string(kind), [&] {
      Area area; prepare(area, original()); auto baseline = parse(original()); const auto saving = area.profile / "tables.config.saving";
      area.write(area.root / "sentinel", "not a saving target");
      if (kind == 0) { area.write(saving, "existing recovery bytes"); }
      else if (kind == 1) { area.symlink(saving, area.root / "sentinel"); }
      else if (kind == 2) { require(::mkfifo(saving.c_str(), 0600) == 0, "unable to create saving FIFO"); }
      else { area.directory(saving); }
      const auto before = area.observe("tables.config");
      error(storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first), storage::ErrorCode::Conflict); unchanged(before);
    }); }
    for (int kind = 0; kind < 3; ++kind) { run("backup leaf safely replaced without following " + std::to_string(kind), [&] {
      Area area; prepare(area, original()); auto baseline = parse(original()); const auto backup = area.profile / "tables.config.bak";
      area.write(area.root / "sentinel", "backup symlink sentinel"); const auto sentinel = stamp(area.root / "sentinel");
      if (kind == 0) { area.write(backup, "previous backup bytes"); }
      else if (kind == 1) { area.symlink(backup, area.root / "sentinel"); }
      else { require(::mkfifo(backup.c_str(), 0600) == 0, "unable to create backup FIFO"); }
      const auto primary = stamp(area.profile / "tables.config");
      auto saved = storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first);
      require(saved.has_value(), "qualified backup leaf replacement failed");
      require(sameRenamedFile(*primary, *stamp(backup)) && same(sentinel, stamp(area.root / "sentinel")), "backup replacement followed leaf or lost primary");
    }); }
    for (int kind = 0; kind < 3; ++kind) { run("unsafe current primary refuses save " + std::to_string(kind), [&] {
      Area area; tracks(area); area.write(area.root / "sentinel", original());
      const auto primary = area.profile / "tables.config";
      if (kind == 0) { area.symlink(primary, area.root / "sentinel"); }
      else if (kind == 1) { require(::mkfifo(primary.c_str(), 0600) == 0, "unable to create primary FIFO"); }
      else { area.directory(primary); }
      area.hold(); auto baseline = parse(original()); const auto before = area.observe("tables.config");
      error(storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first), storage::ErrorCode::UnsafePath); unchanged(before);
    }); }
    run("temporary synced then actual backup rename conflict retains recovery", [&] {
      Area area; prepare(area, original()); auto baseline = parse(original()); const auto old = stamp(area.profile / "tables.config");
      ObserverState state{&area, ObserverAction::BlockBackup, storage::SaveStage::TemporarySynced};
      { Observing observer(state); auto result = storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first, {}, observe); error(result, storage::ErrorCode::Io); require(result.error().message.find("Backup transition failed") != std::string::npos, "actual backup-stage diagnostic lost"); }
      require(state.count == 1 && same(old, stamp(area.profile / "tables.config")), "failed backup transition changed primary");
      auto saving = parse(readBytes(area.profile / "tables.config.saving")); decoded(saving, first);
    });
    run("backup renamed then actual primary rename conflict retains both recovery values", [&] {
      Area area; prepare(area, original()); auto baseline = parse(original()); const auto old = stamp(area.profile / "tables.config");
      ObserverState state{&area, ObserverAction::BlockPrimary, storage::SaveStage::BackupRenamed};
      { Observing observer(state); auto result = storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, second, {}, observe); error(result, storage::ErrorCode::Io); require(result.error().message.find("Configuration replacement failed") != std::string::npos, "actual replacement-stage diagnostic lost"); }
      require(state.count == 2 && S_ISDIR(stamp(area.profile / "tables.config")->mode)
          && sameRenamedFile(*old, *stamp(area.profile / "tables.config.bak")), "failed primary transition lost original backup");
      auto saving = parse(readBytes(area.profile / "tables.config.saving")); decoded(saving, second);
    });
    run("primary renamed then injected directory sync failure stays Io even after stop", [&] {
      Area area; prepare(area, original()); auto baseline = parse(original()); const auto directory = stamp(area.profile);
      FsyncArm sync{directory->device, directory->inode}; std::stop_source stop;
      ObserverState state{&area, ObserverAction::FailFinalSync, storage::SaveStage::PrimaryRenamed, &stop, &sync};
      { Observing observer(state); auto result = storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, second, stop.get_token(), observe); error(result, storage::ErrorCode::Io); require(result.error().message.find("replacement already occurred") != std::string::npos, "post-rename diagnostic lost"); }
      allStages(state); require(sync.matches == 1 && stop.stop_requested() && !stamp(area.profile / "tables.config.saving"), "final-sync injection/stop did not reach committed state");
      auto committed = parse(readBytes(area.profile / "tables.config")); decoded(committed, second);
      require(readBytes(area.profile / "tables.config.bak") == original(), "post-rename error lost old primary");
    });
    run("whole document byte ceiling rejects private growth before temporary creation", [&] {
      Area area; const auto raw = atSize(core::Limits{}.inputBytes); prepare(area, raw); auto baseline = parse(raw); const auto before = area.observe("tables.config");
      error(storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first), storage::ErrorCode::Unsupported); unchanged(before);
    });
    run("oversized current primary uses ordinary rather than generic limits", [&] {
      Area area; const auto raw = atSize(core::Limits{}.inputBytes + 1); prepare(area, raw); auto baseline = parse(original()); const auto before = area.observe("tables.config");
      require(storage::ConfigFile::load(area.held(), "tables.config").has_value(), "oversize fixture not valid under generic loader");
      error(storage::saveClassicPreferencesView(area.held(), baseline, storage::RecoverySource::Primary, first), storage::ErrorCode::InvalidFormat); unchanged(before);
    });
    std::cout << "PASS " << cases << " classic-preferences view cases; " << assertions << " assertions\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << "FAIL " << active << ": " << failure.what() << '\n';
    return 1;
  }
}
