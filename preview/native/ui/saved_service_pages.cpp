#include "saved_service_pages.hpp"

#include "biglybtc/saved_services.hpp"

#include <QAbstractItemView>
#include <QByteArray>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLabel>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <new>
#include <unordered_set>
#include <utility>

namespace biglybtc::ui {
namespace {
// Original schema audit: BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f,
// TRHostConfigImpl and ShareResource*/ShareItemImpl. No live service columns.
QString unknown() { return QString::fromUtf8("—"); }
QVariant missing(int role) { return role == Qt::UserRole ? QVariant{} : QVariant{unknown()}; }
QString text(const std::optional<core::SavedText>& value) {
  if (!value || !value->validEncoding || value->embeddedNul) { return unknown(); }
  if (value->bytes.empty()) { return QObject::tr("(empty)"); }
  if (value->encoding == core::SavedTextEncoding::Latin1) {
    return QString::fromLatin1(value->bytes.data(), static_cast<qsizetype>(value->bytes.size()));
  }
  return QString::fromUtf8(value->bytes.data(), static_cast<qsizetype>(value->bytes.size()));
}
template<typename Integer>
QVariant number(const std::optional<Integer>& value, int role) {
  return value ? QVariant::fromValue(static_cast<qlonglong>(*value)) : missing(role);
}
QVariant flag(const std::optional<bool>& value, int role) {
  if (!value) { return missing(role); }
  if (role == Qt::UserRole) { return *value; }
  return *value ? QObject::tr("Yes") : QObject::tr("No");
}
QVariant status(const std::optional<std::int32_t>& value, int role) {
  if (!value) { return missing(role); }
  if (role == Qt::UserRole) { return *value; }
  switch (*value) {
    case 0: return QObject::tr("Failed");
    case 1: return QObject::tr("Stopped");
    case 2: return QObject::tr("Started");
    case 3: return QObject::tr("Published");
    default: return QObject::tr("Unknown (%1)").arg(*value);
  }
}
QVariant shareType(const std::optional<std::int32_t>& value, int role) {
  if (!value) { return missing(role); }
  if (role == Qt::UserRole) { return *value; }
  switch (*value) {
    case 1: return QObject::tr("File");
    case 2: return QObject::tr("Directory");
    case 3: return QObject::tr("Directory contents");
    default: return QObject::tr("Unknown (%1)").arg(*value);
  }
}
QString hex(const std::optional<std::string>& value) {
  if (!value) { return unknown(); }
  if (value->empty()) { return QObject::tr("(empty)"); }
  return QString::fromLatin1(QByteArray(value->data(), static_cast<qsizetype>(value->size())).toHex().toUpper());
}
bool charge(std::size_t size, std::size_t& total) {
  const core::SavedServiceLimits limits;
  if (size > limits.fieldBytes || size > limits.textBytes - total) { return false; }
  total += size;
  return true;
}
bool charge(const std::optional<std::string>& value, std::size_t& total) {
  return !value || charge(value->size(), total);
}
bool charge(const std::optional<core::SavedText>& value, std::size_t& total) {
  return !value || charge(value->bytes.size(), total);
}
template<typename Snapshot>
bool envelope(const std::shared_ptr<const Snapshot>& value) {
  const core::SavedServiceLimits limits;
  return value && value->records.size() <= limits.rows && value->issues.size() <= limits.issues
      && (value->listPresent || value->records.empty())
      && (!value->source || value->source->originalBytes().size() <= limits.document.inputBytes);
}
bool valid(const std::shared_ptr<const core::SavedHosting>& value) {
  if (!envelope(value)) { return false; }
  std::size_t bytes = 0;
  std::unordered_set<std::size_t> indices;
  for (const auto& row : value->records) {
    if (!indices.insert(row.sourceIndex).second || !charge(row.hash, bytes)
        || !charge(row.torrentFile, bytes)) { return false; }
  }
  return true;
}
bool valid(const std::shared_ptr<const core::SavedShares>& value) {
  if (!envelope(value)) { return false; }
  std::size_t bytes = 0;
  std::unordered_set<std::size_t> indices;
  for (const auto& row : value->records) {
    if (!indices.insert(row.sourceIndex).second || !charge(row.file, bytes)
        || !charge(row.category, bytes) || !charge(row.tagUids, bytes)
        || !charge(row.fingerprint, bytes) || !charge(row.cacheFile, bytes)) { return false; }
  }
  return true;
}
bool validSource(SavedPageSource source) {
  return source == SavedPageSource::Primary || source == SavedPageSource::Backup || source == SavedPageSource::Saving;
}
QString sourceName(SavedPageSource source) {
  switch (source) {
    case SavedPageSource::Primary: return QObject::tr("Primary");
    case SavedPageSource::Backup: return QObject::tr("Backup (.bak)");
    case SavedPageSource::Saving: return QObject::tr("Saving (.saving)");
  }
  return unknown();
}
// One-time preview defaults, never a scan of saved rows or a refresh policy.
// Leave every section interactive and let wide tables scroll horizontally.
void initialColumnWidth(QTableView& table, int column, const QString& sample = {}) {
  const auto title = table.model()->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
  const int headerWidth = table.horizontalHeader()->fontMetrics().horizontalAdvance(title) + 32;
  const int sampleWidth = table.fontMetrics().horizontalAdvance(sample) + 24;
  table.setColumnWidth(column, std::clamp(std::max(headerWidth, sampleWidth), 72, 320));
}
QLabel* label(QString value, QWidget* parent, const QString& id) {
  auto* result = new QLabel(std::move(value), parent);
  result->setObjectName(id);
  result->setTextFormat(Qt::PlainText);
  result->setWordWrap(true);
  return result;
}
} // namespace

SavedHostingModel::SavedHostingModel(QObject* parent) : QAbstractTableModel(parent) {
  setObjectName(QStringLiteral("saved-hosting-model"));
}
SavedHostingModel::~SavedHostingModel() = default;
int SavedHostingModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() || !snapshot_ ? 0 : static_cast<int>(snapshot_->records.size());
}
int SavedHostingModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : Columns; }
std::optional<std::size_t> SavedHostingModel::sourceIndexAt(int row) const {
  if (row < 0 || row >= rowCount()) { return std::nullopt; }
  return snapshot_->records[static_cast<std::size_t>(row)].sourceIndex;
}
QVariant SavedHostingModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= rowCount()
      || index.column() < 0 || index.column() >= Columns) { return {}; }
  const auto& row = snapshot_->records[static_cast<std::size_t>(index.row())];
  if (role == SourceIndexRole) { return QVariant::fromValue(static_cast<qulonglong>(row.sourceIndex)); }
  if (role != Qt::DisplayRole && role != Qt::UserRole && role != Qt::AccessibleTextRole) { return {}; }
  switch (index.column()) {
    case 0: return hex(row.hash);
    case 1:
      if (!row.disposition) { return missing(role); }
      if (role == Qt::UserRole) { return static_cast<int>(*row.disposition); }
      return *row.disposition == core::SavedHostingDisposition::StatsBackup ? tr("Statistics backup") : tr("Persistent host");
    case 2: return status(row.status, role);
    case 3: return flag(row.passive, role);
    case 4: return number(row.dateAdded, role);
    case 5: return number(row.stats ? row.stats->completed : std::nullopt, role);
    case 6: return number(row.stats ? row.stats->announces : std::nullopt, role);
    case 7: return number(row.stats ? row.stats->uploaded : std::nullopt, role);
    case 8: return number(row.stats ? row.stats->downloaded : std::nullopt, role);
    case 9: return number(row.backupTime, role);
    default: return {};
  }
}
QVariant SavedHostingModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section >= Columns) { return {}; }
  const QStringList names{tr("Saved hash"), tr("Record"), tr("Saved status"), tr("Passive"), tr("Added (ms)"),
      tr("Completed"), tr("Announces"), tr("Uploaded (bytes)"), tr("Downloaded (bytes)"), tr("Backup time (ms)")};
  return names[section];
}
bool SavedHostingModel::setSnapshot(std::shared_ptr<const core::SavedHosting> snapshot) {
  try { if (!valid(snapshot)) { return false; } }
  catch (const std::bad_alloc&) { return false; }
  beginResetModel();
  snapshot_ = std::move(snapshot);
  endResetModel();
  return true;
}
void SavedHostingModel::clear() {
  if (!snapshot_) { return; }
  beginResetModel(); snapshot_.reset(); endResetModel();
}

SavedSharesModel::SavedSharesModel(QObject* parent) : QAbstractTableModel(parent) {
  setObjectName(QStringLiteral("saved-shares-model"));
}
SavedSharesModel::~SavedSharesModel() = default;
int SavedSharesModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() || !snapshot_ ? 0 : static_cast<int>(snapshot_->records.size());
}
int SavedSharesModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : Columns; }
std::optional<std::size_t> SavedSharesModel::sourceIndexAt(int row) const {
  if (row < 0 || row >= rowCount()) { return std::nullopt; }
  return snapshot_->records[static_cast<std::size_t>(row)].sourceIndex;
}
QVariant SavedSharesModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= rowCount()
      || index.column() < 0 || index.column() >= Columns) { return {}; }
  const auto& row = snapshot_->records[static_cast<std::size_t>(index.row())];
  if (role == SourceIndexRole) { return QVariant::fromValue(static_cast<qulonglong>(row.sourceIndex)); }
  if (role != Qt::DisplayRole && role != Qt::UserRole && role != Qt::AccessibleTextRole) { return {}; }
  switch (index.column()) {
    case 0: return text(row.file);
    case 1: return shareType(row.type, role);
    case 2: return text(row.category);
    case 3: return flag(row.persistent, role);
    case 4: return flag(row.recursive, role);
    case 5: return text(row.tagUids);
    default: return {};
  }
}
QVariant SavedSharesModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section >= Columns) { return {}; }
  const QStringList names{tr("Saved path"), tr("Type"), tr("Category"), tr("Persistent"), tr("Recursive"), tr("Tag IDs")};
  return names[section];
}
bool SavedSharesModel::setSnapshot(std::shared_ptr<const core::SavedShares> snapshot) {
  try { if (!valid(snapshot)) { return false; } }
  catch (const std::bad_alloc&) { return false; }
  beginResetModel(); snapshot_ = std::move(snapshot); endResetModel();
  return true;
}
void SavedSharesModel::clear() {
  if (!snapshot_) { return; }
  beginResetModel(); snapshot_.reset(); endResetModel();
}

SavedServicePage::SavedServicePage(QString id, QString title, QString footer, QWidget* parent)
    : QWidget(parent), status_(label(tr("Not loaded"), this, QStringLiteral("saved-services-status"))),
      provenance_(label(tr("Source: —"), this, QStringLiteral("saved-services-source"))),
      table_(new QTableView(this)), proxy_(new QSortFilterProxyModel(this)) {
  setObjectName(id);
  setAccessibleName(title);
  table_->setObjectName(id + QStringLiteral("-table"));
  table_->setAccessibleName(title + tr(" saved records"));
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setDragDropMode(QAbstractItemView::NoDragDrop);
  table_->setAlternatingRowColors(true);
  table_->verticalHeader()->hide();
  proxy_->setObjectName(id + QStringLiteral("-proxy"));
  proxy_->setSortRole(Qt::UserRole);
  auto* layout = new QVBoxLayout(this);
  layout->addWidget(label(std::move(title), this, id + QStringLiteral("-heading")));
  layout->addWidget(status_);
  layout->addWidget(provenance_);
  layout->addWidget(table_, 1);
  layout->addWidget(label(std::move(footer), this, QStringLiteral("saved-services-footer")));
}
SavedServicePage::~SavedServicePage() = default;
void SavedServicePage::installModel(QAbstractItemModel* model) {
  proxy_->setSourceModel(model);
  table_->setModel(proxy_);
  table_->setSortingEnabled(true);
  table_->sortByColumn(0, Qt::AscendingOrder);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  for (int column = 0; column < model->columnCount(); ++column) { initialColumnWidth(*table_, column); }
  table_->setColumnWidth(0, 360);
}
QTableView& SavedServicePage::table() { return *table_; }
QString SavedServicePage::statusText() const { return status_->text(); }
QString SavedServicePage::provenanceText() const { return provenance_->text(); }
bool SavedServicePage::accepts(quint64 generation) const {
  return generation != 0 && generation == generation_ && state_ == SavedPageState::Loading;
}
bool SavedServicePage::beginRequest(quint64 generation) {
  if (generation == 0 || generation <= generation_) { return false; }
  generation_ = generation;
  state_ = SavedPageState::Loading;
  clearSnapshot();
  status_->setText(tr("Loading saved configuration…"));
  provenance_->setText(tr("Source: —"));
  return true;
}
bool SavedServicePage::setMissing(quint64 generation) {
  if (!accepts(generation)) { return false; }
  clearSnapshot();
  state_ = SavedPageState::Missing;
  status_->setText(tr("No saved configuration file"));
  provenance_->setText(tr("Source: —"));
  return true;
}
bool SavedServicePage::setFailed(quint64 generation, QString message) {
  if (!accepts(generation)) { return false; }
  clearSnapshot();
  state_ = SavedPageState::Failed;
  status_->setText(message.isEmpty() ? tr("Unable to read saved configuration") : message.left(4096));
  provenance_->setText(tr("Source: —"));
  return true;
}
void SavedServicePage::ready(SavedPageSource source, bool listPresent, std::size_t rows, std::size_t issues) {
  state_ = SavedPageState::Ready;
  if (listPresent) {
    status_->setText(tr("%1 saved records; %2 diagnostics").arg(static_cast<qulonglong>(rows)).arg(static_cast<qulonglong>(issues)));
  } else {
    status_->setText(tr("Saved record list is absent; %1 diagnostics").arg(static_cast<qulonglong>(issues)));
  }
  provenance_->setText(tr("Source: %1").arg(sourceName(source)));
}

SavedHostingPage::SavedHostingPage(QWidget* parent)
    : SavedServicePage(QStringLiteral("saved-hosting-page"), tr("My Tracker"),
          tr("Saved tracker.config data only. This page does not start or control a tracker service."), parent),
      model_(new SavedHostingModel(this)) {
  installModel(model_);
  initialColumnWidth(table(), 1, tr("Statistics backup"));
}
void SavedHostingPage::clearSnapshot() { model_->clear(); }
bool SavedHostingPage::setReady(quint64 generation, std::shared_ptr<const core::SavedHosting> snapshot,
    SavedPageSource source) {
  if (!accepts(generation)) { return false; }
  if (!validSource(source) || !model_->setSnapshot(snapshot)) {
    setFailed(generation, tr("Saved tracker data is unavailable or exceeds the preview limits"));
    return false;
  }
  ready(source, snapshot->listPresent, snapshot->records.size(), snapshot->issues.size());
  return true;
}
SavedSharesPage::SavedSharesPage(QWidget* parent)
    : SavedServicePage(QStringLiteral("saved-shares-page"), tr("My Shares"),
          tr("Saved sharing.config data only. This page does not scan paths, open cached torrents or publish shares."), parent),
      model_(new SavedSharesModel(this)) {
  installModel(model_);
  table().setColumnWidth(0, 520);
  initialColumnWidth(table(), 1, tr("Directory contents"));
}
void SavedSharesPage::clearSnapshot() { model_->clear(); }
bool SavedSharesPage::setReady(quint64 generation, std::shared_ptr<const core::SavedShares> snapshot,
    SavedPageSource source) {
  if (!accepts(generation)) { return false; }
  if (!validSource(source) || !model_->setSnapshot(snapshot)) {
    setFailed(generation, tr("Saved sharing data is unavailable or exceeds the preview limits"));
    return false;
  }
  ready(source, snapshot->listPresent, snapshot->records.size(), snapshot->issues.size());
  return true;
}

} // namespace biglybtc::ui
