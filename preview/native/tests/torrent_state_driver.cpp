#include "torrent_state.hpp"

#include <charconv>
#include <fstream>
#include <iostream>
#include <unistd.h>

using namespace biglybtc;
using namespace biglybtc::storage;

namespace {
StateSaveStage crashStage = StateSaveStage::TemporarySynced;
bool crash = false;
void observer(StateSaveStage stage) { if (crash && stage == crashStage) { ::_exit(66); } }
int failed(const Error& value) {
  std::cerr << value.message << '\n';
  return value.code == ErrorCode::Busy ? 3 : 2;
}
}

int main(int argc, char** argv) {
  if (argc < 4) { return 1; }
  const std::string_view identity(argv[2]);
  if (identity.size() != 40 && identity.size() != 64) { return 1; }
  std::string raw;
  for (std::size_t i = 0; i < identity.size(); i += 2) {
    unsigned value = 0;
    const auto decoded = std::from_chars(identity.data() + i, identity.data() + i + 2, value, 16);
    if (decoded.ec != std::errc{} || decoded.ptr != identity.data() + i + 2) { return 1; }
    raw += static_cast<char>(value);
  }
  auto hash = core::TorrentHash::parse(raw);
  if (!hash) { return 1; }
  auto lease = ProfileLease::acquire(argv[1]);
  if (!lease) { return failed(lease.error()); }
  auto store = TorrentStateStore::open(*lease);
  if (!store) { return failed(store.error()); }
  const std::string_view command(argv[3]);
  if (command == "load") {
    auto state = store->load(*lease, *hash);
    if (!state) { return failed(state.error()); }
    std::cout << "LOADED " << state->recoveredBackup() << '\n';
    const auto blocked = state->replaceSection("attributes", "de");
    return !blocked && blocked.error().code == ErrorCode::Conflict ? 0 : 4;
  }
  auto state = store->beginUpdate(*lease, *hash);
  if (!state) { return failed(state.error()); }
  if (command == "begin") { return 0; }
  if (command == "immutable") {
    const auto blocked = state->replaceSection("info", "de");
    return !blocked && blocked.error().code == ErrorCode::Unsupported ? 0 : 4;
  }
  if (command == "stale") {
    std::ofstream(std::filesystem::path(argv[1]) / "active" / (hash->hex() + ".dat")) << "de";
    const auto blocked = store->save(*lease, *state);
    return !blocked && blocked.error().code == ErrorCode::Conflict ? 0 : 4;
  }
  if (command == "other-profile" && argc == 5) {
    auto other = ProfileLease::acquire(argv[4]);
    if (!other) { return failed(other.error()); }
    auto otherStore = TorrentStateStore::open(*other);
    if (!otherStore) { return failed(otherStore.error()); }
    const auto blocked = otherStore->save(*other, *state);
    return !blocked && blocked.error().code == ErrorCode::UnsafePath ? 0 : 4;
  }
  if (command != "edit" || argc != 5) { return 1; }
  const auto* original = state->document().find("attributes");
  if (!original) { return 4; }
  auto encoded = core::encodeValue(*original, compatibleConfigLimits());
  if (!encoded) { return 4; }
  auto attributes = core::Document::parse(std::move(*encoded), compatibleConfigLimits());
  if (!attributes || !attributes->replaceValue("native.fixture", "i7e")) { return 4; }
  auto edited = state->replaceSection("attributes", attributes->serialize());
  if (!edited) { return failed(edited.error()); }
  const std::string_view stage(argv[4]);
  crash = stage != "normal";
  crashStage = stage == "temp" ? StateSaveStage::TemporarySynced
      : stage == "backup" ? StateSaveStage::BackupSynced : StateSaveStage::PrimaryRenamed;
  const auto saved = store->save(*lease, *state, observer);
  return saved ? 0 : failed(saved.error());
}
