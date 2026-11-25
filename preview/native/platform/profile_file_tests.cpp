// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace {
using biglybtc::storage::ConfigFile;
using biglybtc::storage::ProfileLease;
using biglybtc::storage::RecoverySource;

void require(bool condition, const char* stage) {
  if (!condition) { throw std::runtime_error(stage); }
}

class TemporaryProfile {
public:
  TemporaryProfile() {
    auto pattern = (std::filesystem::temp_directory_path() / "biglybtc-profile-XXXXXX").string();
    const auto* created = ::mkdtemp(pattern.data());
    require(created != nullptr, "Unable to create generated profile");
    path = created;
  }
  ~TemporaryProfile() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  std::filesystem::path path;
};

void exercise() {
  TemporaryProfile fixture;
  const auto lease = ProfileLease::acquire(fixture.path);
  require(lease.has_value(), "Profile lease could not be acquired");
  auto config = ConfigFile::createMissing(*lease, "tables.config", "d4:porti51413ee");
  require(config.has_value(), "First publication failed");
  require(config->recoverySource() == RecoverySource::Primary, "First publication must be primary");
  require(!ConfigFile::createMissing(*lease, "tables.config", "de"), "Existing primary was accepted for replacement");

  auto losingWriter = ConfigFile::load(*lease, "tables.config");
  require(losingWriter.has_value(), "Primary configuration must be readable");
  require(config->document().setInteger("port", 51414), "Preparing a changed configuration failed");
  require(config->save(*lease).has_value(), "Replacing primary and creating backup failed");
  const auto stale = losingWriter->save(*lease);
  require(!stale && stale.error().code == biglybtc::storage::ErrorCode::Conflict,
      "A stale writer must preserve a newer primary");

  const auto current = ConfigFile::load(*lease, "tables.config");
  require(current && current->document().originalBytes() == "d4:porti51414ee", "Wrong current primary");
  {
    std::ofstream primary(fixture.path / "tables.config", std::ios::binary | std::ios::trunc);
    primary << "incomplete";
    require(primary.good(), "Could not stage interrupted primary in generated profile");
  }
  auto recovered = ConfigFile::load(*lease, "tables.config");
  require(recovered && recovered->recoverySource() == RecoverySource::Backup
      && recovered->document().originalBytes() == "d4:porti51413ee", "Backup recovery was not exact");
  require(recovered->save(*lease).has_value(), "Could not restore the backup as primary");
  const auto restored = ConfigFile::load(*lease, "tables.config");
  require(restored && restored->recoverySource() == RecoverySource::Primary
      && restored->document().originalBytes() == "d4:porti51413ee", "Restored primary did not survive reopen");
}

} // namespace

int main() {
  try {
    exercise();
    std::cout << "Profile configuration transaction and backup recovery verified\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
