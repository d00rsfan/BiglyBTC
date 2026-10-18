// SPDX-License-Identifier: GPL-2.0-or-later
#include "rpc_profile.hpp"
#include <iostream>
#include <sstream>
using namespace biglybtc::storage;
namespace {
std::string render(const RpcProfileSnapshot& snapshot) {
  std::ostringstream out;
  out << "COUNTER " << (snapshot.nextId ? std::to_string(*snapshot.nextId) : "-")
      << ' ' << snapshot.counterWrongTypes.size() << '\n';
  for (const auto& item : snapshot.identities) {
    out << "ID " << (item.archived ? 'S' : 'A') << ' ' << item.sourceIndex << ' '
        << (item.hash ? item.hash->hex() : "-") << ' '
        << (item.id ? std::to_string(*item.id) : "-") << '\n';
  }
  out << "DETAIL " << snapshot.primaryStateLoads << ' ' << snapshot.legacyInventory << ' '
      << snapshot.library.inventoryIssues.size() << ' ' << snapshot.library.cacheIssues.size()
      << ' ' << snapshot.library.cacheWarning.has_value() << ' ' << snapshot.issues.size() << '\n';
  for (const auto& item : snapshot.identities) {
    constexpr const char* names[]{"UNAVAILABLE", "CACHE", "PRIMARY", "BACKUP", "ARCHIVE"};
    out << "ORIGIN " << names[static_cast<unsigned>(item.origin)] << '\n';
  }
  return out.str();
}
}
int main(int argc, char** argv) {
  if (argc != 3) { return 2; }
  // Test driver accepts generated profiles only, never the user's live profile.
  const auto path = std::filesystem::canonical(argv[1]);
  if (!path.string().starts_with("/tmp/biglybtc-rpc-profile-")) { return 2; }
  auto lease = ProfileLease::acquire(path);
  if (!lease) { std::cerr << lease.error().message << '\n'; return 3; }
  auto first = readRpcProfileSnapshot(*lease, argv[2]);
  auto second = readRpcProfileSnapshot(*lease, argv[2]);
  if (!first || !second) { std::cerr << (!first ? first.error().message : second.error().message) << '\n'; return 4; }
  const auto output = render(*first);
  if (render(*second) != output) { return 5; }
  std::stop_source stopped; stopped.request_stop();
  const auto cancelled = readRpcProfileSnapshot(*lease, argv[2], stopped.get_token());
  if (cancelled || cancelled.error().code != ErrorCode::Cancelled) { return 6; }
  std::cout << output;
}
