#include "runtime_metrics_view.hpp"
#include "runtime_service_summary.hpp"

#include <QAbstractTableModel>
#include <QByteArray>
#include <QEvent>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include <functional>
#include <limits>
#include <unordered_set>
#include <utility>

namespace biglybtc::ui {
namespace {
constexpr int NameRole = Qt::UserRole + 1, GroupRole = Qt::UserRole + 2;
bool validGroup(core::RuntimeMetricGroup group) {
  switch (group) {
    case core::RuntimeMetricGroup::Disk: case core::RuntimeMetricGroup::Tracker:
    case core::RuntimeMetricGroup::Dht: case core::RuntimeMetricGroup::Peer:
    case core::RuntimeMetricGroup::Net: case core::RuntimeMetricGroup::Other: return true;
  }
  return false;
}
bool validSnapshot(const std::shared_ptr<const core::RuntimeMetrics>& value, quint64 publication) {
  if (!value) { return true; }
  if (!value->sequence || value->sequence > publication || value->ownerElapsedNanoseconds < 0
      || value->metrics.size() > core::MaximumRuntimeMetrics) { return false; }
  std::unordered_set<std::string_view> names;
  names.reserve(value->metrics.size());
  for (const auto& metric : value->metrics) {
    if (metric.name.empty() || metric.name.size() > core::MaximumRuntimeMetricNameBytes
        || metric.name.find('\0') != std::string::npos
        || !names.insert(metric.name).second
        || (metric.type != core::RuntimeMetricType::Counter && metric.type != core::RuntimeMetricType::Gauge)
        || !validGroup(metric.group) || metric.group != core::runtimeMetricGroup(metric.name)) { return false; }
    const auto text = QString::fromUtf8(metric.name.data(), static_cast<qsizetype>(metric.name.size()));
    const auto bytes = text.toUtf8();
    if (std::string_view(bytes.data(), static_cast<std::size_t>(bytes.size())) != metric.name) { return false; }
  }
  return true;
}
QString nameText(const std::string& name) {
  return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}
class MetricsModel final : public QAbstractTableModel {
public:
  using QAbstractTableModel::QAbstractTableModel;
  int rowCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() || !snapshot_ ? 0 : static_cast<int>(snapshot_->metrics.size());
  }
  int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 3; }
  QVariant headerData(int section, Qt::Orientation direction, int role) const override {
    if (direction != Qt::Horizontal || role != Qt::DisplayRole) { return {}; }
    switch (section) { case 0: return tr("Name"); case 1: return tr("Value"); case 2: return tr("Type"); default: return {}; }
  }
  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= rowCount()
        || index.column() < 0 || index.column() >= 3) { return {}; }
    const auto& metric = snapshot_->metrics[static_cast<std::size_t>(index.row())];
    if (role == NameRole) { return QByteArray(metric.name.data(), static_cast<qsizetype>(metric.name.size())); }
    if (role == GroupRole) { return static_cast<int>(metric.group); }
    if (role == Qt::TextAlignmentRole && index.column() == 1) { return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter); }
    if (role == Qt::UserRole && index.column() == 1) { return QVariant::fromValue(static_cast<qlonglong>(metric.value)); }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole && role != Qt::AccessibleTextRole) { return {}; }
    switch (index.column()) {
      case 0: return nameText(metric.name);
      case 1: return QString::number(metric.value);
      case 2: return metric.type == core::RuntimeMetricType::Counter ? tr("Counter") : tr("Gauge");
    }
    return {};
  }
  bool replace(std::shared_ptr<const core::RuntimeMetrics> value, const std::function<bool()>& current) {
    QPointer<MetricsModel> guard(this);
    beginResetModel();
    if (!guard) { return false; }
    const bool commit = current();
    if (!guard) { return false; }
    if (commit) { snapshot_ = std::move(value); }
    endResetModel();
    return commit && guard;
  }
private:
  std::shared_ptr<const core::RuntimeMetrics> snapshot_;
};
class MetricsProxy final : public QSortFilterProxyModel {
public:
  MetricsProxy(std::optional<core::RuntimeMetricGroup> group, QObject* parent)
      : QSortFilterProxyModel(parent), group_(group) {
    setFilterCaseSensitivity(Qt::CaseInsensitive); setFilterKeyColumn(0);
  }
protected:
  bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
    if (group_ && (!validGroup(*group_) || sourceModel()->index(row, 0, parent).data(GroupRole).toInt() != static_cast<int>(*group_))) { return false; }
    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
  }
  bool lessThan(const QModelIndex& a, const QModelIndex& b) const override {
    if (a.column() == 1) {
      const auto left = a.data(Qt::UserRole).toLongLong(), right = b.data(Qt::UserRole).toLongLong();
      if (left != right) { return left < right; }
    } else {
      const auto order = QString::compare(a.data().toString(), b.data().toString(), Qt::CaseSensitive);
      if (order) { return order < 0; }
    }
    return a.data(NameRole).toByteArray() < b.data(NameRole).toByteArray();
  }
private:
  std::optional<core::RuntimeMetricGroup> group_;
};
}

RuntimeMetricsView::RuntimeMetricsView(std::optional<core::RuntimeMetricGroup> filter, QWidget* parent)
    : QWidget(parent) {
  setObjectName(QStringLiteral("runtime-metrics-view")); setAccessibleName(tr("Native engine measurements"));
  auto* layout = new QVBoxLayout(this);
  status_ = new QLabel(tr("Runtime measurements unavailable"), this);
  status_->setObjectName(QStringLiteral("runtime-metrics-status")); status_->setTextFormat(Qt::PlainText);
  status_->setWordWrap(true); layout->addWidget(status_);
  if (filter == core::RuntimeMetricGroup::Disk || filter == core::RuntimeMetricGroup::Tracker
      || filter == core::RuntimeMetricGroup::Peer) {
    summaryGroup_ = filter;
    const bool disk = filter == core::RuntimeMetricGroup::Disk;
    const bool peer = filter == core::RuntimeMetricGroup::Peer;
    auto* title = new QLabel(disk ? tr("Disk activity") : peer ? tr("Connections") : tr("Tracker activity"), this);
    title->setObjectName(QStringLiteral("runtime-service-summary")); title->setTextFormat(Qt::PlainText);
    layout->addWidget(title);
    summaryCount_ = disk || peer ? 3 : 2;
    const std::array<const char*, 3> diskIds{"runtime-disk-jobs", "runtime-disk-buffers-files", "runtime-disk-operations"};
    const std::array<const char*, 3> trackerIds{"runtime-tracker-queue", "runtime-tracker-traffic", ""};
    const std::array<const char*, 3> peerIds{"runtime-peer-connections", "runtime-peer-upload-slots", "runtime-peer-request-queues"};
    for (std::size_t i = 0; i < summaryCount_; ++i) {
      summaries_[i] = new QLabel(tr("Unavailable"), this);
      summaries_[i]->setObjectName(QString::fromLatin1(disk ? diskIds[i] : peer ? peerIds[i] : trackerIds[i]));
      summaries_[i]->setTextFormat(Qt::PlainText); summaries_[i]->setWordWrap(true);
      layout->addWidget(summaries_[i]);
    }
    auto* note = new QLabel(disk
        ? tr("Counts describe disk jobs, buffers and file handles. They do not measure cache capacity or a cache hit rate.")
        : peer ? tr("These are observed peer, slot and waiting-socket counts. Peers with requests are not individual request counts. They do not estimate capacity, ASN, routes or automatic speed control.")
        : tr("The queue covers HTTP announces, not all active trackers. Reported traffic includes estimated protocol costs."), this);
    note->setTextFormat(Qt::PlainText); note->setWordWrap(true); layout->addWidget(note);
  }
  filter_ = new QLineEdit(this); filter_->setObjectName(QStringLiteral("runtime-metrics-filter"));
  filter_->setAccessibleName(tr("Search measurement names")); filter_->setPlaceholderText(tr("Search measurement names"));
  filter_->setMaxLength(256); layout->addWidget(filter_);
  model_ = new MetricsModel(this); proxy_ = new MetricsProxy(filter, this); proxy_->setSourceModel(model_);
  table_ = new QTableView(this); table_->setObjectName(QStringLiteral("runtime-metrics-table"));
  table_->setAccessibleName(tr("Actual native engine counters and gauges")); table_->setModel(proxy_);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->setWordWrap(false);
  table_->setSortingEnabled(true); table_->sortByColumn(0, Qt::AscendingOrder);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  table_->setColumnWidth(0, 480); table_->setColumnWidth(1, 220); table_->setColumnWidth(2, 140);
  layout->addWidget(table_, 1);
  auto* scope = new QLabel(tr("Actual counters and gauges from this native engine. Values use each measurement's own units."), this);
  scope->setTextFormat(Qt::PlainText); scope->setWordWrap(true); layout->addWidget(scope);
  connect(filter_, &QLineEdit::textChanged, this, [this](const QString& text) {
    if (revision_ == std::numeric_limits<quint64>::max()) { return; }
    ++revision_; search_ = text; dirty_ = true; queueRefresh();
  });
}
bool RuntimeMetricsView::beginSession(quint64 session) {
  if (!session || session <= session_ || revision_ == std::numeric_limits<quint64>::max()) { return false; }
  ++revision_; session_ = session; packet_.reset(); snapshotSequence_ = 0; snapshotTime_ = 0;
  dirty_ = true; refresh(); return true;
}
bool RuntimeMetricsView::setPresentation(RuntimeMetricsPresentation packet) {
  if (!packet || !session_ || packet->session != session_ || !packet->epoch || !packet->sequence
      || revision_ == std::numeric_limits<quint64>::max()) { return false; }
  try { if (!validSnapshot(packet->snapshot, packet->sequence)) { return false; } }
  catch (...) { return false; }
  const bool sameEpoch = packet_ && packet_->epoch == packet->epoch;
  if (packet_ && (packet->epoch < packet_->epoch || (sameEpoch && packet->sequence <= packet_->sequence))) { return false; }
  if (sameEpoch && packet->snapshot && snapshotSequence_) {
    const auto& incoming = *packet->snapshot;
    if (incoming.sequence < snapshotSequence_ || incoming.ownerElapsedNanoseconds < snapshotTime_) { return false; }
    if (incoming.sequence == snapshotSequence_ && (!packet_->snapshot
        || incoming.ownerElapsedNanoseconds != packet_->snapshot->ownerElapsedNanoseconds
        || incoming.metrics != packet_->snapshot->metrics)) { return false; }
  }
  if (!sameEpoch) { snapshotSequence_ = 0; snapshotTime_ = 0; }
  if (packet->snapshot) { snapshotSequence_ = packet->snapshot->sequence; snapshotTime_ = packet->snapshot->ownerElapsedNanoseconds; }
  ++revision_; packet_ = std::move(packet); dirty_ = true; refresh(); return true;
}
void RuntimeMetricsView::queueRefresh() {
  if (queued_) { return; }
  queued_ = true; QPointer<RuntimeMetricsView> guard(this);
  QMetaObject::invokeMethod(this, [guard] {
    if (guard) { guard->queued_ = false; guard->refresh(); }
  }, Qt::QueuedConnection);
}
void RuntimeMetricsView::refresh() {
  if (!dirty_ || rendering_ || !isVisible() || window()->isMinimized()) { return; }
  if (!model_ || !proxy_ || !table_ || !filter_ || !status_ || table_->model() != proxy_
      || proxy_->sourceModel() != model_) { return; }
  for (std::size_t i = 0; i < summaryCount_; ++i) { if (!summaries_[i]) { return; } }
  QPointer<RuntimeMetricsView> guard(this);
  const auto packet = packet_; const auto revision = revision_;
  const auto summaries = summaries_; const auto summaryCount = summaryCount_;
  const auto current = [&] { return guard && guard->revision_ == revision && guard->packet_ == packet
      && guard->isVisible() && !guard->window()->isMinimized(); };
  const QPointer<QAbstractTableModel> model = model_; const QPointer<QSortFilterProxyModel> proxy = proxy_;
  const QPointer<QTableView> table = table_; const QPointer<QLabel> status = status_;
  const auto children = [&] {
    if (!current() || !model || !proxy || !table || !status
        || guard->model_ != model || guard->proxy_ != proxy || guard->table_ != table || guard->status_ != status
        || table->model() != proxy || proxy->sourceModel() != model) { return false; }
    for (std::size_t i = 0; i < summaryCount; ++i) {
      if (!summaries[i] || guard->summaries_[i] != summaries[i]) { return false; }
    }
    return true;
  };
  rendering_ = true;
  struct Finish {
    QPointer<RuntimeMetricsView> view; quint64 revision;
    ~Finish() {
      if (!view) { return; }
      view->rendering_ = false;
      if (view->dirty_ && view->revision_ != revision) { view->queueRefresh(); }
    }
  } finish{guard, revision};
  QByteArray selected;
  if (packet && renderedSession_ == packet->session && renderedEpoch_ == packet->epoch) {
    selected = table->currentIndex().data(NameRole).toByteArray();
  }
  const auto snapshot = packet ? packet->snapshot : std::shared_ptr<const core::RuntimeMetrics>{};
  if (!static_cast<MetricsModel*>(model.data())->replace(snapshot, children) || !children()) { return; }
  proxy->setFilterFixedString(search_);
  if (!children()) { return; }
  if (!selected.isEmpty() && !table->currentIndex().isValid()) {
    for (int row = 0; row < proxy->rowCount(); ++row) {
      if (proxy->index(row, 0).data(NameRole).toByteArray() != selected) { continue; }
      table->setCurrentIndex(proxy->index(row, 0));
      if (!children()) { return; }
      break;
    }
  }
  renderedSession_ = packet ? packet->session : 0; renderedEpoch_ = packet ? packet->epoch : 0;
  QString text = tr("Runtime measurements unavailable");
  if (snapshot) {
    text = packet->current ? tr("Current native engine measurements") : tr("Last observed native engine measurements");
    if (!proxy->rowCount()) { text += tr(" — no matching measurements"); }
  }
  const auto tip = snapshot ? tr("Accepted response %1; observed at %2 owner nanoseconds")
      .arg(snapshot->sequence).arg(snapshot->ownerElapsedNanoseconds) : tr("No accepted measurement snapshot; no zero is inferred");
  status->setText(text);
  if (!children()) { return; }
  status->setToolTip(tip);
  if (!children()) { return; }
  status->setAccessibleDescription(text + QLatin1Char(' ') + tip);
  if (!children()) { return; }
  const auto summary = projectRuntimeServiceSummary(snapshot.get());
  const auto number = [&](RuntimeServiceMetric field) {
    const auto value = summary.get(field);
    return value ? QString::number(*value) : tr("Unavailable");
  };
  using Field = RuntimeServiceMetric;
  struct SummaryFact { QString text, source; };
  std::array<SummaryFact, 3> summaryFacts{};
  if (summaryGroup_ == core::RuntimeMetricGroup::Disk) {
    summaryFacts[0] = {tr("Queued / running / fenced jobs: %1 / %2 / %3")
        .arg(number(Field::QueuedDiskJobs)).arg(number(Field::RunningDiskJobs)).arg(number(Field::FencedDiskJobs)),
        QStringLiteral("disk.queued_disk_jobs; disk.num_running_disk_jobs; disk.blocked_disk_jobs (job counts)")};
    summaryFacts[1] = {tr("Buffers in use / pooled open files: %1 / %2")
        .arg(number(Field::DiskBuffers)).arg(number(Field::OpenFiles)),
        QStringLiteral("disk.disk_blocks_in_use; disk.file_pool_size (buffer and file-handle counts, not bytes)")};
    summaryFacts[2] = {tr("Read / write operations: %1 / %2")
        .arg(number(Field::ReadOperations)).arg(number(Field::WriteOperations)),
        QStringLiteral("disk.num_read_ops; disk.num_write_ops (cumulative engine operation counts)")};
  } else if (summaryGroup_ == core::RuntimeMetricGroup::Tracker) {
    summaryFacts[0] = {tr("Queued HTTP announces: %1").arg(number(Field::QueuedHttpAnnounces)),
        QStringLiteral("tracker.num_queued_tracker_announces (queued HTTP announces, not active trackers)")};
    summaryFacts[1] = {tr("Tracker traffic estimate (received / sent bytes): %1 / %2")
        .arg(number(Field::TrackerReceived)).arg(number(Field::TrackerSent)),
        QStringLiteral("net.recv_tracker_bytes; net.sent_tracker_bytes (engine byte reports include estimated protocol costs)")};
  } else if (summaryGroup_ == core::RuntimeMetricGroup::Peer) {
    summaryFacts[0] = {tr("Connected / connecting peers: %1 / %2")
        .arg(number(Field::ConnectedPeers)).arg(number(Field::ConnectingPeers)),
        QStringLiteral("peer.num_peers_connected; peer.num_peers_half_open (peer counts; half-open connections are separate)")};
    summaryFacts[1] = {tr("Allowed upload slots: %1; unchoked / optimistic peers: %2 / %3")
        .arg(number(Field::AllowedUploadSlots)).arg(number(Field::UnchokedUploadPeers)).arg(number(Field::OptimisticUploadPeers)),
        QStringLiteral("ses.num_unchoke_slots (dynamic allowed slots); peer.num_peers_up_unchoked_all; peer.num_peers_up_unchoked_optimistic (peer counts, optimistic included in total)")};
    summaryFacts[2] = {tr("Peers with upload / download requests: %1 / %2; bandwidth queues (send / receive sockets): %3 / %4")
        .arg(number(Field::UploadRequestPeers)).arg(number(Field::DownloadRequestPeers))
        .arg(number(Field::UploadBandwidthQueue)).arg(number(Field::DownloadBandwidthQueue)),
        QStringLiteral("peer.num_peers_up_requests; peer.num_peers_down_requests (peers with requests, not request counts); net.limiter_up_queue; net.limiter_down_queue (waiting socket counts, not bytes)")};
  }
  for (std::size_t i = 0; i < summaryCount; ++i) {
    const auto& fact = summaryFacts[i];
    summaries[i]->setText(fact.text);
    if (!children()) { return; }
    const auto detail = fact.source + QLatin1Char('\n') + fact.text;
    summaries[i]->setToolTip(detail);
    if (!children()) { return; }
    summaries[i]->setAccessibleDescription(detail);
    if (!children()) { return; }
  }
  dirty_ = false;
}
void RuntimeMetricsView::showEvent(QShowEvent* event) {
  QPointer<RuntimeMetricsView> guard(this); QWidget::showEvent(event); if (!guard) { return; }
  if (observedWindow_ != window()) {
    if (observedWindow_) { observedWindow_->removeEventFilter(this); }
    observedWindow_ = window(); observedWindow_->installEventFilter(this);
  }
  queueRefresh();
}
bool RuntimeMetricsView::eventFilter(QObject* object, QEvent* event) {
  if (object == observedWindow_ && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) {
    QPointer<RuntimeMetricsView> guard(this);
    setUpdatesEnabled(!window()->isMinimized());
    if (!guard) { return false; }
    queueRefresh();
  }
  return QWidget::eventFilter(object, event);
}
} // namespace biglybtc::ui
