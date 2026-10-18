#include "history_view.hpp"

#include <QAbstractTableModel>
#include <QDateEdit>
#include <QDateTime>
#include <QEvent>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QTableView>
#include <QVBoxLayout>

#include <array>
#include <limits>
#include <utility>

namespace biglybtc::ui {
namespace {
core::HistoryDate savedDate(QDate date) { return {date.year(), static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day())}; }
QString bytes(std::int64_t value) { return QObject::tr("%1 bytes").arg(static_cast<qlonglong>(value)); }
QLabel* label(const QString& text, const QString& id, QWidget* parent) {
  auto* result = new QLabel(text, parent); result->setObjectName(id); result->setTextFormat(Qt::PlainText);
  result->setWordWrap(true); result->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard); return result;
}
bool admitted(const core::SavedHistoryDay& value, core::HistoryDate date) {
  if (value.date != date || !core::validHistoryDate(value.date) || value.observations > core::SavedHistoryMaximumRecords
      || value.outsideDayRecords > core::SavedHistoryMaximumRecords - value.observations) { return false; }
  std::size_t known = 0;
  for (const auto& hour : value.hours) { if (hour) { ++known; } }
  return value.observations == 0 ? !value.totals && known == 0 : value.totals && known > 0 && known <= value.observations;
}
} // namespace

// Fixed shape: changing an immutable snapshot never inserts/removes rows. The
// notification is the last action, so direct reentry/deletion cannot be followed
// by writes from the older update or leave a half-open model reset.
class HistoryView::HoursModel final : public QAbstractTableModel {
public:
  explicit HoursModel(QObject* parent) : QAbstractTableModel(parent) {}
  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 24; }
  int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 7; }
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
    if (role != Qt::DisplayRole) { return {}; }
    if (orientation == Qt::Vertical) { return section >= 0 && section < 24 ? QVariant(section + 1) : QVariant{}; }
    static const std::array<QString, 7> titles{tr("UTC hour"), tr("Protocol upload"), tr("Payload upload"),
        tr("Protocol download"), tr("Payload download"), tr("DHT upload"), tr("DHT download")};
    return section >= 0 && section < static_cast<int>(titles.size()) ? QVariant(titles[static_cast<std::size_t>(section)]) : QVariant{};
  }
  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || index.parent().isValid() || index.row() < 0 || index.row() >= 24 || index.column() < 0 || index.column() >= 7) { return {}; }
    if (role == Qt::TextAlignmentRole) { return static_cast<int>(index.column() == 0 ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignRight | Qt::AlignVCenter); }
    const auto hourText = tr("%1:00–%1:59 UTC").arg(index.row(), 2, 10, QLatin1Char('0'));
    if (index.column() == 0) { return role == Qt::DisplayRole || role == Qt::ToolTipRole ? QVariant(hourText) : QVariant{}; }
    const auto* hour = snapshot_ ? &snapshot_->hours[static_cast<std::size_t>(index.row())] : nullptr;
    if (!hour || !*hour) {
      if (role == Qt::DisplayRole) { return QStringLiteral("—"); }
      if (role == Qt::ToolTipRole) { return tr("%1: no saved observation; no zero is inferred").arg(hourText); }
      return {};
    }
    const auto value = (**hour)[static_cast<std::size_t>(index.column() - 1)];
    if (role == Qt::UserRole) { return QVariant::fromValue(static_cast<qlonglong>(value)); }
    if (role == Qt::DisplayRole) { return bytes(value); }
    if (role == Qt::ToolTipRole) { return tr("%1; %2; signed saved adjustment").arg(hourText, bytes(value)); }
    return {};
  }
  Qt::ItemFlags flags(const QModelIndex& index) const override {
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
  }
  void setSnapshot(std::shared_ptr<const core::SavedHistoryDay> snapshot) {
    if (snapshot_ == snapshot) { return; }
    snapshot_ = std::move(snapshot);
    emit dataChanged(index(0, 1), index(23, 6), {Qt::DisplayRole, Qt::ToolTipRole, Qt::UserRole});
  }
private:
  std::shared_ptr<const core::SavedHistoryDay> snapshot_;
};

HistoryView::HistoryView(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("statistics-history-view")); setAccessibleName(tr("Saved UTC daily traffic history"));
  auto* layout = new QVBoxLayout(this);
  auto* controls = new QHBoxLayout;
  previous_ = new QPushButton(tr("Previous day"), this); previous_->setObjectName(QStringLiteral("saved-history-previous"));
  next_ = new QPushButton(tr("Next day"), this); next_->setObjectName(QStringLiteral("saved-history-next"));
  date_ = new QDateEdit(QDateTime::currentDateTimeUtc().date(), this); date_->setCalendarPopup(true);
  date_->setDateRange(QDate(1970, 1, 1), QDate(9999, 12, 31)); date_->setDisplayFormat(QStringLiteral("yyyy-MM-dd 'UTC'"));
  date_->setObjectName(QStringLiteral("saved-history-date")); date_->setAccessibleName(tr("Selected UTC day"));
  auto* reload = new QPushButton(tr("Refresh history"), this); reload->setObjectName(QStringLiteral("saved-history-refresh"));
  controls->addWidget(previous_); controls->addWidget(date_); controls->addWidget(next_); controls->addStretch(); controls->addWidget(reload);
  layout->addLayout(controls);
  status_ = label(tr("No saved day has been loaded"), QStringLiteral("saved-history-status"), this); layout->addWidget(status_);
  auto* summary = new QGridLayout;
  downloaded_ = label(tr("Unavailable"), QStringLiteral("saved-history-payload-downloaded"), this);
  uploaded_ = label(tr("Unavailable"), QStringLiteral("saved-history-payload-uploaded"), this);
  summary->addWidget(label(tr("Daily payload downloaded:"), QStringLiteral("saved-history-download-title"), this), 0, 0);
  summary->addWidget(downloaded_, 0, 1);
  summary->addWidget(label(tr("Daily payload uploaded:"), QStringLiteral("saved-history-upload-title"), this), 0, 2);
  summary->addWidget(uploaded_, 0, 3); summary->setColumnStretch(1, 1); summary->setColumnStretch(3, 1); layout->addLayout(summary);
  observations_ = label(tr("No saved observations"), QStringLiteral("saved-history-observations"), this); layout->addWidget(observations_);
  table_ = new QTableView(this); table_->setObjectName(QStringLiteral("saved-history-hours"));
  table_->setAccessibleName(tr("24 UTC hours; six saved byte counters")); model_ = new HoursModel(table_); table_->setModel(model_);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSortingEnabled(false); table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive); table_->horizontalHeader()->setStretchLastSection(false);
  table_->setColumnWidth(0, 155); for (int i = 1; i < 7; ++i) { table_->setColumnWidth(i, 155); }
  table_->setMinimumHeight(150); layout->addWidget(table_, 1);
  layout->addWidget(label(tr("Rolling History unavailable; daily saved records are shown above. UTC hourly values are signed saved observations, not live engine traffic or a complete rolling graph. Unknown hours are not zero. Read-only: no history writer, reset or file changes."),
      QStringLiteral("statistics-history-unavailable"), this));
  connect(previous_, &QPushButton::clicked, this, [this] { date_->setDate(date_->date().addDays(-1)); });
  connect(next_, &QPushButton::clicked, this, [this] { date_->setDate(date_->date().addDays(1)); });
  connect(date_, &QDateEdit::dateChanged, this, [this] { dateChanged(); });
  connect(reload, &QPushButton::clicked, this, [this] { requestRead(); });
}

core::HistoryDate HistoryView::selectedDate() const { return savedDate(date_->date()); }
bool HistoryView::advance() {
  if (exhausted_ || revision_ == std::numeric_limits<quint64>::max()) { exhausted_ = true; return false; }
  ++revision_; dirty_ = true; return true;
}
bool HistoryView::accepts(quint64 session, quint64 request, core::HistoryDate date) const {
  return !exhausted_ && loading_ && session != 0 && request != 0 && session == session_ && request == request_
      && date == selectedDate();
}
void HistoryView::resetSession(quint64 session) {
  if (session == 0 || session <= session_ || !advance()) { return; }
  session_ = session; request_ = 0; loading_ = false; frame_.reset(); requestedDate_.reset(); refresh();
}
void HistoryView::beginRequest(quint64 session, quint64 request, core::HistoryDate date) {
  if (session == 0 || request == 0 || !core::validHistoryDate(date) || date != selectedDate()
      || (session_ != 0 && session != session_) || (session == session_ && request <= request_) || !advance()) { return; }
  session_ = session; request_ = request; loading_ = true; frame_.reset(); requestedDate_ = date; refresh();
}
void HistoryView::setFailed(quint64 session, quint64 request, core::HistoryDate date, const QString& failure) {
  if (!accepts(session, request, date)) { return; }
  auto packet = std::make_shared<HistoryPacket>(); packet->session = session; packet->request = request; packet->date = date;
  packet->failure = failure.isEmpty() ? tr("Saved history could not be read") : failure.left(8192);
  if (!advance()) { return; }
  loading_ = false; frame_ = std::move(packet); refresh();
}
void HistoryView::setPresentation(HistoryPresentation packet) {
  if (!packet || !accepts(packet->session, packet->request, packet->date)) { return; }
  if (packet->state == HistoryReadState::Failed) {
    setFailed(packet->session, packet->request, packet->date, packet->snapshot ? tr("Saved history result is incomplete") : packet->failure); return;
  }
  if (!packet->failure.isEmpty() || (packet->state == HistoryReadState::Missing && packet->snapshot)
      || (packet->state == HistoryReadState::Ready && (!packet->snapshot || !admitted(*packet->snapshot, packet->date)))
      || (packet->state != HistoryReadState::Ready && packet->state != HistoryReadState::Missing)) {
    setFailed(packet->session, packet->request, packet->date, tr("Saved history result is incomplete")); return;
  }
  if (!advance()) { return; }
  loading_ = false; frame_ = std::move(packet); refresh();
}
void HistoryView::dateChanged() {
  if (!advance()) { return; }
  loading_ = false; frame_.reset(); requestedDate_.reset();
  QPointer<HistoryView> guard(this); const auto revision = revision_;
  refresh(); if (guard && guard->revision_ == revision) { guard->requestRead(); }
}
void HistoryView::requestRead() {
  if (exhausted_) { return; }
  if (!isVisible() || window()->isMinimized()) { requestedDate_.reset(); return; }
  requestedDate_ = selectedDate(); emit readRequested(*requestedDate_);
}
void HistoryView::refresh() {
  if (!dirty_ || !isVisible() || window()->isMinimized()) { return; }
  QPointer<HistoryView> guard(this); const auto revision = revision_;
  const auto current = [&] { return guard && guard->revision_ == revision && guard->isVisible() && !guard->window()->isMinimized(); };
  QString status = tr("No saved day has been loaded"), downloaded = tr("Unavailable"), uploaded = tr("Unavailable"), observations = tr("No saved observations");
  std::shared_ptr<const core::SavedHistoryDay> snapshot;
  if (loading_) { status = tr("Loading saved UTC history…"); }
  else if (frame_) {
    if (frame_->state == HistoryReadState::Missing) { status = tr("Saved history file not found for this UTC day"); }
    else if (frame_->state == HistoryReadState::Failed) { status = frame_->failure; }
    else {
      snapshot = frame_->snapshot;
      status = snapshot->observations == 0 ? tr("Saved history file has no observations for this UTC day") : tr("Saved UTC daily history loaded");
      observations = tr("%1 saved observations; %2 outside-day observations validated and excluded")
          .arg(static_cast<qulonglong>(snapshot->observations)).arg(static_cast<qulonglong>(snapshot->outsideDayRecords));
      if (snapshot->totals) { downloaded = bytes((*snapshot->totals)[3]); uploaded = bytes((*snapshot->totals)[1]); }
    }
  }
  struct Fact { QPointer<QLabel> label; QString text, tip; };
  const std::array<Fact, 4> facts{{{status_, status, tr("One selected UTC day; read-only saved observations")},
      {downloaded_, downloaded, downloaded}, {uploaded_, uploaded, uploaded}, {observations_, observations, observations}}};
  for (const auto& fact : facts) {
    if (!current() || !fact.label) { return; } fact.label->setText(fact.text);
    if (!current() || !fact.label) { return; } fact.label->setToolTip(fact.tip);
  }
  if (!current()) { return; } previous_->setEnabled(date_->date() > date_->minimumDate());
  if (!current()) { return; } next_->setEnabled(date_->date() < date_->maximumDate());
  if (!current()) { return; } model_->setSnapshot(std::move(snapshot));
  if (current()) { dirty_ = false; }
}
void HistoryView::observeWindow() {
  if (observedWindow_ == window()) { return; }
  if (observedWindow_) { observedWindow_->removeEventFilter(this); }
  observedWindow_ = window(); if (observedWindow_) { observedWindow_->installEventFilter(this); }
}
void HistoryView::showEvent(QShowEvent* event) {
  QWidget::showEvent(event); observeWindow();
  QPointer<HistoryView> guard(this); refresh();
  if (guard && guard->isVisible() && !guard->window()->isMinimized() && !guard->requestedDate_) { guard->requestRead(); }
}
bool HistoryView::eventFilter(QObject* watched, QEvent* event) {
  if (watched == observedWindow_ && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) {
    QMetaObject::invokeMethod(this, [this] {
      QPointer<HistoryView> guard(this); refresh();
      if (guard && guard->isVisible() && !guard->window()->isMinimized() && !guard->requestedDate_) { guard->requestRead(); }
    }, Qt::QueuedConnection);
  }
  return false;
}

} // namespace biglybtc::ui
