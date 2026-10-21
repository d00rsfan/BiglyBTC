#include "persistent_session.hpp"

#include <chrono>
#include <iostream>
#include <string_view>

// Root runs this separately in default and explicitly source-bound builds.
// A missing binding is a FAILURE in the bound lane, never a skipped test.
int main(int argc, char** argv) {
  using namespace biglybtc::engine;
  if (argc != 2 || (std::string_view(argv[1]) != "bound" && std::string_view(argv[1]) != "unbound")) { return 64; }
  const bool expectBound = std::string_view(argv[1]) == "bound";
  auto invalid = PersistentSession::create({0, 2, 3});
  if (invalid || invalid.error() != AccountingError::InvalidIdentity) { return 1; }
  auto owner = PersistentSession::create({1, 2, 3});
  if (!expectBound) {
    if (owner || owner.error() != AccountingError::DependencyUnqualified) { return 2; }
    std::cout << "unbound: no persistent owner or receipt\n";
    return 0;
  }
  if (!owner) { return 3; }
  if ((*owner)->latest() || (*owner)->lastRetirement() || !(*owner)->poll()) { return 4; }
  const auto closed = (*owner)->close(std::chrono::steady_clock::now() + std::chrono::seconds{5});
  if (!closed.sessionJoined || !closed.retirement.storageReleased || closed.retirement.peerCounters) { return 5; }
  // Creation is lazy: no transfer/no counter sample is fabricated by binding.
  std::cout << "bound: source/artifact candidate only, causal qualification pending\n";
  return 0;
}
