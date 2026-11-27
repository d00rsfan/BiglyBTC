// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_backup.hpp"
#include "biglybtc/library.hpp"
#include "profile_backup_scheduler.hpp"
#include "../platform/utf8_path.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace biglybtc;
namespace fs = std::filesystem;

void check(bool valid, const char* message) {
  if (!valid) { throw std::runtime_error(message); }
}

template<class T>
T take(std::expected<T, storage::Error> value) {
  if (!value) { throw std::runtime_error(value.error().message); }
  return std::move(*value);
}

struct Fixture {
  fs::path root, profile, destination;
  std::optional<storage::ProfileLease> lease;

  Fixture() {
    const auto parent = fs::canonical(fs::temp_directory_path());
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    unsigned attempt = 0;
    do {
      root = parent / platform::pathFromUtf8("BiglyBTC-backup-\u03a9-" + std::to_string(tick) + '-' + std::to_string(++attempt));
    } while (!fs::create_directory(root));
    profile = root / "profile";
    destination = root / "backups";
    fs::create_directory(profile);
    fs::create_directory(destination);
    lease.emplace(take(storage::ProfileLease::acquire(profile)));
  }

  ~Fixture() {
    lease.reset();
    std::error_code error;
    fs::remove_all(root, error);
  }
};

void write(const fs::path& path, std::string_view bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  check(bool(stream), "Write a generated profile file");
}

std::string read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  check(bool(stream), "Read a generated profile file");
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::string encode(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }

void verifyCopy() {
  Fixture files;
  const auto profile = files.profile;
  const auto destination = files.destination;
  const std::string config = "d6:opaque4:keep22:azureus.user.directory5:othere";
  write(profile / "biglybt.config", config);
  write(profile / "downloads.config", "d9:downloadslee");
  const auto stateName = platform::pathFromUtf8("\u0441\u043e\u0441\u0442\u043e\u044f\u043d\u0438\u0435.dat");
  auto body = std::string(256 * 1024, '\0');
  for (std::size_t index = 0; index < body.size(); index += 29) { body[index] = 'B'; }
  write(profile / "active" / stateName, body);
  write(profile / "plugins" / "example" / "settings.config", "de");
  write(profile / "plugins" / "aznettorbrowser" / "browser_cache" / "large.bin", "excluded");
  write(profile / "logs" / "debug.dat", "excluded");
  write(profile / "root.log", "excluded");
  write(destination / "2026-11-27" / "prior", "original backup");
  write(destination / "2026-11-27.2" / "prior", "later backup");

  const auto backup = take(storage::backupSavedProfile(*files.lease, destination, "2026-11-27"));
  check(backup.directory.filename() == "2026-11-27.3" && backup.files == 4 && backup.bytes > body.size(),
      "Use a fresh dated backup and report copied files");
  check(read(destination / "2026-11-27" / "prior") == "original backup"
      && read(destination / "2026-11-27.2" / "prior") == "later backup", "Preserve the existing backups");
  check(read(backup.directory / "active" / stateName) == body
      && read(backup.directory / "downloads.config") == "d9:downloadslee"
      && read(backup.directory / "plugins" / "example" / "settings.config") == "de", "Copy exact saved state and plugin settings");
  check(!fs::exists(backup.directory / ".azlock") && !fs::exists(backup.directory / "logs")
      && !fs::exists(backup.directory / "root.log")
      && !fs::exists(backup.directory / "plugins" / "aznettorbrowser" / "browser_cache"), "Exclude runtime files and browser data");
  const auto saved = core::Document::parse(read(backup.directory / "biglybt.config"));
  check(saved && saved->find("opaque") && saved->find("opaque")->bytes == "keep" && saved->find("azureus.user.directory")
      && fs::equivalent(platform::pathFromUtf8(saved->find("azureus.user.directory")->bytes), profile),
      "Write the original restore directory while retaining configuration values");
  check(read(profile / "biglybt.config") == config, "Preserve the held source configuration exactly");

  auto noPlugins = core::Document::parse(config).value();
  check(noPlugins.replaceValue("br.restore.doplugins", "i0e").has_value(), "Set the original backup option");
  write(profile / "biglybt.config", noPlugins.serialize());
  const auto next = take(storage::backupSavedProfile(*files.lease, destination, "2026-11-27"));
  check(next.directory.filename() == "2026-11-27.4" && !fs::exists(next.directory / "plugins"),
      "Use the saved plugin option for the next backup");
}

void verifyPluginArchiveVersions() {
  Fixture files;
  const auto plugins = fs::path("plugins") / "example";
  const auto metadata = std::array{"example_1.9.jar", "Example_1.10.JAR", "example_1.9.zip", "example_1.10.zip",
      "legacy.jar", "other_1.2.jar", "example_1..3.jar", "example_2147483648.0.jar"};
  write(files.profile / "biglybt.config", "de");
  for (const auto* name : metadata) { write(files.profile / plugins / name, name); }
  for (const auto* name : {"dep_1.jar", "dep_2.jar"}) {
    write(files.profile / name, name);
    write(files.profile / plugins / "tools" / name, name);
  }
  const auto backup = take(storage::backupSavedProfile(*files.lease, files.destination, "2026-11-27")).directory;
  for (const auto* name : metadata) {
    const auto older = std::string_view(name) == "example_1.9.jar" || std::string_view(name) == "example_1.9.zip";
    check(fs::exists(backup / plugins / name) != older, "Keep the highest version for each plugin archive type");
    check(read(files.profile / plugins / name) == name, "Keep every original plugin file untouched");
  }
  for (const auto* name : {"dep_1.jar", "dep_2.jar"}) {
    check(read(backup / name) == name && read(backup / plugins / "tools" / name) == name,
        "Keep root and deeper dependency archives");
  }
}

void verifyRefusalsAndCleanup() {
  Fixture files;
  write(files.profile / "biglybt.config", "de");
  fs::create_directory(files.profile / "nested");
  auto inside = storage::backupSavedProfile(*files.lease, files.profile / "nested", "2026-11-27");
  check(!inside && inside.error().code == storage::ErrorCode::UnsafePath && fs::is_empty(files.profile / "nested"),
      "Refuse a backup destination under the current profile");
  const auto alias = files.root / "profile-alias";
  std::error_code aliasError;
  fs::create_directory_symlink(files.profile / "nested", alias, aliasError);
  if (!aliasError) {
    auto hiddenInside = storage::backupSavedProfile(*files.lease, alias, "2026-11-27");
    check(!hiddenInside && hiddenInside.error().code == storage::ErrorCode::UnsafePath && fs::is_empty(alias),
        "Resolve an alias before checking if the destination is inside the profile");
  }
  auto invalid = storage::backupSavedProfile(*files.lease, files.destination, "2026-02-30");
  check(!invalid && invalid.error().code == storage::ErrorCode::InvalidFormat && fs::is_empty(files.destination),
      "Reject an impossible date before creating a backup");
  std::stop_source cancelled;
  cancelled.request_stop();
  auto stopped = storage::backupSavedProfile(*files.lease, files.destination, "2026-11-27", cancelled.get_token());
  check(!stopped && stopped.error().code == storage::ErrorCode::Cancelled && fs::is_empty(files.destination),
      "Respect cancellation before touching the destination");
  write(files.profile / "BiglyBTC.archive-restore", "pending generated transaction");
  auto guarded = storage::backupSavedProfile(*files.lease, files.destination, "2026-11-27");
  check(!guarded && guarded.error().code == storage::ErrorCode::Conflict && fs::is_empty(files.destination),
      "Keep a partially published archive transaction out of backups");
  fs::remove(files.profile / "BiglyBTC.archive-restore");

  auto deep = files.profile;
  for (int depth = 0; depth != 33; ++depth) { deep /= "d"; }
  write(deep / "untouched", "keep source");
  auto interrupted = storage::backupSavedProfile(*files.lease, files.destination, "2026-11-27");
  check(!interrupted && interrupted.error().code == storage::ErrorCode::Unsupported && fs::is_empty(files.destination)
      && read(deep / "untouched") == "keep source", "Clean the incomplete backup without changing its source");
}

void verifyRestore() {
  Fixture files;
  const auto original = platform::pathToUtf8(files.profile);
  const auto outside = original + "-sibling";
  const auto pathInfo = "d4:kept" + encode(outside) + "4:path" + encode(original + "/torrents/original.torrent") + 'e';
  write(files.profile / "biglybt.config", "d6:opaque4:keepe");
  write(files.profile / "paths.config", pathInfo);
  write(files.profile / "paths.config.bak", "unreadable original backup bytes");
  write(files.profile / "torrents" / "original.torrent", "generated metadata");
  write(files.profile / "active" / "sample.dat", std::string("\0\xff", 2));
  const auto backup = take(storage::backupSavedProfile(*files.lease, files.destination, "2026-11-27"));
  const auto target = files.root / platform::pathFromUtf8("restored-\u03a9");
  fs::create_directory(target);
  const auto restore = take(storage::restoreProfileBackup(*files.lease, backup.directory, target));
  check(fs::equivalent(restore, target), "Restore to the selected folder");
  const auto config = core::Document::parse(read(target / "biglybt.config"));
  const auto saved = config ? config->find("azureus.user.directory") : nullptr;
  check(saved && fs::equivalent(platform::pathFromUtf8(saved->bytes), target) && config->find("opaque")
      && config->find("opaque")->bytes == "keep", "Publish the new profile location and preserve settings");
  const auto paths = core::Document::parse(read(target / "paths.config"));
  const auto* path = paths ? paths->find("path") : nullptr;
  check(path && fs::equivalent(platform::pathFromUtf8(path->bytes), target / "torrents" / "original.torrent")
      && paths->find("kept") && paths->find("kept")->bytes == outside, "Rebase only saved paths belonging to the original profile");
  check(read(target / "paths.config.bak") == "unreadable original backup bytes"
      && read(target / "active" / "sample.dat") == std::string("\0\xff", 2)
      && read(backup.directory / "paths.config") == pathInfo && read(files.profile / "paths.config") == pathInfo,
      "Preserve binary state, historical copies and the supplied backup");

  const auto repeated = storage::restoreProfileBackup(*files.lease, backup.directory, target);
  check(!repeated && repeated.error().code == storage::ErrorCode::Conflict && read(target / "paths.config") == paths->originalBytes(),
      "Preserve a populated restore destination");
  const auto inside = files.profile / "nested";
  fs::create_directory(inside);
  const auto overlapping = storage::restoreProfileBackup(*files.lease, backup.directory, inside);
  check(!overlapping && overlapping.error().code == storage::ErrorCode::UnsafePath && fs::is_empty(inside),
      "Keep the restored copy separate from the open profile");

  const auto incomplete = files.root / "incomplete";
  const auto empty = files.root / "empty";
  fs::create_directory(incomplete);
  fs::create_directory(empty);
  write(incomplete / "paths.config", pathInfo);
  auto missing = storage::restoreProfileBackup(*files.lease, incomplete, empty);
  check(!missing && missing.error().code == storage::ErrorCode::Missing && fs::is_empty(empty),
      "Refuse an incomplete backup before changing the destination");
  write(backup.directory / "malformed.config", "unreadable primary configuration");
  auto malformed = storage::restoreProfileBackup(*files.lease, backup.directory, empty);
  check(!malformed && malformed.error().code == storage::ErrorCode::InvalidFormat
      && !fs::exists(empty / "biglybt.config") && !fs::exists(empty / "paths.config")
      && !fs::exists(empty / "active") && !fs::exists(empty / "torrents"), "Retire a failed restore before publishing its profile");
  fs::remove(backup.directory / "malformed.config");
  check(take(storage::restoreProfileBackup(*files.lease, backup.directory, empty)).filename() == empty.filename(),
      "Reuse the target after a failed restoration");
}

void verifyRetention() {
  Fixture files;
  using storage::backupSavedProfile;
  const auto make = [&](std::string_view date) {
    return take(backupSavedProfile(*files.lease, files.destination, date)).directory;
  };
  auto config = core::Document::parse("de").value();
  check(config.replaceValue("br.backup.auto.enable", "i1e").has_value()
      && config.replaceValue("br.backup.auto.dir", encode(platform::pathToUtf8(files.destination))).has_value()
      && config.replaceValue("br.backup.auto.retain", "i2e").has_value(), "Set original automatic retention");
  write(files.profile / "biglybt.config", config.serialize());
  write(files.profile / "state" / "nested" / "data.bin", "original backup data");
  const auto old = make("2026-11-21");
  const auto kept = make("2026-11-24");
  const auto newerSameDay = make("2026-11-24");
  const auto held = make("2026-11-22");
  write(held / ".azlock", "");
  const auto partial = files.destination / "2026-11-18";
  write(partial / "some-state", "unpublished subset");
  const auto foreign = files.destination / "2026-11-19";
  const auto foreignName = platform::pathToUtf8(files.root / "different-profile");
  write(foreign / "biglybt.config", "d22:azureus.user.directory" + encode(foreignName) + "e");
  const auto retired = files.destination / ".BiglyBTC.retired-backup-2026-11-20";
  const auto interrupted = make("2026-11-20");
  fs::rename(interrupted, retired);
  fs::rename(retired / "biglybt.config", retired / "BiglyBTC.retired-backup.config");

  constexpr std::int64_t Clock = 20000LL * 24 * 60 * 60 * 1000;
  const auto turn = take(storage::pollAutomaticProfileBackup(*files.lease,
      core::ProfileBackupClock{.nowUtcMillis = Clock, .force = true}, "2026-11-27"));
  check(turn.completed && !turn.retentionError && fs::exists(turn.completed->directory),
      "Write a new backup before considering retention");
  check(!fs::exists(old) && !fs::exists(kept) && fs::exists(newerSameDay) && !fs::exists(retired),
      "Keep the newest date and suffix and finish the interrupted retirement");
  check(fs::exists(held) && fs::exists(partial) && fs::exists(foreign),
      "Keep active, incomplete and other profiles out of retention");
  check(read(files.profile / "state" / "nested" / "data.bin") == "original backup data",
      "Keep the active profile untouched during retention");
}

void verifyRetentionRefusesSharedFile() {
  Fixture files;
  auto config = core::Document::parse("de").value();
  check(config.replaceValue("br.backup.auto.enable", "i1e").has_value()
      && config.replaceValue("br.backup.auto.dir", encode(platform::pathToUtf8(files.destination))).has_value()
      && config.replaceValue("br.backup.auto.retain", "i1e").has_value(), "Limit owned backups to one");
  write(files.profile / "biglybt.config", config.serialize());
  write(files.profile / "data.bin", "current saved payload");
  const auto older = take(storage::backupSavedProfile(*files.lease, files.destination, "2026-11-26")).directory;
  const auto external = files.root / "outside-backup.bin";
  write(external, "unrelated payload");
  fs::create_hard_link(external, older / "shared.bin");

  constexpr std::int64_t Clock = 20000LL * 24 * 60 * 60 * 1000;
  const auto turn = take(storage::pollAutomaticProfileBackup(*files.lease,
      core::ProfileBackupClock{.nowUtcMillis = Clock, .force = true}, "2026-11-27"));
  check(turn.completed && turn.retentionError && turn.retentionError->code == storage::ErrorCode::UnsafePath,
      "A refused retirement must preserve the successful backup result");
  check(read(older / "data.bin") == "current saved payload" && read(older / "shared.bin") == "unrelated payload"
      && read(external) == "unrelated payload", "Preflight preserves the entire older copy and the shared file");
}

void verifyRestoreCanStageWithoutOverwritingLiveState() {
  Fixture files;
  write(files.profile/"biglybt.config","de");
  write(files.profile/"downloads.config","d9:downloadslee");
  write(files.profile/"demo.config","d3:key3:olde");
  const auto backup=take(storage::backupSavedProfile(*files.lease,files.destination,"2026-11-27"));
  write(files.profile/"demo.config","d3:key3:newe");
  write(files.profile/"biglybt.config","d6:marker4:livee");
  const auto staged=storage::stageProfileRestore(*files.lease,backup.directory);
  check(staged.has_value(),staged?"Stage an owned backup":staged.error().message.c_str());
  check(read(files.profile/"demo.config")=="d3:key3:newe"
      && read(files.profile/"biglybt.config")=="d6:marker4:livee","Keep applying state independent of the live profile");
  check(read(files.profile/".BiglyBTC.restore-data"/"demo.config")=="d3:key3:olde",
      "Preserve the backup postimage for the next startup");
  const auto recorded=core::Document::parse(read(files.profile/".BiglyBTC.restore-request"));
  check(recorded&&recorded->find("version")&&recorded->find("version")->integer==1,
      "Publish the restore request only with a complete staged copy");
  const auto repeat=storage::stageProfileRestore(*files.lease,backup.directory);
  check(!repeat&&repeat.error().code==storage::ErrorCode::Busy,"Do not replace a pending restore");
  const auto current=take(storage::backupSavedProfile(*files.lease,files.destination,"2026-11-28"));
  check(read(current.directory/"demo.config")=="d3:key3:newe"
      &&!fs::exists(current.directory/".BiglyBTC.restore-request")
      &&!fs::exists(current.directory/".BiglyBTC.restore-data"),"Backup reads the active profile and omits its pending restore");
  write(files.profile/"untouched.bin","unrelated live file");
  files.lease.reset();
  files.lease.emplace(take(storage::ProfileLease::acquire(files.profile)));
  check(take(storage::applyPendingProfileRestore(*files.lease)),"Apply the pending journal on a new profile lease");
  check(read(files.profile/"demo.config")=="d3:key3:olde"
      &&read(files.profile/"untouched.bin")=="unrelated live file","Restore selected backup files and keep new unselected files");
  const auto applied=core::Document::parse(read(files.profile/"biglybt.config"));
  check(applied&&applied->find("azureus.user.directory")&&!applied->find("marker"),
      "Publish the restored main configuration");
  check(!fs::exists(files.profile/".BiglyBTC.restore-request")
      &&!fs::exists(files.profile/".BiglyBTC.restore-data")
      &&!fs::exists(files.profile/".BiglyBTC.restore-applied")
      &&!take(storage::applyPendingProfileRestore(*files.lease)),"Clean the finished journal and resume normal startup");
  check(read(backup.directory/"demo.config")=="d3:key3:olde","Preserve the selected backup after applying it");
}

void verifyRestoreRefusesBackupWithItsOwnPendingOperation() {
  Fixture files;
  write(files.profile/"biglybt.config","de");
  write(files.profile/"demo.config","d3:key3:olde");
  const auto backup=take(storage::backupSavedProfile(*files.lease,files.destination,"2026-11-27"));
  write(backup.directory/"BiglyBTC.archive-restore","de");
  const auto staged=storage::stageProfileRestore(*files.lease,backup.directory);
  check(!staged&&staged.error().code==storage::ErrorCode::Conflict
      &&!fs::exists(files.profile/".BiglyBTC.restore-request")
      &&!fs::exists(files.profile/".BiglyBTC.restore-data"),
      "Refuse to import a suspended backup transaction and clean the unpublished stage");
  check(read(backup.directory/"BiglyBTC.archive-restore")=="de"
      &&read(files.profile/"demo.config")=="d3:key3:olde","Leave both selected directories intact");
}

void verifyRestoreRefusesChangedStagedData() {
  Fixture files;
  write(files.profile/"biglybt.config","de");
  write(files.profile/"demo.config","d3:key3:olde");
  const auto backup=take(storage::backupSavedProfile(*files.lease,files.destination,"2026-11-27"));
  write(files.profile/"demo.config","d3:key3:newe");
  check(storage::stageProfileRestore(*files.lease,backup.directory).has_value(),"Seal the staged copy before testing changes");
  const auto staged=files.profile/".BiglyBTC.restore-data"/"demo.config";
  const auto timestamp=fs::last_write_time(staged);
  write(staged,"d3:key3:bade");
  fs::last_write_time(staged,timestamp);
  const auto applied=storage::applyPendingProfileRestore(*files.lease);
  check(!applied&&applied.error().code==storage::ErrorCode::Conflict
      &&read(files.profile/"demo.config")=="d3:key3:newe"
      &&read(files.profile/"biglybt.config")=="de","Refuse altered staged data before replacing the saved profile");
}

void verifyRestorePausesDownloads() {
  Fixture files;
  const auto active=std::string(20,'a');
  const auto queued=std::string(20,'b');
  const auto stopped=std::string(20,'c');
  const auto row=[](const std::string& hash,int state,int force) {
    return "d10:forceStarti"+std::to_string(force)+"e8:save_dir4:/tmp5:statei"
        +std::to_string(state)+"e7:torrent5:a.tor12:torrent_hash"+encode(hash)+"e";
  };
  const auto inventory="d9:downloads"+std::string("l")+row(active,0,2)+row(queued,75,0)+row(stopped,70,0)
      +"e10:pause_datald5:forcei1e4:hash"+encode(stopped)+"ee6:opaque4:keepe";
  write(files.profile / "downloads.config",inventory);
  write(files.profile / "biglybt.config","de");
  const auto backup=take(storage::backupSavedProfile(*files.lease,files.destination,"2026-11-27"));
  write(files.profile / "biglybt.config","d20:br.restore.autopausei4294967296ee");
  const auto normal=files.root/"restore-normal";
  const auto paused=files.root/"restore-paused";
  fs::create_directory(normal);
  fs::create_directory(paused);
  take(storage::restoreProfileBackup(*files.lease,backup.directory,normal));
  check(read(normal/"downloads.config")==inventory,"Restore the saved run intent by default");

  write(files.profile / "biglybt.config","d20:br.restore.autopausei1ee");
  take(storage::restoreProfileBackup(*files.lease,backup.directory,paused));
  const auto settings=core::Document::parse(read(paused/"biglybt.config"));
  check(settings&&settings->find("br.restore.autopause")&&settings->find("br.restore.autopause")->integer==1,
      "Carry the selected restore preference to the recovered profile");
  const auto changed=core::Document::parse(read(paused/"downloads.config"));
  check(changed.has_value()&&changed->find("opaque")&&changed->find("opaque")->bytes=="keep",
      "Keep unrelated original inventory fields");
  const auto views=core::readLibrary(*changed);
  check(views&&views->issues.empty()&&views->downloads.size()==3&&views->paused.size()==3,
      "Pause eligible active and queued rows while preserving the existing pause");
  for (const auto& item : views->downloads) {
    check(item.startup==core::StartupState::Stopped&&!item.forceStart,"Restored row will wait before transferring");
  }
  for (const auto& pause : views->paused) {
    check(pause.forceStart==!(pause.hash==core::TorrentHash::parse(queued).value()),
        "Keep force-start preference for later resume");
  }
  check(read(backup.directory/"downloads.config")==inventory && read(files.profile/"downloads.config")==inventory,
      "Restore edits are confined to the new profile");
}

void verifyBackupStatus() {
  Fixture files;
  const auto original = core::Document::parse(
      "d20:br.backup.last.error8:original19:br.backup.last.timei42ee");
  check(original.has_value(), "Read the original saved backup status");
  const auto initial = take(storage::readProfileBackupStatus(*files.lease, &*original));
  check(initial.attemptUtcMillis == 42 && initial.failure == "original",
      "Use saved original status before the first native copy");
  check(storage::recordProfileBackupStatus(*files.lease, {41, "older"}).has_value(), "Record an earlier native outcome");
  const auto older = take(storage::readProfileBackupStatus(*files.lease, &*original));
  check(older.attemptUtcMillis == 42 && older.failure == "original",
      "A newer original backup takes precedence");
  check(storage::recordProfileBackupStatus(*files.lease, {43, "native failure"}).has_value(), "Record a native failure");
  const auto failed = take(storage::readProfileBackupStatus(*files.lease, &*original));
  check(failed.attemptUtcMillis == 43 && failed.failure == "native failure",
      "Expose a more recent native failure");
  check(storage::recordProfileBackupStatus(*files.lease, {44, {}}).has_value(), "Record a later native success");
  check(storage::recordProfileBackupStatus(*files.lease, {43, "stale retry"}).has_value(), "Ignore an older retry");
  const auto cleared = take(storage::readProfileBackupStatus(*files.lease, &*original));
  check(cleared.attemptUtcMillis == 44 && cleared.failure.empty(),
      "A later success clears the error and an older retry cannot replace it");
}

void verifyBackupStateSynchronizesBackToFirstJavaProfile() {
  Fixture files;
  const auto untouched="d6:opaque4:keepe";
  write(files.profile/"biglybt.config",untouched);
  check(storage::synchronizeProfileBackupWithJava(*files.lease).has_value()
      &&read(files.profile/"biglybt.config")==untouched
      &&!fs::exists(files.profile/"biglybt.config.bak"),"Leave an original profile without native backup facts alone");
  write(files.profile/"BiglyBTC.backup-schedule.config",
      "d30:br.backup.auto.last_backup_dayi42e31:br.backup.auto.last_backup_houri1008ee");
  check(storage::recordProfileBackupStatus(*files.lease,{1700000000000,"failed attempt"}).has_value(),
      "Record the native outcome through its usual adapter");
  check(read(files.profile/"biglybt.config")==untouched,"Do not replace the main configuration while recording native results");
  check(storage::synchronizeProfileBackupWithJava(*files.lease).has_value(),"Save Java-compatible metadata after shutdown");
  const auto synchronized=read(files.profile/"biglybt.config");
  auto source=core::Document::parse(synchronized);
  check(source&&source->find("br.backup.auto.last_backup_day")
      &&source->find("br.backup.auto.last_backup_hour")&&source->find("br.backup.last.time")
      &&source->find("br.backup.last.error")&&source->find("opaque"),
      "Find the saved Java keys");
  check(source&&source->find("br.backup.auto.last_backup_day")->integer==42
      &&source->find("br.backup.auto.last_backup_hour")->integer==1008
      &&source->find("br.backup.last.time")->integer==1700000000000
      &&source->find("br.backup.last.error")->bytes=="failed attempt"
      &&source->find("opaque")->bytes=="keep","Persist only the original Java attempt and outcome metadata");
  const auto written=fs::last_write_time(files.profile/"biglybt.config");
  check(storage::synchronizeProfileBackupWithJava(*files.lease).has_value()
      &&fs::last_write_time(files.profile/"biglybt.config")==written,"Closing again does not rewrite the same metadata");
  check(source->replaceValue("br.backup.auto.last_backup_day","i45e").has_value()
      &&source->replaceValue("br.backup.last.time","i1700000000001e").has_value()
      &&source->replaceValue("br.backup.last.error","12:Java outcome").has_value(),"Model a later Java backup");
  const auto newer=source->serialize();
  write(files.profile/"biglybt.config",newer);
  check(storage::synchronizeProfileBackupWithJava(*files.lease).has_value()
      &&read(files.profile/"biglybt.config")==newer,"Leave newer Java backup metadata alone");
}

void verifyAutomaticBackup() {
  Fixture files;
  constexpr std::int64_t Day = 24 * 60 * 60 * 1000;
  constexpr std::int64_t Hour = 60 * 60 * 1000;
  constexpr std::int64_t Today = 20000 * Day;
  auto saved = core::Document::parse("de").value();
  core::ProfileBackupClock clock{.nowUtcMillis = Today, .firstCheck = true};
  write(files.profile / "biglybt.config", saved.serialize());
  write(files.profile / "downloads.config", "d9:downloadslee");
  using Action = core::ProfileBackupAction;
  auto turn = take(storage::pollAutomaticProfileBackup(*files.lease, clock, "2026-11-27"));
  check(turn.schedule.action == Action::Disabled && fs::is_empty(files.destination), "A profile without the original option leaves automatic backup disabled");

  check(saved.replaceValue("br.backup.auto.enable", "i1e").has_value()
      && saved.replaceValue("br.backup.auto.dir", encode(platform::pathToUtf8(files.destination))).has_value()
      && saved.replaceValue("br.backup.notify", "i1e").has_value(), "Set saved original automatic backup settings");
  const auto original = saved.serialize();
  write(files.profile / "biglybt.config", original);
  turn = take(storage::pollAutomaticProfileBackup(*files.lease, clock, "2026-11-27"));
  check(turn.schedule.action == Action::Wait && turn.schedule.delayMillis == 300000 && !turn.completed,
      "Wait for the original automatic backup startup delay");
  clock.firstCheck = false;
  clock.nowUtcMillis += 300000;
  turn = take(storage::pollAutomaticProfileBackup(*files.lease, clock, "2026-11-27"));
  check(turn.schedule.action == Action::Run && turn.completed && turn.notify
      && fs::exists(turn.completed->directory / "biglybt.config") && read(files.profile / "biglybt.config") == original,
      "Run the scheduled copy and keep the active main configuration stable");
  const auto success = take(storage::readProfileBackupStatus(*files.lease, &saved));
  check(!turn.statusError && success.attemptUtcMillis == clock.nowUtcMillis && success.failure.empty(),
      "Record a successful scheduled attempt independently of the live main configuration");
  const auto remembered = files.profile / "BiglyBTC.backup-schedule.config";
  check(read(turn.completed->directory / remembered.filename()) == read(remembered), "Record the attempt in the profile and the new backup");

  files.lease.reset();
  files.lease.emplace(take(storage::ProfileLease::acquire(files.profile)));
  clock.nowUtcMillis = Today + Hour;
  turn = take(storage::pollAutomaticProfileBackup(*files.lease, clock, "2026-11-27"));
  check(turn.schedule.action == Action::Wait && turn.schedule.delayMillis == 23 * Hour && !turn.completed,
      "Do not repeat the same saved day after reopening the profile");
  clock.nowUtcMillis = Today + Day;
  turn = take(storage::pollAutomaticProfileBackup(*files.lease, clock, "2026-11-28"));
  check(turn.schedule.action == Action::Run && turn.completed && turn.completed->directory.filename() == "2026-11-28",
      "Run again when the next saved day begins");

  check(saved.replaceValue("br.backup.auto.dir", encode(platform::pathToUtf8(files.root / "missing"))).has_value(),
      "Set an unavailable destination for a failed attempt");
  write(files.profile / "biglybt.config", saved.serialize());
  clock.nowUtcMillis = Today + 2 * Day;
  auto failed = storage::pollAutomaticProfileBackup(*files.lease, clock, "2026-11-29");
  check(!failed && failed.error().code == storage::ErrorCode::UnsafePath, "Report an unavailable scheduled backup destination");
  const auto failure = take(storage::readProfileBackupStatus(*files.lease, &saved));
  check(failure.attemptUtcMillis == clock.nowUtcMillis && failure.failure == failed.error().message,
      "Persist the actual automatic backup failure for later inspection");
  clock.nowUtcMillis += Hour;
  turn = take(storage::pollAutomaticProfileBackup(*files.lease, clock, "2026-11-29"));
  check(turn.schedule.action == Action::Wait && !turn.completed, "Wait until the next original interval after an attempted backup fails");
}
} // namespace

int main() try {
  verifyCopy();
  verifyPluginArchiveVersions();
  verifyRefusalsAndCleanup();
  verifyRestore();
  verifyRestorePausesDownloads();
  verifyRestoreCanStageWithoutOverwritingLiveState();
  verifyRestoreRefusesChangedStagedData();
  verifyRestoreRefusesBackupWithItsOwnPendingOperation();
  verifyAutomaticBackup();
  verifyBackupStatus();
  verifyBackupStateSynchronizesBackToFirstJavaProfile();
  verifyRetention();
  verifyRetentionRefusesSharedFile();
  std::cout << "Profile backup, plugin archive selection, relocation, scheduling, retention and failure recovery passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
