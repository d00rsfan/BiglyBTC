// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_archive_dialog.hpp"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>
#include <array>

namespace biglybtc::ui {
namespace {
QString display(const std::optional<std::string>& value) {
  return value ? QString::fromStdString(*value) : QString{};
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
    if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::UserRole)) { return {}; }
    const auto& row = packet_->rows[static_cast<std::size_t>(index.row())];
    const auto& info = row.metadata;
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
  } else if (packet->rows.empty()) {
    layout->addWidget(new QLabel(tr("No archived downloads"), this));
  } else {
    auto* table = new QTableView(this);
    auto* sorted = new QSortFilterProxyModel(table);
    sorted->setSourceModel(new Model(std::move(packet), sorted));
    sorted->setSortRole(Qt::UserRole);
    table->setModel(sorted);
    table->setObjectName(QStringLiteral("archived-downloads-table"));
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSortingEnabled(true);
    table->sortByColumn(2, Qt::DescendingOrder);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table);
  }
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}
} // namespace biglybtc::ui
