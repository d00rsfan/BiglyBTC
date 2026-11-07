#pragma once
#include <filesystem>
#include <iosfwd>
namespace biglybtc::app {
// Metadata only: no profile lease, recovery, payload access, Qt or engine owner.
int checkExistingProfile(const std::filesystem::path&,std::ostream&);
}
