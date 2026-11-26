// SPDX-License-Identifier: GPL-2.0-or-later
#include "intake_destination_snapshot.hpp"
#include "file_operation_io.hpp"
#include "original_payload_io.hpp"
#include "profile_transaction_pending.hpp"
#include "selected_existing_scope.hpp"
#include "state_directory_listing.hpp"
#include "state_file_io.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
namespace storage = biglybtc::storage;
namespace compat = biglybtc::compat;
namespace core = biglybtc::core;
namespace io = storage::detail;
namespace fileop = storage::fileop;

void require(bool ok, const char* message) {
  if (!ok) { throw std::runtime_error(message); }
}

template<class T> T take(std::expected<T, storage::Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
  return std::move(*result);
}

struct Fixture {
  fs::path root;

  Fixture() {
    fs::path name(u8"biglybtc-scope-\u03bb-");
    name += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    root = fs::temp_directory_path() / name;
    require(fs::create_directory(root), "Cannot create generated scope directory");
    root = fs::canonical(root);
  }

  ~Fixture() {
    std::error_code error;
    fs::remove_all(root, error);
  }
};

void write(const fs::path& path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  require(output.good(), "Cannot write generated payload or control");
}

template<class Op> void expectConflict(Op operation, const char* message) {
  try {
    operation();
  } catch (const fileop::Failure& failure) {
    require(failure.error.code == storage::ErrorCode::Conflict, message);
    return;
  }
  throw std::runtime_error(message);
}

void exerciseJournalIo() {
  Fixture fixture;
  const auto path = fixture.root / fs::path(u8"\u03bb-journal");
  const auto temporary = fs::path(path).concat(".BiglyBTC.fileop-saving");
  require(!fileop::read(path, 64).bytes, "Missing journal unexpectedly has a preimage");
  fileop::publish(path, {}, "prepared", 64);
  const auto first = fileop::read(path, 64);
  require(first.bytes == "prepared" && first.stamp && !fs::exists(temporary),
      "Journal first publication did not consume its temporary");
  fileop::publish(path, first, "applied", 64);
  const auto applied = fileop::read(path, 64);
  require(applied.bytes == "applied" && applied.stamp, "Journal replacement lost its bytes or identity");
  expectConflict([&] { fileop::publish(path, first, "stale", 64); }, "Stale journal preimage was accepted");

  write(temporary, "interrupted");
  expectConflict([&] { fileop::publish(path, applied, "skipped", 64); }, "Interrupted temporary was overwritten");
  require(fileop::read(temporary, 64).bytes == "interrupted" && fileop::read(path, 64).bytes == "applied",
      "Journal collision changed an existing file");

  fs::rename(path, fixture.root / "old-journal");
  write(path, "applied");
  expectConflict([&] { fileop::publish(path, applied, "aliased", 64); }, "Same-byte replacement was published over");
  expectConflict([&] { fileop::erase(path, *applied.stamp); }, "Same-byte replacement was deleted");
  const auto replacement = fileop::inspect(path);
  require(replacement.has_value(), "Replacement journal was lost after conflict");
  fileop::PayloadDigestCache digests;
  const auto digest = digests.digest(path, *replacement);
  require(digest == fileop::digest("applied") && digest == digests.digest(path, *replacement),
      "Digest of the authenticated file changed across reads");
  fileop::erase(path, *replacement);
  require(!fs::exists(path) && fs::exists(fixture.root / "old-journal"),
      "Removing with a retained digest handle did not preserve the original inode");
}

void exerciseRelocationIo() {
  Fixture fixture;
  const auto source = fixture.root / "source";
  const auto target = fixture.root / "target";
  fs::create_directory(source);
  fs::create_directory(target);
  const auto leaf = fs::path(u8"\u03bb-payload");
  const auto temporary = fs::path(u8"\u03bb-relocating");
  std::string data(150000, 'L');
  data[65536] = 'R';
  write(source / leaf, data);
  const auto sourceDirectory = fileop::directory(source);
  const auto targetDirectory = fileop::directory(target);
  const auto original = fileop::inspect(source / leaf);
  require(original.has_value(), "Cannot inspect generated relocation source");
  fileop::PayloadDigestCache digests;
  const auto content = digests.digest(source / leaf, *original);

  write(target / leaf, "occupied");
  expectConflict([&] { fileop::tryLinkPayload(sourceDirectory.get(), leaf, *original, targetDirectory.get(), leaf); },
      "Hard link overwrote an occupied relocation destination");
  require(fileop::read(target / leaf, 64).bytes == "occupied", "Hard link collision changed destination contents");
  fileop::erase(target / leaf, *fileop::inspect(target / leaf));
  require(fileop::tryLinkPayload(sourceDirectory.get(), leaf, *original, targetDirectory.get(), leaf),
      "Cannot link an owned source to an empty destination on the same volume");
  const auto linked = fileop::inspect(target / leaf);
  require(linked && fileop::identity(*linked, *original), "Hard link changed the file identity");
  fileop::erase(target / leaf, *linked);
  const auto sourceAfterLink = fileop::inspect(source / leaf);
  require(sourceAfterLink && fileop::identity(*sourceAfterLink, *original), "Removing an alias changed the original source");

  {
    auto scratch = fileop::openCopyScratch(targetDirectory.get(), temporary, {});
    const std::array partial{std::byte{'P'}, std::byte{'A'}, std::byte{'R'}};
    require(io::writeOriginalPayload(scratch.get(), 0, partial) == 3, "Cannot simulate an interrupted copy");
  }
  const auto partial = fileop::inspect(target / temporary);
  require(partial.has_value(), "Interrupted scratch disappeared");
  {
    auto scratch = fileop::openCopyScratch(targetDirectory.get(), temporary, partial);
    require(fileop::identity(fileop::heldFileStamp(scratch.get()), *partial), "Restart created a different scratch file");
    fileop::copyPayload(sourceDirectory.get(), leaf, *sourceAfterLink, scratch.get());
  }
  const auto full = fileop::inspect(target / temporary);
  require(full && digests.digest(target / temporary, *full) == content, "Resumed scratch copy differs from source");
  write(target / leaf, "occupied");
  expectConflict([&] { fileop::renamePayload(targetDirectory.get(), temporary, *full, targetDirectory.get(), leaf); },
      "Scratch rename overwrote an occupied destination");
  require(fileop::read(target / leaf, 64).bytes == "occupied" && fs::exists(target / temporary),
      "Failed scratch rename changed destination or consumed the scratch");
  fileop::erase(target / leaf, *fileop::inspect(target / leaf));
  fileop::renamePayload(targetDirectory.get(), temporary, *full, targetDirectory.get(), leaf);
  const auto published = fileop::inspect(target / leaf);
  require(published && fileop::identity(*published, *full) && !fs::exists(target / temporary)
      && digests.digest(target / leaf, *published) == content, "Copy publication lost its contents or identity");
}

void exerciseAllocationReceipt() {
  Fixture fixture;
  const auto first = fixture.root / "first";
  const auto second = fixture.root / "second";
  const auto from = fileop::directory(first, true);
  const auto to = fileop::directory(second, true);
  const fs::path leaf(u8"\u03bb-receipt");
  const auto token = std::string(64, 'a');
  bool written;
  {
    const auto file = fileop::openCopyScratch(from.get(), leaf, {});
    written = fileop::writeAllocationToken(from.get(), leaf, file.get(), token);
  }
  const auto stamp = fileop::inspect(first / leaf);
  require(stamp && stamp->size == 0, "Allocation receipt changed the payload length");
  fileop::renamePayload(from.get(), leaf, *stamp, to.get(), leaf);
  require(!fs::exists(first / leaf) && fs::exists(second / leaf), "Cross-directory rename lost an allocation");
  const auto file = take(io::openStateFile(to.get(), leaf));
  const auto receipt = fileop::readAllocationToken(to.get(), leaf, file.get());
  if (written) {
    require(receipt == token, "Allocation receipt did not survive rename");
    std::cout << "Allocation receipt survived cross-directory rename\n";
  } else {
    require(!receipt, "A volume without receipt support returned a token");
    std::cout << "Allocation receipt fallback passed on a volume without token support\n";
  }
}

void exerciseWritableJournal() {
  Fixture fixture;
  const auto parent = fileop::directory(fixture.root);
  const fs::path saving(u8"\u03bb-compaction.saving");
  const fs::path journal(u8"\u03bb-compaction");
  constexpr std::uint64_t tail = 65536;
  const std::array header{std::byte{'H'}};
  const std::array before{std::byte{'B'}};
  const std::array after{std::byte{'A'}};
  std::optional<fileop::FileStamp> original;
  write(fixture.root / journal, "existing journal");
  {
    const auto file = take(io::openWritableStateFile(parent.get(), saving, true));
    require(io::writeOriginalPayload(file.get(), 0, header) == 1
        && io::writeOriginalPayload(file.get(), tail, before) == 1, "Cannot write positioned journal bytes");
    fileop::sync(file.get());
    const auto staged = fileop::heldFileStamp(file.get());
    const auto collision = io::publishStateFile(parent.get(), saving, journal, file.get(), false, &staged);
    require(!collision && collision.error().code == storage::ErrorCode::Conflict,
        "Journal publication overwrote an existing name");
    original = staged;
  }
  {
    const auto file = take(io::openWritableStateFile(parent.get(), saving, false));
    require(fileop::identity(fileop::heldFileStamp(file.get()), *original), "Reopening replaced a journal temporary");
    require(io::writeOriginalPayload(file.get(), tail, after) == 1, "Cannot rewrite the authorized journal tail");
    fileop::sync(file.get());
    const auto staged = fileop::heldFileStamp(file.get());
    require(io::publishStateFile(parent.get(), saving, journal, file.get(), true, &staged).has_value(),
        "Cannot replace the reviewed journal from its held handle");
  }
  const auto current = fileop::read(fixture.root / journal, tail + 1);
  require(current.bytes && current.bytes->size() == tail + 1 && current.bytes->front() == 'H'
      && current.bytes->back() == 'A' && current.stamp && fileop::identity(*original, *current.stamp)
      && !fs::exists(fixture.root / saving), "Published journal lost its identity or positioned bytes");
}

void exercisePendingTransactions() {
  Fixture fixture;
  const auto lease = take(storage::ProfileLease::acquire(fixture.root));
  require(!storage::hasPendingTorrentIntake(lease) && !storage::hasPendingCategoryDeletion(lease),
      "An empty profile reported pending transactions");
  const auto intake = fixture.root / io::intakeJournal::JournalSaving;
  write(intake, "partial");
  require(storage::hasPendingTorrentIntake(lease) && !storage::hasPendingCategoryDeletion(lease),
      "An unpublished intake was missed or mistaken for a category journal");
  fs::remove(intake);
  const auto category = fixture.root / io::LegacyCategoryJournalNames[1];
  write(category, "older backup");
  require(storage::hasPendingCategoryDeletion(lease) && !storage::hasPendingTorrentIntake(lease),
      "A legacy category backup did not block category recovery");
  fs::remove(category);
  require(!storage::hasPendingTorrentIntake(lease) && !storage::hasPendingCategoryDeletion(lease),
      "Retired transaction entries kept the profile blocked");
}

void exerciseStateDirectoryListing() {
  Fixture fixture;
  const auto lease=take(storage::ProfileLease::acquire(fixture.root));
  const fs::path bundle(u8"MessagesBundle_\u03bb.properties");
  write(fixture.root / bundle,"name=fixture");
  write(fixture.root / "marker","second entry");
  const auto limited=io::listStateDirectoryNames(lease.directoryHandle(),1);
  require(!limited,"Profile listing exceeded its bound");
  if(limited.error().code!=storage::ErrorCode::Unsupported){throw std::runtime_error(limited.error().message);}
  for(int pass=0;pass<2;++pass){
    const auto names=take(io::listStateDirectoryNames(lease.directoryHandle(),16));
    require(std::ranges::count(names,bundle)==1&&std::ranges::count(names,fs::path("marker"))==1,
        "Profile listing lost names or reused a consumed cursor");
  }
}

void exerciseScopes() {
  Fixture fixture;
  const auto profile = fixture.root / "profile";
  require(fs::create_directory(profile), "Cannot create generated profile");
  auto lease = take(storage::ProfileLease::acquire(profile));
  const auto destination = fixture.root / fs::path(u8"\u03bb-storage");
  const auto leaf = std::string("payload-\xce\xbb");
  const auto path = destination / fs::path(u8"payload-\u03bb");
  const std::array paths{path};
  const auto torrent = core::Document::parse("d4:infod4:name7:payload6:pieces20:" + std::string(20, 'A') + "ee");
  require(torrent.has_value(), "Cannot decode generated metadata");
  const compat::OriginalLayout layout{leaf, false, 16384, 7, {{leaf, 0, 7, false}}};
  const auto hash = core::TorrentHash::parse(std::string(20, 'A'));
  require(hash.has_value(), "Cannot decode generated selected key");
  std::array<std::byte, 20> full;
  full.fill(std::byte{0x41});
  const storage::OriginalTorrentIdentity identity{*hash, storage::OriginalSelectedHash::V1, {full, {}}};

  const auto snapshot = [&] {
    return take(storage::captureIntakeDestinationSnapshot(*torrent, layout, *hash, paths));
  };
  const auto absentAncestor = snapshot();
  require(absentAncestor == snapshot() && !fs::exists(destination), "Observing missing paths created a directory");
  require(fs::create_directory(destination), "Cannot create generated destination directory");
  const auto absentLeaf = snapshot();
  require(absentAncestor != absentLeaf && !fs::exists(path), "The first missing component did not affect the snapshot");
  write(path, "payload");
  const auto observed = snapshot();
  require(observed != absentLeaf && observed == snapshot(), "Existing leaf was not observed consistently");

  const auto scope = take(storage::VerifiedExistingScope::capture(lease, *torrent, layout, identity, destination, leaf));
  const auto encoded = core::Document::parse(scope.encoded());
  require(encoded.has_value(), "Cannot decode saved full scope");
  const auto restored = take(storage::VerifiedExistingScope::decode(encoded->root()));
  require(restored.validate(lease, *torrent, layout, identity, destination, leaf).has_value(),
      "Restored scope did not accept the same payload");
  auto parent = take(io::openStatePathDirectory(destination));
  {
    const auto file = take(io::openOriginalPayloadAt(parent.get(), path.filename(), io::OriginalPayloadAccess::ReadWrite));
    require(!restored.bind(0, path, file.get()), "Read-only scope accepted a writable descriptor");
  }
  {
    const auto file = take(io::openOriginalPayloadAt(parent.get(), path.filename(), io::OriginalPayloadAccess::ReadOnly));
    const auto proof = take(restored.bind(0, path, file.get()));
    require(proof().has_value(), "Validated read-only descriptor failed its captured proof");
  }

  const std::array policies{compat::FilePolicy{false, 0}};
  const std::array kinds{storage::OriginalStorageKind::Compact};
  const auto control = profile / "active" / hash->hex() / "fmfile0.dat";
  require(fs::create_directories(control.parent_path()), "Cannot create generated control directory");
  constexpr std::string_view controlBytes = "d6:lengthi7e7:versioni0ee";
  write(control, controlBytes);
  const auto captureSelected = [&] {
    return take(storage::SelectedExistingScope::capture(lease, *torrent, layout, identity, policies, kinds, paths));
  };
  const auto validateSelected = [&](const storage::SelectedExistingScope& selected) {
    return selected.validate(lease, *torrent, layout, identity, policies, kinds, paths);
  };
  const auto selected = captureSelected();
  require(validateSelected(selected).has_value(), "Selected scope did not accept its original compact control");
  fs::rename(control, control.parent_path() / "detached-control");
  write(control, controlBytes);
  require(!validateSelected(selected), "Selected scope accepted a same-byte replacement control");
  const auto replacementControl = captureSelected();

  fs::rename(path, destination / "detached-payload");
  write(path, "payload");
  require(!restored.validate(lease, *torrent, layout, identity, destination, leaf)
      && !validateSelected(replacementControl) && observed != snapshot(),
      "Selected and full scopes must reject same-byte replacement payloads");
}
} // namespace

int main() {
  try {
    exerciseScopes();
    std::cout << "Unicode scope paths, read-only proof and payload/control replacements passed" << std::endl;
    exerciseJournalIo();
    std::cout << "Journal publication, conflict checks and digest-backed removal passed" << std::endl;
    exerciseRelocationIo();
    std::cout << "Hard-link relocation, interrupted copy and no-overwrite rename passed" << std::endl;
    exerciseAllocationReceipt();
    exerciseWritableJournal();
    std::cout << "Positioned journal writes, reopen and atomic publication passed" << std::endl;
    exercisePendingTransactions();
    std::cout << "Intake and legacy category recovery fences passed" << std::endl;
    exerciseStateDirectoryListing();
  } catch (const fileop::Failure& failure) {
    std::cerr << failure.error.message << '\n';
    return 1;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
