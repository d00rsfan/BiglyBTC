#include "history_month_view.hpp"

#include "saved_history_plot.hpp"

#include <QAbstractTableModel>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QEvent>
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
QString dateText(core::HistoryDate date) { return QDate(date.year, static_cast<int>(date.month), static_cast<int>(date.day)).toString(Qt::ISODate); }
QString bytes(std::int64_t value) { return QObject::tr("%1 bytes").arg(static_cast<qlonglong>(value)); }
QLabel* label(const QString& text, const QString& id, QWidget* parent) {
  auto* result = new QLabel(text, parent); result->setObjectName(id); result->setTextFormat(Qt::PlainText);
  result->setWordWrap(true); result->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard); return result;
}
bool admittedDay(const core::SavedHistoryDay& value, core::HistoryDate date) {
  if (value.date != date || !core::validHistoryDate(value.date) || value.observations > core::SavedHistoryMaximumRecords
      || value.outsideDayRecords > core::SavedHistoryMaximumRecords - value.observations) { return false; }
  std::size_t known = 0;
  for (const auto& hour : value.hours) { if (hour) { ++known; } }
  return value.observations == 0 ? !value.totals && known == 0 : value.totals && known > 0 && known <= value.observations;
}
bool admitted(const core::SavedHistoryMonth& value, core::HistoryDate endingDate) {
  const auto dates = core::savedHistoryMonthDates(endingDate);
  if (!dates || value.endingDate != endingDate) { return false; }
  for (std::size_t i = 0; i < value.days.size(); ++i) {
    const auto& day = value.days[i];
    if (day.date != (*dates)[i] || (day.snapshot && !admittedDay(*day.snapshot, day.date))) { return false; }
  }
  return true;
}
} // namespace

// Fixed 30 rows. Assign the complete binding before emitting one notification
// as the final action; direct callback deletion/reentry leaves no reset open and
// cannot be followed by writes from this older model update.
class HistoryMonthView::DaysModel final : public QAbstractTableModel {
public:
  DaysModel(core::HistoryDate endingDate, QObject* parent) : QAbstractTableModel(parent) {
    const auto dates = core::savedHistoryMonthDates(endingDate); if (dates) { dates_ = *dates; }
  }
  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 30; }
  int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 8; }
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
    if (role != Qt::DisplayRole) { return {}; }
    if (orientation == Qt::Vertical) { return section >= 0 && section < 30 ? QVariant(section + 1) : QVariant{}; }
    static const std::array<QString, 8> titles{tr("UTC date"), tr("Saved state"), tr("Protocol upload"), tr("Payload upload"),
        tr("Protocol download"), tr("Payload download"), tr("DHT upload"), tr("DHT download")};
    return section >= 0 && section < static_cast<int>(titles.size()) ? QVariant(titles[static_cast<std::size_t>(section)]) : QVariant{};
  }
  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || index.parent().isValid() || index.row() < 0 || index.row() >= 30 || index.column() < 0 || index.column() >= 8) { return {}; }
    if (role == Qt::TextAlignmentRole) { return static_cast<int>(index.column() < 2 ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignRight | Qt::AlignVCenter); }
    const auto date = dateText(dates_[static_cast<std::size_t>(index.row())]);
    if (index.column() == 0) { return role == Qt::DisplayRole || role == Qt::ToolTipRole ? QVariant(date + QStringLiteral(" UTC")) : QVariant{}; }
    const auto* day = frame_ && frame_->state == HistoryMonthReadState::Ready
        ? &frame_->snapshot->days[static_cast<std::size_t>(index.row())] : nullptr;
    if (index.column() == 1) {
      if (role != Qt::DisplayRole && role != Qt::ToolTipRole) { return {}; }
      if (!day) { return loading_ ? tr("Loading") : frame_ ? tr("Unavailable") : tr("Not loaded"); }
      if (!day->snapshot) { return tr("Missing"); }
      return day->snapshot->observations == 0 ? tr("No observations") : tr("Observed");
    }
    if (!day || !day->snapshot || !day->snapshot->totals) {
      if (role == Qt::DisplayRole) { return QStringLiteral("—"); }
      if (role == Qt::ToolTipRole) { return tr("%1 UTC: no saved value available; no zero is inferred").arg(date); }
      return {};
    }
    const auto value = (*day->snapshot->totals)[static_cast<std::size_t>(index.column() - 2)];
    if (role == Qt::UserRole) { return QVariant::fromValue(static_cast<qlonglong>(value)); }
    if (role == Qt::DisplayRole) { return bytes(value); }
    if (role == Qt::ToolTipRole) { return tr("%1 UTC; %2; signed daily saved adjustment").arg(date, bytes(value)); }
    return {};
  }
  Qt::ItemFlags flags(const QModelIndex& index) const override { return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags; }
  void setBinding(HistoryMonthPresentation frame, core::HistoryDate endingDate, bool loading) {
    const auto dates = core::savedHistoryMonthDates(endingDate); if (!dates) { return; }
    if (frame_ == frame && dates_ == *dates && loading_ == loading) { return; }
    frame_ = std::move(frame); dates_ = *dates; loading_ = loading;
    emit dataChanged(index(0, 0), index(29, 7), {Qt::DisplayRole, Qt::ToolTipRole, Qt::UserRole});
  }
private:
  HistoryMonthPresentation frame_;
  std::array<core::HistoryDate, core::SavedHistoryMonthDays> dates_{};
  bool loading_ = false;
};

HistoryMonthView::HistoryMonthView(QWidget* parent, std::optional<core::HistoryDate> endingDate,
    SavedHistoryTraffic initialTraffic) : QWidget(parent) {
  setObjectName(QStringLiteral("statistics-history-month-view")); setAccessibleName(tr("30 saved UTC days"));
  auto* layout = new QVBoxLayout(this); auto* controls = new QHBoxLayout;
  previous_ = new QPushButton(tr("Previous 30 days"), this); previous_->setObjectName(QStringLiteral("saved-history-month-previous"));
  next_ = new QPushButton(tr("Next 30 days"), this); next_->setObjectName(QStringLiteral("saved-history-month-next"));
  const auto initial = endingDate && core::savedHistoryMonthDates(*endingDate)
      ? QDate(endingDate->year, static_cast<int>(endingDate->month), static_cast<int>(endingDate->day))
      : QDateTime::currentDateTimeUtc().date();
  date_ = new QDateEdit(initial, this); date_->setCalendarPopup(true);
  date_->setDateRange(QDate(1970, 1, 30), QDate(9999, 12, 31)); date_->setDisplayFormat(QStringLiteral("yyyy-MM-dd 'UTC'"));
  date_->setObjectName(QStringLiteral("saved-history-month-date")); date_->setAccessibleName(tr("Last UTC date of 30 days"));
  auto* reload = new QPushButton(tr("Refresh 30 days"), this); reload->setObjectName(QStringLiteral("saved-history-month-refresh"));
  controls->addWidget(previous_); controls->addWidget(label(tr("Ending:"), QStringLiteral("saved-history-month-date-title"), this));
  traffic_ = new QComboBox(this); traffic_->setObjectName(QStringLiteral("saved-history-month-traffic"));
  traffic_->setAccessibleName(tr("Chart traffic"));
  traffic_->addItem(tr("Payload")); traffic_->addItem(tr("Protocol")); traffic_->addItem(tr("DHT"));
  traffic_->setCurrentIndex(validSavedHistoryTraffic(initialTraffic) ? static_cast<int>(initialTraffic) : 0);
  auto* trafficTitle = label(tr("Traffic"), QStringLiteral("saved-history-month-traffic-title"), this);
  trafficTitle->setBuddy(traffic_);
  controls->addWidget(date_); controls->addWidget(next_);
  controls->addWidget(trafficTitle); controls->addWidget(traffic_);
  controls->addStretch(); controls->addWidget(reload); layout->addLayout(controls);
  range_ = label({}, QStringLiteral("saved-history-month-range"), this); layout->addWidget(range_);
  status_ = label(tr("No saved days have been loaded"), QStringLiteral("saved-history-month-status"), this); layout->addWidget(status_);
  plot_ = new SavedHistoryPlot(this); plot_->setObjectName(QStringLiteral("saved-history-month-chart"));
  layout->addWidget(plot_);
  table_ = new QTableView(this); table_->setObjectName(QStringLiteral("saved-history-month-days"));
  table_->setAccessibleName(tr("30 UTC dates; saved state and six daily byte counters"));
  model_ = new DaysModel(selectedDate(), table_); table_->setModel(model_);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSortingEnabled(false); table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive); table_->horizontalHeader()->setStretchLastSection(false);
  table_->setColumnWidth(0, 118); table_->setColumnWidth(1, 150);
  for (int i = 2; i < 8; ++i) { table_->setColumnWidth(i, 140); }
  table_->setMinimumHeight(180); layout->addWidget(table_, 1);
  layout->addWidget(label(tr("Daily saved records for 30 UTC dates. Missing files and days without observations are shown as —; recorded zero remains 0 bytes. Values may include signed corrections. No 30-day total is shown."),
      QStringLiteral("saved-history-month-scope"), this));
  connect(previous_, &QPushButton::clicked, this, [this] { date_->setDate(date_->date().addDays(-30)); });
  connect(next_, &QPushButton::clicked, this, [this] { date_->setDate(date_->date().addDays(30)); });
  connect(date_, &QDateEdit::dateChanged, this, [this] { dateChanged(); });
  connect(reload, &QPushButton::clicked, this, [this] { requestRead(); });
  connect(traffic_, &QComboBox::currentIndexChanged, this, [this] {
    if (advance()) { QMetaObject::invokeMethod(this, &HistoryMonthView::refresh, Qt::QueuedConnection); }
  });
}
core::HistoryDate HistoryMonthView::selectedDate() const { return savedDate(date_->date()); }
SavedHistoryTraffic HistoryMonthView::traffic() const { return static_cast<SavedHistoryTraffic>(traffic_->currentIndex()); }
bool HistoryMonthView::advance() {
  if (exhausted_ || revision_ == std::numeric_limits<quint64>::max()) { exhausted_ = true; return false; }
  ++revision_; dirty_ = true; return true;
}
bool HistoryMonthView::accepts(quint64 session, quint64 request, core::HistoryDate date) const {
  return !exhausted_ && loading_ && session != 0 && request != 0 && session == session_ && request == request_ && date == selectedDate();
}
void HistoryMonthView::resetSession(quint64 session) {
  if (session == 0 || session <= session_ || !advance()) { return; }
  session_ = session; request_ = 0; loading_ = false; frame_.reset(); requestedDate_.reset(); refresh();
}
void HistoryMonthView::beginRequest(quint64 session, quint64 request, core::HistoryDate date) {
  if (session == 0 || request == 0 || date != selectedDate() || !core::savedHistoryMonthDates(date)
      || (session_ != 0 && session != session_) || (session == session_ && request <= request_) || !advance()) { return; }
  session_ = session; request_ = request; loading_ = true; frame_.reset(); requestedDate_ = date; refresh();
}
void HistoryMonthView::setFailed(quint64 session, quint64 request, core::HistoryDate date, const QString& failure) {
  if (!accepts(session, request, date)) { return; }
  auto packet = std::make_shared<HistoryMonthPacket>(); packet->session = session; packet->request = request; packet->endingDate = date;
  packet->failure = failure.isEmpty() ? tr("Saved 30-day history could not be read") : failure.left(8192);
  if (!advance()) { return; }
  loading_ = false; frame_ = std::move(packet); refresh();
}
void HistoryMonthView::setPresentation(HistoryMonthPresentation packet) {
  if (!packet || !accepts(packet->session, packet->request, packet->endingDate)) { return; }
  if (packet->state == HistoryMonthReadState::Failed) {
    setFailed(packet->session, packet->request, packet->endingDate, packet->snapshot ? tr("Saved 30-day history result is incomplete") : packet->failure); return;
  }
  if (packet->state != HistoryMonthReadState::Ready || !packet->failure.isEmpty()
      || !packet->snapshot || !admitted(*packet->snapshot, packet->endingDate)) {
    setFailed(packet->session, packet->request, packet->endingDate, tr("Saved 30-day history result is incomplete")); return;
  }
  if (!advance()) { return; }
  loading_ = false; frame_ = std::move(packet); refresh();
}
void HistoryMonthView::dateChanged() {
  if (!advance()) { return; }
  loading_ = false; frame_.reset(); requestedDate_.reset(); QPointer<HistoryMonthView> guard(this); const auto revision = revision_;
  refresh(); if (guard && guard->revision_ == revision) { guard->requestRead(); }
}
void HistoryMonthView::requestRead() {
  if (exhausted_) { return; }
  if (!isVisible() || window()->isMinimized()) { requestedDate_.reset(); return; }
  requestedDate_ = selectedDate(); emit readRequested(*requestedDate_);
}
void HistoryMonthView::refresh() {
  if (!dirty_ || !isVisible() || window()->isMinimized()) { return; }
  QPointer<HistoryMonthView> guard(this); const auto revision = revision_;
  const auto current = [&] { return guard && guard->revision_ == revision && guard->isVisible() && !guard->window()->isMinimized(); };
  const auto dates = core::savedHistoryMonthDates(selectedDate()); if (!dates) { return; }
  const auto range = tr("%1 — %2 UTC").arg(dateText(dates->front()), dateText(dates->back()));
  QString status = tr("No saved days have been loaded");
  if (loading_) { status = tr("Loading 30 saved UTC days…"); }
  else if (frame_) {
    if (frame_->state == HistoryMonthReadState::Failed) { status = frame_->failure; }
    else {
      bool anyFile = false;
      for (const auto& day : frame_->snapshot->days) { if (day.snapshot) { anyFile = true; } }
      status = anyFile ? tr("Saved daily history loaded") : tr("No history files found for these dates");
    }
  }
  struct Fact { QPointer<QLabel> label; QString text, tip; };
  const std::array<Fact, 2> facts{{{range_, range, range}, {status_, status, tr("Saved counters for 30 selected UTC dates; a dash means no saved value is available")}}};
  for (const auto& fact : facts) {
    if (!current() || !fact.label) { return; } fact.label->setText(fact.text);
    if (!current() || !fact.label) { return; } fact.label->setToolTip(fact.tip);
  }
  if (!current()) { return; } previous_->setEnabled(date_->date().addDays(-30) >= date_->minimumDate());
  if (!current()) { return; } next_->setEnabled(date_->date().addDays(30) <= date_->maximumDate());
  if (!current()) { return; }
  const auto source = frame_ && frame_->state == HistoryMonthReadState::Ready ? frame_->snapshot : nullptr;
  plot_->setMonth(selectedDate(), source, traffic());
  if (!current()) { return; } model_->setBinding(frame_, selectedDate(), loading_);
  if (current()) { dirty_ = false; }
}
void HistoryMonthView::observeWindow() {
  if (observedWindow_ == window()) { return; }
  if (observedWindow_) { observedWindow_->removeEventFilter(this); }
  observedWindow_ = window(); if (observedWindow_) { observedWindow_->installEventFilter(this); }
}
void HistoryMonthView::showEvent(QShowEvent* event) {
  QWidget::showEvent(event); observeWindow(); QPointer<HistoryMonthView> guard(this); refresh();
  if (guard && guard->isVisible() && !guard->window()->isMinimized() && !guard->requestedDate_) { guard->requestRead(); }
}
bool HistoryMonthView::eventFilter(QObject* watched, QEvent* event) {
  if (watched == observedWindow_ && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) {
    QMetaObject::invokeMethod(this, [this] {
      QPointer<HistoryMonthView> guard(this); refresh();
      if (guard && guard->isVisible() && !guard->window()->isMinimized() && !guard->requestedDate_) { guard->requestRead(); }
    }, Qt::QueuedConnection);
  }
  return false;
}

} // namespace biglybtc::ui
