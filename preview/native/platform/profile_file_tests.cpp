// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_contacts.hpp"
#include "azureus_value_versions.hpp"
#include "checkpoint_batch.hpp"
#include "file_io.hpp"
#include "mainline_checkpoint.hpp"
#include "profile.hpp"
#include "posix_profile_files.hpp"

#include <cerrno>
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

void publishAcrossDirectories() {
  TemporaryProfile fixture;
  const auto lease = ProfileLease::acquire(fixture.path);
  require(lease.has_value(), "Could not hold the generated profile");
  const int root = lease->directoryHandle();
  require(::mkdirat(root, "destination", 0700) == 0, "Could not create the destination directory");
  const biglybtc::storage::detail::Descriptor destination(
      ::openat(root, "destination", O_DIRECTORY | O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  require(destination.get() >= 0, "Could not hold the destination directory");
  require(ConfigFile::createMissing(*lease, "incoming.config", "d4:porti2ee").has_value(),
      "Could not publish the generated source");
  {
    std::ofstream existing(fixture.path / "destination" / "existing.config", std::ios::binary);
    existing << "d4:porti1ee";
    require(existing.good(), "Could not write the generated destination");
  }
  using biglybtc::platform::renameNoReplaceAt;
  require(renameNoReplaceAt(root, "incoming.config", destination.get(), "existing.config") == -1
      && errno == EEXIST, "An existing destination was replaced");
  require(std::filesystem::exists(fixture.path / "incoming.config"), "The refused source was removed");
  require(renameNoReplaceAt(root, "incoming.config", destination.get(), "new.config") == 0,
      "Could not publish at an empty destination");
  std::ifstream kept(fixture.path / "destination" / "existing.config");
  std::ifstream moved(fixture.path / "destination" / "new.config");
  std::string keptBytes, movedBytes;
  kept >> keptBytes;
  moved >> movedBytes;
  require(keptBytes == "d4:porti1ee" && movedBytes == "d4:porti2ee"
      && !std::filesystem::exists(fixture.path / "incoming.config"),
      "Cross-directory publication changed the expected contents or names");
}

void exercise() {
  TemporaryProfile fixture;
  const auto lease = ProfileLease::acquire(fixture.path);
  require(lease.has_value(), "Profile lease could not be acquired");
  const auto heldPath = biglybtc::platform::namedDescriptorPath(lease->directoryHandle());
  require(heldPath && std::filesystem::path(*heldPath) == std::filesystem::canonical(fixture.path),
      "Kernel pathname must refer to the held profile directory");
  require(!biglybtc::platform::namedDescriptorPath(-1), "Invalid descriptor was accepted");
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

void checkpointTorrent() {
  TemporaryProfile fixture;
  const auto active = fixture.path / "active";
  require(std::filesystem::create_directory(active), "Unable to create generated active directory");
  const auto hash = biglybtc::core::TorrentHash::parse(std::string(20, 'r'));
  require(hash.has_value(), "Invalid generated torrent identity");
  const auto primary = active / (hash->hex() + ".dat");
  {
    std::ofstream file(primary, std::ios::binary);
    file << "d4:infode6:resumed4:datad5:validi1eeee";
    require(file.good(), "Could not write generated torrent state");
  }
  const auto lease = ProfileLease::acquire(fixture.path);
  require(lease.has_value(), "Torrent state lease could not be acquired");
  const auto store = biglybtc::storage::TorrentStateStore::open(*lease);
  require(store.has_value(), "Torrent state store could not be opened");
  auto edited = store->stageUpdate(*lease, *hash);
  require(edited.has_value(), "Torrent state could not be staged");
  require(edited->replaceSection("attributes", "d6:markeri42ee").has_value(), "State edit failed");
  const biglybtc::storage::CheckpointState checkpoint{&*store, &*edited};
  require(biglybtc::storage::CheckpointBatch::save(*lease, {&checkpoint, 1}).has_value(),
      "Torrent state checkpoint publication failed");
  const auto published = store->load(*lease, *hash);
  require(published && !published->recoveredBackup(), "Checkpoint could not be reopened");
  const auto* attributes = published->document().find("attributes");
  require(attributes && attributes->find("marker") && attributes->find("marker")->integer == 42,
      "Checkpoint did not preserve the edited state");

  require(std::filesystem::remove(primary), "Unable to remove generated primary fixture");
  const auto recovery = store->load(*lease, *hash);
  require(recovery && recovery->recoveredBackup(), "Torrent state backup could not be reopened");
  const auto* resume = recovery->document().find("resume");
  const auto* data = resume ? resume->find("data") : nullptr;
  const auto* valid = data ? data->find("valid") : nullptr;
  require(valid && valid->integer == 0, "The backup must force a piece recheck");
}


void saveDhtProfile() {
  using biglybtc::storage::AzureusContacts;
  using biglybtc::storage::AzureusSavedContact;
  using biglybtc::storage::AzureusValueVersions;
  TemporaryProfile fixture;
  const auto lease = ProfileLease::acquire(fixture.path);
  require(lease.has_value(), "DHT profile lease could not be acquired");

  {
    const auto versions = AzureusValueVersions::open(*lease, 0);
    require(versions.has_value(), "DHT version file could not be opened");
    const auto first = (*versions)->next(1000);
    require(first && *first == 1, "DHT version reservation failed");
  }
  const auto versions = AzureusValueVersions::open(*lease, 0);
  require(versions.has_value(), "DHT version file could not be reopened");
  const auto resumed = (*versions)->next(1000);
  require(resumed && *resumed == 129, "DHT version reservation was reused after restart");

  const AzureusSavedContact first{25, 55, 4, {127, 0, 0, 1}, 49001};
  const AzureusSavedContact next{30, 55, 4, {127, 0, 0, 1}, 49002};
  {
    const auto contacts = AzureusContacts::open(*lease, 0, 4);
    require(contacts.has_value(), "DHT contact cache could not be opened");
    require((*contacts)->save({&first, 1}).has_value(), "DHT contacts could not be written");
    require((*contacts)->save({&next, 1}).has_value(), "DHT contacts could not be replaced");
  }
  const auto cache = fixture.path / "dht" / "contacts.dat";
  const auto pending = fixture.path / "dht" / "contacts.saving";
  std::filesystem::rename(cache, pending);
  const auto contacts = AzureusContacts::open(*lease, 0, 4);
  require(contacts.has_value(), "DHT contacts could not be recovered from saving file");
  const auto published = (*contacts)->snapshot();
  require(published && (*published)->contacts == std::vector{next}
      && std::filesystem::is_regular_file(cache) && !std::filesystem::exists(pending),
      "Reopened DHT cache did not match last saved contact");
}


void checkpointMainline() {
  using biglybtc::storage::MainlineCheckpoint;
  TemporaryProfile fixture;
  const auto lease = ProfileLease::acquire(fixture.path);
  require(lease.has_value(), "Mainline profile lease could not be acquired");
  auto firstWriter = MainlineCheckpoint::load(*lease, "mldht");
  auto staleWriter = MainlineCheckpoint::load(*lease, "mldht");
  require(firstWriter && staleWriter && !firstWriter->state(), "New Mainline checkpoint must be absent");
  const auto state = [](char tag) {
    const auto id = std::string(20, tag) + std::string("\x7f\x00\x00\x01", 4);
    return "d9:dht stated7:node-idl24:" + id + "e5:nodesle6:nodes6leee";
  };
  const auto original = state('A');
  const auto next = state('B');
  require(firstWriter->save(*lease, original).has_value(), "First Mainline state publication failed");
  const auto stale = staleWriter->save(*lease, next);
  require(!stale && stale.error().code == biglybtc::storage::ErrorCode::Conflict,
      "A stale Mainline state must preserve the existing checkpoint");
  require(firstWriter->save(*lease, next).has_value(), "Mainline state update failed");

  const auto current = MainlineCheckpoint::load(*lease, "mldht");
  require(current && current->state() == next, "Reopened Mainline state is incorrect");
  require(std::filesystem::remove(fixture.path / "BiglyBTC-mainline.config"), "Unable to remove primary fixture");
  const auto backup = MainlineCheckpoint::load(*lease, "mldht");
  require(backup && backup->state() == original, "Backed-up Mainline state must be recoverable");
}

} // namespace

int main() {
  try {
    exercise();
    publishAcrossDirectories();
    checkpointTorrent();
    saveDhtProfile();
    checkpointMainline();
    std::cout << "Profile configuration transaction and backup recovery verified\n";
    std::cout << "Torrent state checkpoint and recheck-safe backup verified\n";
    std::cout << "Azureus DHT version and contact cache transactions verified\n";
    std::cout << "Mainline DHT state transaction and backup verified\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
