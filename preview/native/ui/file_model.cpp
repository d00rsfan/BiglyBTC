#include "file_model.hpp"

#include <set>

namespace biglybtc::ui {

FileModel::FileModel(QObject* parent) : QAbstractTableModel(parent) {}
void FileModel::select(std::optional<Target> target) {
  if (target_ == target) { return; }
  beginResetModel();
  target_ = std::move(target);
  presentation_.reset();
  endResetModel();
}
bool FileModel::apply(const FilePresentation& value) {
  if (!value || !target_ || value->target != *target_ || value->items.size() > MaximumRows) { return false; }
  std::set<int> indices;
  qsizetype characters = 0;
  for (const auto& row : value->items) {
    if (row.index < 0 || row.index >= static_cast<int>(MaximumRows) || !indices.insert(row.index).second
        || row.path.isEmpty() || row.path.size() > 65536 || row.bytes < 0
        || row.path.size() > 32 * 1024 * 1024 - characters) { return false; }
    characters += row.path.size();
  }
  bool same = presentation_ && presentation_->items.size() == value->items.size();
  if (same) {
    for (std::size_t i = 0; i < value->items.size(); ++i) {
      if (presentation_->items[i].index != value->items[i].index) { same = false; break; }
    }
  }
  if (!same) { beginResetModel(); }
  presentation_ = value;
  if (!same) { endResetModel(); }
  else if (rowCount() > 0) { emit dataChanged(index(0, 0), index(rowCount() - 1, Columns - 1)); }
  return true;
}
int FileModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() || !presentation_ ? 0 : static_cast<int>(presentation_->items.size());
}
int FileModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : Columns; }
QVariant FileModel::data(const QModelIndex& at, int role) const {
  if (!at.isValid() || at.row() < 0 || at.row() >= rowCount() || at.column() < 0 || at.column() >= Columns) { return {}; }
  const auto& row = presentation_->items[static_cast<std::size_t>(at.row())];
  if (role == Qt::UserRole + 1) { return row.index; }
  if (role == Qt::UserRole + 2) { return row.padding ? QStringLiteral("padding") : QStringLiteral("file"); }
  if (role != Qt::DisplayRole && role != Qt::UserRole && role != Qt::AccessibleTextRole) { return {}; }
  switch (at.column()) {
    case 0: return row.path;
    case 1: return QVariant::fromValue(row.bytes);
    case 2: return role == Qt::UserRole ? QVariant{row.priority}
        : QVariant{row.priority == 0 ? tr("Normal") : QString::number(row.priority)};
    case 3: return role == Qt::UserRole ? QVariant{!row.skipped && !row.padding}
        : QVariant{!row.skipped && !row.padding ? tr("Yes") : tr("No")};
    case 4: return row.padding ? tr("Virtual padding") : tr("File");
    default: return {};
  }
}
QVariant FileModel::headerData(int column, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole || column < 0 || column >= Columns) { return {}; }
  const QStringList labels{tr("Path"), tr("Size (bytes)"), tr("Priority"), tr("Download"), tr("Type")};
  return labels[column];
}
std::optional<int> FileModel::originalIndex(int row) const {
  if (row < 0 || row >= rowCount()) { return std::nullopt; }
  return presentation_->items[static_cast<std::size_t>(row)].index;
}
bool FileModel::editableAt(int row) const {
  return presentation_ && presentation_->canEdit && target_ == presentation_->target
      && row >= 0 && row < rowCount() && !presentation_->items[static_cast<std::size_t>(row)].padding;
}
int FileModel::rowFor(int original) const {
  if (!presentation_) { return -1; }
  for (std::size_t i = 0; i < presentation_->items.size(); ++i) {
    if (presentation_->items[i].index == original) { return static_cast<int>(i); }
  }
  return -1;
}
} // namespace biglybtc::ui
