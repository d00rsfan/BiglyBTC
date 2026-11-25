// SPDX-License-Identifier: GPL-2.0-or-later
#include "application_settings.hpp"
#include "checkpoint_batch.hpp"
#include "library_view.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <zlib.h>

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

std::string gzip(std::string input) {
  std::array<unsigned char, 256> output{};
  z_stream stream{};
  require(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) == Z_OK,
      "Cannot initialize generated cache compression");
  stream.next_in = reinterpret_cast<unsigned char*>(input.data());
  stream.avail_in = static_cast<uInt>(input.size());
  stream.next_out = output.data();
  stream.avail_out = static_cast<uInt>(output.size());
  const auto status = deflate(&stream, Z_FINISH);
  const auto size = stream.total_out;
  require(deflateEnd(&stream) == Z_OK && status == Z_STREAM_END, "Cannot compress generated cache");
  return {reinterpret_cast<const char*>(output.data()), size};
}

void exerciseLibrary() {
  Profile fixture;
  const auto active = fixture.root / "active";
  require(fs::remove(active), "Cannot remove generated active directory");
  const std::string hash(20, 'a');
  write(fixture.root / "downloads.config",
      "d9:downloadsld8:save_dir6:folder5:statei70e7:torrent7:one.tor12:torrent_hash20:" + hash + "eee");
  auto lease = take(ProfileLease::acquire(fixture.root));
  const auto withoutCache = take(readLibrarySource(lease));
  require(withoutCache.view.items.size() == 1 && !withoutCache.view.cacheWarning,
      "Missing cache directory changed the saved inventory");
  require(fs::create_directory(active), "Cannot restore generated active directory");
  write(active / "cache.dat", gzip("d5:stateld4:hash20:" + hash + "4:name7:exampleeee"));
  auto source = take(readLibrarySource(lease));
  require(source.view.items.size() == 1 && source.view.items.front().cached
      && source.view.items.front().cached->name == "example" && source.cacheDocument,
      "Original compressed cache did not join the saved download by hash");
  write(active / "cache.dat", "invalid");
  const auto corrupted = take(readLibrarySource(lease));
  require(corrupted.view.items.size() == 1 && corrupted.view.cacheWarning
      && corrupted.view.cacheWarning->code == ErrorCode::InvalidFormat,
      "Invalid cache suppressed the saved inventory or its warning");
}

void exerciseSettings() {
  using biglybtc::core::SettingChange;
  using biglybtc::core::SettingId;
  Profile fixture;
  auto lease = take(ProfileLease::acquire(fixture.root));
  auto baseline = take(readApplicationSettings(lease));
  require(baseline.missing(), "An empty original profile did not use settings defaults");
  const std::array first{SettingChange{SettingId::TcpPort, std::int64_t{6889}}};
  const auto prepared = take(prepareApplicationSettings(baseline, first));
  const auto created = take(saveApplicationSettings(lease, baseline, prepared));
  require(created.source && created.source->find("TCP.Listen.Port")
      && created.source->find("TCP.Listen.Port")->integer == 6889,
      "First settings publication lost the selected port");
  baseline = take(readApplicationSettings(lease));
  const std::array next{SettingChange{SettingId::TcpPort, std::int64_t{6890}}};
  const auto changed = take(prepareApplicationSettings(baseline, next));
  take(saveApplicationSettings(lease, baseline, changed));
  const auto stale = saveApplicationSettings(lease, baseline, changed);
  require(!stale && stale.error().code == ErrorCode::Conflict, "Stale settings overwrote a newer snapshot");
  require(fs::remove(fixture.root / "biglybt.config"), "Cannot remove generated settings primary");
  const auto restored = take(readApplicationSettings(lease));
  require(restored.recoverySource == RecoverySource::Backup && restored.source && restored.source->find("TCP.Listen.Port")
      && restored.source->find("TCP.Listen.Port")->integer == 6889,
      "Settings did not retain the previous snapshot in its backup");
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
  exerciseLibrary();
  exerciseSettings();
  exercise();
  return 0;
} catch (const std::exception& failure) {
  std::cerr << failure.what() << '\n';
  return 1;
}
