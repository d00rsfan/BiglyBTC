// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_backup.hpp"
#include "../platform/utf8_path.hpp"

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
} // namespace

int main() try {
  verifyCopy();
  verifyRefusalsAndCleanup();
  verifyRestore();
  std::cout << "Saved profile backup, profile relocation, collision guards and failure recovery passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
