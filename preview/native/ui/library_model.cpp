#include "library_model.hpp"

#include <algorithm>

namespace biglybtc::ui {
namespace {
QVariant number(std::optional<qint64> value, int role) {
  if (value) { return QVariant::fromValue(*value); }
  return role == Qt::UserRole ? QVariant{} : QVariant{QString::fromUtf8("—")};
}
QString savedSelectedTooltip() {
  return LibraryModel::tr("Saved snapshot of selected files, excluding skipped files. These values are not live progress. An em dash means the saved data is unavailable or inconsistent.");
}
}

LibraryModel::LibraryModel(QObject* parent) : QAbstractTableModel(parent) {}
int LibraryModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
int LibraryModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : Columns; }

QVariant LibraryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || index.column() < 0 || index.column() >= Columns) { return {}; }
  const auto& row = rows_[static_cast<std::size_t>(index.row())];
  if (role == KeyRole) { return row.item.key; }
  if (role == CompletionRole) { return completionAt(index.row()).value_or(false); }
  if (role == Qt::ToolTipRole) { return index.column() >= 10 ? QVariant{savedSelectedTooltip()} : QVariant{}; }
  if (role != Qt::DisplayRole && role != Qt::UserRole && role != Qt::AccessibleTextRole) { return {}; }
  switch (index.column()) {
    case 0: return row.item.name;
    case 1: return row.item.state;
    case 2: return number(row.item.bytes, role);
    case 3: {
      if (!row.item.bytes || !row.item.completed) { return number(std::nullopt, role); }
      const double percent = *row.item.bytes == 0 ? 100.0
          : std::clamp(100.0 * static_cast<double>(*row.item.completed) / static_cast<double>(*row.item.bytes), 0.0, 100.0);
      return role == Qt::UserRole ? QVariant{percent} : QVariant{QString::number(percent, 'f', 1) + '%'};
    }
    case 4: return number(row.down, role);
    case 5: return number(row.up, role);
    case 6: return number(row.item.received, role);
    case 7: return number(row.item.sent, role);
    case 8: return row.item.hash;
    case 9: return number(row.item.savedPosition, role);
    case 10: return number(row.item.savedSelected ? row.item.savedSelected->bytes : std::optional<qint64>{}, role);
    case 11: return number(row.item.savedSelected ? row.item.savedSelected->remaining : std::optional<qint64>{}, role);
    case 12: {
      const auto percent = row.item.savedSelected ? row.item.savedSelected->percentThousandths : std::optional<qint32>{};
      if (!percent) { return number(std::nullopt, role); }
      if (role == Qt::UserRole) { return QVariant::fromValue(*percent); }
      return QStringLiteral("%1.%2%").arg(*percent / 10).arg(*percent % 10);
    }
    default: return {};
  }
}

QVariant LibraryModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || section < 0 || section >= Columns) { return {}; }
  if (role == Qt::ToolTipRole && section >= 10) { return savedSelectedTooltip(); }
  if (role != Qt::DisplayRole) { return {}; }
  const QStringList names{tr("Name"), tr("State"), tr("Size (bytes)"), tr("Progress"),
      tr("Down (B/s)"), tr("Up (B/s)"), tr("Received (bytes)"), tr("Sent (bytes)"), tr("Hash"), tr("Saved queue position"),
      tr("Saved selected size (bytes)"), tr("Saved selected remaining (bytes)"), tr("Saved selected done")};
  return names[section];
}

bool LibraryModel::apply(const Presentation& snapshot) {
  if (!snapshot || snapshot->revision <= revision_ || snapshot->items.size() > MaximumRows) { return false; }
  QHash<QString, int> hashes;
  QHash<QString, bool> keys;
  for (std::size_t i = 0; i < snapshot->items.size(); ++i) {
    const auto& item = snapshot->items[i];
    if (item.key.isEmpty() || keys.contains(item.key) || (item.bytes && *item.bytes < 0)) { return false; }
    if (item.savedSelected) {
      const auto& saved = *item.savedSelected;
      if ((saved.bytes && *saved.bytes < 0) || (saved.remaining && *saved.remaining < 0)
          || (saved.bytes && saved.remaining && *saved.remaining > *saved.bytes)
          || (saved.percentThousandths && (*saved.percentThousandths < 0 || *saved.percentThousandths > 1000))) { return false; }
    }
    keys.insert(item.key, true);
    if (!item.hash.isEmpty()) {
      if (hashes.contains(item.hash)) { hashes[item.hash] = -1; }
      else { hashes.insert(item.hash, static_cast<int>(i)); }
    }
  }
  bool sameRows = rows_.size() == snapshot->items.size();
  if (sameRows) {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
      if (rows_[i].item.key != snapshot->items[i].key) { sameRows = false; break; }
    }
  }
  if (!sameRows) { beginResetModel(); }
  std::vector<Row> replacement;
  replacement.reserve(snapshot->items.size());
  for (std::size_t i = 0; i < snapshot->items.size(); ++i) {
    auto item = snapshot->items[i];
    if (sameRows && !item.bytes) { item.bytes = rows_[i].item.bytes; }
    replacement.push_back({std::move(item)});
  }
  rows_ = std::move(replacement);
  byHash_ = std::move(hashes);
  revision_ = snapshot->revision;
  if (!sameRows) { endResetModel(); }
  else if (!rows_.empty()) { emit dataChanged(index(0, 0), index(rowCount() - 1, Columns - 1)); }
  return true;
}

int LibraryModel::rowFor(const QString& hash) const { return byHash_.value(hash, -1); }
std::optional<bool> LibraryModel::completionAt(int row) const {
  if (row < 0 || row >= rowCount()) { return std::nullopt; }
  const auto& item = rows_[static_cast<std::size_t>(row)].item;
  if (!item.bytes || *item.bytes < 0 || !item.completed || *item.completed < 0) { return std::nullopt; }
  return *item.completed >= *item.bytes;
}
bool LibraryModel::accepts(const Target& target) const { return target.generation == revision_ && rowFor(target.hash) >= 0; }
std::optional<Target> LibraryModel::targetAt(int row) const {
  if (row < 0 || row >= rowCount()) { return std::nullopt; }
  const auto& hash = rows_[static_cast<std::size_t>(row)].item.hash;
  if (hash.isEmpty() || rowFor(hash) < 0) { return std::nullopt; }
  return Target{hash, revision_};
}
bool LibraryModel::canContinue(const Target& target) const {
  return accepts(target) && rows_[static_cast<std::size_t>(rowFor(target.hash))].item.canContinue;
}
bool LibraryModel::hasDetails(const Target& target) const {
  return accepts(target) && rows_[static_cast<std::size_t>(rowFor(target.hash))].item.bytes.has_value();
}
bool LibraryModel::apply(const Telemetry& update) {
  if (!accepts(update.target) || update.completed < 0 || update.down < 0 || update.up < 0) { return false; }
  const auto row = rowFor(update.target.hash);
  auto& value = rows_[static_cast<std::size_t>(row)];
  value.item.completed = update.completed;
  value.item.state = update.state;
  value.down = update.down;
  value.up = update.up;
  emit dataChanged(index(row, 1), index(row, 5));
  return true;
}
bool LibraryModel::apply(const Details& details) {
  if (!accepts(details.target) || details.bytes < 0 || (details.files && details.files->target != details.target)) { return false; }
  const auto row = rowFor(details.target.hash);
  auto& value = rows_[static_cast<std::size_t>(row)].item;
  value.bytes = details.bytes;
  if (!details.name.isEmpty()) { value.name = details.name; }
  emit dataChanged(index(row, 0), index(row, 3));
  return true;
}
} // namespace biglybtc::ui
