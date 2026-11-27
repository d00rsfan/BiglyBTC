// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_archive_dialog.hpp"
#include "download_archive.hpp"

#include <QApplication>
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
  packet->rows = std::move(archive->stubs);
  ui::DownloadArchiveDialog dialog(std::move(packet));
  const auto* table = dialog.findChild<QTableView*>("archived-downloads-table");
  require(table && table->model()->rowCount() == 2, "Show both archive rows");
  auto* model = table->model();
  require(model->index(0, 0).data().toString() == "Original v2" && model->index(0, 3).data().toString().isEmpty()
      && model->index(1, 0).data().toString() == QStringLiteral("Open source Ω"),
      "Display preserved text, newest first, with no fabricated completion date");

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
  std::cout << "Archive source, original Java rows, recovery and desktop display passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
