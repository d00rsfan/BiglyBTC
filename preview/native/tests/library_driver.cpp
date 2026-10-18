#include "biglybtc/library.hpp"
#include "profile.hpp"

#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) { return 1; }
  auto lease = biglybtc::storage::ProfileLease::acquire(argv[1]);
  if (!lease) { return lease.error().code == biglybtc::storage::ErrorCode::Busy ? 3 : 2; }
  auto config = biglybtc::storage::ConfigFile::load(*lease, "downloads.config");
  if (!config) { return 2; }
  const auto snapshot = biglybtc::core::readLibrary(config->document());
  if (!snapshot) { return 2; }
  std::cout << "LIBRARY " << snapshot->downloads.size() << ' ' << snapshot->paused.size() << ' '
      << snapshot->issues.size() << ' ' << snapshot->legacyMap << '\n';
  for (const auto& item : snapshot->downloads) {
    std::cout << "ITEM " << item.sourceIndex << ' ' << (item.hash ? item.hash->hex() : "-") << ' '
        << item.originalState << ' ' << static_cast<int>(item.startup) << ' ' << item.forceStart << ' '
        << item.persistent << ' ' << item.downloaded.value_or(-1) << ' ' << item.uploaded.value_or(-1)
        << ' ' << item.completedBytes.value_or(-1) << '\n';
  }
}
