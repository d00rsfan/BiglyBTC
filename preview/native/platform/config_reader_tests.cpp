// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile.hpp"
#include "profile_path.hpp"

#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
using biglybtc::storage::ConfigFile;
using biglybtc::storage::ErrorCode;
using biglybtc::storage::ProfileLease;
using biglybtc::storage::RecoverySource;

void require(bool condition, const char* what) {
  if (!condition) { throw std::runtime_error(what); }
}

struct GeneratedProfile {
  std::filesystem::path path;

  GeneratedProfile() {
    std::random_device random;
    for (int attempts = 0; attempts < 16; ++attempts) {
      const auto prefix = std::filesystem::path(u"biglybtc-\u0423\u043a\u0440\u0430\u0457\u043d\u0430-");
      const auto leaf = prefix.native() + std::filesystem::path(std::to_string(random())).native();
      const auto candidate = std::filesystem::temp_directory_path() / leaf;
      if (std::filesystem::create_directory(candidate)) {
        path = candidate;
        return;
      }
    }
    throw std::runtime_error("Unable to create generated profile");
  }

  ~GeneratedProfile() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};

void write(const std::filesystem::path& path, const char* text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << text;
  stream.close();
  require(stream.good(), "Unable to finish writing generated configuration");
}

void checkDefaultPath(const std::filesystem::path& home) {
  const auto profile = biglybtc::platform::defaultBiglyBtProfile(home);
#ifdef _WIN32
  require(profile.is_absolute() && profile.filename() == L"BiglyBT", "BiglyBT roaming profile must be absolute");
#elif defined(__APPLE__)
  require(profile == home / "Library" / "Application Support" / "BiglyBT", "Wrong default path on macOS");
#else
  require(profile == home / ".biglybt", "Wrong default path on Linux");
#endif
}

void check() {
  const GeneratedProfile fixture;
  checkDefaultPath(fixture.path);
  const auto lease = ProfileLease::acquire(fixture.path);
  require(lease.has_value(), "Configuration reader could not acquire Unicode profile");

  const std::string name = "downloads.config";
  const auto missing = ConfigFile::load(*lease, name);
  require(!missing && missing.error().code == ErrorCode::Missing, "Missing configuration must stay missing");

  const auto primary = fixture.path / name;
  write(primary, "d4:porti51413ee");
  const auto original = ConfigFile::load(*lease, name);
  require(original && original->recoverySource() == RecoverySource::Primary
      && original->document().originalBytes() == "d4:porti51413ee", "Could not read the primary configuration");
  require(ConfigFile::load(*lease, name).has_value()
      && lease->configReadCacheStatistics().hits == 1, "Expected exact byte cache reuse");

  write(primary, "d4:porti51414ee");
  const auto changed = ConfigFile::load(*lease, name);
  require(changed && changed->document().originalBytes() == "d4:porti51414ee",
      "An external edit must invalidate the cached primary");

  write(primary, "partial primary");
  write(fixture.path / (name + ".bak"), "d4:porti51411ee");
  const auto backup = ConfigFile::load(*lease, name);
  require(backup && backup->recoverySource() == RecoverySource::Backup
      && backup->document().originalBytes() == "d4:porti51411ee", "Backup selection after interrupted primary failed");
  require(std::filesystem::remove(primary), "Unable to clear generated primary");
  write(fixture.path / (name + ".saving"), "d4:porti51412ee");
  const auto saving = ConfigFile::load(*lease, name);
  require(saving && saving->recoverySource() == RecoverySource::Saving
      && saving->document().originalBytes() == "d4:porti51412ee", "Missing primary must prefer complete saving file");

  require(std::filesystem::create_directory(primary), "Unable to create non-file test fixture");
  const auto wrongType = ConfigFile::load(*lease, name);
  require(!wrongType && wrongType.error().code == ErrorCode::UnsafePath, "Rejected candidate must never be used as a file");
}
} // namespace

int main() {
  try {
    check();
    std::cout << "Read legacy profile configuration, selected recovery files and rejected non-files\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
