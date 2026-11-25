#pragma once
#include "country_provider.hpp"
#include "profile.hpp"
#include "biglybtc/plugin.h"
#include <cstdint>
#include <filesystem>
#include <string>

namespace biglybtc::storage {
// App-owned approved catalogue/build values, never read from profile settings or
// arbitrary CLI keys/paths. Only the database directory is user-selected data.
struct CountryModuleApproval {
  std::string sha256;
  std::uint64_t bytes = 0;
  std::string version;
};
// Worker-only. Read-only binding, under the already held profile lease. No
// discovery, downloads, installation, profile writes or fallback module search.
std::expected<std::unique_ptr<CountryProvider>,Error> loadApprovedCountryProvider(
    const ProfileLease&, const std::filesystem::path& appOwnedModule,
    const CountryModuleApproval& compiledApproval,
    const std::filesystem::path& explicitDatabaseDirectory, bool packagedData = false);
std::expected<std::unique_ptr<CountryProvider>,Error> loadBuiltinCountryProvider(
    const ProfileLease&, bt_plugin_entry_fn, const std::filesystem::path& explicitDatabaseDirectory,
    bool packagedData = false);
// Worker-only. The caller retains the signed activation and passes its
// authenticated descriptor. This function binds the same country-data facade.
std::expected<std::unique_ptr<CountryProvider>,Error> loadVerifiedCountryProvider(
    int module, const std::string& version, const std::filesystem::path& explicitDatabaseDirectory);
} // namespace biglybtc::storage
