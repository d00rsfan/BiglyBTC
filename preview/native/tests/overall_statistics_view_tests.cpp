#include "overall_statistics_view.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
using namespace biglybtc;
std::size_t assertions = 0;
void require(bool value, std::string_view message) {
  ++assertions;
  if (!value) { throw std::runtime_error(std::string(message)); }
}
struct Fd {
  int value;
  explicit Fd(int descriptor) : value(descriptor) {}
  ~Fd() { if (value >= 0) { ::close(value); } }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
std::string read(const std::filesystem::path& path) {
  Fd file(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  require(file.value >= 0, "explicit generated file open");
  std::string value;
  std::array<char, 4096> buffer{};
  for (;;) {
    const auto count = ::read(file.value, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) { continue; }
    require(count >= 0, "explicit generated file read");
    if (count == 0) { return value; }
    value.append(buffer.data(), static_cast<std::size_t>(count));
  }
}
void write(const std::filesystem::path& path, std::string_view bytes) {
  Fd file(::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600));
  require(file.value >= 0, "explicit generated file write open");
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = ::write(file.value, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) { continue; }
    require(count > 0, "explicit generated file write");
    offset += static_cast<std::size_t>(count);
  }
}
struct Area {
  std::filesystem::path root;
  std::optional<storage::ProfileLease> lease;
  Area() {
    char pattern[] = "/tmp/biglybtc-overall-writer-tests-XXXXXX";
    const auto* created = ::mkdtemp(pattern);
    require(created != nullptr, "private generated profile directory");
    root = created;
    auto held = storage::ProfileLease::acquire(root);
    require(held.has_value(), "private generated profile lease");
    lease.emplace(std::move(*held));
  }
  ~Area() {
    lease.reset();
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
  Area(const Area&) = delete;
  Area& operator=(const Area&) = delete;
  std::filesystem::path primary() const { return root / "BiglyBT.statistics"; }
  std::filesystem::path backup() const { return root / "BiglyBT.statistics.bak"; }
  std::filesystem::path saving() const { return root / "BiglyBT.statistics.saving"; }
  const storage::ProfileLease& held() const { return *lease; }
};
struct SyncFault { dev_t device; ino_t inode; bool armed = false; bool directory = true; std::size_t matches = 0; };
thread_local SyncFault* syncFault = nullptr;
struct Observation {
  std::vector<storage::SaveStage> stages;
  storage::SaveStage trigger = storage::SaveStage::PrimaryRenamed;
  std::stop_source* stop = nullptr;
  SyncFault* failure = nullptr;
  std::filesystem::path introduce;
  Observation() { stages.reserve(3); }
};
thread_local Observation* observation = nullptr;
void observe(storage::SaveStage stage) {
  auto* current = observation;
  if (!current) { return; }
  current->stages.push_back(stage);
  if (stage != current->trigger) { return; }
  if (current->stop) { current->stop->request_stop(); }
  if (current->failure) { current->failure->armed = true; }
  if (!current->introduce.empty()) { write(current->introduce, "de"); }
}
struct Observe {
  explicit Observe(Observation& value) { require(observation == nullptr, "observer not nested"); observation = &value; }
  ~Observe() { observation = nullptr; syncFault = nullptr; }
  Observe(const Observe&) = delete;
  Observe& operator=(const Observe&) = delete;
};
SyncFault directoryFault(const Area& area) {
  struct stat info{};
  require(::fstat(area.held().directoryHandle(), &info) == 0, "identify owned directory for injected sync result");
  return {info.st_dev, info.st_ino, false, true, 0};
}
struct Identity { dev_t device; ino_t inode; mode_t mode; off_t size; timespec modified; };
Identity identity(const std::filesystem::path& path) {
  struct stat info{};
  require(::lstat(path.c_str(), &info) == 0, "inspect explicit generated path");
  return {info.st_dev, info.st_ino, info.st_mode, info.st_size, info.st_mtim};
}
bool same(const Identity& left, const Identity& right) {
  return left.device == right.device && left.inode == right.inode && left.mode == right.mode
      && left.size == right.size && left.modified.tv_sec == right.modified.tv_sec && left.modified.tv_nsec == right.modified.tv_nsec;
}
void absenceCreationAndOrdinarySave() {
  Area area;
  auto missing = storage::readOverallStatisticsView(area.held());
  require(missing && missing->missing(), "true missing observed distinctly");
  core::OverallValues values{}; values[0] = 37; values[1] = -11;
  Observation first;
  storage::OverallStatisticsView baseline;
  {
    Observe active(first);
    auto created = storage::saveOverallStatisticsView(area.held(), *missing, values, {}, observe);
    require(created && created->disposition == storage::OverallSaveDisposition::Committed, "first publication committed");
    baseline = created->current;
  }
  require(first.stages == std::vector{storage::SaveStage::TemporarySynced, storage::SaveStage::PrimaryRenamed}, "first creation exact two stages");
  require((identity(area.primary()).mode & 07777) == 0600 && !std::filesystem::exists(area.backup())
      && !std::filesystem::exists(area.saving()), "first primary private and no invented backup");
  const auto original = read(area.primary());
  require(baseline.snapshot->values == values && baseline.recoverySource == storage::RecoverySource::Primary, "owned returned primary baseline");
  values[12] = std::numeric_limits<std::int64_t>::max();
  Observation next;
  {
    Observe active(next);
    auto saved = storage::saveOverallStatisticsView(area.held(), baseline, values, {}, observe);
    require(saved && saved->current.snapshot->values == values, "next ordinary save from first baseline");
    baseline = saved->current;
  }
  require(next.stages == std::vector{storage::SaveStage::TemporarySynced, storage::SaveStage::BackupRenamed,
      storage::SaveStage::PrimaryRenamed} && read(area.backup()) == original, "ordinary transaction preserves exact previous primary as backup");
  const auto before = identity(area.primary());
  Observation noop;
  {
    Observe active(noop);
    auto saved = storage::saveOverallStatisticsView(area.held(), baseline, values, {}, observe);
    require(saved && saved->disposition == storage::OverallSaveDisposition::Unchanged, "identical absolute document is no-op");
  }
  require(noop.stages.empty() && same(before, identity(area.primary())), "no-op changes no primary inode/metadata or transaction stages");
}
void opaqueRetentionAndCas() {
  Area area;
  const std::string opaque("\0\xffz", 3);
  const auto source = std::string("d4:tail3:") + opaque + "3:alld6:plugin3:" + opaque + "10:downloadedi8ee4:lasti-3ee";
  write(area.primary(), source);
  auto baseline = storage::readOverallStatisticsView(area.held());
  require(baseline && baseline->snapshot->values[0] == 8, "read original primary snapshot");
  auto values = baseline->snapshot->values; values[0] = 99;
  auto saved = storage::saveOverallStatisticsView(area.held(), *baseline, values);
  require(saved && read(area.backup()) == source, "backup raw bytes exact");
  const auto& doc = *saved->current.snapshot->source;
  require(doc.find("tail")->bytes == opaque && doc.find("all")->find("plugin")->bytes == opaque
      && doc.find("last")->integer == -3, "root and all unknown bytes retained through actual transaction");
  const auto current = read(area.primary());
  auto stale = storage::saveOverallStatisticsView(area.held(), *baseline, values);
  require(!stale && stale.error().code == storage::ErrorCode::Conflict && read(area.primary()) == current
      && !std::filesystem::exists(area.saving()), "stale primary fails CAS before write");
  storage::OverallStatisticsView falseMissing;
  auto conflict = storage::saveOverallStatisticsView(area.held(), falseMissing, values);
  require(!conflict && conflict.error().code == storage::ErrorCode::Conflict, "missing baseline cannot overwrite present primary");
}
void recoveryIsReadOnly() {
  for (const bool primaryPresent : {false, true}) {
    Area area;
    if (primaryPresent) { write(area.primary(), "broken"); }
    write(area.backup(), "d3:alld10:downloadedi3eee");
    write(area.saving(), "d3:alld10:downloadedi5eee");
    auto loaded = storage::readOverallStatisticsView(area.held());
    require(loaded && loaded->recoverySource == (primaryPresent ? storage::RecoverySource::Backup : storage::RecoverySource::Saving)
        && loaded->snapshot->values[0] == (primaryPresent ? 3 : 5), "exact candidate precedence");
    const auto backup = read(area.backup());
    const auto saving = read(area.saving());
    auto refused = storage::saveOverallStatisticsView(area.held(), *loaded, {});
    require(!refused && refused.error().code == storage::ErrorCode::Conflict && read(area.backup()) == backup
        && read(area.saving()) == saving, "recovery cannot be silently rewritten by writer");
  }
  Area malformed;
  write(malformed.backup(), "bad");
  auto loaded = storage::readOverallStatisticsView(malformed.held());
  require(!loaded && loaded.error().code == storage::ErrorCode::InvalidFormat, "malformed recovery is not missing");
  auto refused = storage::saveOverallStatisticsView(malformed.held(), {}, {});
  require(!refused && refused.error().code == storage::ErrorCode::InvalidFormat, "missing save checks malformed candidates too");
}
void fixedFilenameBoundary() {
  Area area;
  write(area.primary(), "de");
  require(storage::ConfigFile::load(area.held(), "BiglyBT.statistics", core::Limits{}).has_value(), "exact filename extension admitted");
  write(area.root / "kept.config", "de");
  require(storage::ConfigFile::load(area.held(), "kept.config", core::Limits{}).has_value(), "old config filename behavior retained");
  for (const auto* name : {"biglybt.statistics", "Other.statistics", "BiglyBT.statistics.bak", "BiglyBT.statistics.saving",
      "BiglyBT.statistics.", "BiglyBT.statistics/", "./BiglyBT.statistics", "../BiglyBT.statistics", "/BiglyBT.statistics",
      "BiglyBT.statistics\n", ".BiglyBT.statistics", "BiglyBT.statistic"}) {
    auto loaded = storage::ConfigFile::load(area.held(), name, core::Limits{});
    require(!loaded && loaded.error().code == storage::ErrorCode::UnsafePath, "near-match load filename rejected");
    auto created = storage::ConfigFile::createMissing(area.held(), name, "de", core::Limits{});
    require(!created && created.error().code == storage::ErrorCode::UnsafePath, "near-match creation filename rejected");
  }
}
void editedAndContradictoryBaselines() {
  Area area;
  write(area.primary(), "d1:xi1e3:alldee");
  auto loaded = storage::readOverallStatisticsView(area.held());
  require(loaded.has_value(), "baseline for negative admission");
  auto forged = *loaded->snapshot;
  forged.values[0] = 17;
  storage::OverallStatisticsView bad{std::make_shared<const core::OverallDocument>(forged), storage::RecoverySource::Primary};
  auto refused = storage::saveOverallStatisticsView(area.held(), bad, {});
  require(!refused && refused.error().code == storage::ErrorCode::Conflict, "projected facts cannot contradict original bytes");
  auto edited = *loaded->snapshot->source;
  require(edited.setInteger("x", 2), "make ordinary public document edit");
  forged = *loaded->snapshot; forged.source = std::make_shared<const core::Document>(std::move(edited));
  bad.snapshot = std::make_shared<const core::OverallDocument>(forged);
  refused = storage::saveOverallStatisticsView(area.held(), bad, {});
  require(!refused && refused.error().code == storage::ErrorCode::Conflict, "edited public tree rejected");
  auto hostile = *loaded->snapshot->source;
  auto& root = const_cast<core::Node&>(hostile.root());
  root.children[0].bytes = std::string_view(reinterpret_cast<const char*>(1), 1);
  forged.source = std::make_shared<const core::Document>(std::move(hostile));
  bad.snapshot = std::make_shared<const core::OverallDocument>(forged);
  refused = storage::saveOverallStatisticsView(area.held(), bad, {});
  require(!refused && refused.error().code == storage::ErrorCode::Conflict, "hostile byte view is compared without dereference");
  auto highLimits = core::Limits{}; highLimits.depth = 128;
  const auto deepBytes = std::string("d1:x") + std::string(65, 'l') + "i0e" + std::string(65, 'e') + "3:alldee";
  auto higherQuota = core::Document::parse(deepBytes, highLimits);
  require(higherQuota.has_value(), "prepare caller tree parsed beyond ordinary depth");
  forged.source = std::make_shared<const core::Document>(std::move(*higherQuota));
  bad.snapshot = std::make_shared<const core::OverallDocument>(forged);
  refused = storage::saveOverallStatisticsView(area.held(), bad, {});
  require(!refused && refused.error().code == storage::ErrorCode::Unsupported,
      "higher-quota caller document cannot bypass ordinary save bounds");
  bad = {}; bad.recoverySource = storage::RecoverySource::Primary;
  refused = storage::saveOverallStatisticsView(area.held(), bad, {});
  require(!refused && refused.error().code == storage::ErrorCode::Conflict, "contradictory missing state rejected");
  require(!std::filesystem::exists(area.saving()), "invalid baseline never begins transaction");
}
void cancellationBoundary() {
  Area area;
  std::stop_source stopped; stopped.request_stop();
  auto readCancelled = storage::readOverallStatisticsView(area.held(), stopped.get_token());
  auto saveCancelled = storage::saveOverallStatisticsView(area.held(), {}, {}, stopped.get_token());
  require(!readCancelled && !saveCancelled && readCancelled.error().code == storage::ErrorCode::Cancelled
      && saveCancelled.error().code == storage::ErrorCode::Cancelled && !std::filesystem::exists(area.primary()), "pre-stop read/save has no publication");
  std::stop_source during;
  Observation events; events.trigger = storage::SaveStage::TemporarySynced; events.stop = &during;
  Observe active(events);
  auto saved = storage::saveOverallStatisticsView(area.held(), {}, {}, during.get_token(), observe);
  require(saved && during.stop_requested() && std::filesystem::exists(area.primary()), "stop during synchronous creation cannot hide committed result");
}
void actualTransactionFailures() {
  {
    Area area;
    write(area.primary(), "de");
    auto baseline = storage::readOverallStatisticsView(area.held());
    require(baseline.has_value(), "baseline for exclusive-save conflict");
    write(area.saving(), "preserve");
    auto saved = storage::saveOverallStatisticsView(area.held(), *baseline, {});
    require(!saved && saved.error().code == storage::ErrorCode::Conflict && read(area.primary()) == "de"
        && read(area.saving()) == "preserve", "existing saving path preserved by actual O_EXCL");
  }
  {
    Area area;
    write(area.primary(), "de");
    require(::mkdir(area.backup().c_str(), 0700) == 0, "create owned incompatible backup pathname");
    auto baseline = storage::readOverallStatisticsView(area.held());
    require(baseline.has_value(), "primary valid despite backup directory");
    auto saved = storage::saveOverallStatisticsView(area.held(), *baseline, {});
    require(!saved && saved.error().code == storage::ErrorCode::Io && read(area.primary()) == "de"
        && std::filesystem::exists(area.saving()), "actual backup rename failure retains original and saving");
  }
  {
    Area area;
    auto missing = storage::readOverallStatisticsView(area.held());
    require(missing.has_value(), "missing baseline for no-replace race");
    Observation events; events.trigger = storage::SaveStage::TemporarySynced; events.introduce = area.primary();
    Observe active(events);
    auto saved = storage::saveOverallStatisticsView(area.held(), *missing, {}, {}, observe);
    require(!saved && saved.error().code == storage::ErrorCode::Conflict && read(area.primary()) == "de"
        && std::filesystem::exists(area.saving()), "first publication never overwrites newly introduced primary");
  }
}
void injectedDirectorySyncOutcome() {
  for (const bool creation : {true, false}) {
    Area area;
    if (!creation) { write(area.primary(), "de"); }
    auto baseline = storage::readOverallStatisticsView(area.held());
    require(baseline.has_value(), "baseline for injected post-publication sync result");
    auto fault = directoryFault(area);
    std::stop_source stopped;
    Observation events; events.failure = &fault; events.stop = &stopped;
    Observe active(events); syncFault = &fault;
    auto saved = storage::saveOverallStatisticsView(area.held(), *baseline, {}, stopped.get_token(), observe);
    require(!saved && saved.error().code == storage::ErrorCode::Io
        && saved.error().message.find("already occurred") != std::string::npos && fault.matches == 1
        && stopped.stop_requested(), "post-publication sync uncertainty is Io, never Cancelled or success");
    require(std::filesystem::exists(area.primary()) && !std::filesystem::exists(area.saving()), "actual renamed primary remains after injected final sync result");
    auto reread = storage::readOverallStatisticsView(area.held());
    require(reread && reread->snapshot->hasAll, "reconcile by actual read rather than reapplying delta");
  }
}
void unsafeAndMalformedInputs() {
  {
    Area area;
    write(area.root / "target", "de");
    require(::symlink("target", area.primary().c_str()) == 0, "owned symlink fixture");
    auto loaded = storage::readOverallStatisticsView(area.held());
    require(!loaded && loaded.error().code == storage::ErrorCode::UnsafePath, "no-follow primary boundary");
  }
  {
    Area area; write(area.primary(), "d3:alli1ee"); write(area.backup(), "de");
    auto loaded = storage::readOverallStatisticsView(area.held());
    require(!loaded && loaded.error().code == storage::ErrorCode::InvalidFormat, "wrong all is fatal, not hidden by backup");
  }
  {
    Area area; write(area.primary(), std::string(core::Limits{}.inputBytes + 1, 'x'));
    auto loaded = storage::readOverallStatisticsView(area.held());
    require(!loaded && loaded.error().code == storage::ErrorCode::InvalidFormat, "ordinary maximum before permissive ConfigFile limits");
  }
}
} // namespace

extern "C" int __real_fsync(int descriptor);
extern "C" int __wrap_fsync(int descriptor) {
  auto* gate = syncFault;
  struct stat info{};
  if (gate && gate->armed && ::fstat(descriptor, &info) == 0 && S_ISDIR(info.st_mode)
      && gate->directory && info.st_dev == gate->device && info.st_ino == gate->inode) {
    gate->armed = false; ++gate->matches; errno = EIO; return -1;
  }
  return __real_fsync(descriptor);
}

int main() {
  try {
    absenceCreationAndOrdinarySave(); opaqueRetentionAndCas(); recoveryIsReadOnly(); fixedFilenameBoundary();
    editedAndContradictoryBaselines(); cancellationBoundary(); actualTransactionFailures();
    injectedDirectorySyncOutcome(); unsafeAndMalformedInputs();
    std::cout << "PASS overall-statistics-view: 9 generated-profile families; assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL overall-statistics-view: " << error.what() << '\n';
    return 1;
  }
}
