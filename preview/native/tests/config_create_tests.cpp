#include "profile.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <linux/fs.h>
#include <fcntl.h>
#include <iostream>
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
struct FsyncArm {
  dev_t device;
  ino_t inode;
  bool armed = false;
  std::size_t failures = 0;
};
thread_local FsyncArm* armedFsync = nullptr;
struct RenameArm {
  dev_t device;
  ino_t inode;
  int descriptor;
  int injectedError = 0;
  std::string_view foreignBytes{};
  bool armed = true;
  std::size_t matches = 0;
  int fixtureError = 0;
  std::size_t realCalls = 0;
  int realResult = 1;
  int realError = 0;
  struct stat foreign{};
};
thread_local RenameArm* armedRename = nullptr;
} // namespace
extern "C" int __real_fsync(int descriptor);
extern "C" int __wrap_fsync(int descriptor) {
  auto* gate = armedFsync;
  struct stat info{};
  if (gate && gate->armed && ::fstat(descriptor, &info) == 0 && S_ISDIR(info.st_mode)
      && info.st_dev == gate->device && info.st_ino == gate->inode) {
    gate->armed = false;
    ++gate->failures;
    // Deterministic test-only syscall-result injection on the exact generated
    // directory after publication, not a real disk/power-loss durability test.
    errno = EIO;
    return -1;
  }
  return __real_fsync(descriptor);
}
extern "C" int __real_renameat2(int oldDirectory, const char* oldName,
    int newDirectory, const char* newName, unsigned int flags);
extern "C" int __wrap_renameat2(int oldDirectory, const char* oldName,
    int newDirectory, const char* newName, unsigned int flags) {
  auto* gate = armedRename;
  struct stat oldInfo{}, newInfo{};
  if (!gate || !gate->armed || oldDirectory != gate->descriptor || newDirectory != gate->descriptor || !oldName || !newName
      || std::strcmp(oldName, "tables.config.saving") != 0 || std::strcmp(newName, "tables.config") != 0
      || flags != RENAME_NOREPLACE || ::fstat(oldDirectory, &oldInfo) != 0 || ::fstat(newDirectory, &newInfo) != 0
      || !S_ISDIR(oldInfo.st_mode) || !S_ISDIR(newInfo.st_mode)
      || oldInfo.st_dev != gate->device || oldInfo.st_ino != gate->inode
      || newInfo.st_dev != gate->device || newInfo.st_ino != gate->inode) {
    return __real_renameat2(oldDirectory, oldName, newDirectory, newName, flags);
  }
  gate->armed = false; ++gate->matches;
  if (gate->injectedError != 0) { errno = gate->injectedError; return -1; }
  // Introduce a different actor's Primary after all source prechecks, exactly
  // at the actual no-replace syscall boundary within our held generated dir.
  const int file = ::openat(newDirectory, newName, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (file < 0) { gate->fixtureError = errno; return -1; }
  std::size_t offset = 0;
  while (offset < gate->foreignBytes.size()) {
    const auto count = ::write(file, gate->foreignBytes.data() + offset, gate->foreignBytes.size() - offset);
    if (count < 0 && errno == EINTR) { continue; }
    if (count <= 0) { gate->fixtureError = count < 0 ? errno : EIO; break; }
    offset += static_cast<std::size_t>(count);
  }
  if (!gate->fixtureError && (__real_fsync(file) != 0 || ::fstat(file, &gate->foreign) != 0)) { gate->fixtureError = errno; }
  if (::close(file) != 0 && !gate->fixtureError) { gate->fixtureError = errno; }
  if (gate->fixtureError) { errno = gate->fixtureError; return -1; }
  ++gate->realCalls;
  gate->realResult = __real_renameat2(oldDirectory, oldName, newDirectory, newName, flags);
  gate->realError = errno;
  errno = gate->realError;
  return gate->realResult;
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
      char pattern[] = "/tmp/biglybtc-config-create-tests-XXXXXX";
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

// All inputs are newly generated. No existing Java/client profile, payload,
// network, source oracle or unowned process participates in these tests.
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string raw(std::string_view marker = "first") {
  return "d6:opaque" + bytes(std::string("\0\xff\r\n", 4)) + "4:porti7e6:marker" + bytes(marker) + "e";
}
std::string changed(std::string input) {
  const auto offset = input.find("4:porti7e"); require(offset != std::string::npos, "generated port slot missing");
  input[offset + 7] = '8'; return input;
}
void tracks(Area& area, std::string_view name = "tables.config") {
  for (const auto* suffix : {"", ".bak", ".saving"}) { area.track(area.profile / (std::string(name) + suffix)); }
}
void sameRenamed(const Stamp& before, const Stamp& after) {
  require(before.device == after.device && before.inode == after.inode && before.mode == after.mode
      && before.uid == after.uid && before.gid == after.gid && before.links == after.links
      && before.size == after.size && sameTime(before.modified, after.modified) && before.bytes == after.bytes,
      "renamed primary lost inode/mode/content");
}
template<typename T>
void error(const std::expected<T, storage::Error>& result, storage::ErrorCode code) {
  require(!result && result.error().code == code, "createMissing error category differs");
}
struct Mask final { mode_t previous; explicit Mask(mode_t value = 0) : previous(::umask(value)) {} ~Mask() { ::umask(previous); } };
void candidate(Area& area, const std::filesystem::path& path, int kind) {
  if (kind == 0) { area.write(path, raw("other")); }
  else if (kind == 1) { area.write(path, ""); }
  else if (kind == 2) { area.write(path, "malformed existing config"); }
  else if (kind == 3) { area.directory(path); }
  else if (kind == 4) { area.symlink(path, area.root / "sentinel"); }
  else { area.track(path); require(::mkfifo(path.c_str(), 0600) == 0, "unable to create owned candidate FIFO"); }
}
enum class Action { Record, IntroducePrimary, IntroduceBackup, ReplaceSaving, EditSaving, TruncateSaving, SymlinkSaving, LinkSaving, FinalSyncFailure };
struct State {
  Area* area;
  Action action = Action::Record;
  int introducedKind = 0;
  FsyncArm* sync = nullptr;
  std::array<storage::SaveStage, 2> stages{};
  std::size_t count = 0;
  std::optional<Stamp> temporary{};
  std::vector<Observed> afterInterference{};
};
thread_local State* state = nullptr;
void observer(storage::SaveStage stage) {
  auto* current = state;
  require(current && current->count < current->stages.size(), "unexpected create observer stage/count");
  current->stages[current->count++] = stage;
  if (stage == storage::SaveStage::PrimaryRenamed && current->action == Action::FinalSyncFailure) {
    require(current->sync, "missing final directory sync gate"); current->sync->armed = true; return;
  }
  if (stage != storage::SaveStage::TemporarySynced) { return; }
  auto& area = *current->area; const auto saving = area.profile / "tables.config.saving";
  current->temporary = stamp(saving);
  require(current->temporary && S_ISREG(current->temporary->mode) && current->temporary->bytes == raw(), "temporary observer saw wrong complete data");
  if (current->action == Action::IntroducePrimary || current->action == Action::IntroduceBackup) {
    candidate(area, area.profile / (current->action == Action::IntroducePrimary ? "tables.config" : "tables.config.bak"), current->introducedKind);
  } else if (current->action == Action::ReplaceSaving || current->action == Action::SymlinkSaving) {
    const auto old = area.root / "owned-saving-before-replacement"; area.track(old);
    require(::rename(saving.c_str(), old.c_str()) == 0, "unable to preserve original owned saving inode");
    if (current->action == Action::ReplaceSaving) { area.write(saving, raw()); }
    else { area.symlink(saving, area.root / "sentinel"); }
  } else if (current->action == Action::EditSaving || current->action == Action::TruncateSaving) {
    Fd file(::open(saving.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW)); require(file.value >= 0, "unable to open exact owned saving for interference");
    if (current->action == Action::TruncateSaving) { require(::ftruncate(file.value, 0) == 0, "unable to truncate saving fixture"); }
    else {
      const auto replacement = raw("other"); require(replacement.size() == raw().size(), "same-size interference fixture differs");
      require(::pwrite(file.value, replacement.data(), replacement.size(), 0) == static_cast<ssize_t>(replacement.size()), "unable to alter same owned inode");
      // Do not rely on clock granularity to make this same-size edit observable.
      timespec times[2]{{0, UTIME_OMIT}, current->temporary->modified};
      times[1].tv_sec = times[1].tv_sec == 1 ? 2 : 1;
      require(::futimens(file.value, times) == 0, "unable to make owned same-size modification metadata explicit");
      const auto edited = stamp(saving);
      require(edited && !sameTime(edited->modified, current->temporary->modified), "same-size metadata interference was not observable");
    }
    require(::fsync(file.value) == 0, "unable to sync explicit interference");
  } else if (current->action == Action::LinkSaving) {
    const auto alias = area.root / "saving-alias"; area.track(alias);
    require(::link(saving.c_str(), alias.c_str()) == 0, "unable to create owned saving hardlink interference");
  }
  if (current->action != Action::Record && current->action != Action::FinalSyncFailure) { current->afterInterference = area.observe("tables.config"); }
}
struct Observing final {
  explicit Observing(State& value) { require(!state, "nested create observer"); state = &value; armedFsync = value.sync; }
  ~Observing() { state = nullptr; armedFsync = nullptr; }
};
struct Renaming final {
  explicit Renaming(RenameArm& value) { require(!armedRename, "nested rename gate"); armedRename = &value; }
  ~Renaming() { armedRename = nullptr; }
};
void completeStages(const State& value) {
  require(value.count == 2 && value.stages == std::array{storage::SaveStage::TemporarySynced, storage::SaveStage::PrimaryRenamed},
      "create emitted a backup stage or lost temporary/publication order");
}
std::string atSize(std::size_t total) {
  std::size_t payload = total;
  for (int i = 0; i < 20; ++i) {
    const auto prefix = "d6:opaque" + std::to_string(payload) + ':';
    const auto next = total - prefix.size() - 1;
    if (next == payload) { return prefix + std::string(payload, 'x') + 'e'; }
    payload = next;
  }
  throw std::runtime_error("unable to construct exact-size encoded input");
}
} // namespace

int main() {
  std::string active; std::size_t cases = 0;
  try {
    const auto run = [&](std::string name, auto body) { active = std::move(name); body(); ++cases; };
    run("absent candidates create pristine owned mode0600 primary without backup", [&] {
      Area area; tracks(area); area.hold(); auto encoded = raw(); const auto original = encoded;
      area.write(area.root / "sentinel", "untouched unrelated file"); const auto sentinel = stamp(area.root / "sentinel");
      State observed{&area};
      std::expected<storage::ConfigFile, storage::Error> created = std::unexpected(storage::Error{});
      { Mask mask; Observing scope(observed); created = storage::ConfigFile::createMissing(area.held(), "tables.config", encoded, core::Limits{}, observer); }
      require(created.has_value(), "creation from true absence failed"); completeStages(observed);
      require(created->recoverySource() == storage::RecoverySource::Primary && created->document().originalBytes() == original
          && created->document().serialize() == original, "created result is not pristine Primary with exact bytes");
      const auto primary = stamp(area.profile / "tables.config");
      require(primary && S_ISREG(primary->mode) && (primary->mode & 0777) == 0600 && primary->uid == ::geteuid() && primary->links == 1,
          "new primary file mode/ownership/link shape differs");
      require(primary->bytes == original && !stamp(area.profile / "tables.config.bak") && !stamp(area.profile / "tables.config.saving"), "create invented backup/retained temporary");
      require(same(sentinel, stamp(area.root / "sentinel")), "create touched unrelated sentinel");
      encoded.assign(encoded.size(), 'x'); require(created->document().originalBytes() == original, "created document borrowed caller input");
      auto loaded = storage::ConfigFile::load(area.held(), "tables.config", core::Limits{});
      require(loaded && loaded->recoverySource() == storage::RecoverySource::Primary && loaded->document().serialize() == original, "ordinary load cannot read created primary");
      require(created->document().setInteger("port", 8), "created ConfigFile cannot stage ordinary following edit");
      require(created->save(area.held()).has_value(), "created Primary metadata rejected ordinary following save");
      require(readBytes(area.profile / "tables.config") == changed(original) && readBytes(area.profile / "tables.config.bak") == original,
          "following ordinary save lost raw opaque map/previous primary");
      sameRenamed(*primary, *stamp(area.profile / "tables.config.bak"));
    });
    run("restrictive umask still produces exact private owner mode", [&] {
      Area area; tracks(area); area.hold();
      std::expected<storage::ConfigFile, storage::Error> created = std::unexpected(storage::Error{});
      { Mask mask(0777); created = storage::ConfigFile::createMissing(area.held(), "tables.config", raw()); }
      const auto primary = stamp(area.profile / "tables.config");
      require(created && primary && (primary->mode & 07777) == 0600 && primary->bytes == raw(), "create did not normalize actual owned fd mode after restrictive umask");
    });
    run("empty dictionary is a valid present primary", [&] {
      Area area; tracks(area); area.hold(); auto created = storage::ConfigFile::createMissing(area.held(), "tables.config", "de");
      require(created && created->document().originalBytes() == "de" && readBytes(area.profile / "tables.config") == "de", "empty dictionary became absence/failure");
      require(!stamp(area.profile / "tables.config.bak") && !stamp(area.profile / "tables.config.saving"), "empty creation left recovery artifacts");
    });
    run("generic valid config filename stays inside held directory", [&] {
      Area area; tracks(area, "native-preferences.config"); area.hold();
      auto created = storage::ConfigFile::createMissing(area.held(), "native-preferences.config", raw());
      require(created && readBytes(area.profile / "native-preferences.config") == raw() && !stamp(area.profile / "tables.config"), "create ignored explicit safe name");
    });
    run("owned document survives generated profile and lease destruction", [&] {
      std::optional<storage::ConfigFile> retained;
      { Area area; tracks(area); area.hold(); auto created = storage::ConfigFile::createMissing(area.held(), "tables.config", raw()); require(created.has_value(), "lifetime creation failed"); retained.emplace(std::move(*created)); }
      require(retained && retained->document().serialize() == raw() && retained->recoverySource() == storage::RecoverySource::Primary, "created document lost its owned bytes");
    });
    for (int position = 0; position < 3; ++position) { for (int kind = 0; kind < 6; ++kind) { run("existing candidate refused " + std::to_string(position) + '/' + std::to_string(kind), [&] {
      Area area; tracks(area); area.write(area.root / "sentinel", "external candidate target");
      const char* suffix = position == 0 ? "" : position == 1 ? ".bak" : ".saving";
      candidate(area, area.profile / (std::string("tables.config") + suffix), kind); area.hold(); const auto before = area.observe("tables.config");
      State observed{&area};
      { Observing scope(observed); error(storage::ConfigFile::createMissing(area.held(), "tables.config", raw(), core::Limits{}, observer), storage::ErrorCode::Conflict); }
      require(observed.count == 0, "present candidate reached publication observer"); unchanged(before);
    }); } }
    for (const auto& name : {std::string{}, std::string(".hidden.config"), std::string("../sentinel.config"), std::string("nested/escape.config"),
        std::string("tables.conf"), std::string("space name.config"), std::string("line\n.config"), std::string(241, 'x') + ".config"}) {
      run("invalid name prewrite refused " + std::to_string(name.size()), [&] {
        Area area; tracks(area); area.write(area.root / "sentinel.config", raw()); area.hold(); const auto before = area.observe("tables.config");
        error(storage::ConfigFile::createMissing(area.held(), name, raw()), storage::ErrorCode::UnsafePath); unchanged(before);
      });
    }
    run("invalid moved-from lease refuses without consuming live lease", [&] {
      Area area; tracks(area); area.hold(); auto live = std::move(*area.lease); const auto before = area.observe("tables.config");
      error(storage::ConfigFile::createMissing(area.held(), "tables.config", raw()), storage::ErrorCode::UnsafePath);
      require(live.valid(), "create consumed separately moved live lease"); unchanged(before);
    });
    for (const auto& encoded : {std::string{}, std::string("malformed"), std::string("dex"), std::string("le"), std::string("i7e"), std::string("3:abc"), std::string("d1:ai1e1:ai2ee")}) {
      run("invalid or non-dictionary input prewrite refused " + std::to_string(encoded.size()), [&] {
        Area area; tracks(area); area.hold(); const auto before = area.observe("tables.config"); State observed{&area};
        { Observing scope(observed); error(storage::ConfigFile::createMissing(area.held(), "tables.config", encoded, core::Limits{}, observer), storage::ErrorCode::InvalidFormat); }
        require(observed.count == 0, "bad encoded input reached write observer"); unchanged(before);
      });
    }
    for (int field = 0; field < 4; ++field) { run("supplied parser limit enforced before write " + std::to_string(field), [&] {
      Area area; tracks(area); area.hold(); const auto before = area.observe("tables.config"); auto limits = core::Limits{};
      if (field == 0) { limits.inputBytes = raw().size() - 1; }
      if (field == 1) { limits.stringBytes = 3; }
      if (field == 2) { limits.nodes = 2; }
      if (field == 3) { limits.depth = 0; }
      error(storage::ConfigFile::createMissing(area.held(), "tables.config", raw(), limits), storage::ErrorCode::InvalidFormat); unchanged(before);
    }); }
    run("ordinary byte cap rejects otherwise generic-compatible input before write", [&] {
      Area area; tracks(area); area.hold(); const auto before = area.observe("tables.config"); const auto encoded = atSize(core::Limits{}.inputBytes + 1);
      require(core::Document::parse(encoded, storage::compatibleConfigLimits()).has_value(), "generated oversized dictionary not generic-compatible");
      error(storage::ConfigFile::createMissing(area.held(), "tables.config", encoded, core::Limits{}), storage::ErrorCode::InvalidFormat); unchanged(before);
    });
    run("exact supplied byte limit is accepted", [&] {
      Area area; tracks(area); area.hold(); auto limits = core::Limits{}; limits.inputBytes = raw().size();
      auto created = storage::ConfigFile::createMissing(area.held(), "tables.config", raw(), limits);
      require(created && created->document().serialize() == raw(), "exact encoded byte boundary rejected");
    });
    for (const auto action : {Action::IntroducePrimary, Action::IntroduceBackup}) { for (const int kind : {0, 3, 4}) {
      run("candidate introduced after temporary sync is not overwritten " + std::to_string(static_cast<int>(action)) + '/' + std::to_string(kind), [&] {
        Area area; tracks(area); area.write(area.root / "sentinel", "introduced link target"); area.hold(); State observed{&area, action, kind};
        { Observing scope(observed); error(storage::ConfigFile::createMissing(area.held(), "tables.config", raw(), core::Limits{}, observer), storage::ErrorCode::Conflict); }
        require(observed.count == 1 && observed.temporary && readBytes(area.profile / "tables.config.saving") == raw(), "failed publish lost owned complete temporary");
        unchanged(observed.afterInterference);
        if (kind == 0) {
          auto recovered = storage::ConfigFile::load(area.held(), "tables.config", core::Limits{});
          require(recovered && recovered->recoverySource() == (action == Action::IntroducePrimary ? storage::RecoverySource::Primary : storage::RecoverySource::Saving), "ordinary recovery order differs after refused publication");
          require(recovered->document().originalBytes() == (action == Action::IntroducePrimary ? raw("other") : raw()), "recovery selected wrong bytes");
        }
      });
    } }
    for (const auto action : {Action::ReplaceSaving, Action::EditSaving, Action::TruncateSaving, Action::SymlinkSaving, Action::LinkSaving}) {
      run("observable saving tamper is refused " + std::to_string(static_cast<int>(action)), [&] {
        Area area; tracks(area); area.write(area.root / "sentinel", "saving symlink sentinel"); area.hold(); State observed{&area, action};
        { Observing scope(observed); const auto result = storage::ConfigFile::createMissing(area.held(), "tables.config", raw(), core::Limits{}, observer);
          error(result, storage::ErrorCode::Conflict); }
        require(observed.count == 1 && !stamp(area.profile / "tables.config") && !stamp(area.profile / "tables.config.bak"), "tampered creation produced a committed candidate");
        unchanged(observed.afterInterference);
      });
    }
    run("kernel no-replace boundary preserves a new foreign primary", [&] {
      Area area; tracks(area); area.hold(); const auto directory = stamp(area.profile);
      const auto foreign = raw("foreign-owner"); RenameArm gate{directory->device, directory->inode, area.held().directoryHandle(), 0, foreign}; State observed{&area};
      { Renaming rename(gate); Observing scope(observed);
        error(storage::ConfigFile::createMissing(area.held(), "tables.config", raw(), core::Limits{}, observer), storage::ErrorCode::Conflict); }
      require(gate.matches == 1 && gate.fixtureError == 0 && gate.realCalls == 1 && gate.realResult == -1 && gate.realError == EEXIST && observed.count == 1, "actual exact-owned no-replace syscall did not return EEXIST once");
      const auto primary = stamp(area.profile / "tables.config"); const auto saving = stamp(area.profile / "tables.config.saving");
      require(primary && saving && primary->bytes == foreign && primary->device == gate.foreign.st_dev && primary->inode == gate.foreign.st_ino
          && primary->mode == gate.foreign.st_mode && primary->uid == gate.foreign.st_uid && primary->gid == gate.foreign.st_gid
          && primary->links == gate.foreign.st_nlink && primary->size == gate.foreign.st_size
          && sameTime(primary->modified, gate.foreign.st_mtim) && sameTime(primary->changed, gate.foreign.st_ctim), "EEXIST overwrote/altered the introduced primary inode or metadata");
      require(same(observed.temporary, saving) && !stamp(area.profile / "tables.config.bak"), "kernel conflict lost own complete saving or invented backup");
    });
    for (const int kernelError : {ENOSYS, EOPNOTSUPP, EINVAL}) { run("unsupported no-replace kernel result has no fallback " + std::to_string(kernelError), [&] {
      Area area; tracks(area); area.hold(); const auto directory = stamp(area.profile);
      RenameArm gate{directory->device, directory->inode, area.held().directoryHandle(), kernelError}; State observed{&area};
      { Renaming rename(gate); Observing scope(observed);
        error(storage::ConfigFile::createMissing(area.held(), "tables.config", raw(), core::Limits{}, observer), storage::ErrorCode::Unsupported); }
      require(gate.matches == 1 && gate.fixtureError == 0 && gate.realCalls == 0 && observed.count == 1, "unsupported kernel hook was skipped or real rename fallback ran");
      require(!stamp(area.profile / "tables.config") && !stamp(area.profile / "tables.config.bak")
          && same(observed.temporary, stamp(area.profile / "tables.config.saving")), "unsupported no-replace attempted a replacing fallback or changed recovery bytes");
    }); }
    run("final directory sync error reports actual committed primary", [&] {
      Area area; tracks(area); area.hold(); const auto directory = stamp(area.profile); FsyncArm sync{directory->device, directory->inode};
      State observed{&area, Action::FinalSyncFailure, 0, &sync};
      { Observing scope(observed); const auto result = storage::ConfigFile::createMissing(area.held(), "tables.config", raw(), core::Limits{}, observer);
        error(result, storage::ErrorCode::Io); require(result.error().message.find("already occurred") != std::string::npos, "postcommit failure hid replacement state"); }
      completeStages(observed); require(sync.failures == 1 && readBytes(area.profile / "tables.config") == raw(), "postcommit injected error lost actual primary");
      require(!stamp(area.profile / "tables.config.bak") && !stamp(area.profile / "tables.config.saving"), "postcommit error fabricated recovery state");
      auto loaded = storage::ConfigFile::load(area.held(), "tables.config", core::Limits{});
      require(loaded && loaded->recoverySource() == storage::RecoverySource::Primary && loaded->document().serialize() == raw(), "committed primary not readable after reported sync error");
    });
    std::cout << "PASS " << cases << " create-missing config cases; " << assertions << " assertions\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << "FAIL " << active << ": " << failure.what() << '\n'; return 1;
  }
}
