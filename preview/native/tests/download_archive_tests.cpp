// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_archive_dialog.hpp"
#include "download_archive.hpp"
#include "biglybtc/library.hpp"
#include "original_identity_digest.hpp"
#include "biglybtc/saved_download_tags.hpp"
#include "tag_manager_startup.hpp"
#include "utf8_path.hpp"

#include <QApplication>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <array>
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

void verifyDiscard(const std::filesystem::path& root, const core::TorrentHash& hash,
    std::optional<storage::DownloadArchiveRestoreStage> interrupt) {
  const auto archive = root / "dlarchive";
  const auto state = archive / (hash.hex() + ".dat");
  const auto backup = archive / (hash.hex() + ".dat.bak");
  const auto inventory = read(root / "downloads.config");
  const auto tags = read(root / "tag.config");
  write(root / "original.torrent", "Keep original metadata");
  write(archive / "foreign.dat", "Keep other archived state");
  write(backup, read(state));
  {
    auto lease = storage::ProfileLease::acquire(root);
    require(lease.has_value(), "Hold the profile for archive removal");
    if (interrupt) { interruptAfter = *interrupt; }
    const auto result = storage::removeArchivedDownload(*lease, hash, {}, interrupt ? interruptRestore : nullptr);
    require(result.has_value() != interrupt.has_value(), result ? "Interruption was not observed" : result.error().message.c_str());
    require(storage::hasPendingArchiveRestore(*lease) == interrupt.has_value(), "Detect a pending archive removal");
  }
  if (interrupt) {
    if (*interrupt == storage::DownloadArchiveRestoreStage::CatalogPublished) {
      require(std::filesystem::remove(state), "Simulate the first owned state removal before a process exit");
    }
    auto lease = storage::ProfileLease::acquire(root);
    require(lease.has_value(), "Reopen an interrupted archive removal");
    const auto recovered = storage::recoverArchiveRestore(*lease);
    require(recovered && *recovered, recovered ? "No archive removal was recovered" : recovered.error().message.c_str());
  }
  auto lease = storage::ProfileLease::acquire(root);
  require(lease.has_value() && !storage::hasPendingArchiveRestore(*lease), "Retire the archive removal journal");
  require(read(root / storage::DownloadArchiveFile) == "d4:note4:kept5:stubslee", "Remove only the selected archive row");
  require(!std::filesystem::exists(state) && !std::filesystem::exists(backup) && !std::filesystem::exists(archive / hash.hex()),
      "Retire the selected archived state, backup and auxiliary tree");
  require(read(root / "downloads.config") == inventory && read(root / "tag.config") == tags
      && read(root / "fixture.bin") == "payload" && read(root / "original.torrent") == "Keep original metadata"
      && read(archive / "foreign.dat") == "Keep other archived state", "Keep payload, torrent metadata and unrelated profile files");
}

void verifyRestore(std::optional<storage::DownloadArchiveRestoreStage> interrupt, bool existingMarker = false) {
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
      + encode(metadata) + "12:torrent_hash" + encode(rawHash) + "e4:hash" + encode(rawHash) + "1:l" + encode(target)
      + "1:tl4:Auto4:Kept5:Savedee";
  const auto catalog = "d4:note4:kept5:stubsl" + stub + "ee";
  write(root / storage::DownloadArchiveFile, catalog);
  write(root / "downloads.config", "d9:downloadslee");
  write(root / "fixture.bin", "payload");
  const auto useRussian = existingMarker || interrupt == storage::DownloadArchiveRestoreStage::TagsPublished;
  const std::string markerName = useRussian ? "Восстановлено" : "Restored";
  if (useRussian) { write(root / "biglybt.config", "d6:locale5:ru_RUe"); }
  const std::string markerRecord = existingMarker ? "1:3d1:cd3:pubi1ee1:n" + encode(markerName) + "1:olee" : "";
  write(root / "tag.config", "d1:3d1:1d1:cd11:pp.untaggedi1ee1:n4:Auto1:olee"
      "1:2d1:cd6:b.downl4:2048e4:b.upl2:17ee1:n4:Kept1:olee" + markerRecord + "ee");
  {
    auto lease = storage::ProfileLease::acquire(root);
    require(lease.has_value(), "Acquire isolated original restore profile");
    const auto mode = storage::TagManagerStartupMode::capture(*lease);
    require(mode && mode->enabled(), "Capture original tag manager startup mode");
    require(storage::archiveRestoredTagName(*mode) == markerName, "Read the same localized marker for archive and restore");
    if (interrupt) { interruptAfter = *interrupt; }
    const auto result = storage::restoreArchivedDownload(*lease, hash, {}, interrupt ? interruptRestore : nullptr,
        &*mode, true);
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
  const auto savedTags = core::readSavedDownloadTags(read(root / "tag.config"));
  require(savedTags && savedTags->tags.size() == 4 && std::ranges::all_of(savedTags->tags, [&](const auto& tag) {
    return tag.rawName == "Auto" ? tag.uniqueHashes.empty() : tag.uniqueHashes == std::vector<core::TorrentHash>{hash};
  }), "Restore ordinary manual tags and leave automatic memberships to their owner");
  const auto created = std::ranges::find_if(savedTags->tags, [](const auto& tag) { return tag.rawName == "Saved"; });
  require(created != savedTags->tags.end(), "Recreate the archived tag missing from the current profile");
  const auto* createdRecord = savedTags->source->find("3")->find(created->idKey);
  const auto* createdSettings = createdRecord ? createdRecord->find("c") : nullptr;
  require(createdRecord && (!createdSettings || (!createdSettings->find("gr") && !createdSettings->find("pub"))),
      "A recreated archive tag retains the ordinary grouping and public defaults");
  const auto marker = std::ranges::find_if(savedTags->tags, [&](const auto& tag) { return tag.rawName == markerName; });
  require(marker != savedTags->tags.end(), "Restore adds the original localized marker once");
  const auto* markerSettings = savedTags->source->find("3")->find(marker->idKey)->find("c");
  const auto* publicTag = markerSettings ? markerSettings->find("pub") : nullptr;
  require(publicTag && publicTag->type == core::Node::Type::Integer && publicTag->integer == (existingMarker ? 1 : 0),
      "New marker is private and the existing marker retains its public preference");
  const auto* totals = savedTags->source->find("3")->find("2")->find("c")->find("b.up");
  require(totals && totals->children.front().bytes == "17", "Preserve original tag transfer statistics");

  const std::array<std::string, 2> manualTags{"Kept", "Saved"};
  const bool interruptArchive = interrupt && *interrupt != storage::DownloadArchiveRestoreStage::TagsPublished;
  {
    auto lease = storage::ProfileLease::acquire(root);
    require(lease.has_value(), "Hold the original restored profile for archiving");
    const auto result = storage::archiveStoppedDownload(*lease, hash, 1700000000000, manualTags, {},
        interruptArchive ? interruptRestore : nullptr);
    require(result.has_value() != interruptArchive, result ? "Interruption was not observed" : result.error().message.c_str());
  }
  {
    auto lease = storage::ProfileLease::acquire(root);
    require(lease.has_value(), "Reopen the archived original profile");
    if (interruptArchive) {
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
  if (!interrupt || *interrupt == storage::DownloadArchiveRestoreStage::JournalSynced
      || *interrupt == storage::DownloadArchiveRestoreStage::CatalogPublished) {
    verifyDiscard(root, hash, interrupt);
  }
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
  auto earlier = first;
  const auto firstHash = QString::fromStdString(first.hash->hex());
  const auto secondWritable = core::TorrentHash::parse("abcdefghijklmnopqrst");
  require(secondWritable.has_value(), "Use a second writable identity for the selection test");
  earlier.hash = *secondWritable;
  const auto earlierHash = QString::fromStdString(earlier.hash->hex());
  earlier.metadata.archivedMillis = 1;
  packet->rows = std::move(archive->stubs);
  packet->rows.push_back(std::move(earlier));
  ui::DownloadArchiveDialog dialog(std::move(packet));
  auto* table = dialog.findChild<QTableView*>("archived-downloads-table");
  require(table && table->model()->rowCount() == 3, "Show all archive rows");
  auto* model = table->model();
  require(model->index(0, 0).data().toString() == "Original v2" && model->index(0, 3).data().toString().isEmpty()
      && model->index(1, 0).data().toString() == QStringLiteral("Open source Ω"),
      "Display preserved text, newest first, with no fabricated completion date");
  auto* restore = dialog.findChild<QPushButton*>("restore-archived-download");
  auto* restoreAndStart = dialog.findChild<QPushButton*>("restore-and-start-archived-downloads");
  auto* remove = dialog.findChild<QPushButton*>("remove-archived-downloads");
  require(restore && restoreAndStart && remove && !restore->isEnabled() && !restoreAndStart->isEnabled() && !remove->isEnabled(),
      "Archive actions require a selection");
  table->selectRow(0);
  require(!restore->isEnabled() && !restoreAndStart->isEnabled() && !remove->isEnabled(),
      "Full v2 display identity cannot issue an original archive action");
  table->selectRow(1);
  require(restore->isEnabled() && restoreAndStart->isEnabled() && remove->isEnabled(),
      "Archive actions enable for a selected original identity");
  quint64 requestedSession = 0;
  QStringList requestedHashes;
  bool requestedStart = false;
  QObject::connect(&dialog, &ui::DownloadArchiveDialog::restoreRequested, &dialog, [&](quint64 session, QStringList hashes, bool start) {
    requestedSession = session;
    requestedHashes = std::move(hashes);
    requestedStart = start;
  });
  restore->click();
  require(requestedSession == 17 && requestedHashes == QStringList{firstHash} && !requestedStart,
      "Sortable archive selection routes the original identity and live profile session");
  auto* selection = table->selectionModel();
  selection->select(model->index(2, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
  restoreAndStart->click();
  require(requestedSession == 17 && requestedHashes.size() == 2 && requestedHashes.contains(firstHash)
      && requestedHashes.contains(earlierHash) && requestedStart, "Restore and Start routes both selected original identities");
  QStringList removedHashes;
  QObject::connect(&dialog, &ui::DownloadArchiveDialog::removeRequested, &dialog, [&](quint64 session, QStringList hashes) {
    requestedSession = session;
    removedHashes = std::move(hashes);
  });
  const auto respondToRemoval = [&](QMessageBox::StandardButton answer) {
    QTimer::singleShot(0, &dialog, [&dialog, answer] {
      if (auto* question = dialog.findChild<QMessageBox*>()) {
        if (auto* choice = question->button(answer)) { choice->click(); }
        else { question->reject(); }
      }
    });
    remove->click();
  };
  respondToRemoval(QMessageBox::Cancel);
  require(removedHashes.isEmpty(), "Cancelling removal preserves the selection");
  respondToRemoval(QMessageBox::Yes);
  require(requestedSession == 17 && removedHashes.size() == 2 && removedHashes.contains(firstHash)
      && removedHashes.contains(earlierHash), "Confirmed removal forwards both selected archive identities");
  selection->select(model->index(0, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
  require(!restore->isEnabled() && !restoreAndStart->isEnabled() && !remove->isEnabled(),
      "A mixed supported and unsupported selection cannot issue a partial command");

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
  verifyRestore({}, true);
  for (const auto stage : {storage::DownloadArchiveRestoreStage::JournalSynced, storage::DownloadArchiveRestoreStage::StatePublished,
      storage::DownloadArchiveRestoreStage::InventoryPublished, storage::DownloadArchiveRestoreStage::CatalogPublished,
      storage::DownloadArchiveRestoreStage::TagsPublished}) {
    verifyRestore(stage);
  }
  std::cout << "Original archive rows, desktop restore selection, durable restoration and archive removal passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
