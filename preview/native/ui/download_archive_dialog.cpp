// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_archive_dialog.hpp"

#include <QAbstractTableModel>
#include <QAction>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QCheckBox>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>
#include <array>
#include <limits>

namespace biglybtc::ui {
namespace {
enum ArchiveRole { ArchiveHashRole = Qt::UserRole + 1, ArchivePathRole };

QString display(const std::optional<std::string>& value) {
  return value ? QString::fromStdString(*value) : QString{};
}

QString localPath(QString value) {
  const auto path = QDir::fromNativeSeparators(std::move(value));
  if (path.isEmpty() || path.contains(QChar(u'\0'))) { return {}; }
  const QFileInfo location(path);
  return location.isAbsolute() && location.isNativePath() ? QDir::cleanPath(path) : QString{};
}

QString filePath(const core::ArchivedDownloadMetadata& download, const core::ArchivedFile& file) {
  auto value=QDir::fromNativeSeparators(display(file.path));
  if (file.relative) {
    const auto root=localPath(display(download.savePath));
    if (root.isEmpty()||value.isEmpty()||QDir::isAbsolutePath(value)) { return {}; }
    value=QDir::cleanPath(value);
    if (value==QStringLiteral(".")||value==QStringLiteral("..")||value.startsWith(QStringLiteral("../"))) { return {}; }
    value=QDir(root).filePath(value);
  }
  return localPath(std::move(value));
}

QString existingLocation(QString path) {
  QFileInfo location(path);
  while (!location.exists()) {
    const auto parent = location.absolutePath();
    if (parent.isEmpty() || parent == path) { return {}; }
    path = parent;
    location.setFile(path);
  }
  return path;
}

class Model final : public QAbstractTableModel {
public:
  explicit Model(DownloadArchivePresentation packet, QObject* parent)
      : QAbstractTableModel(parent), packet_(std::move(packet)) {}

  int rowCount(const QModelIndex& parent) const override {
    return parent.isValid() ? 0 : static_cast<int>(packet_->rows.size());
  }
  int columnCount(const QModelIndex& parent) const override { return parent.isValid() ? 0 : 6; }

  QVariant headerData(int index, Qt::Orientation direction, int role) const override {
    if (role != Qt::DisplayRole || direction != Qt::Horizontal || index < 0 || index >= 6) { return {}; }
    const std::array labels{DownloadArchiveDialog::tr("Name"), DownloadArchiveDialog::tr("Size"),
        DownloadArchiveDialog::tr("Date archived"), DownloadArchiveDialog::tr("Date completed"),
        DownloadArchiveDialog::tr("Category"), DownloadArchiveDialog::tr("Save location")};
    return labels[static_cast<std::size_t>(index)];
  }

  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid()) { return {}; }
    const auto& row = packet_->rows[static_cast<std::size_t>(index.row())];
    const auto& info = row.metadata;
    if (role == ArchiveHashRole) {
      return row.hash && row.hash->bytes().size() == 20 ? QVariant(QString::fromStdString(row.hash->hex())) : QVariant{};
    }
    if (role == ArchivePathRole) { return display(info.savePath); }
    if (role != Qt::DisplayRole && role != Qt::UserRole) { return {}; }
    const auto time = [role](std::int64_t milliseconds) -> QVariant {
      if (role == Qt::UserRole) { return QVariant::fromValue<qint64>(milliseconds); }
      return milliseconds > 0 ? QLocale().toString(QDateTime::fromMSecsSinceEpoch(milliseconds),
          QLocale::ShortFormat) : QString{};
    };
    switch (index.column()) {
      case 0: return info.name ? display(info.name) : row.hash ? QString::fromStdString(row.hash->hex())
          : DownloadArchiveDialog::tr("Invalid archive entry");
      case 1: return role == Qt::UserRole ? QVariant::fromValue<qint64>(info.size)
          : QVariant(QLocale().formattedDataSize(info.size));
      case 2: return time(info.archivedMillis);
      case 3: return time(info.completedMillis);
      case 4: return display(info.category);
      case 5: return display(info.savePath);
      default: return {};
    }
  }
private:
  DownloadArchivePresentation packet_;
};

class FileModel final : public QAbstractTableModel {
public:
  FileModel(DownloadArchivePresentation packet,QObject* parent)
      :QAbstractTableModel(parent),packet_(std::move(packet)) {}

  void setDownload(const QModelIndex& row) {
    beginResetModel();
    download_=row.isValid()?&packet_->rows[static_cast<std::size_t>(row.row())].metadata:nullptr;
    endResetModel();
  }

  int rowCount(const QModelIndex& parent={}) const override {
    return parent.isValid()||!download_?0:static_cast<int>(download_->fileEntries.size());
  }
  int columnCount(const QModelIndex& parent) const override { return parent.isValid()?0:2; }

  QVariant headerData(int section,Qt::Orientation direction,int role) const override {
    if (role!=Qt::DisplayRole||direction!=Qt::Horizontal) { return {}; }
    return section==0?DownloadArchiveDialog::tr("File"):DownloadArchiveDialog::tr("Size");
  }

  QVariant data(const QModelIndex& index,int role) const override {
    if (!index.isValid()||!download_) { return {}; }
    const auto& file=download_->fileEntries[static_cast<std::size_t>(index.row())];
    if (index.column()==1&&file.length) {
      const auto length=*file.length;
      if (role==Qt::UserRole) { return QVariant::fromValue<qint64>(length); }
      if (role==Qt::DisplayRole&&length!=std::numeric_limits<qint64>::min()) {
        const auto size=QLocale().formattedDataSize(length<0?-length:length);
        return length<0?QStringLiteral("(%1)").arg(size):size;
      }
      return {};
    }
    if (index.column()!=0) { return {}; }
    const auto path=filePath(*download_,file);
    if (role==ArchivePathRole||role==Qt::ToolTipRole) { return path; }
    if (role==Qt::DisplayRole||role==Qt::UserRole) {
      if (path.isEmpty()) { return display(file.path); }
      return packet_->showFullFilePaths?QDir::toNativeSeparators(path):QFileInfo(path).fileName();
    }
    return {};
  }

private:
  DownloadArchivePresentation packet_;
  const core::ArchivedDownloadMetadata* download_ = nullptr;
};
} // namespace

DownloadArchiveDialog::DownloadArchiveDialog(DownloadArchivePresentation packet, QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Archived Downloads"));
  setObjectName(QStringLiteral("archived-downloads"));
  resize(840, 460);
  auto* layout = new QVBoxLayout(this);
  if (packet->recovered) {
    layout->addWidget(new QLabel(tr("Viewing a recovery copy of the archive"), this));
  }
  if (!packet->error.isEmpty()) {
    auto* label = new QLabel(packet->error, this);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    layout->addWidget(label);
  }
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  if (packet->rows.empty() && packet->error.isEmpty()) {
    layout->addWidget(new QLabel(tr("No archived downloads"), this));
  } else if (!packet->rows.empty()) {
    auto* table = new QTableView(this);
    auto* sorted = new QSortFilterProxyModel(table);
    sorted->setSourceModel(new Model(packet,sorted));
    sorted->setSortRole(Qt::UserRole);
    table->setModel(sorted);
    table->setObjectName(QStringLiteral("archived-downloads-table"));
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setSortingEnabled(true);
    table->sortByColumn(2, Qt::DescendingOrder);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table);
    auto* files=new QTableView(this);
    files->setObjectName(QStringLiteral("archived-files-table"));
    files->setSelectionBehavior(QAbstractItemView::SelectRows);
    files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto* sortedFiles=new QSortFilterProxyModel(files);
    auto* fileModel=new FileModel(packet,sortedFiles);
    sortedFiles->setSourceModel(fileModel);
    sortedFiles->setSortRole(Qt::UserRole);
    files->setModel(sortedFiles);
    files->setSortingEnabled(true);
    files->sortByColumn(0,Qt::AscendingOrder);
    files->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    files->hide();
    layout->addWidget(files);
    connect(table->selectionModel(),&QItemSelectionModel::selectionChanged,files,[=] {
      const auto selected=table->selectionModel()->selectedRows();
      fileModel->setDownload(selected.size()==1?sorted->mapToSource(selected.front()):QModelIndex{});
      files->setVisible(fileModel->rowCount()>0);
    });
    const auto containing = packet->openContainingFolder;
    auto* open = buttons->addButton(containing ? tr("Open Containing Folder") : tr("Show in File Manager"),
        QDialogButtonBox::ActionRole);
    open->setObjectName(QStringLiteral("open-archived-folders"));
    const auto selectedLocations = [](const QTableView* source) {
      QStringList locations;
      for (const auto& index : source->selectionModel()->selectedRows()) {
        auto location = localPath(index.data(ArchivePathRole).toString());
        if (!location.isEmpty()) { locations.push_back(std::move(location)); }
      }
      return locations;
    };
    open->setEnabled(false);
    connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, open,
        [open, table, selectedLocations] { open->setEnabled(!selectedLocations(table).isEmpty()); });
    const auto showLocations=[this,containing](const QStringList& paths) {
      QStringList locations;
      QSet<QString> seen;
      for (const auto& location : paths) {
        auto selected = containing ? QFileInfo(location).absolutePath() : existingLocation(location);
        if (containing && (selected == location || !QFileInfo(selected).isDir())) { continue; }
        if (selected.isEmpty() || seen.contains(selected)) { continue; }
        seen.insert(selected);
        locations.push_back(std::move(selected));
      }
      if (!locations.isEmpty()) { emit openLocationsRequested(std::move(locations),!containing); }
      else { QMessageBox::information(this, tr("Show in File Manager"), tr("The selected save locations are unavailable.")); }
    };
    connect(open,&QPushButton::clicked,this,[=]{showLocations(selectedLocations(table));});
    auto* revealFile=new QAction(open->text(),files);
    files->addAction(revealFile);
    files->setContextMenuPolicy(Qt::ActionsContextMenu);
    revealFile->setEnabled(false);
    connect(files->selectionModel(),&QItemSelectionModel::selectionChanged,revealFile,
        [=]{revealFile->setEnabled(!selectedLocations(files).isEmpty());});
    connect(revealFile,&QAction::triggered,this,[=]{showLocations(selectedLocations(files));});
    if (packet->writable) {
      auto* restore = buttons->addButton(tr("Restore"), QDialogButtonBox::ActionRole);
      restore->setObjectName(QStringLiteral("restore-archived-download"));
      auto* restoreAndStart = buttons->addButton(tr("Restore and Start"), QDialogButtonBox::ActionRole);
      restoreAndStart->setObjectName(QStringLiteral("restore-and-start-archived-downloads"));
      auto* remove = buttons->addButton(tr("Remove from Archive"), QDialogButtonBox::DestructiveRole);
      remove->setObjectName(QStringLiteral("remove-archived-downloads"));
      const auto selectedHashes = [table] {
        QStringList hashes;
        for (const auto& index : table->selectionModel()->selectedRows()) {
          auto hash = index.data(ArchiveHashRole).toString();
          if (hash.isEmpty()) { return QStringList{}; }
          hashes.push_back(std::move(hash));
        }
        return hashes;
      };
      const auto updateActions = [restore, restoreAndStart, remove, selectedHashes] {
        const auto enabled = !selectedHashes().isEmpty();
        restore->setEnabled(enabled);
        restoreAndStart->setEnabled(enabled);
        remove->setEnabled(enabled);
      };
      connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, this, updateActions);
      updateActions();
      const auto request = [this, session = packet->session, selectedHashes](bool start) {
        auto hashes = selectedHashes();
        if (!hashes.isEmpty()) { emit restoreRequested(session, std::move(hashes), start); }
      };
      connect(restore, &QPushButton::clicked, this, [request] { request(false); });
      connect(restoreAndStart, &QPushButton::clicked, this, [request] { request(true); });
      connect(remove, &QPushButton::clicked, this, [this, session = packet->session, selectedHashes, removeMetainfoByDefault = packet->removeMetainfoByDefault] {
        auto hashes = selectedHashes();
        if (hashes.isEmpty()) { return; }
        const QPointer<DownloadArchiveDialog> guard(this);
        QPointer<QMessageBox> confirmation=new QMessageBox(QMessageBox::Question,tr("Remove from Archive"),
            tr("Remove the selected downloads from the archive?"),QMessageBox::Yes|QMessageBox::Cancel,this);
        confirmation->setDefaultButton(QMessageBox::Cancel);
        auto* metainfo=new QCheckBox(tr("Also remove the saved .torrent files"),confirmation);
        metainfo->setChecked(removeMetainfoByDefault);
        confirmation->setCheckBox(metainfo);
        const auto accepted=confirmation->exec();
        if (guard&&confirmation) {
          const bool deleteMetainfo=metainfo->isChecked();
          confirmation->deleteLater();
          if (accepted==QMessageBox::Yes) { emit guard->removeRequested(session,std::move(hashes),deleteMetainfo); }
        }
      });
    }
  }
  layout->addWidget(buttons);
}
} // namespace biglybtc::ui
