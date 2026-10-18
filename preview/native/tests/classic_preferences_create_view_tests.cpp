#include "column_layout_view.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// Generated private profiles only. No original Java/client/profile, inode CAS,
// real storage fault, external writer exclusion or physical durability claim.
namespace {
using namespace biglybtc;
std::size_t assertions = 0;
void check(bool condition, std::string_view message) {
  ++assertions;
  if (!condition) { throw std::runtime_error(std::string(message)); }
}
struct Fd final {
  int value;
  explicit Fd(int descriptor) : value(descriptor) {}
  ~Fd() { if (value >= 0) { (void)::close(value); } }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
std::string read(const std::filesystem::path& path) {
  Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  check(fd.value >= 0, "unable to open explicit generated file");
  std::string result;
  std::array<char, 8192> buffer{};
  for (;;) {
    const auto count = ::read(fd.value, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) { continue; }
    check(count >= 0, "unable to read explicit generated file");
    if (count == 0) { return result; }
    result.append(buffer.data(), static_cast<std::size_t>(count));
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
  std::string content;
};
std::optional<Stamp> stamp(const std::filesystem::path& path) {
  struct stat info{};
  if (::lstat(path.c_str(), &info) != 0) { check(errno == ENOENT, "generated pathname inspection failed"); return std::nullopt; }
  std::string content;
  if (S_ISREG(info.st_mode)) { content = read(path); }
  if (S_ISLNK(info.st_mode)) {
    std::array<char, 4096> target{};
    const auto size = ::readlink(path.c_str(), target.data(), target.size());
    check(size >= 0 && static_cast<std::size_t>(size) < target.size(), "generated symlink inspection failed");
    content.assign(target.data(), static_cast<std::size_t>(size));
  }
  return Stamp{info.st_dev, info.st_ino, info.st_mode, info.st_uid, info.st_gid, info.st_nlink,
      info.st_size, info.st_mtim, info.st_ctim, std::move(content)};
}
bool sameTime(timespec a, timespec b) { return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec; }
bool same(const std::optional<Stamp>& a, const std::optional<Stamp>& b) {
  if (a.has_value() != b.has_value()) { return false; }
  if (!a) { return true; }
  // Fixture reads may change atime; every other observed field must remain.
  return a->device == b->device && a->inode == b->inode && a->mode == b->mode && a->uid == b->uid
      && a->gid == b->gid && a->links == b->links && a->size == b->size
      && sameTime(a->modified, b->modified) && sameTime(a->changed, b->changed) && a->content == b->content;
}
struct Observed { std::filesystem::path path; std::optional<Stamp> state; };
struct Area final {
  std::filesystem::path root, profile;
  std::vector<std::filesystem::path> leaves, directories;
  std::optional<storage::ProfileLease> lease;
  Area() {
    try {
      char pattern[] = "/tmp/biglybtc-classic-create-view-XXXXXX";
      const auto* path = ::mkdtemp(pattern);
      check(path != nullptr, "unable to create owned generated root");
      root = path; profile = root / "profile"; directory(profile);
      track(profile / ".azlock");
      for (const char* suffix : {"", ".bak", ".saving"}) { track(profile / (std::string("tables.config") + suffix)); }
      auto held = storage::ProfileLease::acquire(profile);
      check(held.has_value(), "unable to acquire generated lease");
      lease.emplace(std::move(*held));
    } catch (...) { cleanup(); throw; }
  }
  ~Area() { cleanup(); }
  Area(const Area&) = delete;
  Area& operator=(const Area&) = delete;
  const storage::ProfileLease& held() const { return *lease; }
  void track(const std::filesystem::path& path) {
    if (std::find(leaves.begin(), leaves.end(), path) == leaves.end()) { leaves.push_back(path); }
  }
  void directory(const std::filesystem::path& path) {
    directories.push_back(path);
    check(::mkdir(path.c_str(), 0700) == 0, "unable to create owned directory");
  }
  void write(const std::filesystem::path& path, std::string_view bytes) {
    track(path);
    Fd fd(::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    check(fd.value >= 0, "unable to create exclusive generated file");
    for (std::size_t offset = 0; offset < bytes.size();) {
      const auto count = ::write(fd.value, bytes.data() + offset, bytes.size() - offset);
      if (count < 0 && errno == EINTR) { continue; }
      check(count > 0, "unable to write generated file");
      offset += static_cast<std::size_t>(count);
    }
    check(::fsync(fd.value) == 0, "unable to sync generated input");
  }
  std::vector<Observed> observe() const {
    std::vector<Observed> result;
    for (const auto& path : leaves) { result.push_back({path, stamp(path)}); }
    for (const auto& path : directories) { result.push_back({path, stamp(path)}); }
    result.push_back({root, stamp(root)});
    return result;
  }
  static void unchanged(const std::vector<Observed>& before) {
    for (const auto& entry : before) { check(same(entry.state, stamp(entry.path)), "operation changed refused/generated input bytes or identity/metadata"); }
  }
private:
  void cleanup() noexcept {
    lease.reset();
    for (auto i = leaves.rbegin(); i != leaves.rend(); ++i) { (void)::unlink(i->c_str()); }
    for (auto i = directories.rbegin(); i != directories.rend(); ++i) { (void)::rmdir(i->c_str()); }
    if (!root.empty()) { (void)::rmdir(root.c_str()); }
  }
};
core::ClassicPreferences preferences(bool second = false) {
  core::ClassicPreferences result;
  for (std::size_t pane = 0; pane < 2; ++pane) {
    auto& value = pane == 0 ? result.incomplete : result.complete;
    for (std::size_t logical = 0; logical < core::ClassicPreferenceIds.size(); ++logical) {
      value.columns[logical].visible = (logical + pane + (second ? 1 : 0)) % 3 != 0;
      if (logical != 0) { value.columns[logical].width = static_cast<int>(120 + logical * 13 + pane * 73 + (second ? 250 : 0)); }
      value.order[logical] = core::ClassicPreferenceIds[(logical + pane * 5 + (second ? 3 : 0)) % 13];
    }
    value.sortId = core::ClassicPreferenceIds[second ? pane + 10 : pane + 2];
    value.ascending = (pane == 0) != second;
  }
  result.incomplete.columns[1].width = 16;
  result.complete.columns[12].width = 8192;
  check(core::validateClassicPreferences(result).has_value(), "generated preferences invalid");
  return result;
}
void decoded(const core::Document& document, const core::ClassicPreferences& expected) {
  check(document.serialize() == document.originalBytes(), "successful baseline is not pristine");
  const auto value = core::readClassicPreferences(document);
  check(value && value->has_value() && **value == expected, "created native preference roundtrip differs");
}
template<typename T> void error(const std::expected<T, storage::Error>& value, storage::ErrorCode code) {
  check(!value && value.error().code == code, "creation error category differs");
}
struct SyncGate { dev_t device; ino_t inode; bool armed = false; std::size_t matches = 0; };
thread_local SyncGate* syncGate = nullptr;
struct Observer {
  std::array<storage::SaveStage, 3> stages{};
  std::size_t count = 0;
  storage::SaveStage trigger = storage::SaveStage::TemporarySynced;
  std::stop_source* stop = nullptr;
  SyncGate* sync = nullptr;
  Area* createPrimary = nullptr;
};
thread_local Observer* observer = nullptr;
void observe(storage::SaveStage stage) {
  auto* current = observer;
  check(current && current->count < current->stages.size(), "unexpected creation stage");
  current->stages[current->count++] = stage;
  if (stage != current->trigger) { return; }
  if (current->stop) { current->stop->request_stop(); }
  if (current->sync) { current->sync->armed = true; }
  if (current->createPrimary) { current->createPrimary->write(current->createPrimary->profile / "tables.config", "d7:foreigni9ee"); }
}
struct Observing final {
  explicit Observing(Observer& state) { check(!observer && !syncGate, "nested observer fixture"); observer = &state; syncGate = state.sync; }
  ~Observing() { observer = nullptr; syncGate = nullptr; }
};
void completeStages(const Observer& state) {
  check(state.count == 2 && state.stages[0] == storage::SaveStage::TemporarySynced
      && state.stages[1] == storage::SaveStage::PrimaryRenamed, "create must emit only TemporarySynced then PrimaryRenamed");
}
struct Mask final {
  mode_t previous = ::umask(0);
  ~Mask() { ::umask(previous); }
};
} // namespace

extern "C" int __real_fsync(int descriptor);
extern "C" int __wrap_fsync(int descriptor) {
  auto* gate = syncGate;
  struct stat info{};
  if (gate && gate->armed && ::fstat(descriptor, &info) == 0 && S_ISDIR(info.st_mode)
      && info.st_dev == gate->device && info.st_ino == gate->inode) {
    gate->armed = false; ++gate->matches;
    errno = EIO; return -1; // Injected owned-directory result only, not a real disk fault.
  }
  return __real_fsync(descriptor);
}

int main() {
  std::string active;
  std::size_t cases = 0;
  try {
    const auto run = [&](std::string name, auto function) { active = std::move(name); function(); ++cases; };
    const auto first = preferences(), second = preferences(true);
    run("absent create full snapshot no original fallback and ordinary next save", [&] {
      Area area;
      const auto missing = storage::readColumnLayoutsView(area.held());
      check(missing && missing->missing() && !missing->recoverySource && !missing->nativePreferences, "all absent did not read missing");
      Observer state;
      std::expected<std::shared_ptr<const core::Document>, storage::Error> created = std::unexpected(storage::Error{});
      { Mask mask; Observing observing(state); created = storage::createClassicPreferencesView(area.held(), first, {}, observe); }
      check(created.has_value(), "absent native configuration create failed"); completeStages(state); decoded(**created, first);
      check(area.held().valid(), "create consumed borrowed profile lease");
      const auto primary = stamp(area.profile / "tables.config");
      check(primary && (primary->mode & 0777) == 0600 && primary->links == 1 && primary->uid == ::geteuid(), "new primary ownership/mode differs");
      check(primary->content == (*created)->originalBytes() && !stamp(area.profile / "tables.config.bak")
          && !stamp(area.profile / "tables.config.saving"), "successful create left recovery or different bytes");
      check((*created)->root().children.size() == 2 && !(*created)->find("Table.MyTorrents") && !(*created)->find("Table.MySeeders"), "create fabricated original Java fallback keys");
      auto view = storage::readColumnLayoutsView(area.held());
      check(view && view->snapshot && view->nativePreferences == first && view->recoverySource == storage::RecoverySource::Primary, "created preferences did not read through normal adapter");
      check(view->snapshot->incomplete.presence == core::ColumnLayoutPresence::Missing
          && view->snapshot->complete.presence == core::ColumnLayoutPresence::Missing, "private preferences invented original table facts");
      const auto beforeRetry = area.observe();
      error(storage::createClassicPreferencesView(area.held(), second), storage::ErrorCode::Conflict); Area::unchanged(beforeRetry);
      const auto saved = storage::saveClassicPreferencesView(area.held(), **created, storage::RecoverySource::Primary, second);
      check(saved.has_value(), "created pristine baseline failed ordinary next save"); decoded(**saved, second);
      check(read(area.profile / "tables.config.bak") == (*created)->originalBytes(), "next save did not back up actual created bytes");
      decoded(**created, first);
    });
    run("created owned snapshot outlives lease and generated profile", [&] {
      std::shared_ptr<const core::Document> retained;
      { Area area; auto created = storage::createClassicPreferencesView(area.held(), second); check(created.has_value(), "lifetime create failed"); retained = std::move(*created); }
      check(retained != nullptr, "snapshot missing after lease/profile release"); decoded(*retained, second);
    });
    for (const char* suffix : {"", ".bak", ".saving"}) {
      for (int kind = 0; kind < 7; ++kind) {
        run("present candidate refuses without following or touching " + std::string(suffix) + '/' + std::to_string(kind), [&] {
          Area area; const auto path = area.profile / (std::string("tables.config") + suffix);
          area.write(area.root / "sentinel", "untouched target bytes");
          if (kind < 3) { area.write(path, kind == 0 ? "de" : kind == 1 ? "malformed recovery bytes" : ""); }
          else if (kind == 3) { check(::symlink((area.root / "sentinel").c_str(), path.c_str()) == 0, "symlink fixture failed"); }
          else if (kind == 4) { check(::mkfifo(path.c_str(), 0600) == 0, "FIFO fixture failed"); }
          else if (kind == 5) { area.directory(path); }
          else { check(::link((area.root / "sentinel").c_str(), path.c_str()) == 0, "hardlink fixture failed"); }
          const auto before = area.observe(); Observer state;
          { Observing observing(state); error(storage::createClassicPreferencesView(area.held(), first, {}, observe), storage::ErrorCode::Conflict); }
          check(state.count == 0, "existing candidate reached creation observer"); Area::unchanged(before);
        });
      }
    }
    run("all candidates existing remain distinct and unchanged", [&] {
      Area area; area.write(area.profile / "tables.config", "d1:ai1ee");
      area.write(area.profile / "tables.config.bak", "d1:bi2ee"); area.write(area.profile / "tables.config.saving", "broken");
      const auto before = area.observe(); error(storage::createClassicPreferencesView(area.held(), first), storage::ErrorCode::Conflict); Area::unchanged(before);
    });
    for (int kind = 0; kind < 7; ++kind) {
      run("invalid private preferences cannot create any configuration " + std::to_string(kind), [&] {
        Area area; auto invalid = first;
        if (kind == 0) { invalid.incomplete.columns[0].width = 30; }
        if (kind == 1) { invalid.complete.columns[1].width.reset(); }
        if (kind == 2) { invalid.incomplete.columns[12].width = 15; }
        if (kind == 3) { invalid.complete.columns[2].width = 8193; }
        if (kind == 4) { invalid.incomplete.order[0] = invalid.incomplete.order[1]; }
        if (kind == 5) { invalid.complete.order[1] = "unknown-plugin"; }
        if (kind == 6) { invalid.incomplete.sortId = "status"; }
        const auto before = area.observe(); Observer state;
        { Observing observing(state); error(storage::createClassicPreferencesView(area.held(), invalid, {}, observe), storage::ErrorCode::InvalidFormat); }
        check(state.count == 0, "invalid preferences reached publication"); Area::unchanged(before);
      });
    }
    for (bool stopped : {false, true}) {
      run("moved-from lease refuses no files stop priority " + std::to_string(stopped), [&] {
        Area area; auto live = std::move(*area.lease); const auto before = area.observe();
        std::stop_source stop; if (stopped) { stop.request_stop(); }
        error(storage::createClassicPreferencesView(area.held(), first, stop.get_token()), stopped ? storage::ErrorCode::Cancelled : storage::ErrorCode::UnsafePath);
        check(live.valid(), "create consumed separately moved live lease"); Area::unchanged(before);
      });
    }
    run("pre-stop absent profile refuses no files or observers", [&] {
      Area area; const auto before = area.observe(); Observer state; std::stop_source stop; stop.request_stop();
      { Observing observing(state); error(storage::createClassicPreferencesView(area.held(), first, stop.get_token(), observe), storage::ErrorCode::Cancelled); }
      check(state.count == 0, "pre-stop reached observer"); Area::unchanged(before);
    });
    for (const auto stage : {storage::SaveStage::TemporarySynced, storage::SaveStage::PrimaryRenamed}) {
      run("observer cancellation returns actual successful creation " + std::to_string(static_cast<int>(stage)), [&] {
        Area area; std::stop_source stop; Observer state; state.trigger = stage; state.stop = &stop;
        std::expected<std::shared_ptr<const core::Document>, storage::Error> created = std::unexpected(storage::Error{});
        { Observing observing(state); created = storage::createClassicPreferencesView(area.held(), second, stop.get_token(), observe); }
        check(created.has_value() && stop.stop_requested(), "successful creation hidden by late cancellation"); completeStages(state); decoded(**created, second);
        check(read(area.profile / "tables.config") == (*created)->originalBytes()
            && !stamp(area.profile / "tables.config.bak") && !stamp(area.profile / "tables.config.saving"), "late stop changed actual created files");
      });
    }
    run("late primary arrival plus stop reports conflict without overwrite", [&] {
      Area area; std::stop_source stop; Observer state; state.stop = &stop; state.createPrimary = &area;
      { Observing observing(state); auto result = storage::createClassicPreferencesView(area.held(), first, stop.get_token(), observe); error(result, storage::ErrorCode::Conflict); }
      check(stop.stop_requested() && state.count == 1, "late primary/stop stage not reached");
      check(read(area.profile / "tables.config") == "d7:foreigni9ee", "late primary overwritten");
      check(!stamp(area.profile / "tables.config.bak"), "late primary was promoted to backup");
      auto saving = core::Document::parse(read(area.profile / "tables.config.saving")); check(saving.has_value(), "owned recovery bytes not retained"); decoded(*saving, first);
    });
    run("published directory sync fault remains actual Io after cancellation", [&] {
      Area area; const auto directory = stamp(area.profile); SyncGate sync{directory->device, directory->inode};
      std::stop_source stop; Observer state; state.trigger = storage::SaveStage::PrimaryRenamed; state.stop = &stop; state.sync = &sync;
      { Observing observing(state); auto result = storage::createClassicPreferencesView(area.held(), second, stop.get_token(), observe);
        error(result, storage::ErrorCode::Io); check(result.error().message.find("already occurred") != std::string::npos, "published-error ambiguity diagnostic lost"); }
      completeStages(state); check(sync.matches == 1 && stop.stop_requested(), "owned final sync fault/stop gate did not fire");
      auto published = core::Document::parse(read(area.profile / "tables.config")); check(published.has_value(), "published configuration missing after reported Io"); decoded(*published, second);
      check(!stamp(area.profile / "tables.config.bak") && !stamp(area.profile / "tables.config.saving"), "post-publication failure altered recovery facts");
    });
    std::cout << "PASS " << cases << " classic-preferences create-view cases; " << assertions << " assertions\n";
    return 0;
  } catch (const std::exception& failure) { std::cerr << "FAIL " << active << ": " << failure.what() << '\n'; return 1; }
}
