// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_archive_dialog.hpp"
#include "download_archive.hpp"
#include "biglybtc/library.hpp"
#include "original_identity_digest.hpp"
#include "biglybtc/saved_download_tags.hpp"
#include "tag_manager_startup.hpp"
#include "utf8_path.hpp"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QDir>
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
    std::optional<storage::DownloadArchiveRestoreStage> interrupt, bool deleteMetainfo,
    const std::optional<std::filesystem::path>& configuredDirectory) {
  const auto archive = root / "dlarchive";
  const auto state = archive / (hash.hex() + ".dat");
  const auto backup = archive / (hash.hex() + ".dat.bak");
  const auto inventory = read(root / "downloads.config");
  const auto tags = read(root / "tag.config");
  const auto torrents=configuredDirectory.value_or(root/"torrents");
  const auto config=root/"biglybt.config";
  std::string configuredSource;
  const auto metainfo=torrents/"original.torrent";
  const auto metainfoBackup=torrents/"original.torrent.bak";
  const auto savedTorrent=read(state);
  const auto catalogBefore=read(root/storage::DownloadArchiveFile);
  const auto snapshot=core::Document::parse(catalogBefore);
  require(snapshot&&snapshot->find("stubs")&&snapshot->find("stubs")->children.size()==1,"Read the selected archive row");
  const auto* owningRow=snapshot->find("stubs")->children.front().find("gm");
  require(owningRow,"Read the archive's original inventory entry");
  const auto referencingInventory="d9:downloadsl"+catalogBefore.substr(owningRow->begin,owningRow->end-owningRow->begin)+"ee";
  std::filesystem::create_directories(torrents);
  write(root / "original.torrent", "Keep original metadata");
  write(torrents/"neighbor.torrent","Keep neighboring metadata");
  write(archive / "foreign.dat", "Keep other archived state");
  write(backup,savedTorrent);
  write(metainfoBackup,savedTorrent);
  {
    auto lease = storage::ProfileLease::acquire(root);
    require(lease.has_value(), "Hold the profile for archive removal");
    if (configuredDirectory) {
      write(metainfo,savedTorrent);
      const auto rejected=storage::removeArchivedDownload(*lease,hash,{},nullptr,true);
      require(!rejected&&rejected.error().code==storage::ErrorCode::Unsupported
          &&!storage::hasPendingArchiveRestore(*lease)&&read(metainfo)==savedTorrent,
          "Refuse metainfo in an external directory until the saved setting names it");
      auto values=core::Document::parse(std::filesystem::exists(config)?read(config):"de");
      require(values&&values->replaceValue("General_sDefaultTorrent_Directory",encode(platform::pathToUtf8(torrents))),
          "Set the original metainfo directory in the generated profile");
      configuredSource=values->serialize();
      write(config,configuredSource);
    }
    if (deleteMetainfo) {
      write(metainfo,"Unrelated replacement contents");
      const auto rejected=storage::removeArchivedDownload(*lease,hash,{},nullptr,true);
      require(!rejected&&!storage::hasPendingArchiveRestore(*lease)&&read(root/storage::DownloadArchiveFile)==catalogBefore
          &&read(metainfo)=="Unrelated replacement contents","Reject replaced .torrent files before archive publication");
    }
    write(metainfo,savedTorrent);
    if (deleteMetainfo) {
      write(root/"downloads.config",referencingInventory);
      const auto conflict=storage::removeArchivedDownload(*lease,hash,{},nullptr,true);
      require(!conflict&&conflict.error().code==storage::ErrorCode::Conflict&&!storage::hasPendingArchiveRestore(*lease)
          &&read(metainfo)==savedTorrent&&read(root/storage::DownloadArchiveFile)==catalogBefore,
          "Keep metainfo referenced by the active library");
      write(root/"downloads.config",inventory);
    }
    if (interrupt) { interruptAfter = *interrupt; }
    const auto result = storage::removeArchivedDownload(*lease, hash, {}, interrupt ? interruptRestore : nullptr,deleteMetainfo);
    require(result.has_value() != interrupt.has_value(), result ? "Interruption was not observed" : result.error().message.c_str());
    require(storage::hasPendingArchiveRestore(*lease) == interrupt.has_value(), "Detect a pending archive removal");
  }
  if (interrupt) {
    if (*interrupt == storage::DownloadArchiveRestoreStage::CatalogPublished) {
      if (deleteMetainfo) { require(std::filesystem::remove(metainfo), "Simulate the first owned metainfo removal before exit"); }
      require(std::filesystem::remove(state), "Simulate the first owned state removal before a process exit");
    }
    auto lease = storage::ProfileLease::acquire(root);
    require(lease.has_value(), "Reopen an interrupted archive removal");
    if (deleteMetainfo) {
      write(root/"downloads.config",referencingInventory);
      const auto conflict=storage::recoverArchiveRestore(*lease);
      require(!conflict&&conflict.error().code==storage::ErrorCode::Conflict&&storage::hasPendingArchiveRestore(*lease)
          &&read(metainfoBackup)==savedTorrent,"Preserve a newly referenced backup during interrupted removal");
      write(root/"downloads.config",inventory);
    }
    if (configuredDirectory) {
      auto values=core::Document::parse(configuredSource);
      require(values&&values->replaceValue("General_sDefaultTorrent_Directory",encode(platform::pathToUtf8(archive))),
          "Change the saved metainfo directory during recovery");
      write(config,values->serialize());
      const auto conflict=storage::recoverArchiveRestore(*lease);
      require(!conflict&&conflict.error().code==storage::ErrorCode::Conflict&&storage::hasPendingArchiveRestore(*lease)
          &&read(metainfoBackup)==savedTorrent,"Preserve the pending backup when the saved directory changes");
      write(config,configuredSource);
    }
    const auto recovered = storage::recoverArchiveRestore(*lease);
    require(recovered && *recovered, recovered ? "No archive removal was recovered" : recovered.error().message.c_str());
  }
  auto lease = storage::ProfileLease::acquire(root);
  require(lease.has_value() && !storage::hasPendingArchiveRestore(*lease), "Retire the archive removal journal");
  require(read(root / storage::DownloadArchiveFile) == "d4:note4:kept5:stubslee", "Remove only the selected archive row");
  require(!std::filesystem::exists(state) && !std::filesystem::exists(backup) && !std::filesystem::exists(archive / hash.hex()),
      "Retire the selected archived state, backup and auxiliary tree");
  require(deleteMetainfo?(!std::filesystem::exists(metainfo)&&!std::filesystem::exists(metainfoBackup)):
      (read(metainfo)==savedTorrent&&read(metainfoBackup)==savedTorrent),"Respect the explicit metainfo removal option");
  require(read(root / "downloads.config") == inventory && read(root / "tag.config") == tags
      && read(root / "fixture.bin") == "payload" && read(root / "original.torrent") == "Keep original metadata"
      && read(archive / "foreign.dat") == "Keep other archived state"&&read(torrents/"neighbor.torrent")=="Keep neighboring metadata",
      "Keep payload, unrelated torrent metadata and unrelated profile files");
}

void verifyRestore(std::optional<storage::DownloadArchiveRestoreStage> interrupt, bool existingMarker = false,
    bool deleteMetainfo = false, bool externalMetainfo = false) {
  QTemporaryDir work;
  std::optional<QTemporaryDir> external;
  std::optional<std::filesystem::path> externalMetadata;
  if (externalMetainfo) {
    external.emplace();
    require(external->isValid(),"Create an independent saved metainfo directory");
    externalMetadata.emplace(external->path().toStdU16String());
  }
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
  auto metadataRoot=root;
#ifndef _WIN32
  if (deleteMetainfo&&!externalMetainfo) {
    metadataRoot=root/"alias";
    std::filesystem::create_directory_symlink(root,metadataRoot);
  }
#endif
  const auto directory=externalMetadata.value_or(metadataRoot/"torrents");
  const auto metadata=platform::pathToUtf8(directory/"original.torrent");
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
      "1:2d1:cd6:b.downl4:2048e4:b.upl2:17e6:rl.alsi0e9:rl.maxadli2e9:rl.maxacdi1ee1:n4:Kept1:olee" + markerRecord + "ee");
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
  const auto queueRules=core::savedTagQueueRules(*savedTags);
  const auto queueTag=queueRules.find(hash.hex());
  require(queueTag!=queueRules.end()&&queueTag->second.activityLimits.size()==1,
      "Restoration returns the download to its configured queue cohort");
  const auto& limit=queueTag->second.activityLimits.front();
  require(limit.id==2&&limit.maximumDownloads==2&&limit.maximumSeeds==1&&!limit.strict,
      "Preserve the active download, seed, and strictness settings when restoring a tagged item");

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
    verifyDiscard(root, hash, interrupt, deleteMetainfo, externalMetadata);
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
  require(first.metadata.fileEntries.size()==2&&first.metadata.fileEntries[0].relative
      &&first.metadata.fileEntries[0].path=="part.iso"&&first.metadata.fileEntries[0].length==-1024
      &&!first.metadata.fileEntries[1].relative&&first.metadata.fileEntries[1].path=="/nonexistent/linked file",
      "Distinguish relative and linked archived files in their original order");
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
  const auto savedFolder = root / "Saved files";
  const auto fallbackFolder = root / std::filesystem::path(u"Archive \u03a9");
  std::filesystem::create_directories(savedFolder);
  std::filesystem::create_directories(fallbackFolder);
  write(savedFolder / "payload.bin", "fixture data");
  earlier.metadata.savePath = platform::pathToUtf8(fallbackFolder / "missing" / "file.bin");
  packet->rows = std::move(archive->stubs);
  packet->rows[0].metadata.savePath = platform::pathToUtf8(savedFolder / "payload.bin");
  packet->rows[1].metadata.savePath = "relative/example";
  packet->rows.push_back(std::move(earlier));
  auto parentPacket = std::make_shared<ui::DownloadArchivePacket>(*packet);
  parentPacket->openContainingFolder = true;
  parentPacket->showFullFilePaths = true;
  parentPacket->removeMetainfoByDefault = true;
  parentPacket->rows[0].metadata.savePath = platform::pathToUtf8(savedFolder);
  parentPacket->rows[0].metadata.fileEntries[0].path = "payload.bin";
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
  auto* openFolder = dialog.findChild<QPushButton*>("open-archived-folders");
  require(restore && restoreAndStart && remove && !restore->isEnabled() && !restoreAndStart->isEnabled() && !remove->isEnabled(),
      "Archive actions require a selection");
  require(openFolder && !openFolder->isEnabled(), "Opening a folder requires a selected native save location");
  table->selectRow(0);
  require(!restore->isEnabled() && !restoreAndStart->isEnabled() && !remove->isEnabled(),
      "Full v2 display identity cannot issue an original archive action");
  require(!openFolder->isEnabled(), "A relative archive save location cannot be opened");
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
  QStringList shownLocations;
  bool showSelectedItem = false;
  QObject::connect(&dialog, &ui::DownloadArchiveDialog::openLocationsRequested, &dialog,
      [&](QStringList locations, bool reveal) { shownLocations = std::move(locations); showSelectedItem = reveal; });
  openFolder->click();
  const auto nativeFolder = [](const auto& path) {
    return QDir::fromNativeSeparators(QString::fromStdString(platform::pathToUtf8(path)));
  };
  require(showSelectedItem && shownLocations.size() == 2 && shownLocations.contains(nativeFolder(savedFolder / "payload.bin"))
      && shownLocations.contains(nativeFolder(fallbackFolder)), "Show the exact archived file and the nearest existing ancestor");
  ui::DownloadArchiveDialog parentDialog(std::move(parentPacket));
  auto* parentTable = parentDialog.findChild<QTableView*>("archived-downloads-table");
  auto* openParent = parentDialog.findChild<QPushButton*>("open-archived-folders");
  require(parentTable && openParent && openParent->text() == QStringLiteral("Open Containing Folder"),
      "Show the original containing folder action when enabled");
  QStringList openedParents;
  bool revealParent = true;
  QObject::connect(&parentDialog, &ui::DownloadArchiveDialog::openLocationsRequested, &parentDialog,
      [&](QStringList locations, bool reveal) { openedParents = std::move(locations); revealParent = reveal; });
  parentTable->selectRow(1);
  openParent->click();
  require(!revealParent && openedParents == QStringList{nativeFolder(savedFolder.parent_path())},
      "Containing folder for a saved directory opens its parent");
  auto* parentFiles=parentDialog.findChild<QTableView*>("archived-files-table");
  require(parentFiles&&parentFiles->model()->rowCount()==2,"Show files for the selected archive row");
  auto* fileModel=parentFiles->model();
  const auto fullName=QDir::toNativeSeparators(nativeFolder(savedFolder/"payload.bin"));
  int fileIndex=-1;
  for (int row=0;row<fileModel->rowCount();++row) {
    if (fileModel->index(row,0).data().toString()==fullName) { fileIndex=row; }
  }
  require(fileIndex>=0&&fileModel->index(fileIndex,1).data().toString().startsWith('(')
      &&fileModel->index(fileIndex,1).data(Qt::UserRole).toLongLong()==-1024,
      "Show the absolute path and the original incomplete size sign");
  parentFiles->selectRow(fileIndex);
  require(parentFiles->actions().size()==1&&parentFiles->actions().front()->isEnabled(),"Reveal a selected archived file");
  parentFiles->actions().front()->trigger();
  require(!revealParent&&openedParents==QStringList{nativeFolder(savedFolder)},"Open the selected archived file's containing folder");
  auto* removeFromParent=parentDialog.findChild<QPushButton*>("remove-archived-downloads");
  require(removeFromParent&&removeFromParent->isEnabled(),"A saved torrent can be removed from the archive");
  bool configuredDefault=false, removedFromParent=false;
  QObject::connect(&parentDialog,&ui::DownloadArchiveDialog::removeRequested,&parentDialog,
      [&](quint64,QStringList,bool){removedFromParent=true;});
  QTimer::singleShot(0,&parentDialog,[&] {
    const auto questions=parentDialog.findChildren<QMessageBox*>();
    for(auto* question:questions){
      if(!question->isVisible()){continue;}
      configuredDefault=question->checkBox()&&question->checkBox()->isChecked();
      question->reject();break;
    }
  });
  removeFromParent->click();
  require(configuredDefault&&!removedFromParent,"Show the configured metainfo preference in the confirmation");
  restoreAndStart->click();
  require(requestedSession == 17 && requestedHashes.size() == 2 && requestedHashes.contains(firstHash)
      && requestedHashes.contains(earlierHash) && requestedStart, "Restore and Start routes both selected original identities");
  QStringList removedHashes;
  bool removedMetainfo=false;
  QObject::connect(&dialog, &ui::DownloadArchiveDialog::removeRequested, &dialog, [&](quint64 session, QStringList hashes, bool metainfo) {
    requestedSession = session;
    removedHashes = std::move(hashes);
    removedMetainfo=metainfo;
  });
  const auto respondToRemoval = [&](QMessageBox::StandardButton answer, bool metainfo=false) {
    QTimer::singleShot(0, &dialog, [&dialog, answer, metainfo] {
      for (auto* question:dialog.findChildren<QMessageBox*>()) {
        if (!question->isVisible()) { continue; }
        if (metainfo&&question->checkBox()) { question->checkBox()->setChecked(true); }
        if (auto* choice = question->button(answer)) { choice->click(); }
        else { question->reject(); }
        break;
      }
    });
    remove->click();
  };
  respondToRemoval(QMessageBox::Cancel);
  require(removedHashes.isEmpty(), "Cancelling removal preserves the selection");
  respondToRemoval(QMessageBox::Yes);
  require(requestedSession == 17 && removedHashes.size() == 2 && removedHashes.contains(firstHash)
      && removedHashes.contains(earlierHash)&&!removedMetainfo,"Confirmed removal forwards both selected archive identities");
  respondToRemoval(QMessageBox::Yes,true);
  require(removedMetainfo,"Explicit metainfo selection reaches the archive owner");
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
  verifyRestore({},false,true);
  verifyRestore(storage::DownloadArchiveRestoreStage::CatalogPublished,false,true);
  verifyRestore({},false,true,true);
  verifyRestore(storage::DownloadArchiveRestoreStage::CatalogPublished,false,true,true);
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
