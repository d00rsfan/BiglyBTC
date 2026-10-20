#include "history_week_view.hpp"

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
bool admitted(const core::SavedHistoryWeek& value, core::HistoryDate endingDate) {
  const auto dates = core::savedHistoryWeekDates(endingDate);
  if (!dates || value.endingDate != endingDate) { return false; }
  for (std::size_t i = 0; i < value.days.size(); ++i) {
    const auto& day = value.days[i];
    if (day.date != (*dates)[i] || (day.snapshot && !admittedDay(*day.snapshot, day.date))) { return false; }
  }
  return true;
}
} // namespace

// Fixed seven rows. Assign the complete binding before emitting one notification
// as the final action; direct callback deletion/reentry leaves no reset open and
// cannot be followed by writes from this older model update.
class HistoryWeekView::DaysModel final : public QAbstractTableModel {
public:
  DaysModel(core::HistoryDate endingDate, QObject* parent) : QAbstractTableModel(parent) {
    const auto dates = core::savedHistoryWeekDates(endingDate); if (dates) { dates_ = *dates; }
  }
  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 7; }
  int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 8; }
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
    if (role != Qt::DisplayRole) { return {}; }
    if (orientation == Qt::Vertical) { return section >= 0 && section < 7 ? QVariant(section + 1) : QVariant{}; }
    static const std::array<QString, 8> titles{tr("UTC date"), tr("Saved state"), tr("Protocol upload"), tr("Payload upload"),
        tr("Protocol download"), tr("Payload download"), tr("DHT upload"), tr("DHT download")};
    return section >= 0 && section < static_cast<int>(titles.size()) ? QVariant(titles[static_cast<std::size_t>(section)]) : QVariant{};
  }
  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || index.parent().isValid() || index.row() < 0 || index.row() >= 7 || index.column() < 0 || index.column() >= 8) { return {}; }
    if (role == Qt::TextAlignmentRole) { return static_cast<int>(index.column() < 2 ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignRight | Qt::AlignVCenter); }
    const auto date = dateText(dates_[static_cast<std::size_t>(index.row())]);
    if (index.column() == 0) { return role == Qt::DisplayRole || role == Qt::ToolTipRole ? QVariant(date + QStringLiteral(" UTC")) : QVariant{}; }
    const auto* day = frame_ && frame_->state == HistoryWeekReadState::Ready
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
  void setBinding(HistoryWeekPresentation frame, core::HistoryDate endingDate, bool loading) {
    const auto dates = core::savedHistoryWeekDates(endingDate); if (!dates) { return; }
    if (frame_ == frame && dates_ == *dates && loading_ == loading) { return; }
    frame_ = std::move(frame); dates_ = *dates; loading_ = loading;
    emit dataChanged(index(0, 0), index(6, 7), {Qt::DisplayRole, Qt::ToolTipRole, Qt::UserRole});
  }
private:
  HistoryWeekPresentation frame_;
  std::array<core::HistoryDate, core::SavedHistoryWeekDays> dates_{};
  bool loading_ = false;
};

HistoryWeekView::HistoryWeekView(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("statistics-history-week-view")); setAccessibleName(tr("Seven saved UTC days"));
  auto* layout = new QVBoxLayout(this); auto* controls = new QHBoxLayout;
  previous_ = new QPushButton(tr("Previous 7 days"), this); previous_->setObjectName(QStringLiteral("saved-history-week-previous"));
  next_ = new QPushButton(tr("Next 7 days"), this); next_->setObjectName(QStringLiteral("saved-history-week-next"));
  date_ = new QDateEdit(QDateTime::currentDateTimeUtc().date(), this); date_->setCalendarPopup(true);
  date_->setDateRange(QDate(1970, 1, 7), QDate(9999, 12, 31)); date_->setDisplayFormat(QStringLiteral("yyyy-MM-dd 'UTC'"));
  date_->setObjectName(QStringLiteral("saved-history-week-date")); date_->setAccessibleName(tr("Last UTC date of seven days"));
  auto* reload = new QPushButton(tr("Refresh 7 days"), this); reload->setObjectName(QStringLiteral("saved-history-week-refresh"));
  controls->addWidget(previous_); controls->addWidget(label(tr("Ending:"), QStringLiteral("saved-history-week-date-title"), this));
  resolution_ = new QComboBox(this); resolution_->setObjectName(QStringLiteral("saved-history-week-resolution"));
  resolution_->setAccessibleName(tr("Graph resolution")); resolution_->addItem(tr("Daily totals")); resolution_->addItem(tr("Hourly values"));
  auto* resolutionTitle = label(tr("Graph resolution:"), QStringLiteral("saved-history-week-resolution-title"), this);
  resolutionTitle->setBuddy(resolution_);
  controls->addWidget(date_); controls->addWidget(next_); controls->addWidget(resolutionTitle); controls->addWidget(resolution_);
  controls->addStretch(); controls->addWidget(reload); layout->addLayout(controls);
  range_ = label({}, QStringLiteral("saved-history-week-range"), this); layout->addWidget(range_);
  status_ = label(tr("No saved days have been loaded"), QStringLiteral("saved-history-week-status"), this); layout->addWidget(status_);
  plot_ = new SavedHistoryPlot(this); plot_->setObjectName(QStringLiteral("saved-history-week-chart"));
  layout->addWidget(plot_);
  table_ = new QTableView(this); table_->setObjectName(QStringLiteral("saved-history-week-days"));
  table_->setAccessibleName(tr("Seven UTC dates; saved state and six daily byte counters"));
  model_ = new DaysModel(selectedDate(), table_); table_->setModel(model_);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSortingEnabled(false); table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive); table_->horizontalHeader()->setStretchLastSection(false);
  table_->setColumnWidth(0, 118); table_->setColumnWidth(1, 150);
  for (int i = 2; i < 8; ++i) { table_->setColumnWidth(i, 140); }
  table_->setMinimumHeight(180); layout->addWidget(table_, 1);
  layout->addWidget(label(tr("Daily saved records for seven UTC dates. Missing files and days without observations are shown as —; recorded zero remains 0 bytes. Values may include signed corrections. No weekly total is shown."),
      QStringLiteral("saved-history-week-scope"), this));
  connect(previous_, &QPushButton::clicked, this, [this] { date_->setDate(date_->date().addDays(-7)); });
  connect(next_, &QPushButton::clicked, this, [this] { date_->setDate(date_->date().addDays(7)); });
  connect(date_, &QDateEdit::dateChanged, this, [this] { dateChanged(); });
  connect(reload, &QPushButton::clicked, this, [this] { requestRead(); });
  connect(resolution_, &QComboBox::currentIndexChanged, this, [this] {
    if (advance()) { QMetaObject::invokeMethod(this, &HistoryWeekView::refresh, Qt::QueuedConnection); }
  });
}
core::HistoryDate HistoryWeekView::selectedDate() const { return savedDate(date_->date()); }
bool HistoryWeekView::advance() {
  if (exhausted_ || revision_ == std::numeric_limits<quint64>::max()) { exhausted_ = true; return false; }
  ++revision_; dirty_ = true; return true;
}
bool HistoryWeekView::accepts(quint64 session, quint64 request, core::HistoryDate date) const {
  return !exhausted_ && loading_ && session != 0 && request != 0 && session == session_ && request == request_ && date == selectedDate();
}
void HistoryWeekView::resetSession(quint64 session) {
  if (session == 0 || session <= session_ || !advance()) { return; }
  session_ = session; request_ = 0; loading_ = false; frame_.reset(); requestedDate_.reset(); refresh();
}
void HistoryWeekView::beginRequest(quint64 session, quint64 request, core::HistoryDate date) {
  if (session == 0 || request == 0 || date != selectedDate() || !core::savedHistoryWeekDates(date)
      || (session_ != 0 && session != session_) || (session == session_ && request <= request_) || !advance()) { return; }
  session_ = session; request_ = request; loading_ = true; frame_.reset(); requestedDate_ = date; refresh();
}
void HistoryWeekView::setFailed(quint64 session, quint64 request, core::HistoryDate date, const QString& failure) {
  if (!accepts(session, request, date)) { return; }
  auto packet = std::make_shared<HistoryWeekPacket>(); packet->session = session; packet->request = request; packet->endingDate = date;
  packet->failure = failure.isEmpty() ? tr("Saved seven-day history could not be read") : failure.left(8192);
  if (!advance()) { return; }
  loading_ = false; frame_ = std::move(packet); refresh();
}
void HistoryWeekView::setPresentation(HistoryWeekPresentation packet) {
  if (!packet || !accepts(packet->session, packet->request, packet->endingDate)) { return; }
  if (packet->state == HistoryWeekReadState::Failed) {
    setFailed(packet->session, packet->request, packet->endingDate, packet->snapshot ? tr("Saved seven-day history result is incomplete") : packet->failure); return;
  }
  if (packet->state != HistoryWeekReadState::Ready || !packet->failure.isEmpty()
      || !packet->snapshot || !admitted(*packet->snapshot, packet->endingDate)) {
    setFailed(packet->session, packet->request, packet->endingDate, tr("Saved seven-day history result is incomplete")); return;
  }
  if (!advance()) { return; }
  loading_ = false; frame_ = std::move(packet); refresh();
}
void HistoryWeekView::dateChanged() {
  if (!advance()) { return; }
  loading_ = false; frame_.reset(); requestedDate_.reset(); QPointer<HistoryWeekView> guard(this); const auto revision = revision_;
  refresh(); if (guard && guard->revision_ == revision) { guard->requestRead(); }
}
void HistoryWeekView::requestRead() {
  if (exhausted_) { return; }
  if (!isVisible() || window()->isMinimized()) { requestedDate_.reset(); return; }
  requestedDate_ = selectedDate(); emit readRequested(*requestedDate_);
}
void HistoryWeekView::refresh() {
  if (!dirty_ || !isVisible() || window()->isMinimized()) { return; }
  QPointer<HistoryWeekView> guard(this); const auto revision = revision_;
  const auto current = [&] { return guard && guard->revision_ == revision && guard->isVisible() && !guard->window()->isMinimized(); };
  const auto dates = core::savedHistoryWeekDates(selectedDate()); if (!dates) { return; }
  const auto range = tr("%1 — %2 UTC").arg(dateText(dates->front()), dateText(dates->back()));
  QString status = tr("No saved days have been loaded");
  if (loading_) { status = tr("Loading seven saved UTC days…"); }
  else if (frame_) {
    if (frame_->state == HistoryWeekReadState::Failed) { status = frame_->failure; }
    else {
      bool anyFile = false;
      for (const auto& day : frame_->snapshot->days) { if (day.snapshot) { anyFile = true; } }
      status = anyFile ? tr("Saved daily history loaded") : tr("No history files found for these dates");
    }
  }
  struct Fact { QPointer<QLabel> label; QString text, tip; };
  const std::array<Fact, 2> facts{{{range_, range, range}, {status_, status, tr("Saved counters for seven selected UTC dates; a dash means no saved value is available")}}};
  for (const auto& fact : facts) {
    if (!current() || !fact.label) { return; } fact.label->setText(fact.text);
    if (!current() || !fact.label) { return; } fact.label->setToolTip(fact.tip);
  }
  if (!current()) { return; } previous_->setEnabled(date_->date().addDays(-7) >= date_->minimumDate());
  if (!current()) { return; } next_->setEnabled(date_->date().addDays(7) <= date_->maximumDate());
  if (!current()) { return; }
  const auto source = frame_ && frame_->state == HistoryWeekReadState::Ready ? frame_->snapshot : nullptr;
  if (resolution_->currentIndex() == 1) { plot_->setWeekHourly(selectedDate(), source); }
  else { plot_->setWeek(selectedDate(), source); }
  if (!current()) { return; } model_->setBinding(frame_, selectedDate(), loading_);
  if (current()) { dirty_ = false; }
}
void HistoryWeekView::observeWindow() {
  if (observedWindow_ == window()) { return; }
  if (observedWindow_) { observedWindow_->removeEventFilter(this); }
  observedWindow_ = window(); if (observedWindow_) { observedWindow_->installEventFilter(this); }
}
void HistoryWeekView::showEvent(QShowEvent* event) {
  QWidget::showEvent(event); observeWindow(); QPointer<HistoryWeekView> guard(this); refresh();
  if (guard && guard->isVisible() && !guard->window()->isMinimized() && !guard->requestedDate_) { guard->requestRead(); }
}
bool HistoryWeekView::eventFilter(QObject* watched, QEvent* event) {
  if (watched == observedWindow_ && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) {
    QMetaObject::invokeMethod(this, [this] {
      QPointer<HistoryWeekView> guard(this); refresh();
      if (guard && guard->isVisible() && !guard->window()->isMinimized() && !guard->requestedDate_) { guard->requestRead(); }
    }, Qt::QueuedConnection);
  }
  return false;
}

} // namespace biglybtc::ui
