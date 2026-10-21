#include "history_range_view.hpp"

#include "history_range_plot.hpp"

#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace biglybtc::ui {
namespace {
bool validPeriod(core::HistoryPeriod period) {
  return period >= core::HistoryPeriod::Hour && period <= core::HistoryPeriod::All;
}
bool validChoice(core::HistoryPeriod period, std::int64_t offset, SavedHistoryTraffic traffic) {
  return validPeriod(period) && offset <= 0 && (period != core::HistoryPeriod::All || offset == 0)
      && validSavedHistoryTraffic(traffic);
}
QString utc(std::int64_t value) {
  return QDateTime::fromMSecsSinceEpoch(value, QTimeZone::utc()).toUTC().toString(Qt::ISODateWithMs);
}
QLabel* label(const QString& text, const QString& id, QWidget* parent) {
  auto* value = new QLabel(text, parent); value->setObjectName(id);
  value->setTextFormat(Qt::PlainText); value->setWordWrap(true);
  value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  return value;
}
std::optional<core::SavedHistoryRangePlan> admittedPlan(const core::SavedHistoryRangePlan& plan) {
  const auto expected = core::planSavedHistoryRange(plan.request,
      plan.request.period == core::HistoryPeriod::All ? std::optional{plan.startMillis} : std::nullopt);
  return expected && *expected == plan ? std::optional{plan} : std::nullopt;
}
bool admitted(const core::SavedHistoryRange& source, core::HistoryRangeRequest query) {
  if (source.plan.request != query || !admittedPlan(source.plan)
      || source.plan.bucketCount == 0 || source.plan.bucketCount > core::SavedHistoryRangeMaximumBuckets
      || source.buckets.size() != source.plan.bucketCount
      || source.presentFiles > core::SavedHistoryRangeMaximumFiles
      || source.missingFiles > core::SavedHistoryRangeMaximumFiles - source.presentFiles
      || source.observations > source.presentFiles * core::SavedHistoryMaximumRecords
      || source.totals.has_value() != (source.observations != 0)) { return false; }
  const auto& first = source.plan.firstFile; const auto& last = source.plan.lastFile;
  const QDate firstDate(first.year, static_cast<int>(first.month), static_cast<int>(first.day));
  const QDate lastDate(last.year, static_cast<int>(last.month), static_cast<int>(last.day));
  const auto files = firstDate.daysTo(lastDate) + 1;
  if (files <= 0 || static_cast<std::size_t>(files) != source.presentFiles + source.missingFiles) { return false; }
  auto start = source.plan.startMillis;
  std::size_t bucketObservations = 0;
  for (const auto& bucket : source.buckets) {
    const auto end = start + source.plan.bucketMillis; // Regenerated plan proves the checked interval.
    if (bucket.startMillis != start || bucket.endMillis != end
        || bucket.observations > source.observations - bucketObservations
        || bucket.totals.has_value() != (bucket.observations != 0)) { return false; }
    bucketObservations += bucket.observations;
    start = end;
  }
  return start == source.plan.endMillis;
}
QString resolution(std::int64_t millis) {
  if (millis == 60000) { return QObject::tr("1 minute"); }
  if (millis == 3600000) { return QObject::tr("1 hour"); }
  if (millis == 86400000) { return QObject::tr("1 day"); }
  return QObject::tr("1 week");
}
} // namespace

HistoryRangeView::HistoryRangeView(QWidget* parent, core::HistoryPeriod initialPeriod,
    std::int64_t initialOffset, SavedHistoryTraffic initialTraffic) : QWidget(parent) {
  query_ = {initialPeriod, initialOffset, QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()};
  if (!validChoice(initialPeriod, initialOffset, initialTraffic) || !core::planSavedHistoryRange(query_)) {
    query_.period = core::HistoryPeriod::Week; query_.offset = 0; initialTraffic = SavedHistoryTraffic::Payload;
  }
  setObjectName(QStringLiteral("statistics-history-range-view")); setAccessibleName(tr("Rolling saved UTC history"));
  auto* layout = new QVBoxLayout(this); auto* controls = new QHBoxLayout;
  period_ = new QComboBox(this); period_->setObjectName(QStringLiteral("saved-history-range-period"));
  period_->setAccessibleName(tr("History period"));
  for (const auto& text : {tr("Hour"), tr("Day"), tr("Week"), tr("Month"), tr("Year"), tr("All")}) { period_->addItem(text); }
  period_->setCurrentIndex(static_cast<int>(query_.period));
  auto* periodTitle = label(tr("Period:"), QStringLiteral("saved-history-range-period-title"), this); periodTitle->setBuddy(period_);
  previous_ = new QPushButton(tr("Previous period"), this); previous_->setObjectName(QStringLiteral("saved-history-range-previous"));
  next_ = new QPushButton(tr("Next period"), this); next_->setObjectName(QStringLiteral("saved-history-range-next"));
  traffic_ = new QComboBox(this); traffic_->setObjectName(QStringLiteral("saved-history-range-traffic")); traffic_->setAccessibleName(tr("Chart traffic"));
  traffic_->addItem(tr("Payload")); traffic_->addItem(tr("Protocol")); traffic_->addItem(tr("DHT"));
  traffic_->setCurrentIndex(static_cast<int>(initialTraffic));
  auto* trafficTitle = label(tr("Traffic:"), QStringLiteral("saved-history-range-traffic-title"), this); trafficTitle->setBuddy(traffic_);
  auto* reload = new QPushButton(tr("Refresh history"), this); reload->setObjectName(QStringLiteral("saved-history-range-refresh"));
  controls->addWidget(periodTitle); controls->addWidget(period_); controls->addWidget(previous_); controls->addWidget(next_);
  controls->addWidget(trafficTitle); controls->addWidget(traffic_); controls->addStretch(); controls->addWidget(reload);
  layout->addLayout(controls);
  range_ = label({}, QStringLiteral("saved-history-range-range"), this); layout->addWidget(range_);
  resolution_ = label({}, QStringLiteral("saved-history-range-resolution"), this); layout->addWidget(resolution_);
  coverage_ = label({}, QStringLiteral("saved-history-range-coverage"), this); layout->addWidget(coverage_);
  status_ = label(tr("No rolling saved history loaded"), QStringLiteral("saved-history-range-status"), this); layout->addWidget(status_);
  plot_ = new HistoryRangePlot(this); plot_->setObjectName(QStringLiteral("saved-history-range-chart")); layout->addWidget(plot_, 1);
  auto* totals = new QGroupBox(tr("Totals for selected period"), this); totals->setObjectName(QStringLiteral("saved-history-range-totals"));
  auto* form = new QFormLayout(totals);
  const std::array<const char*, 6> ids{"protocol-up", "payload-up", "protocol-down", "payload-down", "dht-up", "dht-down"};
  const std::array<QString, 6> names{tr("Protocol uploaded:"), tr("Payload uploaded:"), tr("Protocol downloaded:"), tr("Payload downloaded:"), tr("DHT uploaded:"), tr("DHT downloaded:")};
  for (std::size_t i = 0; i < totals_.size(); ++i) {
    totals_[i] = label(QStringLiteral("—"), QStringLiteral("saved-history-range-total-") + QString::fromLatin1(ids[i]), totals);
    form->addRow(label(names[i], QStringLiteral("saved-history-range-total-title-") + QString::fromLatin1(ids[i]), totals), totals_[i]);
  }
  layout->addWidget(totals);
  layout->addWidget(label(tr("Saved history. Period totals can differ from chart bucket sums at boundaries. Missing observations remain unknown; recorded zero and signed corrections are preserved. Month means 30 days and Year means 365 days. Refresh reloads saved records."),
      QStringLiteral("saved-history-range-scope"), this));
  connect(period_, &QComboBox::currentIndexChanged, this, [this] { choiceChanged(); });
  connect(traffic_, &QComboBox::currentIndexChanged, this, [this] { if (advance()) { queueTurn(); } });
  connect(previous_, &QPushButton::clicked, this, [this] { movePeriod(-1); });
  connect(next_, &QPushButton::clicked, this, [this] { movePeriod(1); });
  connect(reload, &QPushButton::clicked, this, &HistoryRangeView::requestRefresh);
}
SavedHistoryTraffic HistoryRangeView::traffic() const { return static_cast<SavedHistoryTraffic>(traffic_->currentIndex()); }
bool HistoryRangeView::advance() {
  if (exhausted_ || revision_ == std::numeric_limits<quint64>::max()) { exhausted_ = true; return false; }
  ++revision_; dirty_ = true; return true;
}
bool HistoryRangeView::accepts(quint64 session, quint64 request, core::HistoryRangeRequest query) const {
  return !exhausted_ && loading_ && session != 0 && request != 0 && session == session_ && request == request_ && query == query_;
}
void HistoryRangeView::beginRequest(quint64 session, quint64 request, core::HistoryRangeRequest query) {
  if (session == 0 || request == 0 || query.period != query_.period || query.offset != query_.offset
      || !core::planSavedHistoryRange(query) || (session_ != 0 && session != session_)
      || (session == session_ && request <= request_) || !advance()) { return; }
  session_ = session; request_ = request; query_ = query; loading_ = true; needsRead_ = false; frame_.reset(); render();
}
void HistoryRangeView::setPresentation(HistoryRangePresentation packet) {
  if (!packet || !accepts(packet->session, packet->request, packet->query)) { return; }
  if (packet->state == HistoryRangeReadState::Failed) {
    setFailed(packet->session, packet->request, packet->query, packet->snapshot ? tr("Saved range result is inconsistent") : packet->failure); return;
  }
  if (packet->state != HistoryRangeReadState::Ready || !packet->failure.isEmpty() || !packet->snapshot
      || !admitted(*packet->snapshot, packet->query)) {
    setFailed(packet->session, packet->request, packet->query, tr("Saved range result is inconsistent")); return;
  }
  if (!advance()) { return; }
  loading_ = false; frame_ = std::move(packet); render();
}
void HistoryRangeView::setFailed(quint64 session, quint64 request, core::HistoryRangeRequest query, const QString& failure) {
  if (!accepts(session, request, query)) { return; }
  auto packet = std::make_shared<HistoryRangePacket>(); packet->session = session; packet->request = request; packet->query = query;
  packet->failure = failure.isEmpty() ? tr("Rolling saved history could not be read") : failure.left(8192);
  if (!advance()) { return; }
  loading_ = false; frame_ = std::move(packet); render();
}
void HistoryRangeView::resetSession(quint64 session) {
  if (session == 0 || session <= session_ || !advance()) { return; }
  session_ = session; request_ = 0; loading_ = false; frame_.reset(); needsRead_ = true; queueTurn();
}
void HistoryRangeView::choiceChanged() {
  const auto period = static_cast<core::HistoryPeriod>(period_->currentIndex());
  if (!validPeriod(period) || !advance()) { return; }
  query_.period = period; query_.offset = 0; frame_.reset(); loading_ = false; needsRead_ = true; queueTurn();
}
void HistoryRangeView::movePeriod(int direction) {
  if (query_.period == core::HistoryPeriod::All || (direction < 0 && query_.offset == std::numeric_limits<std::int64_t>::min())
      || (direction > 0 && query_.offset >= 0)) { return; }
  auto next = query_; next.offset += direction;
  if (!core::planSavedHistoryRange(next) || !advance()) { return; }
  query_ = next; frame_.reset(); loading_ = false; needsRead_ = true; queueTurn();
}
void HistoryRangeView::requestRefresh() {
  if (!advance()) { return; }
  frame_.reset(); loading_ = false; needsRead_ = true; queueTurn();
}
void HistoryRangeView::queueTurn() {
  if (turnQueued_) { return; }
  turnQueued_ = true;
  QMetaObject::invokeMethod(this, [this] {
    turnQueued_ = false; QPointer<HistoryRangeView> guard(this); render();
    if (guard && guard->needsRead_ && guard->isVisible() && !guard->window()->isMinimized()) { guard->requestRead(); }
  }, Qt::QueuedConnection);
}
void HistoryRangeView::requestRead() {
  if (exhausted_ || !needsRead_ || !isVisible() || window()->isMinimized()) { return; }
  if (!advance()) { return; }
  query_.nowMillis = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
  needsRead_ = false; const auto query = query_;
  emit readRequested(query); // Final operation: direct owner callback may delete/reenter.
}
void HistoryRangeView::render() {
  if (!dirty_ || !isVisible() || window()->isMinimized()) { return; }
  QPointer<HistoryRangeView> guard(this); const auto revision = revision_;
  const auto current = [&] { return guard && guard->revision_ == revision && guard->isVisible() && !guard->window()->isMinimized(); };
  const auto source = frame_ && frame_->state == HistoryRangeReadState::Ready ? frame_->snapshot : nullptr;
  auto planned = core::planSavedHistoryRange(query_);
  const auto plan = source ? std::optional{source->plan} : planned ? std::optional{*planned} : std::nullopt;
  QString status = tr("No rolling saved history loaded");
  if (loading_) { status = tr("Loading saved UTC history…"); }
  else if (frame_) {
    if (frame_->state == HistoryRangeReadState::Failed) { status = frame_->failure; }
    else if (source->presentFiles == 0) { status = tr("No saved files found for this query"); }
    else if (source->observations == 0) { status = tr("Saved files contain no observations for this query"); }
    else { status = tr("Saved UTC history loaded"); }
  }
  struct Fact { QPointer<QLabel> label; QString text, tip; };
  const auto range = plan ? tr("%1 — %2 UTC; period offset %3")
      .arg(utc(plan->startMillis), utc(std::min(plan->endMillis, query_.nowMillis))).arg(query_.offset)
      : tr("Requested range is outside supported bounds");
  const auto res = plan ? tr("Resolution: %1; %2 buckets").arg(resolution(plan->bucketMillis)).arg(static_cast<qulonglong>(plan->bucketCount)) : tr("Resolution unavailable");
  const auto coverage = source ? tr("Files: %1 present, %2 missing; %3 whole-query observations")
      .arg(static_cast<qulonglong>(source->presentFiles)).arg(static_cast<qulonglong>(source->missingFiles)).arg(static_cast<qulonglong>(source->observations))
      : tr("File coverage unavailable");
  const std::array<Fact, 4> facts{{{range_, range, range}, {resolution_, res, res}, {coverage_, coverage, coverage}, {status_, status, status}}};
  for (const auto& fact : facts) {
    if (!current() || !fact.label) { return; } fact.label->setText(fact.text);
    if (!current() || !fact.label) { return; } fact.label->setToolTip(fact.tip);
    if (!current()) { return; }
  }
  for (std::size_t i = 0; i < totals_.size(); ++i) {
    const auto text = source && source->totals ? tr("%1 bytes").arg((*source->totals)[i]) : QStringLiteral("—");
    if (!current()) { return; } totals_[i]->setText(text);
    if (!current()) { return; } totals_[i]->setToolTip(tr("Independent inclusive whole-query saved value; not a sum of chart buckets"));
    if (!current()) { return; }
  }
  bool previous = false;
  if (query_.period != core::HistoryPeriod::All && query_.offset > std::numeric_limits<std::int64_t>::min()) {
    auto candidate = query_; --candidate.offset; previous = core::planSavedHistoryRange(candidate).has_value();
  }
  if (!current()) { return; } previous_->setEnabled(previous);
  if (!current()) { return; } next_->setEnabled(query_.period != core::HistoryPeriod::All && query_.offset < 0);
  if (!current()) { return; }
  if (plan && validSavedHistoryTraffic(traffic())) { plot_->setRange(*plan, source, traffic()); }
  else { plot_->clear(); }
  if (current()) { dirty_ = false; }
}
void HistoryRangeView::observeWindow() {
  if (observedWindow_ == window()) { return; }
  if (observedWindow_) { observedWindow_->removeEventFilter(this); }
  observedWindow_ = window(); if (observedWindow_) { observedWindow_->installEventFilter(this); }
}
void HistoryRangeView::showEvent(QShowEvent* event) {
  QWidget::showEvent(event); observeWindow(); queueTurn();
}
bool HistoryRangeView::eventFilter(QObject* watched, QEvent* event) {
  if (watched == observedWindow_ && (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) { queueTurn(); }
  return false;
}
} // namespace biglybtc::ui
