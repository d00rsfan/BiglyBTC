// SPDX-License-Identifier: GPL-2.0-or-later
#include "checkpoint_batch.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using namespace biglybtc::storage;

void require(bool condition, const char* message) {
  if (!condition) { throw std::runtime_error(message); }
}

template<class T>
T take(std::expected<T, Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
  return std::move(*result);
}

void check(std::expected<void, Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
}

struct Profile {
  fs::path root;
  Profile() {
    fs::path name(u8"biglybtc-state-\u03bb-");
    name += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    root = fs::temp_directory_path() / name;
    require(fs::create_directory(root), "Cannot create generated profile");
    require(fs::create_directory(root / "active"), "Cannot create generated active directory");
  }
  ~Profile() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
};

void write(const fs::path& file, std::string_view bytes) {
  std::ofstream output(file, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  require(output.good(), "Cannot write generated torrent state");
}

void expectMarker(const TorrentStateFile& state, int expected, bool recovered) {
  const auto* attributes = state.document().find("attributes");
  const auto* marker = attributes ? attributes->find("marker") : nullptr;
  require(marker && marker->integer == expected && state.recoveredBackup() == recovered,
      "Published marker or recovery origin differs");
  const auto* resume = state.document().find("resume");
  const auto* data = resume ? resume->find("data") : nullptr;
  const auto* valid = data ? data->find("valid") : nullptr;
  require(valid && valid->integer == (recovered ? 0 : 1), "Recovery did not require a piece recheck");
}

struct Interrupted {};
void interruptAfterBackup(StateSaveStage stage) {
  if (stage == StateSaveStage::BackupSynced) { throw Interrupted{}; }
}

void exercise() {
  Profile fixture;
  const auto first = *biglybtc::core::TorrentHash::parse(std::string(20, 'a'));
  const auto second = *biglybtc::core::TorrentHash::parse(std::string(20, 'b'));
  const auto primary = fixture.root / "active" / (first.hex() + ".dat");
  for (const auto& hash : {first, second}) {
    write(fixture.root / "active" / (hash.hex() + ".dat"), "d4:infode6:resumed4:datad5:validi1eeee");
  }
  auto lease = take(ProfileLease::acquire(fixture.root));
  auto store = take(TorrentStateStore::open(lease));
  auto state = take(store.stageUpdate(lease, first));
  auto stale = take(store.stageUpdate(lease, first));
  auto other = take(store.stageUpdate(lease, second));
  check(state.replaceSection("attributes", "d6:markeri42ee"));
  check(other.replaceSection("attributes", "d6:markeri7ee"));
  const std::array batch{CheckpointState{&store, &state}, CheckpointState{&store, &other}};
  check(CheckpointBatch::save(lease, batch));
  expectMarker(take(store.load(lease, first)), 42, false);
  expectMarker(take(store.load(lease, second)), 7, false);
  const auto obsolete = store.save(lease, stale);
  require(!obsolete && obsolete.error().code == ErrorCode::Conflict, "Stale state overwrote the checkpoint");

  check(state.replaceSection("attributes", "d6:markeri43ee"));
  bool interrupted = false;
  try {
    check(store.save(lease, state, interruptAfterBackup));
  } catch (const Interrupted&) { interrupted = true; }
  require(interrupted, "Did not observe backup-before-primary publication");
  expectMarker(take(store.load(lease, first)), 42, false);
  check(store.save(lease, state));
  expectMarker(take(store.load(lease, first)), 43, false);
  auto retained = primary;
  retained += ".saving.bak";
  require(fs::exists(retained), "Retry did not retain interrupted staging");

  require(fs::remove(primary), "Cannot remove generated primary before recovery");
  expectMarker(take(store.load(lease, first)), 43, true);
}
} // namespace

int main() try {
  exercise();
  return 0;
} catch (const std::exception& failure) {
  std::cerr << failure.what() << '\n';
  return 1;
}
