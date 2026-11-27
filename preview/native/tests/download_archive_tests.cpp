// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_archive_dialog.hpp"
#include "download_archive.hpp"
#include "biglybtc/library.hpp"
#include "original_identity_digest.hpp"
#include "utf8_path.hpp"

#include <QApplication>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace biglybtc;

void require(bool ok, const char* message) {
  if (!ok) { throw std::runtime_error(message); }
}

std::string read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  require(bool(file), "Read the archive fixture");
  return {std::istreambuf_iterator<char>(file), {}};
}

void write(const std::filesystem::path& path, std::string_view bytes) {
  std::ofstream file(path, std::ios::binary);
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  require(bool(file), "Write the isolated archive fixture");
}

std::string encode(std::string_view value) {
  return std::to_string(value.size()) + ':' + std::string(value);
}

storage::DownloadArchiveRestoreStage interruptAfter;

void interruptRestore(storage::DownloadArchiveRestoreStage stage) {
  if (stage == interruptAfter) { throw std::runtime_error("Interrupted archive transaction test"); }
}

void verifyRestore(std::optional<storage::DownloadArchiveRestoreStage> interrupt) {
  QTemporaryDir work;
  require(work.isValid(), "Create the isolated restore profile");
  const std::filesystem::path root(work.path().toStdU16String());
  std::filesystem::create_directories(root / "dlarchive");
  std::filesystem::create_directories(root / "active");
  const auto state = "d10:attributesd5:flagsi0e10:storetypesl1:Lee4:infod6:lengthi7e4:name11:fixture.bin"
      "12:piece lengthi16384e6:pieces20:" + std::string(20, 'p') + "e6:resumed4:datad11:resume data1:\x01" "5:validi1eeee";
  const auto document = core::Document::parse(state);
  require(document.has_value(), "Parse test torrent state");
  const auto identity = storage::digestOriginalTorrentIdentity(*document);
  require(identity.has_value(), "Derive the original test torrent identity");
  const auto hash = identity->originalKey;
  const auto hashBytes = hash.bytes();
  const std::string_view rawHash(reinterpret_cast<const char*>(hashBytes.data()), hashBytes.size());
  const auto archived = root / "dlarchive";
  const auto active = root / "active";
  const auto stateName = hash.hex() + ".dat";
  const auto nested = std::filesystem::path(hash.hex()) / "auxiliary";
  std::filesystem::create_directories(archived / nested);
  write(archived / stateName, state);
  write(archived / nested / "kept.bin", "auxiliary bytes");
  const auto metadata = platform::pathToUtf8(root / "original.torrent");
  const auto target = platform::pathToUtf8(root / "fixture.bin");
  const auto stub = "d2:gmd8:save_dir" + encode(platform::pathToUtf8(root)) + "9:save_file11:fixture.bin5:statei70e7:torrent"
      + encode(metadata) + "12:torrent_hash" + encode(rawHash) + "e4:hash" + encode(rawHash) + "1:l" + encode(target) + 'e';
  const auto catalog = "d4:note4:kept5:stubsl" + stub + "ee";
  write(root / storage::DownloadArchiveFile, catalog);
  write(root / "downloads.config", "d9:downloadslee");
  write(root / "fixture.bin", "payload");
  {
    auto lease = storage::ProfileLease::acquire(root);
    require(lease.has_value(), "Acquire isolated original restore profile");
    if (interrupt) { interruptAfter = *interrupt; }
    const auto result = storage::restoreArchivedDownload(*lease, hash, {}, interrupt ? interruptRestore : nullptr);
    require(result.has_value() != interrupt.has_value(), result ? "Interruption was not observed" : result.error().message.c_str());
    require(storage::hasPendingArchiveRestore(*lease) == interrupt.has_value(), "Detect the actual restore journal");
  }
  if (interrupt) {
    auto lease = storage::ProfileLease::acquire(root);
    require(lease.has_value(), "Reopen the interrupted original profile");
    const auto result = storage::recoverArchiveRestore(*lease);
    require(result && *result, result ? "No restore journal was recovered" : result.error().message.c_str());
    require(!storage::hasPendingArchiveRestore(*lease), "Retire the recovered restore journal");
  }
  const auto inventory = core::Document::parse(read(root / "downloads.config"));
  require(inventory.has_value(), "Reparse the restored Java library inventory");
  const auto saved = core::readLibrary(*inventory);
  require(saved && saved->downloads.size() == 1 && saved->downloads.front().hash == hash
      && saved->downloads.front().startup == core::StartupState::Stopped, "Restore one stopped original row");
  require(read(root / storage::DownloadArchiveFile) == "d4:note4:kept5:stubslee",
      "Remove only the archived row and preserve other catalog fields");
  require(read(active / stateName) == state && read(active / nested / "kept.bin") == "auxiliary bytes"
      && std::filesystem::is_empty(archived) && read(root / "fixture.bin") == "payload",
      "Restore exact state while leaving payload content intact");

  const std::array<std::string, 2> manualTags{"Kept", "Saved"};
  {
    auto lease = storage::ProfileLease::acquire(root);
    require(lease.has_value(), "Hold the original restored profile for archiving");
    const auto result = storage::archiveStoppedDownload(*lease, hash, 1700000000000, manualTags, {},
        interrupt ? interruptRestore : nullptr);
    require(result.has_value() != interrupt.has_value(), result ? "Interruption was not observed" : result.error().message.c_str());
  }
  auto lease = storage::ProfileLease::acquire(root);
  require(lease.has_value(), "Reopen the archived original profile");
  if (interrupt) {
    const auto result = storage::recoverArchiveRestore(*lease);
    require(result && *result, result ? "No archive journal was recovered" : result.error().message.c_str());
  }
  const auto catalogAfter = storage::readDownloadArchive(*lease);
  require(catalogAfter && catalogAfter->stubs.size() == 1 && catalogAfter->stubs.front().hash == hash
      && catalogAfter->stubs.front().metadata.tags == std::vector<std::string>{"Kept", "Saved"},
      "Archive the original identity and manual tag names");
  require(!storage::hasPendingArchiveRestore(*lease) && !std::filesystem::exists(active / stateName)
      && read(archived / stateName) == state && read(archived / nested / "kept.bin") == "auxiliary bytes"
      && read(root / "fixture.bin") == "payload", "Archive verified state and auxiliary bytes while preserving payload files");
}
} // namespace

int main(int argc, char** argv) try {
  QApplication application(argc, argv);
  if (argc != 2) { return 2; }
  const auto fixture = read(argv[1]);
  QTemporaryDir work;
  require(work.isValid(), "Create the isolated archive profile");
  const std::filesystem::path root(work.path().toStdU16String());
  const auto primary = root / storage::DownloadArchiveFile;
  auto lease = storage::ProfileLease::acquire(root);
  require(lease.has_value(), "Hold the archive fixture profile");

  const auto missing = storage::readDownloadArchive(*lease);
  require(missing && !missing->source && missing->stubs.empty(), "A profile with no archive has no entries");
  write(primary, fixture);
  auto archive = storage::readDownloadArchive(*lease);
  require(archive && archive->source && archive->stubs.size() == 2, "Load both actual Java rows");
  require(archive->source->document().find("other") && read(primary) == fixture, "Preserve raw archive bytes");
  const auto& first = archive->stubs[0];
  const auto& second = archive->stubs[1];
  require(first.hash && first.hash->bytes().size() == 20 && second.hash && second.hash->bytes().size() == 32,
      "Preserve the original v1 then v2 catalog order");
  require(first.metadata.category == "Linux" && first.metadata.files == 2
      && first.metadata.fileLengths == std::vector<std::int64_t>{-1024, 4096}
      && first.metadata.shareRatio == 1245 && first.metadata.tags == std::vector<std::string>{"Kept", "Saved"},
      "Retain Original archive fields and incomplete-file sign");
  require(first.metadata.size == 5120 && first.metadata.completedMillis == 1699999000000
      && second.metadata.completedMillis == 0, "Match Original display values");

  auto packet = std::make_shared<ui::DownloadArchivePacket>();
  packet->session = 17;
  packet->writable = true;
  packet->rows = std::move(archive->stubs);
  ui::DownloadArchiveDialog dialog(std::move(packet));
  auto* table = dialog.findChild<QTableView*>("archived-downloads-table");
  require(table && table->model()->rowCount() == 2, "Show both archive rows");
  auto* model = table->model();
  require(model->index(0, 0).data().toString() == "Original v2" && model->index(0, 3).data().toString().isEmpty()
      && model->index(1, 0).data().toString() == QStringLiteral("Open source Ω"),
      "Display preserved text, newest first, with no fabricated completion date");
  auto* restore = dialog.findChild<QPushButton*>("restore-archived-download");
  require(restore && !restore->isEnabled(), "Restore requires a selected archived download");
  table->selectRow(0);
  require(!restore->isEnabled(), "Full v2 display identity cannot issue an original restore");
  table->selectRow(1);
  require(restore->isEnabled(), "Restore enables for a selected original identity");
  quint64 requestedSession = 0;
  QString requestedHash;
  QObject::connect(&dialog, &ui::DownloadArchiveDialog::restoreRequested, &dialog, [&](quint64 session, QString hash) {
    requestedSession = session;
    requestedHash = std::move(hash);
  });
  restore->click();
  require(requestedSession == 17 && requestedHash == QString::fromStdString(first.hash->hex()),
      "Sortable archive selection routes the original identity and live profile session");

  const auto backup = root / "dlarchive.config.bak";
  write(backup, fixture);
  write(primary, "unreadable original bytes");
  const auto recovered = storage::readDownloadArchive(*lease);
  require(recovered && recovered->source && recovered->source->recoverySource() == storage::RecoverySource::Backup
      && recovered->stubs.size() == 2, "Read the recoverable archive candidate");
  require(read(primary) == "unreadable original bytes" && read(backup) == fixture,
      "Inspecting a recovery copy preserves both candidates");
  std::filesystem::remove(primary);
  write(backup, "incomplete archive backup");
  const auto incomplete = storage::readDownloadArchive(*lease);
  require(!incomplete && incomplete.error().code == storage::ErrorCode::InvalidFormat
      && read(backup) == "incomplete archive backup", "Report an incomplete archive without clearing it");

  std::stop_source cancelled;
  cancelled.request_stop();
  const auto stopped = storage::readDownloadArchive(*lease, cancelled.get_token());
  require(!stopped && stopped.error().code == storage::ErrorCode::Cancelled, "Stop before reading a cancelled request");
  verifyRestore({});
  for (const auto stage : {storage::DownloadArchiveRestoreStage::JournalSynced, storage::DownloadArchiveRestoreStage::StatePublished,
      storage::DownloadArchiveRestoreStage::InventoryPublished, storage::DownloadArchiveRestoreStage::CatalogPublished}) {
    verifyRestore(stage);
  }
  std::cout << "Original archive rows, desktop restore selection and durable restore recovery passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
